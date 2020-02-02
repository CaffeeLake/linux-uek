/*
 * Copyright (c) 2018, Oracle and/or its affiliates.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/kernel.h>
#include <linux/in.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/if_arp.h>
#include <net/arp.h>
#include <linux/delay.h>
#include <net/sock.h>
#include <net/route.h>
#include <net/addrconf.h>
#include <net/inet_common.h>
#include <net/ipoib/if_ipoib.h>
#include <linux/rtnetlink.h>
#include <linux/time.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_verbs.h>
#include "rdmaip.h"

static struct workqueue_struct *rdmaip_wq;
static void rdmaip_device_add(struct ib_device *device);
static void rdmaip_device_remove(struct ib_device *device, void *client_data);
static void rdmaip_ip_config_init(void);
static void rdmaip_ip_failover_groups_init(void);
static void rdmaip_update_port_status_all_layers(u8 port, int event_type,
						 int event);

static DECLARE_DELAYED_WORK(riif_dlywork, rdmaip_initial_failovers);
/*
 * This structure is registed with IB core. IB core calls
 * rdmaip_device_add() function when ever a new RDMA adpter
 * registered with it. IB core calls rdmaip_device_remove()
 * function whenever RDMA adapter unregisters with the IB
 * core.
 */
struct ib_client rdmaip_client = {
	.name   = "rdmaip",
	.add    = rdmaip_device_add,
	.remove = rdmaip_device_remove
};

bool rdmaip_is_event_pending(void)
{
	return test_bit(RDMAIP_FLAG_EVENT_PENDING, &rdmaip_global_flag);
}

void rdmaip_set_event_pending(void)
{
	__set_bit(RDMAIP_FLAG_EVENT_PENDING, &rdmaip_global_flag);
}

void rdmaip_clear_event_pending(void)
{
	__clear_bit(RDMAIP_FLAG_EVENT_PENDING, &rdmaip_global_flag);
}

bool rdmaip_is_busy_flag_set(void)
{
	return test_bit(RDMAIP_FLAG_BUSY, &rdmaip_global_flag);
}

void rdmaip_set_busy_flag(void)
{
	__set_bit(RDMAIP_FLAG_BUSY, &rdmaip_global_flag);
}

void rdmaip_clear_busy_flag(void)
{
	__clear_bit(RDMAIP_FLAG_BUSY, &rdmaip_global_flag);
}

/*
 * This structure is registed with network stack to monitor
 * netdev links states.
 */
static int rdmaip_netdev_callback(struct notifier_block *,
				  unsigned long, void *);
static struct notifier_block rdmaip_nb = {
	.notifier_call = rdmaip_netdev_callback
};

/* Returns string representation of an IB port state. */
static const char *rdmaip_portstate2name(int state)
{
	switch (state) {
	case RDMAIP_PORT_INIT:
		return "INIT";
	case RDMAIP_PORT_UP:
		return "UP";
	case RDMAIP_PORT_DOWN:
		return "DOWN";
	default:
		return "UNKNOWN";
	}
}

/* Returns string representation of NETDEV event  */
static const char *rdmaip_netdevevent2name(int event)
{
	switch (event) {
	case NETDEV_UP:
		return "NETDEV-UP";
	case NETDEV_DOWN:
		return "NETDEV-DOWN";
	case NETDEV_CHANGE:
		return "NETDEV-CHANGE";
	default:
		return "Other";
	}
}

static void rdmaip_dump_ip_config_entry(u8 port)
{
	struct rdmaip_port *cfg;
	char	devname[RDMAIP_MAX_NAME_LEN];
	int i;

	cfg = &ip_config[port];

	if ((cfg->rdmaip_dev) && (cfg->rdmaip_dev->ibdev))
		strncpy(devname, cfg->rdmaip_dev->ibdev->name,
			RDMAIP_MAX_NAME_LEN);
	else
		strncpy(devname, "No RDMAIP device", RDMAIP_MAX_NAME_LEN);

	pr_info("rdmaip: %s/port_%d/%s:  IPv4 %pI4/%pI4/%pI4  Link Status: %s\n",
		devname, cfg->port_num, cfg->if_name,
		&cfg->ip_addr, &cfg->ip_bcast,
		&cfg->ip_mask, rdmaip_portstate2name(cfg->port_state));

	for (i = 0; i < cfg->ip6_addrs_cnt; i++) {
		pr_info("rdmaip:   IPv6 %pI6c%%%d/%d\n",
			&cfg->ip6_addrs[i].addr, cfg->ifindex,
			cfg->ip6_addrs[i].prefix_len);
	}

	for (i = 0; i < cfg->alias_cnt; i++) {
		pr_info("rdmaip: Alias %s IP %pI4/%pI4/%pI4\n",
			cfg->aliases[i].if_name, &cfg->aliases[i].ip_addr,
			&cfg->aliases[i].ip_bcast, &cfg->aliases[i].ip_mask);
	}
}

static void rdmaip_dump_ip_config(void)
{
	int	i;

	for (i = 1; i <= ip_port_cnt; i++)
		rdmaip_dump_ip_config_entry(i);
}

/*
 * rdmaip_is_link_layer_up
 *
 * Test for link layer UP derived from how the LOWER_UP flag is set for 'ip'
 * CLI command (which talks to kernel via netlink sockets).
 *
 * Note: IPv6 addrconf uses  an alternative test "!qdisc_tx_is_noop(dev)" to
 * signal an UP link layer. Any pros/cons of the two * different tests for an
 * UP link layer?
 */
static inline int
rdmaip_is_link_layer_up(const struct net_device *dev)
{
	return (netif_running(dev)) && (netif_carrier_ok(dev));
}

static inline int
rdmaip_port_all_layers_up(struct rdmaip_port *rdmaip_port)
{
	if ((rdmaip_port->port_layerflags & RDMAIP_PORT_STATUS_ALLUP) ==
	    RDMAIP_PORT_STATUS_ALLUP)
		return 1;
	return 0;
}

static unsigned long
rdmaip_get_failback_sync_jiffies(struct rdmaip_port *rdmaip_port)
{
	u64 time_after_portup =
		get_jiffies_64() - rdmaip_port->port_active_ts;
	u64 sync_delay;

	sync_delay = msecs_to_jiffies(rdmaip_sysctl_active_bonding_failback_ms);

	if (time_after_portup > sync_delay)
		return 0;

	return sync_delay - time_after_portup;
}

/*
 * Get a failover port for port argument ('port')
 * based on failover group and pkey/vlan match.
 */
static u8 rdmaip_get_failover_port(u8 port)
{
	u8	i;

	for (i = 1; i <= ip_port_cnt; i++) {
		if ((i != port) &&
		    ip_config[i].failover_group &&
		    (ip_config[i].failover_group ==
		     ip_config[port].failover_group) &&
		    (ip_config[i].pkey_vlan == ip_config[port].pkey_vlan) &&
		    (ip_config[i].port_state == RDMAIP_PORT_UP)) {
			return i;
		}
	}

	return 0;
}

static void rdmaip_send_gratuitous_arp(struct net_device *out_dev,
				       unsigned char *dev_addr, __be32 ip_addr)
{
	int i;

	if (out_dev)
		RDMAIP_DBG2_PTR("Sending GARP message for adding IP addr %pI4 on %s\n",
			       (void *)&ip_addr, out_dev->name);

	/* Send multiple ARPs to improve reliability */
	for (i = 0; i < rdmaip_active_bonding_arps; i++) {
		arp_send(ARPOP_REQUEST, ETH_P_ARP, ip_addr, out_dev, ip_addr,
			 NULL, dev_addr, NULL);
	}
}

/* Add or remove an IPv6 address to/from an interface. */
static int rdmaip_change_ip6(int ifindex, struct in6_addr *addr,
			     u32 prefix_len, bool add)
{
	struct in6_ifreq ifr6 = { };

	if (!rdmaip_inet6_socket)
		return -EPROTONOSUPPORT;

	ifr6.ifr6_ifindex = ifindex;
	ifr6.ifr6_addr = *addr;
	ifr6.ifr6_prefixlen = prefix_len;

	return inet6_ioctl(rdmaip_inet6_socket, add ? SIOCSIFADDR : SIOCDIFADDR,
			   (unsigned long)&ifr6);
}

/*
 * Remove all non-link local IPv6 addresses associated with a given rdmaip_port
 * (port).
 */
static int rdmaip_clear_ip6(u8 port)
{
	int i, ret, addrs_cnt;

	if (!rdmaip_inet6_socket)
		return 0;

	for (i = 0, addrs_cnt = ip_config[port].ip6_addrs_cnt; i < addrs_cnt;
	     i++) {
		ret = rdmaip_change_ip6(ip_config[port].ifindex,
					&ip_config[port].ip6_addrs[i].addr,
					ip_config[port].ip6_addrs[i].prefix_len,
					false);
		if (ret) {
			/*
			 * If the link is administratively marked down, all
			 * the addresses are already gone so the removal will
			 * fail.  Continue the removal.
			 */
			if (ret != -EADDRNOTAVAIL) {
				pr_warn("RDMAIP failed to remove %pI6c%%%d from %s\n",
					&ip_config[port].ip6_addrs[i].addr,
					ip_config[port].ifindex,
					ip_config[port].netdev->name);
				return ret;
			}
		}
	}
	return 0;
}

static int rdmaip_set_ip4(struct net_device *out_dev, unsigned char *dev_addr,
			  char *if_name, __be32 addr, __be32 bcast, __be32 mask)
{
	struct ifreq		ir = { };
	struct sockaddr_in	*sin;
	int			ret = 0;

	sin = (struct sockaddr_in *)&ir.ifr_addr;
	sin->sin_family = AF_INET;
	strcpy(ir.ifr_ifrn.ifrn_name, if_name);

	sin->sin_addr.s_addr = addr;
	ret = inet_ioctl(rdmaip_inet_socket, SIOCSIFADDR, (unsigned long) &ir);
	if (ret && addr) {
		pr_err("rdmaip: inet_ioctl(SIOCSIFADDR) on %s failed (%d)\n",
		       if_name, ret);
		goto out;
	}

	if (!addr)
		goto out;

	sin->sin_addr.s_addr = bcast;
	ret = inet_ioctl(rdmaip_inet_socket, SIOCSIFBRDADDR,
			(unsigned long) &ir);
	if (ret) {
		pr_err("rdmaip: inet_ioctl(SIOCSIFBRDADDR) on %s failed (%d)\n",
		       if_name, ret);
		goto out;
	}

	sin->sin_addr.s_addr = mask;
	ret = inet_ioctl(rdmaip_inet_socket, SIOCSIFNETMASK,
			(unsigned long) &ir);
	if (ret) {
		pr_err("rdmaip: inet_ioctl(SIOCSIFNETMASK) on %s failed (%d)\n",
		       if_name, ret);
		goto out;
	}

	rdmaip_send_gratuitous_arp(out_dev, dev_addr, addr);

out:
	return ret;
}

static int rdmaip_addr_exist(struct net_device *ndev,
			     void *addr, char *if_name, bool is_ipv6)
{
	struct in_device        *in_dev;
	struct in_ifaddr        *ifa;
	struct in_ifaddr        **ifap;
	int			found = 0;
	__be32			v4addr;

	if (is_ipv6) {
		struct in6_addr *v6addr;

		v6addr = (struct in6_addr *)addr;
		return ipv6_chk_addr(&init_net, v6addr, ndev, 0);
	}

	v4addr = *(__be32 *)addr;
	rtnl_lock();
	in_dev = in_dev_get(ndev);
	if (in_dev) {
		for (ifap = &in_dev->ifa_list; (ifa = *ifap);
		     ifap = &ifa->ifa_next) {
			if (ifa->ifa_address == v4addr) {
				found = 1;
				if (if_name)
					strcpy(if_name, ifa->ifa_label);
				break;
			}
		}
		in_dev_put(in_dev);
	}
	rtnl_unlock();

	return found;
}

static void rdmaip_notify_addr_change(__be32 addr)
{
	struct sockaddr_in sin;

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = addr;
	sin.sin_port = 0;
	if (rdma_notify_addr_change((struct sockaddr *)&sin))
		pr_err("rdmaip: %pI4 address change notification failed\n",
		       &addr);
}

/*
 * Move all IPv6 addresses from the net_device of one rdmaip_port to another
 * (from_port to to_port) and remove those IPv6 addresses from the first
 * net_device.  If it is failover, the address list is taken from the
 * from_port.  If it is not a failover (meaning failing back to the original
 * port), the address list is taken from the to_port (which is the original
 * port).
 */
static void rdmaip_move_ip6(u8 from_port, u8 to_port, bool failover)
{
	int i, addrs_cnt, ret;
	struct rdmaip_ip6_port_addr *addr_list;
	struct in6_addr *addr;
	int from_ifindex, to_ifindex;
	u32 prefix_len;

	if (!rdmaip_inet6_socket)
		return;

	if (failover) {
		addr_list = ip_config[from_port].ip6_addrs;
		addrs_cnt = ip_config[from_port].ip6_addrs_cnt;
	} else {
		addr_list = ip_config[to_port].ip6_addrs;
		addrs_cnt = ip_config[to_port].ip6_addrs_cnt;
	}

	from_ifindex = ip_config[from_port].ifindex;
	to_ifindex = ip_config[to_port].ifindex;

	for (i = 0; i < addrs_cnt; i++) {
		addr = &addr_list[i].addr;
		prefix_len = addr_list[i].prefix_len;

		ret = rdmaip_change_ip6(from_ifindex, addr, prefix_len, false);
		if (ret) {
			if (ret != -EADDRNOTAVAIL) {
				pr_warn("rdmaip: could not remove %pI6c%%%d/%d from %s (port %d): %d\n",
					addr, ip_config[from_port].ifindex,
					prefix_len,
					ip_config[from_port].if_name,
					from_port, ret);
			}
		}
		if (rdmaip_addr_exist(ip_config[to_port].netdev, addr, NULL,
				      true)) {
			RDMAIP_DBG2("IPv6 Address already set on the port\n");
			continue;
		}

		if (rdmaip_change_ip6(to_ifindex, addr, prefix_len, true)) {
			pr_err("rdmaip: could not add %pI6c%%%d/%d to %s (port %d)\n",
			       addr, ip_config[to_port].ifindex,
			       prefix_len,
			       ip_config[to_port].if_name, to_port);
		} else {
			pr_notice("rdmaip: IPv6 %pI6c%%%d/%d migrated from %s (port %d) to %s (port %d)\n",
			       addr, ip_config[from_port].ifindex, prefix_len,
			       ip_config[from_port].if_name, from_port,
			       ip_config[to_port].if_name, to_port);
		}
	}
}

static int rdmaip_move_ip4(char *from_dev, char *to_dev, u8 from_port,
			   u8 to_port, __be32 addr, __be32 bcast,
			   __be32 mask, int alias, bool failover)
{
	struct ifreq		ir = { };
	struct sockaddr_in	*sin;
	char			from_dev2[2*IFNAMSIZ + 1];
	char			to_dev2[2*IFNAMSIZ + 1];
	char                    *tmp_str;
	int			ret = 0;
	u8			active_port;
	struct in_device	*in_dev;

	RDMAIP_DBG2_PTR("from_dev %s :  to_dev %s : from_port %d : to_port %d"
		    " IP addr %pI4 : failover %s\n",
		    from_dev, to_dev, from_port, to_port, (void *)&addr,
		    (failover ? "True" : "False"));

	sin = (struct sockaddr_in *)&ir.ifr_addr;
	sin->sin_family = AF_INET;

	/* Set the primary IP if it hasn't been set */
	if (ip_config[to_port].ip_addr && failover) {
		strcpy(ir.ifr_ifrn.ifrn_name, ip_config[to_port].netdev->name);
		ret = inet_ioctl(rdmaip_inet_socket, SIOCGIFADDR,
					(unsigned long) &ir);
		if (ret == -EADDRNOTAVAIL) {
			RDMAIP_DBG2_PTR("Setting primary IP on %s %pI4\n",
				    ip_config[to_port].netdev->name,
				    &ip_config[to_port].ip_addr);

			/* Set the IP on new port */
			ret = rdmaip_set_ip4(ip_config[to_port].netdev,
				ip_config[to_port].netdev->dev_addr,
				ip_config[to_port].netdev->name,
				ip_config[to_port].ip_addr,
				ip_config[to_port].ip_bcast,
				ip_config[to_port].ip_mask);

			if (ret) {
				pr_err("rdmaip: failed to set IP %pI4 on %s failed (%d)\n",
				       &ip_config[to_port].ip_addr,
				       ip_config[to_port].netdev->name, ret);
				goto out;
			}
		} else if (ret) {
			pr_err("rdmaip: Failed to get primary IP for %s on port:%d ret:%d\n",
			       ip_config[to_port].netdev->name, to_port, ret);
			goto out;
		}
	}

	if (failover) {
		in_dev = in_dev_get(ip_config[to_port].netdev);
		if (in_dev && !in_dev->ifa_list) {
			strcpy(to_dev2, to_dev);
		} else {
			strcpy(to_dev2, to_dev);
			strcat(to_dev2, ":");
			strcat(to_dev2, ip_config[from_port].port_label);
			if (alias) {
				tmp_str = strchr(from_dev, ':');
				strcat(to_dev2, tmp_str);
			}
			to_dev2[IFNAMSIZ - 1] = 0;
		}
		if (in_dev)
			in_dev_put(in_dev);

		/* Bailout if IP already exists on target port */
		if (rdmaip_addr_exist(ip_config[to_port].netdev, &addr, NULL,
				      false)) {
			pr_err("rdmaip_mov_ip: Address already exist\n");
			ret = -EADDRINUSE;
			goto out;
		}

		active_port = ip_config[from_port].ip_active_port;
		if (alias || active_port == from_port) {
			strcpy(from_dev2, from_dev);
		} else if (ip_config[active_port].port_state ==
				RDMAIP_PORT_UP) {
			if (!rdmaip_addr_exist(ip_config[active_port].netdev,
					       &addr, from_dev2, false)) {
				strcpy(from_dev2,
					ip_config[active_port].netdev->name);
				strcat(from_dev2, ":");
				strcat(from_dev2,
					ip_config[from_port].port_label);
				from_dev2[IFNAMSIZ-1] = 0;
			}
		} else {
			strcpy(from_dev2, to_dev);
		}

		RDMAIP_DBG3("failover: %s -> %s\n", from_dev2, to_dev2);

	} else {
		if (!rdmaip_addr_exist(ip_config[from_port].netdev,
				       &addr, from_dev2, false)) {
			strcpy(from_dev2, from_dev);
			strcat(from_dev2, ":");
			strcat(from_dev2, ip_config[to_port].port_label);
			from_dev2[IFNAMSIZ-1] = 0;
		}
		strcpy(to_dev2, to_dev);
		RDMAIP_DBG3("failback: %s -> %s\n", from_dev2, to_dev);
	}

	/* Clear the IP on old port */
	ret = rdmaip_set_ip4(NULL, NULL, from_dev2, 0, 0, 0);

	/* Set the IP on new port */
	ret = rdmaip_set_ip4(ip_config[to_port].netdev,
			     ip_config[to_port].netdev->dev_addr, to_dev2, addr,
			     bcast, mask);

	if (ret) {
		pr_notice("rdmaip: failed to move IP %pI4 from %s to %s\n",
			  &addr, from_dev2, to_dev2);
	} else {
		if (!strcmp(from_dev2, to_dev2)) {
			/* from_dev2, to_dev2 are identical */
			pr_notice("rdmaip: IPv4 %pI4 resurrected on migrated interface %s\n",
				  &addr, to_dev2);
		} else
			pr_notice("rdmaip: IPv4 %pI4 migrated from %s (port %d) to %s (port %d)\n",
				  &addr, from_dev2, from_port, to_dev2,
				  to_port);

		if (!ip_config[from_port].rdmaip_dev) {
			RDMAIP_DBG3("rdmaip_dev is NULL\n");
			goto out;
		}
		rdmaip_notify_addr_change(addr);
	}

out:
	return ret;
}

static void rdmaip_init_ip4_addrs(struct net_device *net_dev,
				  struct in_device *in_dev, u8 port)
{
	__be32			excl_addr = 0;
	unsigned int		idx, i;
	char			*if_name;
	__be32			ip_addr;
	__be32			ip_bcast;
	__be32			ip_mask;
	struct in_ifaddr	*ifa;

	RDMAIP_DBG2("%s: IPv4 addresses are%sconfigured\n", net_dev->name,
		    in_dev->ifa_list ? " " : " not ");

	for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next) {
		if_name = ifa->ifa_label;
		ip_addr = ifa->ifa_address;
		ip_bcast = ifa->ifa_broadcast;
		ip_mask = ifa->ifa_mask;

		for (i = 0; i < exclude_ips_cnt; i++) {
			if (!((exclude_ips_tbl[i].ip ^ ip_addr) &
			      exclude_ips_tbl[i].mask)) {
				excl_addr = 1;
				break;
			}
		}

		if (!strcmp(net_dev->name, if_name)) {
			if (excl_addr) {
				ip_addr = 0;
				ip_bcast = 0;
				ip_mask = 0;
			}
			strcpy(ip_config[port].if_name, if_name);
			ip_config[port].ip_addr = ip_addr;
			ip_config[port].ip_bcast = ip_bcast;
			ip_config[port].ip_mask = ip_mask;
			ip_config[port].ip_active_port = port;
		} else if (!excl_addr) {
			for (i = 0; i < ip_config[port].alias_cnt; i++) {
				if (ip_config[port].aliases[i].ip_addr ==
					ip_addr) {
					RDMAIP_DBG2("IPv4 Alias is already present\n");
					break;
				}
			}

			if (i != ip_config[port].alias_cnt)
				continue;

			idx = ip_config[port].alias_cnt++;
			if (idx >= RDMAIP_MAX_ALIASES) {
				pr_warn("rdmaip: max number of address alias reached for %s\n",
				       if_name);
				ip_config[port].alias_cnt--;
				return;
			}

			strcpy(ip_config[port].aliases[idx].if_name, if_name);
			ip_config[port].aliases[idx].ip_addr = ip_addr;
			ip_config[port].aliases[idx].ip_bcast = ip_bcast;
			ip_config[port].aliases[idx].ip_mask = ip_mask;
		}
	}
}


/*
 * For the given net_device, populate the specified rdmaip_port (port) with
 * the non-link local IPv6 addresses associated with that device.
 */
static void rdmaip_init_ip6_addrs(struct net_device *net_dev,
				  struct inet6_dev *in6_dev, u8 port)
{
	struct inet6_ifaddr *ifa;
	u32 idx, i;

	read_lock_bh(&in6_dev->lock);
	list_for_each_entry(ifa, &in6_dev->addr_list, if_list) {
		/* Exclude link local address. */
		if (ipv6_addr_type(&ifa->addr) & IPV6_ADDR_LINKLOCAL)
			continue;

		for (i = 0; i < ip_config[port].ip6_addrs_cnt; i++) {
			if (!ipv6_addr_cmp(&ifa->addr,
				&ip_config[port].ip6_addrs[i].addr)) {
				RDMAIP_DBG2("IPv6 address already present\n");
				break;
			}
		}

		if (i != ip_config[port].ip6_addrs_cnt)
			continue;

		idx = ip_config[port].ip6_addrs_cnt++;
		if (idx >= RDMAIP_MAX_ADDRS) {
			pr_warn("rdmaip: max number of IPv6 addresses reached for %s.\n",
			       net_dev->name);
			ip_config[port].ip6_addrs_cnt--;
			break;
		}

		ip_config[port].ip6_addrs[idx].addr = ifa->addr;
		ip_config[port].ip6_addrs[idx].prefix_len = ifa->prefix_len;
	}
	read_unlock_bh(&in6_dev->lock);

	ip_config[port].ifindex = net_dev->ifindex;
	ip_config[port].ip_active_port = port;

	RDMAIP_DBG2("%s: IPv6 addresses are%sconfigured\n", net_dev->name,
		    ip_config[port].ip6_addrs_cnt ? " " : " not ");
}

static u8 rdmaip_init_port(struct rdmaip_device	*rdmaip_dev,
			   struct net_device *net_dev, u8 port_num,
			   uint16_t pkey_vid, struct in_device *in_dev,
			   struct inet6_dev *in6_dev)
{
	const char *digits = "0123456789";
	u8 next_port_idx;

	/*
	 * First initialize new entry before bumping ip_port_cnt
	 */
	next_port_idx = ip_port_cnt + 1;

	RDMAIP_DBG3("ndev : %s port_num %d port idx %d\n",
		    net_dev->name, port_num, next_port_idx);

	if (next_port_idx >= ip_port_max) {
		pr_err("rdmaip: Exceeded max ports (%d) for device %s\n",
				ip_port_max, rdmaip_dev->ibdev->name);
		return 0;
	}
	ip_config[next_port_idx].port_num = port_num;
	ip_config[next_port_idx].port_label[0] = 'P';
	ip_config[next_port_idx].port_label[1] = digits[next_port_idx / 10];
	ip_config[next_port_idx].port_label[2] = digits[next_port_idx % 10];
	ip_config[next_port_idx].port_label[3] = 0;
	ip_config[next_port_idx].netdev = net_dev;
	ip_config[next_port_idx].rdmaip_dev = rdmaip_dev;
	ip_config[next_port_idx].ip_active_port = 0;
	strcpy(ip_config[next_port_idx].if_name, net_dev->name);
	ip_config[next_port_idx].pkey_vlan = pkey_vid;
	ip_config[next_port_idx].port_state = RDMAIP_PORT_INIT;
	ip_config[next_port_idx].port_layerflags = 0x0; /* all clear to begin */

	rdmaip_update_port_status_all_layers(next_port_idx,
					     RDMAIP_EVENT_NONE, 0);

	/*
	 * bump global ip_port_cnt last - no racing thread should
	 * access the 'next_port_idx' entry before it is all initialized
	 * above
	 */
	ip_port_cnt++;

	if (in_dev)
		rdmaip_init_ip4_addrs(net_dev, in_dev, ip_port_cnt);

	if (in6_dev)
		rdmaip_init_ip6_addrs(net_dev, in6_dev, ip_port_cnt);

	return ip_port_cnt;
}

static int rdmaip_testset_ip4(u8 port)
{
	struct ifreq		ir = { };
	struct sockaddr_in	*sin;
	int			ret = 0;
	int                     ii;

	if (!ip_config[port].ip_addr) {
		pr_warn("rdmaip: IP address is unavailable on port index %u\n",
			port);
		return 0;
	}

	sin = (struct sockaddr_in *)&ir.ifr_addr;
	sin->sin_family = AF_INET;

	/*
	 * If the primary IP is not set revive it
	 * and also the IP addrs on aliases
	 */
	strcpy(ir.ifr_ifrn.ifrn_name, ip_config[port].netdev->name);
	ret = inet_ioctl(rdmaip_inet_socket, SIOCGIFADDR, (unsigned long) &ir);
	if (ret == -EADDRNOTAVAIL) {
		/* Set the IP on this port */
		ret = rdmaip_set_ip4(ip_config[port].netdev,
				     ip_config[port].netdev->dev_addr,
				     ip_config[port].netdev->name,
				     ip_config[port].ip_addr,
				     ip_config[port].ip_bcast,
				     ip_config[port].ip_mask);
		if (ret) {
			pr_err("rdmaip: failed to resurrect IP %pI4 on %s failed (%d)\n",
			       &ip_config[port].ip_addr,
			       ip_config[port].netdev->name, ret);
			goto out;
		}
		pr_notice("rdmaip: IP %pI4 resurrected on interface %s\n",
			  &ip_config[port].ip_addr,
			  ip_config[port].netdev->name);
		for (ii = 0; ii < ip_config[port].alias_cnt; ii++) {
			struct rdmaip_alias *alias;

			alias = &ip_config[port].aliases[ii];
			ret = rdmaip_set_ip4(ip_config[port].netdev,
					     ip_config[port].netdev->dev_addr,
					     alias->if_name, alias->ip_addr,
					     alias->ip_bcast, alias->ip_mask);
			if (ret) {
				pr_err("rddmaip: failed to resurrect IP %pI4 on alias %s ret %d\n",
				       &alias->ip_addr, alias->if_name, ret);
				goto out;
			}
			pr_notice("rdmaip: IP %pI4 resurrected on alias %s on interface %s\n",
				  &ip_config[port].ip_addr,
				  ip_config[port].aliases[ii].if_name,
				  ip_config[port].netdev->name);
		}
	} else if (ret)
		pr_err("rdmaip: inet_ioctl(SIOCGIFADDR) failed (%d)\n", ret);
	else
		RDMAIP_DBG2("Primary addr already set on port index %u devname %s\n",
			    port, ip_config[port].netdev->name);
out:
	return ret;
}

/* Check if the IPv6 addresses associated with the specifid rdmaip_port (port)
 * is configured in the port's net_device.  If not, try setting them.
 */
static int rdmaip_testset_ip6(u8 port)
{
	int i, addrs_cnt;
	struct net_device *dev;
	struct in6_addr *addr;
	u32 prefix_len;

	if (!rdmaip_inet6_socket)
		return 0;

	for (i = 0, addrs_cnt = ip_config[port].ip6_addrs_cnt;
	     i < addrs_cnt; i++) {
		addr = &ip_config[port].ip6_addrs[i].addr;
		dev = ip_config[port].netdev;

		if (rdmaip_addr_exist(dev, addr, NULL, true))
			continue;

		prefix_len = ip_config[port].ip6_addrs[i].prefix_len;
		return rdmaip_change_ip6(ip_config[port].ifindex, addr,
					 prefix_len, true);
	}
	return 0;
}

/* Check if the IP addresses are configured in the specified rdmaip_port
 * (port) net_device.  If not, try setting them.
 */
static int rdmaip_testset_ip(u8 port)
{
	int ret;

	ret = rdmaip_testset_ip4(port);
	if (ret)
		return ret;

	return rdmaip_testset_ip6(port);
}

static void rdmaip_do_failover(u8 from_port, u8 to_port)
{
	bool	v4move, v6move;
	u8      j;

	if (!from_port) {
		RDMAIP_DBG2("rdmaip: NULL from_port\n");
		return;
	}

	v4move = RDMAIP_IPV4_ADDR_SET(from_port);
	v6move = RDMAIP_IPV6_ADDR_SET(from_port);

	RDMAIP_DBG3("from:%d to:%d v4:%d v6:%d\n", from_port,
		    to_port, v4move, v6move);

	if (!(v4move || v6move)) {
		RDMAIP_DBG2("No IPv4 or IPv6 addresses configured\n");
		return;
	}

	if (!to_port) {
		to_port = rdmaip_get_failover_port(from_port);

		if (!to_port) {
			pr_info("rdmaip: IP %pI4 failed to migrate from %s: no matching dest port avail!\n",
				&ip_config[from_port].ip_addr,
				ip_config[from_port].if_name);
			return;
		}
	} else {
		/*
		 * Caller explicitly specified failover port
		 * Validate pkey/vlanid and flag error if we were
		 * passed incorrect pkey/vlanid.
		 */
		if (ip_config[from_port].pkey_vlan !=
		    ip_config[to_port].pkey_vlan) {
			pr_err("rdmaip: port failover request to ports with mismatched pkeys/vlans");
			return;
		}
	}

	if (v4move && !rdmaip_move_ip4(ip_config[from_port].if_name,
			    ip_config[to_port].if_name,
			    from_port, to_port,
			    ip_config[from_port].ip_addr,
			    ip_config[from_port].ip_bcast,
			    ip_config[from_port].ip_mask, 0, true)) {

		ip_config[from_port].ip_active_port = to_port;
		for (j = 0; j < ip_config[from_port].alias_cnt; j++) {
			struct rdmaip_alias *alias;

			alias = &ip_config[from_port].aliases[j];
			rdmaip_move_ip4(alias->if_name,
					ip_config[to_port].if_name,
					from_port, to_port,
					alias->ip_addr,
					alias->ip_bcast,
					alias->ip_mask, 1, true);
		}
	}

	if (v6move) {
		rdmaip_move_ip6(from_port, to_port, true);
		ip_config[from_port].ip_active_port = to_port;
	}
}

static void rdmaip_do_failback(u8 port)
{
	u8      ipap = ip_config[port].ip_active_port;
	bool	v4move, v6move;
	u8      j;
	int     ret;

	v4move = RDMAIP_IPV4_ADDR_SET(port);
	v6move = RDMAIP_IPV6_ADDR_SET(port);

	if (!(v4move || v6move))
		return;

	if (port != ip_config[port].ip_active_port) {
		char	*from_name;

		from_name = ip_config[ipap].if_name;
		RDMAIP_DBG3("Failback from %s -> to %s\n", from_name,
			    ip_config[port].if_name);

		if (v4move && !rdmaip_move_ip4(from_name,
					       ip_config[port].if_name,
					       ipap, port,
					       ip_config[port].ip_addr,
					       ip_config[port].ip_bcast,
					       ip_config[port].ip_mask,
					       0, false)) {

			ip_config[port].ip_active_port = port;
			for (j = 0; j < ip_config[port].alias_cnt; j++) {
				struct rdmaip_alias *alias;

				alias = &ip_config[port].aliases[j];

				rdmaip_move_ip4(from_name,
						alias->if_name,
						ipap, port,
						alias->ip_addr,
						alias->ip_bcast,
						alias->ip_mask,
						1, false);
			}
		}

		if (v6move) {
			rdmaip_move_ip6(ipap, port, false);
			ip_config[port].ip_active_port = port;
		}
	} else {
		RDMAIP_DBG3("Active port is same as failover port, resurrect the IPs %s:port%d\n",
			   ip_config[port].netdev->name, port);
		/*
		 * Our 'active_port' is parked at its home base so 'failback'
		 * is just an interface coming UP.
		 *
		 * We get here in two cases.
		 * (1) When a startup script (such as during boot) brings up
		 *     the interface the IP address is set by it and we dont
		 *     do anything here!
		 * (2) When this port went DOWN, it tried but did not succeed
		 *     in failing over(no UP ports or compatible pkey ports
		 *     left to failover to!) so the 'failover' failed and
		 *     our 'active port' stayed parked at its original place.
		 *     If such as port is being resurrected, it will not have
		 *     an IP address set we resurrect it here!
		 */
		/* Test IP addresses and set them if not already set */
		ret = rdmaip_testset_ip(port);
		if (ret) {
			pr_err("rdmaip: failed to resurrect port idx %u dev %s or one of its aliases\n",
				port, ip_config[port].netdev->name);
		}
	}
}

static void rdmaip_failover(int port)
{
	int		ret;
	u8		i;
	char		if_name[IFNAMSIZ];

	if (ip_config[port].port_state == RDMAIP_PORT_INIT) {
		pr_err("rdmaip: devname %s failover request with port_state in INIT state!",
		       ip_config[port].netdev->name);
		return;
	}

	for (i = 1; i <= ip_port_cnt; i++) {
		if (i != port &&
			ip_config[i].port_state == RDMAIP_PORT_DOWN &&
			ip_config[i].ip_active_port == port) {

			strcpy(if_name, ip_config[port].if_name);
			strcat(if_name, ":");
			strcat(if_name, ip_config[i].port_label);
			if_name[IFNAMSIZ - 1] = 0;

			RDMAIP_DBG3("Zeroing IP address on %s\n", if_name);
			ret = rdmaip_set_ip4(NULL, NULL, if_name, 0, 0, 0);

			rdmaip_do_failover(i, 0);
		}
	}

	if (RDMAIP_PORT_ADDR_SET(port))
		rdmaip_do_failover(port, 0);

	if (ip_config[port].ip_active_port == port) {
		ret = rdmaip_set_ip4(NULL, NULL,
				ip_config[port].if_name, 0, 0, 0);
	}
}

static void rdmaip_failback(struct work_struct *_work)
{
	struct rdmaip_port_ud_work	*work =
		container_of(_work, struct rdmaip_port_ud_work, work.work);
	u8				i, ip_active_port, port = work->port;

	if ((ip_config[port].port_state == RDMAIP_PORT_INIT) ||
	    (ip_config[port].port_state == RDMAIP_PORT_DOWN)) {
		pr_err("rdmaip: devname %s failback request with port_state in %s state!",
		       ip_config[port].netdev->name,
		       ip_config[port].port_state == RDMAIP_PORT_INIT ?
		       "INIT":"DOWN");
		goto out;
	}

	ip_active_port = ip_config[port].ip_active_port;

	rdmaip_do_failback(port);

	for (i = 1; i <= ip_port_cnt; i++) {
		if (i == port ||
		    ip_config[i].port_state == RDMAIP_PORT_UP ||
		    !RDMAIP_PORT_ADDR_SET(i))
			continue;

		if (ip_config[i].ip_active_port == i) {
			RDMAIP_DBG3("ip_active_port == i : %d\n", i);
			rdmaip_do_failover(i, 0);
		} else if ((ip_config[i].ip_active_port == port) &&
			   (ip_config[i].pkey_vlan ==
				ip_config[port].pkey_vlan)) {
			RDMAIP_DBG3("ip_active_port == port : %d\n", i);
			rdmaip_do_failover(i, port);
		} else if (ip_config[ip_config[i].ip_active_port].port_state ==
			   RDMAIP_PORT_DOWN) {
			RDMAIP_DBG3("ip_active_port is DOWN : %d\n", i);
			rdmaip_do_failover(i, 0);
		} else if ((ip_config[port].failover_group ==
				ip_config[i].failover_group) &&
			   (ip_config[i].pkey_vlan ==
				ip_config[port].pkey_vlan)) {
			rdmaip_do_failover(i, port);
		}
	}

	if (ip_active_port != ip_config[port].ip_active_port) {
		RDMAIP_DBG3("ip_active_port != port active port : %d\n",
			    ip_active_port);
		for (i = 1; i <= ip_port_cnt; i++) {
			if (ip_config[i].port_state == RDMAIP_PORT_DOWN &&
			    i != ip_active_port && RDMAIP_PORT_ADDR_SET(i) &&
			    ip_config[i].ip_active_port == ip_active_port &&
			    ip_config[i].pkey_vlan ==
			    ip_config[ip_active_port].pkey_vlan) {
				rdmaip_do_failover(i, ip_active_port);
			}
		}
	}

out:
	kfree(work);
}

/*
 * This funciton returns port transition state
 *
 * Returns RDMAIP_PORT_TRANSITION_NOOP if previous
 * state and current state are same.
 *
 * Returns RDMAIP_PORT_TRANSITION_UP if previous
 * state was not UP and current state is UP.
 *
 * Returns RDMAIP_PORT_TRANSITION_DOWN if previous
 * state was not DOWN and current state is DOWN.
 */
static int rdmaip_find_port_tstate(u8 port)
{
	int	tstate = RDMAIP_PORT_TRANSITION_NOOP;
	struct	rdmaip_device	*rdmaip_dev;

	rdmaip_dev = ip_config[port].rdmaip_dev;

	switch (ip_config[port].port_state) {
	case RDMAIP_PORT_INIT:
		/*
		 * We are in INIT state but not during module
		 * initialization. This can happens when
		 *
		 * 1) A new port is detected and initialized
		 *    in rdmaip_addintf_after_initscripts().
		 *
		 * 2) It can happen via init script 'stop'
		 *    invocation -whichdisables active bonding
		 *    temporarily by unsetting sysctl variable
		 *    rdmaip_sysctl_active_bonding
		 */
		if (rdmaip_sysctl_active_bonding) {
			if (rdmaip_port_all_layers_up(&ip_config[port])) {
				ip_config[port].port_state = RDMAIP_PORT_UP;
				tstate = RDMAIP_PORT_TRANSITION_UP;
				RDMAIP_DBG3("Port transition INIT to UP\n");
			} else {
				ip_config[port].port_state = RDMAIP_PORT_DOWN;
				tstate = RDMAIP_PORT_TRANSITION_DOWN;
				RDMAIP_DBG3("Port transition INIT to DOWN\n");
			}
		} else {
			tstate = RDMAIP_PORT_TRANSITION_NOOP;
			pr_warn("rdmaip: %s active bonding is disabled using sysctl\n",
				rdmaip_dev->ibdev->name);
		}
		break;

	case RDMAIP_PORT_DOWN:
		if (rdmaip_port_all_layers_up(&ip_config[port])) {
			ip_config[port].port_state = RDMAIP_PORT_UP;
			tstate = RDMAIP_PORT_TRANSITION_UP;
			RDMAIP_DBG3("Port transition DOWN to UP\n");
		}
		break;

	case RDMAIP_PORT_UP:
		if (!rdmaip_port_all_layers_up(&ip_config[port])) {
			ip_config[port].port_state = RDMAIP_PORT_DOWN;
			tstate = RDMAIP_PORT_TRANSITION_DOWN;
			RDMAIP_DBG3("Port transition UP to DOWN\n");
		}
		break;

	default:
		pr_err("rdmaip: INVALID port_state %d port index %u devname %s\n",
		       ip_config[port].port_state, port,
		       ip_config[port].netdev->name);
	}
	return tstate;
}

static void rdmaip_update_port_status_all_layers(u8 port, int event_type,
						 int event)
{
	switch (event_type) {
	case RDMAIP_EVENT_IB:
		if (event == IB_EVENT_PORT_ACTIVE) {
			ip_config[port].port_layerflags |=
				RDMAIP_PORT_STATUS_HWPORTUP;
		} else {
			ip_config[port].port_layerflags &=
				~RDMAIP_PORT_STATUS_HWPORTUP;
		}
		break;
	case RDMAIP_EVENT_NET:
	case RDMAIP_EVENT_NONE:
		/*
		 * On VM, in some cases, Port up event was not delivered.
		 * So,  Update hw port status if netdev Lower link status
		 * is UP.
		 */
		if (rdmaip_is_link_layer_up(ip_config[port].netdev)) {
			ip_config[port].port_layerflags |=
				RDMAIP_PORT_STATUS_HWPORTUP;
		} else {
			ip_config[port].port_layerflags &=
			~RDMAIP_PORT_STATUS_HWPORTUP;
		}
		break;
	}

	if (ip_config[port].netdev->flags & IFF_UP)
		ip_config[port].port_layerflags |= RDMAIP_PORT_STATUS_NETDEVUP;
	else
		ip_config[port].port_layerflags &= ~RDMAIP_PORT_STATUS_NETDEVUP;

	if (rdmaip_is_link_layer_up(ip_config[port].netdev))
		ip_config[port].port_layerflags |= RDMAIP_PORT_STATUS_LINKUP;
	else
		ip_config[port].port_layerflags &= ~RDMAIP_PORT_STATUS_LINKUP;

	RDMAIP_DBG2("Overall netdev %s Port %d Status 0x%x\n",
		    ip_config[port].if_name, port,
		    ip_config[port].port_layerflags);

	RDMAIP_DBG2("rdmaip_port state %s\n",
		    rdmaip_portstate2name(ip_config[port].port_state));
}

static void rdmaip_sched_failover_failback(struct net_device *netdev, u8 port,
					   int port_transition_to)
{
	struct rdmaip_port_ud_work	*work;

	if (port_transition_to == RDMAIP_PORT_TRANSITION_UP) {
		/*
		 * TBD:  Investigate whether rdmaip_failback() can be
		 * called directly from here instead of a defered task.
		 */
		work = kzalloc(sizeof(*work), GFP_ATOMIC);
		if (!work) {
			RDMAIP_DBG1("rdmaip: failed to allocate port work\n");
			return;
		}
		work->port = port;
		work->netdev = netdev;
		if (rdmaip_active_bonding_failback) {
			RDMAIP_DBG2("Schedule failback\n");
			INIT_DELAYED_WORK(&work->work, rdmaip_failback);
			queue_delayed_work(rdmaip_wq, &work->work,
				rdmaip_get_failback_sync_jiffies(&ip_config[port]));
		} else
			kfree(work);
	} else {
		RDMAIP_DBG2("Schedule failover\n");
		rdmaip_failover(port);
	}
}

static void rdmaip_process_async_event(u8 port, int event_type, int event)
{
	int port_tstate = RDMAIP_PORT_TRANSITION_NOOP;
	struct net_device *netdev;

	netdev = ip_config[port].netdev;

	/*
	 * Network service disables active bonding temporarily
	 * when user stops network service and re-enables
	 * when user restarts the network service. There is
	 * need to failover or failback during that period.
	 * The port state is set to RDMAIP_PORT_INIT to indicate
	 * it needs do failover/failback as needed when the
	 * network serivice restarts.
	 */
	if (!rdmaip_sysctl_active_bonding) {
		RDMAIP_DBG2("Skip failover and failback %s\n",
			    ip_config[port].if_name);
		ip_config[port].port_state = RDMAIP_PORT_INIT;
		ip_config[port].ip_active_port = port;
		return;
	}

	rdmaip_update_port_status_all_layers(port, event_type, event);
	port_tstate = rdmaip_find_port_tstate(port);

	/*
	 * Log the event details and its disposition
	 */
	pr_notice("rdmaip: NET-EVENT: %s, PORT %s/port_%d/%s : %s%s (portlayers 0x%x)\n",
		  ((event_type == RDMAIP_EVENT_IB) ?
		  ib_event_msg(event) : rdmaip_netdevevent2name(event)),
		  ip_config[port].rdmaip_dev->ibdev->name,
		  ip_config[port].port_num, netdev->name,
		  (port_tstate == RDMAIP_PORT_TRANSITION_NOOP ?
		  "port state transition NONE - port retained in state " :
		  "port state transition to "),
		  rdmaip_portstate2name(ip_config[port].port_state),
		  ip_config[port].port_layerflags);

	if ((port_tstate == RDMAIP_PORT_TRANSITION_NOOP) ||
		!ip_config[port].failover_group)
		return;

	rdmaip_sched_failover_failback(netdev, port, port_tstate);
}

static void rdmaip_impl_ib_event_handler(struct work_struct *_work)
{

	struct rdmaip_device		*rdmaip_dev;
	u8				port;
	bool				found = false;
	struct rdmaip_port_ud_work	*work =
		container_of(_work, struct rdmaip_port_ud_work, work.work);

	rdmaip_dev = work->rdmaip_dev;

	RDMAIP_DBG2("rdmaip: RDMA device %s ip_port_cnt %d, event: %s\n",
		    rdmaip_dev->ibdev->name, ip_port_cnt,
		    ib_event_msg(work->ib_event));

	for (port = 1; port <= ip_port_cnt; port++) {
		if (ip_config[port].port_num != work->ib_port ||
			ip_config[port].rdmaip_dev != rdmaip_dev)
			continue;
		found = true;
		break;
	}

	if (!found) {
		RDMAIP_DBG2("ERROR: No rdmaip_port found\n");
		return;
	}

	mutex_lock(&rdmaip_global_flag_lock);
	if (rdmaip_is_busy_flag_set()) {
		rdmaip_set_event_pending();
		RDMAIP_DBG2("Busy flag is set: skip ibevent processing\n");
		mutex_unlock(&rdmaip_global_flag_lock);
		return;
	}
	mutex_unlock(&rdmaip_global_flag_lock);

	RDMAIP_DBG2("PORT %s/port_%d/%s received PORT-EVENT %s\n",
		    rdmaip_dev->ibdev->name, work->ib_port,
		    ip_config[port].if_name, ib_event_msg(work->ib_event));

	if (work->ib_event == IB_EVENT_PORT_ACTIVE)
		ip_config[port].port_active_ts = get_jiffies_64();

	rdmaip_process_async_event(port, RDMAIP_EVENT_IB, work->ib_event);

	kfree(work);
}

/*
 * rdmaip_event_handler is called by the IB core subsystem
 * to inform certain RDMA events. See ib_event_type definitions
 * in include/rdma/ibverbs.h for more details.
 *
 * RDMAIP module is only interested about the below two events
 * and all other events are ignore for now.
 *     1. IB_EVENT_PORT_ACTIVE - IB core sends this event when
 *                               physical RDMA port is active and
 *                               ready for use
 *     2. IB_EVENT_PORT_ERR    - IB core sends this event when
 *                               physical port is down
 *
 * If the event handler is called before initialzaing the
 * ip_config  structure, ignore the event.
 * Otherwise, update the port state in the ip_config and schedule
 * failover or failback as needed.
 * Process the events in a separate kernel thread.
 */
static void rdmaip_event_handler(struct ib_event_handler *handler,
				 struct ib_event *event)
{
	struct rdmaip_port_ud_work	*work;

	if (!ip_port_cnt || (rdmaip_init_flag & RDMAIP_TEARDOWN_IN_PROGRESS))
		return;

	if (event->event != IB_EVENT_PORT_ACTIVE &&
		event->event != IB_EVENT_PORT_ERR) {
		return;
	}

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work) {
		RDMAIP_DBG1("rdmaip: failed to allocate port work\n");
		return;
	}

	work->rdmaip_dev	= container_of(handler,
					typeof(struct rdmaip_device),
					       event_handler);
	work->event_type	= RDMAIP_EVENT_IB;
	work->ib_event		= event->event;
	work->ib_port		= event->element.port_num;

	INIT_DELAYED_WORK(&work->work, rdmaip_impl_ib_event_handler);
	queue_delayed_work(rdmaip_wq, &work->work, 0);

	RDMAIP_DBG2("Queued IB event handler to process events : %s\n",
		    ib_event_msg(work->ib_event));
}

static void rdmaip_update_ip_addrs(int port)
{
	struct inet6_dev  *in6_dev;
	struct net_device *ndev;
	struct in_device *in_dev;

	ndev = ip_config[port].netdev;
	if (!ndev)
		return;

	/*
	 * All IP addresses may be configured when
	 * rdmaip_port was initialized during boot.
	 * Re_read the IP addresses again to capture
	 * all the IP addresses before doing the
	 * initial failovers.
	 */
	in_dev = in_dev_get(ndev);
	if (in_dev) {
		rdmaip_init_ip4_addrs(ndev, in_dev, port);
		in_dev_put(in_dev);
	}

	in6_dev = in6_dev_get(ndev);
	if (in6_dev) {
		rdmaip_init_ip6_addrs(ndev, in6_dev, port);
		in6_dev_put(in6_dev);
	}
}

static void rdmaip_do_initial_failovers(void)
{
	unsigned int ii;
	unsigned int ports_deactivated = 0;
	int ret = 0;

	/*
	 * Scan all ports and mark them UP/DOWN based on
	 * detections of port_layerflags!
	 */
	for (ii = 1; ii <= ip_port_cnt; ii++) {
		if (rdmaip_port_all_layers_up(&ip_config[ii])) {
			ip_config[ii].port_state = RDMAIP_PORT_UP;
			pr_notice("rdmaip_do_initial_failover:  port index %u interface %s transitioned from INIT to UP state (portlayers 0x%x)\n",
				  ii, ip_config[ii].netdev->name,
				  ip_config[ii].port_layerflags);
		} else {
			ip_config[ii].port_state = RDMAIP_PORT_DOWN;
			pr_notice("rdmaip_do_initial_failover: port index %u interface %s transitioned from INIT to DOWN state (portlayers 0x%x)\n",
				  ii, ip_config[ii].netdev->name,
				  ip_config[ii].port_layerflags);
		}
		ip_config[ii].ip_active_port = ii; /* starting at home base! */

		/*
		 * As a part of the NETDEV_UP event handling, rdmaip_init_port()
		 * is called which updates the IP address each port. But at
		 * this time, IP addresses may not be available. So, try to
		 * reinitialize IP addresses again before doing initial
		 * failovers.
		 */
		if (ip_config[ii].netdev) {
			RDMAIP_DBG2("Update IP addresses for %s -  do_initial_failover\n",
				    ip_config[ii].netdev->name);
			rdmaip_update_ip_addrs(ii);
		}
	}

	/*
	 * Now do failover for ports that are down!
	 */
	for (ii = 1; ii <= ip_port_cnt; ii++) {
		/* Failover the port */
		if ((ip_config[ii].port_state == RDMAIP_PORT_DOWN) &&
		    (ip_config[ii].failover_group) &&
		    (RDMAIP_PORT_ADDR_SET(ii))) {

			rdmaip_do_failover(ii, 0);

			/*
			 * reset IP addr of DOWN port to 0 if the
			 * failover did not succeed !
			 * Note: rdmaip_do_failover() logs successful migrations
			 * but not unsuccesful ones. We log unsuccessful
			 * attempts for this instance here and deactivate the
			 * port by its IP address!
			 */
			if (ip_config[ii].ip_active_port == ii) {
				pr_notice("rdmaip: IP %pI4 deactivated on interface %s (no suitable failover target available)\n",
					  &ip_config[ii].ip_addr,
					  ip_config[ii].netdev->name);

				ret = rdmaip_set_ip4(NULL, NULL,
						    ip_config[ii].if_name,
						    0, 0, 0);
				(void)rdmaip_clear_ip6(ii);
				ports_deactivated++;

			}
		}
	}

}

static void rdmaip_initial_failovers(struct work_struct *workarg)
{
	bool do_failover;

	if (rdmaip_init_flag & RDMAIP_TEARDOWN_IN_PROGRESS) {
		RDMAIP_DBG2("Teardown in progress\n");
		return;
	}

	if (!rdmaip_sysctl_trigger_active_bonding) {
		/*
		 * Normally trigger set by network init
		 * script as signal that network devices
		 * config/setup scripts have been run and
		 * we can proceed with active bonding failovers
		 * etc now!
		 * If trigger not set, defer, unless we have
		 * reached a max timeout!
		 */
		if (timeout_until_initial_failovers > 0) {
			timeout_until_initial_failovers -=
			  msecs_to_jiffies(100);
			queue_delayed_work(rdmaip_wq, &riif_dlywork,
					   msecs_to_jiffies(100));
			initial_failovers_iterations++;
			return;
		}
		/*
		 * timeout exceeed, we set the trigger to a
		 * distinctive value to indicated that
		 * we did it due to timeout exceeded (network
		 * init script normally sets it to 1)
		 */
		rdmaip_sysctl_trigger_active_bonding = 999;

		pr_info("rdmaip: Triggering initial failovers after max time");
	} else {
		pr_info("rdmaip: Triggering initial failovers(itercount %d)\n",
			initial_failovers_iterations);
	}

	rdmaip_ip_config_init();
	rdmaip_init_flag |= RDMAIP_IP_CONFIG_INIT_DONE;
	rdmaip_ip_failover_groups_init();

	do_failover = true;

	mutex_lock(&rdmaip_global_flag_lock);
	rdmaip_clear_event_pending();
	mutex_unlock(&rdmaip_global_flag_lock);

	while (do_failover) {
		rdmaip_do_initial_failovers();

		mutex_lock(&rdmaip_global_flag_lock);
		if (rdmaip_is_event_pending()) {
			rdmaip_clear_event_pending();
			RDMAIP_DBG2("Event pending, do failovers again\n");
		} else {
			do_failover = false;
			rdmaip_clear_busy_flag();
			RDMAIP_DBG2("No pending event, Clear busy flag\n");
		}
		mutex_unlock(&rdmaip_global_flag_lock);
	}
	rdmaip_dump_ip_config();
}


/*
 * Scheduling initial failovers. The ASCII art below documents the startup
 * timeline of events of significance related to activation of active
 * bonding initial failovers after reboot.
 *               ---
 *                V
 *                |
 *             t0 | <reboot>
 *                |
 *             t1 |<-- (1) rdma service  inits IB and RoCE
 *                |    interfaces. Scripts
 *                |    /etc/sysconfig/network-scripts/ifcfg-*
 *                |    are run to bring interfaces UP.
 *             t2 |<-- (2) RDMAIP module init code runs on module
 *                |    load which initializes ip_config[] array based
 *                |    on IB device based on kernel global &init_net
 *                |    list; "initialization phase" is started  and
 *                |    rdmaip_initial_failovers() scheduled to run
 *                |    first time!
 *             t3 |<-- (3) network init script (S10network) runs which
 *                |    also inits all networking devices (including IB).
 *                |    Scripts /etc/sysconfig/network-scripts/ifcfg-*
 *                |    are run AGAIN!
 *             t4 |<-- (4) sysctl rdmaip_sysctl_trigger_active_bonding
 *                |    is run in network init script(S10network) *after*
 *                |    attempting bringing up regular IB, RoCE and VLAN
 *                |    devices as part of step(3) above.
 *             t5 |<-- As scheduled in step(2)
 *                |    rdmaip_initial_failovers() runs at t5. If t5 < t4
 *                |    (rdmaip_sysctl_trigger_active_bonding is NOT set)
 *                |    it reschedules itself after short duration
 *                |    (100 jiffies) until t5 > t4 (i.e.
 *                |    rdmaip_sysctl_trigger_active_bonding IS set).
 *                |    Then it calls rdmaip_do_initial_failovers() to
 *                |    actually do the failovers and ends the
 *                |    "initialization phase". [ Note: to take care of
 *                |    cases where older init scripts are run with
 *                |    newer kernels (not recommended!)
 *                |    rdmaip_do_initial_failovers() runs anyway after
 *                |    a conservative max timeout interval expires. ]
 *                .
 *                .
 *                .
 *                V
 */
static void
rdmaip_sched_initial_failovers(void)
{

	if (rdmaip_trigger_delay_min_msecs >=
	    rdmaip_trigger_delay_max_msecs) {
		/*
		 * If these parameters are set inconsistently using
		 * module parameters, try to recover from it by deriving
		 * reasonable values such that max > min
		 */
		rdmaip_trigger_delay_max_msecs =
			rdmaip_trigger_delay_min_msecs + 10000;
	}

	timeout_until_initial_failovers =
		msecs_to_jiffies(rdmaip_trigger_delay_max_msecs);

	queue_delayed_work(rdmaip_wq, &riif_dlywork,
			   msecs_to_jiffies(rdmaip_trigger_delay_min_msecs));
}

static int rdmaip_is_roce_netdev(u8 port, struct rdmaip_device *rdmaip_dev,
				 struct net_device *real_dev)
{
	union ib_gid		gid;
	int index, found = 0;
	int gid_tbl_len;

	gid_tbl_len = rdmaip_dev->pinfo[port - 1].gid_tbl_len;
	for (index = 0; index < gid_tbl_len; index++) {
		if (!rdma_query_gid(rdmaip_dev->ibdev, port, index, &gid)) {
			const struct ib_gid_attr *attrp =
				rdma_find_gid(rdmaip_dev->ibdev, &gid,
					IB_GID_TYPE_ROCE, NULL);

				if (!IS_ERR(attrp)) {
					struct net_device *ndev = attrp->ndev;

					rdma_put_gid_attr(attrp);

					if (ndev == real_dev) {
						found = 1;
						break;
					}
				}
		}
	}

	return found;
}

/*
 * rdmaip_is_roce_device() returns if the netdevice (physical or vlan)
 * is associated with the RDMA device. If the netdevice is vlan, then
 * get the parenat netdevcice associaged with vlan and verify that the
 * device is roce. This function also returns the port number.
 */

static struct rdmaip_device *rdmaip_is_roce_device(struct net_device *dev,
						   u8 *port_num)
{
	struct rdmaip_device	*rdmaip_dev;
	struct net_device *real_dev = rdma_vlan_dev_real_dev(dev) ? : dev;
	int port, nports, found = 0;

	list_for_each_entry_rcu(rdmaip_dev, &rdmaip_devlist_head, list) {
		nports = rdmaip_dev->ibdev->phys_port_cnt;
		for (port = 1; (port <= nports) && (!found); port++) {
			found = rdmaip_is_roce_netdev(port, rdmaip_dev,
						      real_dev);
			if (found && port_num)
				*port_num = port;
		}
		if (found)
			break;
	}
	return found ? rdmaip_dev : NULL;
}

/*
 * Returns rdmaip_dev correstpoing to the netdevice if ndev a RDMA capable
 * adapter. Otherwise, it returns NULL. Also, it returns Pkey for IB devices
 * and Vlan ID for RoCE devices
 */
static struct rdmaip_device *rdmaip_get_rdmaip_dev(struct net_device *ndev,
						   u16 *pkey_vid, u8  *port_num)
{
	struct rdmaip_device	*rdmaip_dev = NULL;
	union ib_gid		gid;

	if (ndev->type == ARPHRD_INFINIBAND) {

		/*
		 * The netdev is IPoIB device. Each IPoIB device is
		 * associated with a particular pkey. Pkey (Partition
		 * key) is like VLAN ID. IPoIB devices with same
		 * PKEY can communicate with eadh other. Get the
		 * Pkey and save it. This is used when identifying
		 * correct netdev to fail over/failback.
		 *
		 * TBD: This call creates a dependency on IPoIB for
		 * resilient_rdmaip. A generic API is needed to
		 * eliminate this dependency
		 */
		if (ipoib_get_netdev_pkey(ndev, pkey_vid)) {
			pr_err("rdmaip: failed to get pkey for netdev %s\n",
			       ndev->name);
			return NULL;
		}


		/*
		 * IPoIB netdev MAC address is a tuple of
		 * 1) QP number - 4 byte long
		 * 2) 8 byte GID prefix
		 * 3) 8 byte Port GUID.
		 *
		 * Get the rdmaip_dev associagted with the netdevice
		 */

		memcpy(&gid, ndev->dev_addr + 4, sizeof(gid));
		list_for_each_entry_rcu(rdmaip_dev, &rdmaip_devlist_head,
		    list) {
                        const struct ib_gid_attr *attrp =
                                rdma_find_gid(rdmaip_dev->ibdev, &gid,
                                    IB_GID_TYPE_IB, NULL);

                        if (!IS_ERR(attrp)) {
                                rdma_put_gid_attr(attrp);
                                return rdmaip_dev;
                        }
		}
	} else {
		*pkey_vid = rdma_vlan_dev_vlan_id(ndev);
		rdmaip_dev = rdmaip_is_roce_device(ndev, port_num);
	}

	return rdmaip_dev;
}

/*
 * rdmaip_ip_config_init() function initializes the active bonding groups
 * in ip_config global structure. It loops through all the available net
 * devices in the systems and initializes the ip_config with RDMA capable
 * net devices.
 */
static void rdmaip_ip_config_init(void)
{
	struct net_device	*dev;
	struct in_device	*in_dev;
	struct inet6_dev	*in6_dev;
	struct rdmaip_device	*rdmaip_dev;
	u8                      ret = 1, port_num;

	read_lock(&dev_base_lock);
	for_each_netdev(&init_net, dev) {
		in_dev = in_dev_get(dev);
		if (rdmaip_inet6_socket)
			in6_dev = in6_dev_get(dev);
		else
			in6_dev = NULL;

		/*
		 * Enumerate all Infiniband and RoCE devices that
		 * are UP and not part of a bond(master or slave)
		 */
		if (((dev->type == ARPHRD_INFINIBAND) ||
		   (dev->type == ARPHRD_ETHER)) &&
		   !(dev->flags & IFF_SLAVE) &&
		   !(dev->flags & IFF_MASTER) &&
		   in_dev) {
			u16 pkey_vid = 0; /* Pkey for IB/Vlan id of Ethernet */

			/*
			 * We are interested only RDMA capable adapters.
			 * rdmaip_get_rdmaip_dev returns NULL if "dev" is not
			 * a RDMA capable adapter.
			 */
			rdmaip_dev = rdmaip_get_rdmaip_dev(dev, &pkey_vid,
							   &port_num);
			if (rdmaip_dev) {
				ret = rdmaip_init_port(rdmaip_dev, dev,
						       port_num, pkey_vid,
						       in_dev, in6_dev);
			}
		}

		if (in_dev)
			in_dev_put(in_dev);

		if (in6_dev)
			in6_dev_put(in6_dev);

		if (ret == 0) {
			RDMAIP_DBG2("Max number of port exceeded\n");
			break;
		}
	}
	read_unlock(&dev_base_lock);
}

static int rdmaip_init_exclude_ip_tbl(char *str)
{
	char *tok, *nxt_tok, *prefix_str;
	unsigned int octet_cnt = 0;
	unsigned long prefix = 0;
	__be32  ip = 0;

	prefix_str = strchr(str, '/');
	if (prefix_str) {
		*prefix_str = '\0';
		prefix_str++;
		if (kstrtol(prefix_str, 0, &prefix)) {
			pr_warn("rdmaip: IP prefix %s improperly formatted\n",
				prefix_str);
			return 1;
		}
		if (prefix > 32) {
			pr_warn("rdmaip: IP prefix %lu out of range\n", prefix);
			return 1;
		}
		tok = str;
		while (tok && octet_cnt < 4) {
			unsigned long octet;

			nxt_tok = strchr(tok, '.');
			if (nxt_tok) {
				*nxt_tok = '\0';
				nxt_tok++;
			}
			if (kstrtoul(tok, 0, &octet)) {
				pr_warn("rdmaip: IP octet %s improperly formatted\n",
					tok);
				return 1;
			}
			if (octet > 255) {
				pr_warn("rdmaip: IP octet %lu out of range\n",
					octet);
				return 1;
			}
			((unsigned char *)&ip)[octet_cnt] =
				(unsigned char)octet;
			octet_cnt++;
			tok = nxt_tok;
		}

		if (tok) {
			pr_warn("rdmaip: IP %s is improperly formatted\n", str);
			return 1;
		}
	} else {
		pr_warn("rdmaip: IP prefix not specified\n");
		return 1;
	}

	exclude_ips_tbl[exclude_ips_cnt].ip = ip;
	exclude_ips_tbl[exclude_ips_cnt].prefix = prefix;
	exclude_ips_tbl[exclude_ips_cnt].mask = inet_make_mask(prefix);

	exclude_ips_cnt++;

	return 0;
}

void rdmaip_read_exclude_ip_list(void)
{
	char *tok, *nxt_tok;
	char str[1024];

	if (!rdmaip_ipv4_exclude_ips_list)
		return;

	strcpy(str, rdmaip_ipv4_exclude_ips_list);

	tok = str;
	while (tok) {
		nxt_tok = strchr(tok, ',');
		if (nxt_tok) {
			*nxt_tok = '\0';
			nxt_tok++;
		}

		if (rdmaip_init_exclude_ip_tbl(tok))
			return;

		tok = nxt_tok;
	}
}

/*
 * Read the user specified active bonding groups information.
 * Module parameter "rdmaip_active_bonding_failover_groups"
 * contains the user specified active bonding groups. The
 * format of the groups is
 *        "<ifname>[,<ifname>]*[;<ifname>[,<ifname>]*]*");
 * If user specified any active bond groups, use them. Otherwise
 * create default active bonding groups across the ports of
 * the same RDMA device.
 */
void rdmaip_ip_failover_groups_init(void)
{
	char *tok, *grp, *nxt_tok, *nxt_grp;
	char str[1024];
	unsigned int	grp_id = 1;
	int i;
	struct rdmaip_device *rdmaip_dev;

	if (!rdmaip_active_bonding_failover_groups) {
		list_for_each_entry_rcu(rdmaip_dev,
					&rdmaip_devlist_head, list) {
			for (i = 1; i <= ip_port_cnt; i++) {
				if (ip_config[i].rdmaip_dev == rdmaip_dev)
					ip_config[i].failover_group = grp_id;
			}
			grp_id++;
		}
		return;
	}

	strcpy(str, rdmaip_active_bonding_failover_groups);
	nxt_grp = strchr(str, ';');
	if (nxt_grp) {
		*nxt_grp = '\0';
		nxt_grp++;
	}
	grp = str;
	while (grp) {
		tok = grp;
		nxt_tok = strchr(tok, ',');
		if (nxt_tok) {
			*nxt_tok = '\0';
			nxt_tok++;
		}
		while (tok) {
			for (i = 1; i <= ip_port_cnt; i++) {
				if (!strcmp(tok, ip_config[i].if_name)) {
					ip_config[i].failover_group = grp_id;
					break;
				}
			}
			tok = nxt_tok;
			if (nxt_tok)
				nxt_tok = strchr(nxt_tok, ',');
			if (nxt_tok) {
				*nxt_tok = '\0';
				nxt_tok++;
			}
		}

		grp = nxt_grp;
		if (nxt_grp)
			nxt_grp = strchr(nxt_grp, ';');
		if (nxt_grp) {
			*nxt_grp = '\0';
			nxt_grp++;
		}
		grp_id++;
	}
}

/*
 * The IB stack is letting us know that a new RDMA device is available for use.
 * This happens when RDMA device registers with IB core.
 */
static void rdmaip_device_add(struct ib_device *device)
{
	struct rdmaip_device *rdmaip_dev;
	struct ib_port_attr port_attr;
	int i, ret;

	RDMAIP_DBG2("RDMA device %p name: %s num_ports: %u\n", device,
		    device->name, device->phys_port_cnt);

	/* Only handle IB and RoCE (no iWARP) devices */
	if (device->node_type != RDMA_NODE_IB_CA)
		return;

	rdmaip_dev = kzalloc_node(sizeof(struct rdmaip_device), GFP_KERNEL,
				ibdev_to_rdmaipdev(device));
	if (!rdmaip_dev) {
		RDMAIP_DBG2("Failed to allocate memory for rdmaip_dev\n");
		return;
	}
	rdmaip_dev->ibdev = device;

	INIT_IB_EVENT_HANDLER(&rdmaip_dev->event_handler,
			      rdmaip_dev->ibdev, rdmaip_event_handler);
	ib_register_event_handler(&rdmaip_dev->event_handler);

	for (i = 1; i <= device->phys_port_cnt; i++) {
		ret = ib_query_port(device, device->phys_port_cnt, &port_attr);
		if (ret) {
			RDMAIP_DBG2("ib_query_port failed %d\n", ret);
			rdmaip_dev->pinfo[i - 1].gid_tbl_len =
				RDMAIP_DEFAULT_GIDTBL_LEN;
		} else
			rdmaip_dev->pinfo[i - 1].gid_tbl_len =
				port_attr.gid_tbl_len;

		RDMAIP_DBG2("Gid table len %d for the port %d\n",
			    rdmaip_dev->pinfo[i-1].gid_tbl_len, i);
	}

	down_write(&rdmaip_devlist_lock);
	list_add_tail_rcu(&rdmaip_dev->list, &rdmaip_devlist_head);
	up_write(&rdmaip_devlist_lock);

	ib_set_client_data(device, &rdmaip_client, rdmaip_dev);
}

/*
 * The IB stack is letting us know that a device is going away.
 * This happens when RDMA device un-registers with IB core.
 */
static void rdmaip_device_remove(struct ib_device *device, void *client_data)
{
	struct rdmaip_device *rdmaip_dev;
	int i;

	RDMAIP_DBG2("Removing RDMA device: %p name: %s num_ports: %u\n",
		    device, device->name, device->phys_port_cnt);

	rdmaip_dev = ib_get_client_data(device, &rdmaip_client);
	if (!rdmaip_dev) {
		RDMAIP_DBG2("rdmaip_dev is NULL, ib_device %p\n", device);
		return;
	}

	for (i = 1; i <= ip_port_cnt; i++) {
		if (ip_config[i].rdmaip_dev == rdmaip_dev)
			ip_config[i].rdmaip_dev = NULL;
	}
	ib_unregister_event_handler(&rdmaip_dev->event_handler);

	/* stop connection attempts from getting a reference to this device. */
	ib_set_client_data(device, &rdmaip_client, NULL);

	down_write(&rdmaip_devlist_lock);
	list_del_rcu(&rdmaip_dev->list);
	up_write(&rdmaip_devlist_lock);

	RDMAIP_DBG2("Deallocating  rdmaip_dev %p name: %s\n",
		    rdmaip_dev, device->name);
	kfree(rdmaip_dev);
}


static void rdmaip_update_ip_config(void)
{
	struct net_device	*dev;
	struct in_device	*in_dev;
	struct inet6_dev	*in6_dev;
	int			i;

	read_lock(&dev_base_lock);
	for_each_netdev(&init_net, dev) {
		in_dev = in_dev_get(dev);
		in6_dev = in6_dev_get(dev);
		if (in_dev || in6_dev) {
			for (i = 1; i <= ip_port_cnt; i++) {
				if (!strcmp(dev->name, ip_config[i].if_name)) {
					if (ip_config[i].netdev != dev) {
						ip_config[i].netdev = dev;
						RDMAIP_DBG2("RDMA device %s/port_%d/%s updated",
							    ip_config[i].rdmaip_dev->ibdev->name,
							    ip_config[i].port_num,
							    dev->name);
					}
				}
			}
			if (in_dev)
				in_dev_put(in_dev);
			if (in6_dev)
				in6_dev_put(in6_dev);
		}
	}
	read_unlock(&dev_base_lock);
}

static void rdmaip_portstate_delayed_initswitch(struct work_struct *_work)
{
	struct rdmaip_port_ud_work      *work =
		container_of(_work, struct rdmaip_port_ud_work, work.work);
	u8 port = work->port;

	if (ip_config[port].port_state != RDMAIP_PORT_INIT) {
		/*
		 * We moved out of INIT state likely in NETDEV_CHANGE handler
		 * processing after initialization workqueue triggered by
		 * NETDEV_UP processing after a added interface!
		 * Nothing to do - we are done!
		 */
		kfree(work);
		return;
	}

	/*
	 * We get here when we had a "perfect race" in a narrow
	 * window when both netdev notifier callback and workqueue
	 * execute simultaneously on separate threads.
	 *
	 * (a) The NETDEV_CHANGE handler ran ahead
	 * of or during workqueue executed initializaion
	 * (but missed the new interface)
	 * (b) The workqueue initialization code missed detecting
	 *     the link layer UP state!
	 *
	 * Maintain layer flags for port still in INIT state
	 */

	/* Paranoia assertion and NETDEV layer */
	if (!(ip_config[port].netdev->flags & IFF_UP) ||
	    !((ip_config[port].port_layerflags & RDMAIP_PORT_STATUS_NETDEVUP)
	      == RDMAIP_PORT_STATUS_NETDEVUP)) {
		pr_warn("rdmaip: Device %s flag NOT marked UP in delayed INIT transition processing!\n",
			ip_config[port].netdev->name);
		/* init port layer flag after warning! */
		if (ip_config[port].netdev->flags & IFF_UP)
			ip_config[port].port_layerflags |=
				RDMAIP_PORT_STATUS_NETDEVUP;
		else
			ip_config[port].port_layerflags &=
				~RDMAIP_PORT_STATUS_NETDEVUP;
	}
	/* Check on link (and hw) layers */
	if (rdmaip_is_link_layer_up(ip_config[port].netdev)) {
		ip_config[port].port_layerflags |= RDMAIP_PORT_STATUS_LINKUP;
		ip_config[port].port_layerflags |= RDMAIP_PORT_STATUS_HWPORTUP;
	} else {
		ip_config[port].port_layerflags &= ~RDMAIP_PORT_STATUS_LINKUP;
	}

	/*
	 * Mark the port state UP or DOWN
	 */
	if (rdmaip_port_all_layers_up(&ip_config[port])) {
		ip_config[port].port_state = RDMAIP_PORT_UP;
		pr_notice("rdmaip: delayed INIT transition: port index %u interface %s transitioned from INIT to UP state(portlayers 0x%x)\n",
			  port, ip_config[port].netdev->name,
			  ip_config[port].port_layerflags);
	} else {
		ip_config[port].port_state = RDMAIP_PORT_DOWN;
		pr_notice("rdmaip: delayed INIT transition: port index %u interface %s transitioned from INIT to DOWN state(portlayers 0x%x)\n",
			  port, ip_config[port].netdev->name,
			  ip_config[port].port_layerflags);
	}
	kfree(work);
}

static int rdmaip_get_port_index(struct net_device *ndev)
{
	int i;

	for (i = 1; i <= ip_port_cnt; i++) {
		if (!strcmp(ndev->name, ip_config[i].if_name) &&
			ip_config[i].rdmaip_dev) {
			return i;
		}
	}
	return 0;
}

static void rdmaip_addintf_after_initscripts(struct work_struct *_work)
{
	struct rdmaip_port_ud_work      *work =
		container_of(_work, struct rdmaip_port_ud_work, work.work);
	struct net_device *ndev = work->netdev;
	struct in_device	*in_dev;
	struct inet6_dev	*in6_dev;
	struct rdmaip_device    *rdmaip_dev;
	u8                      port_num;
	u8                      port = 0;

	read_lock(&dev_base_lock);
	in_dev = in_dev_get(ndev);

	if (rdmaip_inet6_socket)
		in6_dev = in6_dev_get(ndev);
	else
		in6_dev = NULL;

	if (((in_dev && !in_dev->ifa_list) ||
	     (in6_dev && list_empty(&in6_dev->addr_list))) &&
	     work->timeout > 0) {

		INIT_DELAYED_WORK(&work->work,
				  rdmaip_addintf_after_initscripts);
		work->timeout -= msecs_to_jiffies(100);
		queue_delayed_work(rdmaip_wq, &work->work,
				   msecs_to_jiffies(100));
	} else if ((in_dev && in_dev->ifa_list) ||
		(in6_dev && !list_empty(&in6_dev->addr_list))) {

		u16 pkey_vid = 0;

		rdmaip_dev = rdmaip_get_rdmaip_dev(ndev, &pkey_vid, &port_num);
		if (!rdmaip_dev)
			RDMAIP_DBG2("netdevice %s has no associated port\n",
				    ndev->name);
		else {

			if (rdmaip_get_port_index(ndev)) {
				RDMAIP_DBG2("rdmaip: Port already exists in ip_config\n");
				goto out;
			}

			port = rdmaip_init_port(rdmaip_dev, ndev, port_num,
						pkey_vid, in_dev, in6_dev);
			if (port > 0) {

				/*
				 * We just added a new interface, which is in
				 * INIT state and it needs to go to UP or DOWN
				 * depending on various layers being UP or down
				 * (layerflags will be clear in the newly added
				 *  port entry!)
				 *
				 * (1) This routine processing triggered by a
				 *     NETDEV_UP event so we can safely mark
				 *     that layer UP. (Note: No
				 *     failback/failover processing done for
				 *     this initial NETDEV_UP event for a new
				 *     device!)
				 */
				ip_config[port].port_layerflags |=
					RDMAIP_PORT_STATUS_NETDEVUP;
				if (!(ip_config[port].netdev->flags & IFF_UP)) {
					pr_warn("rdmaip: Device %s flag NOT marked UP in "
						"NETDEV_UP(addintf after initscripts) processing!\n",
						ip_config[port].netdev->name);
				}
				/*
				 * (2) Note:
				 *   While we were scheduled and run from
				 *   workqueue link layer may have come up and
				 *   NETDEV_CHANGE event processing missed us.
				 *   (If NETDEV_CHANGE processing happens later
				 *   we will have updates in the netdev callback
				 *   handler). If link layer is UP, we can set
				 *   the flags sooner rather than later.
				 *    (As usual we can mark HW layer UP if
				 *   link layer is up)
				 */
				if (rdmaip_is_link_layer_up(
						ip_config[port].netdev)) {
					ip_config[port].port_layerflags |=
						RDMAIP_PORT_STATUS_LINKUP;
					ip_config[port].port_layerflags |=
						RDMAIP_PORT_STATUS_HWPORTUP;
				}
				/*
				 * Mark the port state UP sooner if we can!
				 */
				if (rdmaip_port_all_layers_up(&ip_config[port]))
					ip_config[port].port_state =
						RDMAIP_PORT_UP;

				/*
				 * We could have missed NETDEV_CHANGE processing
				 * and link layer coming up still while
				 * executing rest of this function. We will
				 * schedule something for later to fix port
				 * state if needed.
				 */
			}
		}
		rdmaip_ip_failover_groups_init();
		/*
		 * If port is still in INIT state, schedule
		 * a call to check back later.
		 */

		if (port && ip_config[port].port_state == RDMAIP_PORT_INIT) {
			work->port = port;
			INIT_DELAYED_WORK(&work->work,
					  rdmaip_portstate_delayed_initswitch);
			/* check after 10 seconds */
			queue_delayed_work(rdmaip_wq, &work->work,
					   msecs_to_jiffies(10000));
		} else {
			kfree(work);
		}
	} else if (!work->timeout)
		kfree(work);

out:
	if (in_dev)
		in_dev_put(in_dev);
	if (in6_dev)
		in6_dev_put(in6_dev);

	read_unlock(&dev_base_lock);
}

static void rdmaip_add_new_rdmaip_port(struct net_device *netdev, u8 port)
{
	struct rdmaip_port_ud_work *work;

	RDMAIP_DBG2("Adding to new netdev %s port%d to ip_config\n",
		    netdev->name, port);

	if ((netdev->flags & IFF_UP) && !(netdev->flags & IFF_SLAVE) &&
	   !(netdev->flags & IFF_MASTER)) {

		work = kzalloc(sizeof(*work), GFP_ATOMIC);
		if (work) {
			work->netdev = netdev;
			work->timeout = msecs_to_jiffies(10000);
			INIT_DELAYED_WORK(&work->work,
				  rdmaip_addintf_after_initscripts);
			queue_delayed_work(rdmaip_wq, &work->work,
					msecs_to_jiffies(100));
		} else
			RDMAIP_DBG2("Failed to allocated memory for work\n");
	}
}

static void rdmaip_impl_netdev_callback(struct work_struct *_work)
{
	u8 port = 0;
	struct rdmaip_port_ud_work	*work =
		container_of(_work, struct rdmaip_port_ud_work, work.work);
	long int event = work->net_event;
	struct net_device *ndev = work->netdev;

	mutex_lock(&rdmaip_global_flag_lock);
	if (rdmaip_is_busy_flag_set()) {
		rdmaip_set_event_pending();
		RDMAIP_DBG2("Busy flag is set: skip netevent processing\n");
		mutex_unlock(&rdmaip_global_flag_lock);
		return;
	}
	mutex_unlock(&rdmaip_global_flag_lock);

	/*
	 * Find the port by netdev->name and update ip_config if name exists
	 * but ndev has changed
	 */
	port = rdmaip_get_port_index(ndev);

	if (port && event == NETDEV_UP && ip_config[port].netdev != ndev)
		rdmaip_update_ip_config();

	if (!port && event == NETDEV_UP) {
		/*
		 * New port. Schedule new port initialization
		 * and bail out from here.
		 */
		rdmaip_add_new_rdmaip_port(ndev, port);
		return;
	}

	/*
	 * No matching port found in the ip_config.
	 */
	if (!port)
		return;

	/*
	 * Bail out here if we are racing with device teardown we are done!
	 * TBD: Should this be protected with a lock ?
	 */
	if (!ip_config[port].rdmaip_dev) {
		RDMAIP_DBG2("rdmaip_dev is NULL, device tearing down in progress\n");
		return;
	}

	RDMAIP_DBG2("PORT %s/port_%d/%s received NET-EVENT %s\n",
		    ip_config[port].rdmaip_dev->ibdev->name,
		    ip_config[port].port_num, ndev->name,
		    rdmaip_netdevevent2name(event));

	rdmaip_process_async_event(port, RDMAIP_EVENT_NET, event);

	kfree(work);
}

/*
 * Network stack calls this callback routine when ever there
 * is a status change for any netdev. We are interested only
 * about InfiniBand and RoCE (RDMA devices) related netdevs
 * and also NETDEV_UP, NETDEV_CHANGE and NETDEV_ERROR.
 *
 * This call back updates the interface status, and scheules
 * fail over or failback to move the IP's from one port to
 * another port as needed. If netdev is not known, then
 * it initiales a async request to initialize the ip_config
 * global structure with this new netdev information.
 */
static int rdmaip_netdev_callback(struct notifier_block *self,
				  unsigned long event, void *ctx)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ctx);
	struct rdmaip_port_ud_work	*work;

	if (rdmaip_init_flag & RDMAIP_TEARDOWN_IN_PROGRESS) {
		RDMAIP_DBG2("netdev %s event %lx teardown in progress\n",
			    ndev->name, event);
		return NOTIFY_DONE;
	}

	/* Ignore the event if the event is not related to RDMA device */
	if (ndev->type != ARPHRD_INFINIBAND) {
		if (!rdmaip_is_roce_device(ndev, NULL))
			return NOTIFY_DONE;
	}

	if (event != NETDEV_UP && event != NETDEV_DOWN &&
	    event != NETDEV_CHANGE) {
		RDMAIP_DBG2("Event %lx netdev %s - Ignored\n",
			    event, ndev->name);
		return NOTIFY_DONE;
	}
	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work) {
		RDMAIP_DBG1("rdmaip: failed to allocate port work\n");
		return NOTIFY_DONE;
	}
	work->event_type	= RDMAIP_EVENT_NET;
	work->net_event		= event;
	work->netdev		= ndev;

	INIT_DELAYED_WORK(&work->work, rdmaip_impl_netdev_callback);
	queue_delayed_work(rdmaip_wq, &work->work, 0);

	RDMAIP_DBG2("Scheduled event processing thread for %s : %s\n",
		    ndev->name, rdmaip_netdevevent2name(event));

	return NOTIFY_DONE;
}

void rdmaip_destroy_ip_workq(void)
{
	cancel_delayed_work_sync(&riif_dlywork);
	flush_workqueue(rdmaip_wq);
	destroy_workqueue(rdmaip_wq);
}

static void rdmaip_restore_ip_addresses(void)
{
	mm_segment_t	old_fs = get_fs();
	u8		port;

	for (port = 1; port <= ip_port_cnt; port++) {
		if (!ip_config[port].failover_group) {
			RDMAIP_DBG2("%s: failover group is zero\n", __func__);
			continue;
		}
		RDMAIP_DBG2("%s: Resetting IPs on %d\n", __func__, port);

		/*
		 * rdmaip_do_failback() calls inet(6)_ioctl call which calls
		 * copy_from_user() to copy the data from user address space
		 * into kernel address space. copy_from_user() checks whether
		 * the address is within the range for the process user address
		 * space. This function can be called as result of a user
		 * process trying to unload the module. Under these conditions
		 * addr_limit is set to max user address space. The address
		 * passed to inet(6)_ioct is kernel address which is out of user
		 * address speace. As a result, copy_from_user() fails as kernel
		 * address * does not fall into the user address space limit.
		 * To avoid the failure, set the address limit temporarily to
		 * accept kernel addresses and reset set after the call.
		 */
		set_fs(KERNEL_DS);
		rdmaip_do_failback(port);
		set_fs(old_fs);
	}
}


/*
 * rdmaip_cleanup
 *     This functions tear downs all the resources allocated in
 *     in the rdma_init() function.
 */
void rdmaip_cleanup(void)
{
	RDMAIP_DBG2("%s Enter rdmaip_init_flag = 0x%x\n", __func__,
		    rdmaip_init_flag);

	rdmaip_init_flag |= RDMAIP_TEARDOWN_IN_PROGRESS;

	rdmaip_restore_ip_addresses();

	if (rdmaip_init_flag & RDMAIP_REG_NETDEV_NOTIFIER) {
		unregister_netdevice_notifier(&rdmaip_nb);
		rdmaip_init_flag &= ~RDMAIP_REG_NETDEV_NOTIFIER;
	}

	if (rdmaip_init_flag & RDMAIP_IB_REG) {
		ib_unregister_client(&rdmaip_client);
		rdmaip_init_flag &= ~RDMAIP_IB_REG;
	}

	if (rdmaip_init_flag & RDMAIP_REG_NET_SYSCTL) {
		unregister_net_sysctl_table(rdmaip_sysctl_hdr);
		rdmaip_init_flag &= ~RDMAIP_REG_NET_SYSCTL;
	}

	if (rdmaip_init_flag & RDMAIP_IP_WQ_CREATED) {
		rdmaip_destroy_ip_workq();
		rdmaip_init_flag &= ~RDMAIP_IP_WQ_CREATED;
	}

	kfree(ip_config);

	if (rdmaip_init_flag & RDMAIP_IPv4_SOCK_CREATED) {
		sock_release(rdmaip_inet_socket);
		rdmaip_init_flag &= ~RDMAIP_IPv4_SOCK_CREATED;
	}

	if (rdmaip_init_flag & RDMAIP_IPv6_SOCK_CREATED) {
		sock_release(rdmaip_inet6_socket);
		rdmaip_init_flag &= ~RDMAIP_IPv6_SOCK_CREATED;
	}
	rdmaip_init_flag &= ~RDMAIP_IP_CONFIG_INIT_DONE;
	rdmaip_init_flag &= ~RDMAIP_TEARDOWN_IN_PROGRESS;

	RDMAIP_DBG2("%s done rdmaip_init_flag = 0x%x\n", __func__,
		    rdmaip_init_flag);
}


/*
 * module initialization function
 *
 * This function
 *     1. Creates a kernel socket that will be used to send ioctl
 *        to network stack for different options. For example, this
 *        is used to move/set the IP address on an interface.
 *     2. Registers a callback functions with the IB core to monitor
 *        RDMA device insertions and deletions.
 *     3. Regisgters with the network stack to monitor the NETDEV
 *        events such as NETDEV_UP, NETDEV_DOWN and NETDEV_CHANGE
 *     4. Registers with the IB stack to monitor the IB events
 *        such as IB_EVENT_PORT_UP and IB_EVENT_PORT_ERR (DOWN)
 *     5. Creats sysctl variables which allows to change the behaviour
 *        of the running kernel without reboot.
 *     6. A work queue is created which is used to handle all the
 *        asynchrous requests.
 *
 * TBD: For using active bonding feature, IP addresses must be
 *      configured. Initialing ip_config or registering other
 *      callback is not much useful. Need to explore to defere
 *      ip_config and other initialization until IP addresses
 *      configured.
 */
int rdmaip_init(void)
{
	int ret = 0;

	if (!rdmaip_active_bonding_enabled) {
		RDMAIP_DBG2("%s: Active Bonding is DISABLED\n", __func__);
		return ret;
	}
	RDMAIP_DBG2("%s: !! Active Bonding is ENABLED\n", __func__);

	INIT_LIST_HEAD(&rdmaip_devlist_head);

	ret = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP,
			       &rdmaip_inet_socket);
	if (ret < 0) {
		RDMAIP_DBG2("%s: Failed to create TCP transport IPv4 socket (%d)\n",
			     __func__, ret);
		return ret;
	}
	rdmaip_init_flag |= RDMAIP_IPv4_SOCK_CREATED;
	sock_net_set(rdmaip_inet_socket->sk, &init_net);

	ret = sock_create_kern(&init_net, PF_INET6, SOCK_DGRAM, 0,
			       &rdmaip_inet6_socket);
	if (ret < 0) {
		RDMAIP_DBG2("%s: Failed to create TCP transport IPv6 socket (%d)\n",
				  __func__, ret);
		rdmaip_cleanup();
		return ret;
	}
	rdmaip_init_flag |= RDMAIP_IPv6_SOCK_CREATED;

	if (rdmaip_inet6_socket)
		sock_net_set(rdmaip_inet6_socket->sk, &init_net);

	rdmaip_sysctl_hdr = register_net_sysctl(&init_net, "net/rdmaip",
						rdmaip_sysctl_table);
	if (!rdmaip_sysctl_hdr) {
		RDMAIP_DBG2("%s: register_net_sysctl failed\n",
				  __func__);
		rdmaip_cleanup();
		return ret;
	}
	rdmaip_init_flag |= RDMAIP_REG_NET_SYSCTL;

	/*
	 * Set the busy flag before registing with IB core
	 * and network stack. Events can come any time after
	 * registering with IB core and network stack. IB and
	 * netdev callback function will skip event processing
	 * during the initialization process by checking the
	 * busy flag.
	 */
	rdmaip_set_busy_flag();

	ret = ib_register_client(&rdmaip_client);
	if (ret) {
		RDMAIP_DBG2("%s: ib_register_client failed  (%d)\n",
				  __func__, ret);
		rdmaip_cleanup();
		return ret;
	}
	rdmaip_init_flag |= RDMAIP_IB_REG;

	rdmaip_wq = create_singlethread_workqueue("rdmaip_wq");
	if (!rdmaip_wq) {
		RDMAIP_DBG2("%s: failed to create IP Work queue\n",
				  __func__);
		rdmaip_cleanup();
		return -ENOMEM;
	}
	rdmaip_init_flag |= RDMAIP_IP_WQ_CREATED;

	ip_config = kzalloc(sizeof(struct rdmaip_port) * (ip_port_max + 1),
			    GFP_KERNEL);
	if (!ip_config) {
		RDMAIP_DBG1("rdmaip: failed to allocate IP config\n");
		return -ENOMEM;
	}
	rdmaip_read_exclude_ip_list();
	register_netdevice_notifier(&rdmaip_nb);
	rdmaip_init_flag |= RDMAIP_REG_NETDEV_NOTIFIER;

	rdmaip_sched_initial_failovers();
	return ret;
}

module_init(rdmaip_init);
module_exit(rdmaip_cleanup);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Sudhakar Dindukurti");
MODULE_DESCRIPTION("Resilient RDMA IP");
