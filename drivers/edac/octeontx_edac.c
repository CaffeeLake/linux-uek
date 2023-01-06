// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Marvell.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of_address.h>
#include <linux/arm_sdei.h>
#include <linux/arm-smccc.h>
#include <linux/of_device.h>
#include "edac_mc.h"
#include "edac_device.h"
#include "edac_module.h"
#include "octeontx_edac.h"


static const struct of_device_id octeontx_edac_ghes_of_match[] = {
	{.compatible = "marvell,sdei-ghes",},
	{},
};
MODULE_DEVICE_TABLE(of, octeontx_edac_ghes_of_match);


static const struct of_device_id tad_of_match[] = {
	{.name = "tad",},
	{},
};
MODULE_DEVICE_TABLE(of, tad_of_match);


static const struct of_device_id dss_of_match[] = {
	{.name = "dss",},
	{},
};
MODULE_DEVICE_TABLE(of, dss_of_match);


static const struct of_device_id mdc_of_match[] = {
	{.name = "mdc",},
	{},
};
MODULE_DEVICE_TABLE(of, mdc_of_match);

static const struct of_device_id mcc_of_match[] = {
	{.name = "mcc",},
	{},
};
MODULE_DEVICE_TABLE(of, mcc_of_match);

static const struct of_device_id cpu_of_match[] = {
	{.name = "core",},
	{},
};
MODULE_DEVICE_TABLE(of, cpu_of_match);

static const struct pci_device_id octeontx_edac_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_OCTEONTX2_LMC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_OCTEONTX2_MCC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_OCTEONTX2_MDC) },
	{ 0, },
};


static struct octeontx_ghes_list __ro_after_init ghes_list;


#define otx_printk(level, fmt, arg...) edac_printk(level, "octeontx", fmt, ##arg)

#define to_mci(k) container_of(k, struct mem_ctl_info, dev)

#define TEMPLATE_SHOW(reg)					\
static ssize_t reg##_show(struct device *dev,			\
		struct device_attribute *attr,			\
		char *data)					\
{								\
	struct mem_ctl_info *mci = to_mci(dev);			\
	struct octeontx_edac_pvt *pvt = mci->pvt_info;		\
	return sprintf(data, "0x%016llx\n", (u64)pvt->reg);	\
}

#define TEMPLATE_STORE(reg)					\
static ssize_t reg##_store(struct device *dev,			\
		struct device_attribute *attr,			\
		const char *data, size_t count)			\
{								\
	struct mem_ctl_info *mci = to_mci(dev);			\
	struct octeontx_edac_pvt *pvt = mci->pvt_info;		\
	if (isdigit(*data)) {					\
		if (!kstrtoul(data, 0, &pvt->reg))		\
			return count;				\
	}							\
	return 0;						\
}

static const u64 einj_val = 0x5555555555555555;
static u64 einj_fn(void)
{
	return einj_val;
}

static void octeontx_edac_mc_inject_error(struct mem_ctl_info *mci)
{
	struct arm_smccc_res res;
	unsigned long arg[8] = {0};
	struct octeontx_edac_pvt *pvt = mci->pvt_info;
	bool read = false;
	bool call = false;
	u64 val = einj_val;
	int error_type = pvt->error_type & 0x0000FFFF;

	if (MIDR_PARTNUM(read_cpuid_id()) == CN10K_CPU_MODEL) {
		arg[0] = CN10K_EDAC_INJECT;
		arg[1] = 0xd;
		arg[2] = pvt->address;
		arg[3] = (error_type >> 8) & 1;
		arg[4] = error_type & 0xFF;
		otx_printk(KERN_DEBUG, "%s %lx %lx %lx %lx %lx %lx %lx %lx\n", __func__,
				arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7]);
		arm_smccc_smc(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], &res);
	} else {
		arg[0] = OCTEONTX2_EDAC_INJECT;
		arg[1] = 0x3;
		arg[2] = pvt->address;
		arg[3] = error_type;

		arg[3] &= ~OCTEONTX_EDAC_F_REREAD;
		switch (arg[2]) {
		case 1 ... 2:
			arg[2] = (u64)&val;
			read = true;
			break;
		case 5 ... 6:
			arg[2] = (u64)einj_fn;
			call = true;
			break;
		case 3:
		case 7:
			arg[3] |= OCTEONTX_EDAC_F_REREAD;
			break;
		}

		otx_printk(KERN_DEBUG, "%s %lx %lx %lx %lx %lx %lx %lx %lx\n", __func__,
				arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7]);
		arm_smccc_smc(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], &res);

		if (read && val != einj_val)
			otx_printk(KERN_DEBUG, "read mismatch\n");

		if (call && einj_fn() != einj_val)
			otx_printk(KERN_DEBUG, "call mismatch\n");
	}

	otx_printk(KERN_DEBUG, "%s: (%lx, %lx, %lx, %lx) -> e?%ld\n",
			__func__, arg[0], arg[1], arg[2], arg[3], res.a0);
}


static ssize_t inject_store(struct device *dev,
		struct device_attribute *attr,
		const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct octeontx_edac_pvt *pvt = mci->pvt_info;

	if (!(pvt->error_type & pvt->ghes->ecc_cap))
		return count;

	if (!isdigit(*data))
		return count;

	if (kstrtoul(data, 0, &pvt->inject))
		return count;

	if (pvt->inject != 1)
		return count;

	pvt->inject = 0;

	octeontx_edac_mc_inject_error(mci);

	return count;
}

TEMPLATE_SHOW(address);
TEMPLATE_STORE(address);
TEMPLATE_SHOW(error_type);
TEMPLATE_STORE(error_type);

static DEVICE_ATTR_WO(inject);
static DEVICE_ATTR_RW(error_type);
static DEVICE_ATTR_RW(address);

static struct attribute *octeontx_dev_attrs[] = {
	&dev_attr_inject.attr,
	&dev_attr_error_type.attr,
	&dev_attr_address.attr,
	NULL
};

ATTRIBUTE_GROUPS(octeontx_dev);


static enum hw_event_mc_err_type octeontx_edac_severity(int cper_sev)
{
	switch (cper_sev) {
	case CPER_SEV_CORRECTED:
		return HW_EVENT_ERR_CORRECTED;
	case CPER_SEV_RECOVERABLE:
		return HW_EVENT_ERR_UNCORRECTED;
	case CPER_SEV_FATAL:
		return HW_EVENT_ERR_FATAL;
	case CPER_SEV_INFORMATIONAL:
		return HW_EVENT_ERR_INFO;
	}

	return HW_EVENT_ERR_INFO;
}

static const char * const mem_err_types[] = {
	"unknown",
	"no error",
	"single-bit ECC",
	"multi-bit ECC",
	"single-symbol chipkill ECC",
	"multi-symbol chipkill ECC",
	"master abort",
	"target abort",
	"parity error",
	"watchdog timeout",
	"invalid address",
	"mirror Broken",
	"memory sparing",
	"scrub corrected error",
	"scrub uncorrected error",
	"physical memory map-out event",
	"unknown error",
};

static void octeontx_edac_make_error_desc(struct cper_sec_mem_err *mem_err,
		struct edac_raw_error_desc *e)
{
	char *p;

	memset(e, 0, sizeof(*e));
	e->error_count = 1;
	e->grain = 1;
	e->top_layer = -1;
	e->mid_layer = -1;
	e->low_layer = -1;
	e->syndrome = 0;

	if (mem_err->validation_bits & CPER_MEM_VALID_ERROR_TYPE)
		e->other_detail = mem_err_types[mem_err->error_type];

	if (mem_err->validation_bits & CPER_MEM_VALID_PA) {
		e->page_frame_number = mem_err->physical_addr >> PAGE_SHIFT;
		e->offset_in_page = mem_err->physical_addr & ~PAGE_MASK;
	}

	p = e->location;
	if (mem_err->validation_bits & CPER_MEM_VALID_ERROR_STATUS)
		p += sprintf(p, "status:0x%llx ", mem_err->error_status);
	if (mem_err->validation_bits & CPER_MEM_VALID_NODE)
		p += sprintf(p, "node:%d ", mem_err->node);
	if (mem_err->validation_bits & CPER_MEM_VALID_CARD)
		p += sprintf(p, "card:%d ", mem_err->card);
	if (mem_err->validation_bits & CPER_MEM_VALID_MODULE)
		p += sprintf(p, "module:%d ", mem_err->module);
	if (mem_err->validation_bits & CPER_MEM_VALID_RANK_NUMBER)
		p += sprintf(p, "rank:%d ", mem_err->rank);
	if (mem_err->validation_bits & CPER_MEM_VALID_BANK)
		p += sprintf(p, "bank:%d ", mem_err->bank);
	if (mem_err->validation_bits & CPER_MEM_VALID_ROW)
		p += sprintf(p, "row:%d ", mem_err->row);
	if (mem_err->validation_bits & CPER_MEM_VALID_COLUMN)
		p += sprintf(p, "col:%d ", mem_err->column);
	if (mem_err->validation_bits & CPER_MEM_VALID_BIT_POSITION)
		p += sprintf(p, "bit_pos:%d ", mem_err->bit_pos);
	if (mem_err->validation_bits & CPER_MEM_VALID_REQUESTOR_ID)
		p += sprintf(p, "requestorID: 0x%llx ", mem_err->requestor_id);
	if (mem_err->validation_bits & CPER_MEM_VALID_RESPONDER_ID)
		p += sprintf(p, "responderID: 0x%llx ", mem_err->responder_id);
	if (mem_err->validation_bits & CPER_MEM_VALID_TARGET_ID)
		p += sprintf(p, "targetID: 0x%llx ", mem_err->responder_id);
	if (p > e->location)
		*(p - 1) = '\0';
}

static int octeontx_sdei_register(struct octeontx_ghes *ghes, sdei_event_callback *cb)
{
	int ret = 0;

	ret = sdei_event_register(ghes->sdei_num, cb, ghes);
	if (ret < 0) {
		pr_err("Unable register SDEI\n");
		return ret;
	}

	ret = sdei_event_enable(ghes->sdei_num);
	if (ret < 0) {
		pr_err("Unable enable SDEI\n");
		return ret;
	}

	ghes->ring->reg = OCTEONTX_GHES_ERR_RING_SIG; //ghes->sdei_num;
	/*Ensure that reg updated*/
	wmb();

	return 0;
}

static void octeontx_sdei_unregister(struct octeontx_ghes *ghes)
{
	int ret = 0;

	ret = sdei_event_disable(ghes->sdei_num);
	if (ret < 0)
		pr_err("Unable disable SDEI\n");

	ret = sdei_event_unregister(ghes->sdei_num);
	if (ret < 0)
		pr_err("Unable unregister SDEI\n");

	ghes->ring->reg = 0;
	/*Ensure that reg updated*/
	wmb();
}


static int octeontx_mc_sdei_callback(u32 event_id, struct pt_regs *regs, void *arg)
{
	struct octeontx_ghes *ghes = arg;
	struct mem_ctl_info *mci = ghes->mci;

	edac_queue_work(&mci->work, msecs_to_jiffies(0));

	return 0;
}

static int octeontx_device_sdei_callback(u32 event_id, struct pt_regs *regs, void *arg)
{
	struct octeontx_ghes *ghes = arg;
	struct edac_device_ctl_info *edac_dev = ghes->edac_dev;

	edac_queue_work(&edac_dev->work, msecs_to_jiffies(0));

	return 0;
}

static int octeontx_cpu_sdei_callback(u32 event_id, struct pt_regs *regs, void *arg)
{
	struct octeontx_ghes *ghes = arg;

	edac_queue_work(&ghes->work, msecs_to_jiffies(0));

	return 0;
}

static void octeontx_edac_mc_wq(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct mem_ctl_info *mci = container_of(dw, struct mem_ctl_info, work);
	struct octeontx_edac_pvt *pvt = mci->pvt_info;
	struct octeontx_ghes *ghes = pvt->ghes;
	struct octeontx_ghes_ring *ring = ghes->ring;
	struct octeontx_ghes_ring_record rec;
	struct cper_sec_mem_err *cper = NULL;
	struct edac_raw_error_desc *e = NULL;
	u32 head, tail;

	mutex_lock(&ghes->lock);

loop:
	head = ring->head;
	tail = ring->tail;

	/*Ensure that head updated*/
	rmb();

	otx_printk(KERN_DEBUG, "%s:[%08x] tail=%d, head=%d, size=%d\n",
			ghes->name, ghes->sdei_num, tail, head, ghes->ring->size);

	if (head == tail)
		goto exit;

	memcpy_fromio(&rec, &ring->records[tail], sizeof(rec));

	e = &mci->error_desc;
	cper = &rec.cper.mem;
	if (cper->validation_bits != CANARY)
		octeontx_edac_make_error_desc(cper, e);
	e->msg = rec.msg;
	e->type = octeontx_edac_severity(rec.severity);

	++tail;
	if (tail >= ring->size)
		tail = 0;
	ring->tail = tail;
	/*Ensure that tail updated*/
	wmb();

	edac_raw_mc_handle_error(e);

	if (head != tail)
		goto loop;

exit:
	mutex_unlock(&ghes->lock);
}

static void octeontx_edac_device_wq(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct edac_device_ctl_info *edac_dev =
			container_of(dw, struct edac_device_ctl_info, work);
	struct octeontx_edac_pvt *pvt = edac_dev->pvt_info;
	struct octeontx_ghes *ghes = pvt->ghes;
	struct octeontx_ghes_ring *ring = ghes->ring;
	struct octeontx_ghes_ring_record rec;
	struct cper_sec_mem_err *e = NULL;
	enum hw_event_mc_err_type type;
	u32 head = 0, tail = 0;
	char msg[SIZE] = {0};
	int l = 0;
	char *p = NULL;

	mutex_lock(&ghes->lock);

loop:
	head = ring->head;
	tail = ring->tail;

	/*Ensure that head updated*/
	rmb();

	otx_printk(KERN_DEBUG, "%s:[%08x] tail=%d, head=%d, size=%d\n",
			ghes->name, ghes->sdei_num, tail, head, ghes->ring->size);

	if (head == tail)
		goto exit;

	memcpy_fromio(&rec, &ring->records[tail], sizeof(rec));
	e = &rec.cper.mem;

	p = msg;
	p[SIZE-1] = '\0';
	l += snprintf(p, SIZE, "%s ", rec.msg);
	if ((e->validation_bits & CPER_MEM_VALID_ERROR_TYPE) && (l < SIZE))
		l += snprintf(&p[l], SIZE - l, "%s ", mem_err_types[e->error_type]);
	if ((e->validation_bits & CPER_MEM_VALID_PA) && (l < SIZE))
		l += snprintf(&p[l], SIZE - l, "addr: 0x%llx ", e->physical_addr);
	if ((e->validation_bits & CPER_MEM_VALID_ERROR_STATUS) && (l < SIZE))
		l += snprintf(&p[l], SIZE - l, "status(0x%llx): ", e->error_status);
	if ((e->validation_bits & CPER_MEM_VALID_REQUESTOR_ID) && (l < SIZE))
		l += snprintf(&p[l], SIZE - l, "requestorID: 0x%llx ", e->requestor_id);
	if ((e->validation_bits & CPER_MEM_VALID_RESPONDER_ID) && (l < SIZE))
		l += snprintf(&p[l], SIZE - l, "responderID: 0x%llx ", e->responder_id);
	if ((e->validation_bits & CPER_MEM_VALID_TARGET_ID) && (l < SIZE))
		l += snprintf(&p[l], SIZE - l, "targetID: 0x%llx ", e->responder_id);
	if (l < SIZE)
		p[l] = '\0';

	type = octeontx_edac_severity(rec.severity);

	++tail;
	if (tail >= ring->size)
		tail = 0;
	ring->tail = tail;

	/*Ensure that tail updated*/
	wmb();

	if (e->validation_bits != CANARY) {
		if (type == HW_EVENT_ERR_FATAL || type == HW_EVENT_ERR_UNCORRECTED)
			edac_device_handle_ue(edac_dev, 0, 0, msg);
		else
			edac_device_handle_ce(edac_dev, 0, 0, msg);
	}

	if (head != tail)
		goto loop;

exit:
	mutex_unlock(&ghes->lock);
}

static void octeontx_edac_cpu_wq(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct octeontx_ghes *ghes = container_of(dw, struct octeontx_ghes, work);
	struct edac_device_ctl_info *edac_dev = ghes->edac_dev;
	struct octeontx_ghes_ring *ring = ghes->ring;
	struct octeontx_ghes_ring_record rec;
	struct processor_error *e = NULL;
	struct cper_sec_proc_arm *desc = NULL;
	struct cper_arm_err_info *info = NULL;
	enum hw_event_mc_err_type type;
	u32 head = 0, tail = 0;
	char msg[SIZE] = {0};
	int l = 0;
	char *p = NULL;
	int cpu = 0;

	mutex_lock(&ghes->lock);

loop:
	head = ring->head;
	tail = ring->tail;

	/*Ensure that head updated*/
	rmb();

	otx_printk(KERN_DEBUG, "%s:[%08x] tail=%d, head=%d, size=%d\n",
			ghes->name, ghes->sdei_num, tail, head, ghes->ring->size);

	if (head == tail)
		goto exit;

	if (kstrtoint(&ghes->name[4], 10, &cpu) || cpu < 0 || cpu > 255)
		cpu = 0;

	memcpy_fromio(&rec, &ring->records[tail], sizeof(rec));
	e = &rec.cper.core;
	desc = &e->desc;
	info = &e->info;

	p = msg;
	p[SIZE-1] = '\0';
	l += snprintf(p, SIZE, "%s ", rec.msg);
	if ((info->validation_bits & CPER_ARM_INFO_VALID_PHYSICAL_ADDR) && (l < SIZE))
		l += snprintf(&p[l], SIZE - l, "paddr=0x%llx ", info->physical_fault_addr);
	if ((info->validation_bits & CPER_ARM_INFO_VALID_VIRT_ADDR) && (l < SIZE))
		l += snprintf(&p[l], SIZE - l, "vaddr=0x%llx ", info->virt_fault_addr);


	if (l < SIZE)
		p[l] = '\0';

	type = octeontx_edac_severity(rec.severity);

	++tail;
	if (tail >= ring->size)
		tail = 0;
	ring->tail = tail;

	/*Ensure that tail updated*/
	wmb();

	if (desc->validation_bits != CANARY) {
		if (type == HW_EVENT_ERR_FATAL || type == HW_EVENT_ERR_UNCORRECTED)
			edac_device_handle_ue(edac_dev, cpu, 0, msg);
		else
			edac_device_handle_ce(edac_dev, cpu, 0, msg);
	}

	if (head != tail)
		goto loop;

exit:
	mutex_unlock(&ghes->lock);
}


static void octeontx_edac_enable_msix(struct pci_dev *pdev)
{
	u16 ctrl;

	if ((pdev->msi_enabled) || (pdev->msix_enabled)) {
		dev_err(&pdev->dev, "MSI(%d) or MSIX(%d) already enabled\n",
			pdev->msi_enabled, pdev->msix_enabled);
		return;
	}

	pdev->msix_cap = pci_find_capability(pdev, PCI_CAP_ID_MSIX);
	if (pdev->msix_cap) {
		pci_read_config_word(pdev, pdev->msix_cap + PCI_MSIX_FLAGS, &ctrl);
		ctrl |= PCI_MSIX_FLAGS_ENABLE;
		pci_write_config_word(pdev, pdev->msix_cap + PCI_MSIX_FLAGS, ctrl);

		otx_printk(KERN_DEBUG, "Set MSI-X Enable for PCI dev %04d:%02d.%d\n",
			pdev->bus->number, PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
	} else {
		dev_err(&pdev->dev, "PCI dev %04d:%02d.%d missing MSIX capabilities\n",
			pdev->bus->number, PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
	}
}

static void octeontx_edac_msix_init(void)
{
	const struct pci_device_id *pdevid;
	struct pci_dev *pdev;
	size_t i;

	if (MIDR_PARTNUM(read_cpuid_id()) == CN10K_CPU_MODEL)
		return;

	for (i = 0; i < ARRAY_SIZE(octeontx_edac_pci_tbl); i++) {
		pdevid = &octeontx_edac_pci_tbl[i];
		pdev = NULL;
		while ((pdev = pci_get_device(pdevid->vendor, pdevid->device, pdev)))
			octeontx_edac_enable_msix(pdev);
	}
}

static int octeontx_ghes_of_match_resource(struct octeontx_ghes_list *list)
{
	struct device_node *root = NULL;
	struct device_node *node = NULL;
	struct octeontx_ghes *ghes = NULL;
	const __be32 *res = NULL;
	u64 size = 0;
	u64 base = 0;
	size_t count = 0;

	root = of_find_matching_node(NULL, octeontx_edac_ghes_of_match);
	if (!root)
		return -ENODEV;

	for_each_available_child_of_node(root, node)
		count++;

	if (!count)
		return -ENODEV;

	list->count = count;
	list->ghes = NULL;

	list->ghes = kcalloc(count, sizeof(struct octeontx_ghes), GFP_KERNEL);
	if (!list->ghes)
		return -ENOMEM;

	ghes = list->ghes;
	for_each_available_child_of_node(root, node) {

		strncpy(ghes->name, node->name, sizeof(ghes->name));

		mutex_init(&ghes->lock);

		if (of_property_read_u32(node, "event-id", &ghes->sdei_num)) {
			otx_printk(KERN_ERR, "Unable read SDEI id\n");
			return -EINVAL;
		}

		res = of_get_address(node, 2, &size, NULL);
		base = of_translate_address(node, res);
		if (base == OF_BAD_ADDR) {
			otx_printk(KERN_ERR, "Unable translate address\n");
			return -EINVAL;
		}
		ghes->ring_pa = (phys_addr_t)base;
		ghes->ring_sz = (size_t)size;

		otx_printk(KERN_DEBUG, "%s 0x%08x: 0x%llx/0x%lx\n",
				ghes->name, ghes->sdei_num, ghes->ring_pa, ghes->ring_sz);

		ghes++;
	}

	return 0;
}

static struct octeontx_ghes *octeontx_edac_get_ghes(const char *name)
{
	struct octeontx_ghes *ghes = NULL;
	int i = 0;

	if (!name)
		return NULL;

	for (i = 0; i < ghes_list.count; i++) {
		ghes = &ghes_list.ghes[i];
		if (strcmp(name, ghes->name) == 0)
			return ghes;
	}

	return NULL;
}

static int octeontx_edac_map_resource(struct platform_device *pdev,
					struct octeontx_ghes **src, char *str)
{
	struct octeontx_ghes *ghes = NULL;
	struct device *dev = &pdev->dev;
	const char *name = str ? str : dev->driver->of_match_table->name;

	ghes = octeontx_edac_get_ghes(name);
	if (!ghes) {
		dev_err(dev, "Unable find ghes\n");
		return -ENODEV;
	}

	if (!devm_request_mem_region(dev, ghes->ring_pa, ghes->ring_sz, ghes->name)) {
		dev_err(dev, "Unable request ring\n");
		return -EBUSY;
	}

	ghes->ring = devm_ioremap(dev, ghes->ring_pa, ghes->ring_sz);
	if (!ghes->ring) {
		dev_err(dev, "Unable map ring\n");
		return -ENOMEM;
	}

	*src = ghes;

	return 0;
}

static int octeontx_edac_mc_init(struct platform_device *pdev,
					struct octeontx_ghes *ghes)
{
	struct device *dev = &pdev->dev;
	struct octeontx_edac_pvt *pvt = NULL;
	struct mem_ctl_info *mci = NULL;
	struct edac_mc_layer layers[1] = {0};
	int ret = 0;
	int idx = 0;

	idx = edac_device_alloc_index();

	layers[0].type = EDAC_MC_LAYER_ALL_MEM;
	layers[0].size = 1;
	layers[0].is_virt_csrow = false;

	mci = edac_mc_alloc(idx, ARRAY_SIZE(layers), layers,
			sizeof(struct octeontx_edac_pvt));
	if (!mci) {
		dev_err(dev, "Unable alloc MC\n");
		ret = -ENOMEM;
		goto err0;
	}

	mci->pdev = dev;
	pvt = mci->pvt_info;
	platform_set_drvdata(pdev, mci);
	mci->edac_ctl_cap = EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = dev->driver->name;
	mci->ctl_name = ghes->name;
	mci->dev_name = ghes->name;
	mci->scrub_mode = SCRUB_HW_PROG;
	mci->edac_check = NULL;
	pvt->ghes = ghes;
	ghes->mci = mci;

	ret = edac_mc_add_mc_with_groups(mci, ghes->ecc_cap ? octeontx_dev_groups : NULL);
	if (ret)
		goto err1;

	INIT_DELAYED_WORK(&mci->work, octeontx_edac_mc_wq);
	edac_stop_work(&mci->work);

	ret = octeontx_sdei_register(ghes, octeontx_mc_sdei_callback);
	if (ret)
		goto err2;

	otx_printk(KERN_DEBUG, "Register %s %d/%d/%d\n",
			ghes->name, ghes->ring->tail,
			ghes->ring->head, ghes->ring->size);

	return 0;

err2:
	octeontx_sdei_unregister(ghes);
err1:
	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);

err0:
	dev_err(dev, "Unable register %d\n", ret);

	return ret;
}

static int octeontx_dss_probe(struct platform_device *pdev)
{
	struct octeontx_ghes *ghes = NULL;
	int ret = 0;

	ret = octeontx_edac_map_resource(pdev, &ghes, NULL);
	if (ret)
		return ret;

	ghes->ecc_cap = CN10KA_DSS_EINJ_CAP;

	ret = octeontx_edac_mc_init(pdev, ghes);

	return ret;
}

static int octeontx_edac_device_init(struct platform_device *pdev,
					struct octeontx_ghes *ghes,
					char *dev_name, char *blk_name)
{
	struct device *dev = &pdev->dev;
	struct edac_device_ctl_info *edac_dev = NULL;
	struct octeontx_edac_pvt *pvt = NULL;
	int idx = 0;
	int ret = 0;

	idx = edac_device_alloc_index();

	edac_dev = edac_device_alloc_ctl_info(sizeof(*pvt),
			dev_name, 1, blk_name, 1, 0, NULL, 0, idx);
	if (!edac_dev)
		return -ENOMEM;

	edac_dev->dev = dev;
	pvt = edac_dev->pvt_info;
	platform_set_drvdata(pdev, edac_dev);
	edac_dev->mod_name = dev->driver->name;
	edac_dev->ctl_name = ghes->name;
	edac_dev->dev_name = ghes->name;
	edac_dev->edac_check = NULL;
	pvt->ghes = ghes;
	ghes->edac_dev = edac_dev;

	if (edac_device_add_device(edac_dev)) {
		dev_err(dev, "Unable register\n");
		goto err0;
	}

	INIT_DELAYED_WORK(&edac_dev->work, octeontx_edac_device_wq);
	edac_stop_work(&edac_dev->work);

	ret = octeontx_sdei_register(ghes, octeontx_device_sdei_callback);
	if (ret)
		goto err1;

	otx_printk(KERN_DEBUG, "Register %s %d/%d/%d\n",
			ghes->name, ghes->ring->tail,
			ghes->ring->head, ghes->ring->size);

	return 0;

err1:
	octeontx_sdei_unregister(ghes);
err0:
	dev_err(dev, "Unable register %d\n", ret);
	edac_device_free_ctl_info(edac_dev);

	return -ENXIO;
}

static int octeontx_tad_probe(struct platform_device *pdev)
{
	struct octeontx_ghes *ghes = NULL;
	int ret = 0;

	ret = octeontx_edac_map_resource(pdev, &ghes, NULL);
	if (ret)
		return ret;

	ret = octeontx_edac_device_init(pdev, ghes, "llc", ghes->name);

	return ret;
}

static int octeontx_mdc_probe(struct platform_device *pdev)
{
	struct octeontx_ghes *ghes = NULL;
	int ret = 0;

	ret = octeontx_edac_map_resource(pdev, &ghes, NULL);
	if (ret)
		return ret;

	if (MIDR_PARTNUM(read_cpuid_id()) == CN10K_CPU_MODEL)
		ghes->ecc_cap = 0;
	else
		ghes->ecc_cap = OCTEONTX_MDC_EINJ_CAP;

	ret = octeontx_edac_device_init(pdev, ghes, ghes->name, ghes->name);

	return ret;
}

static int octeontx_mcc_probe(struct platform_device *pdev)
{
	struct octeontx_ghes *ghes = NULL;
	int ret = 0;

	ret = octeontx_edac_map_resource(pdev, &ghes, NULL);
	if (ret)
		return ret;

	ghes->ecc_cap = OCTEONTX_MCC_EINJ_CAP;

	ret = octeontx_edac_mc_init(pdev, ghes);

	return ret;
}

static int octeontx_cpu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct octeontx_ghes *ghes = NULL;
	struct edac_device_ctl_info *edac_dev = NULL;
	char *name = (char *)dev->driver->of_match_table->name;
	int idx = 0;
	int ret = 0;
	int i = 0;
	int cores = 0;

	for (i = 0; i < ghes_list.count; i++) {
		ghes = &ghes_list.ghes[i];
		if (!strncmp(name, ghes->name, 4))
			cores++;
	}

	idx = edac_device_alloc_index();

	edac_dev = edac_device_alloc_ctl_info(0, "cpu", cores, name, 1, 0, NULL, 0, idx);
	if (!edac_dev)
		return -ENOMEM;

	edac_dev->dev = dev;
	platform_set_drvdata(pdev, edac_dev);
	edac_dev->mod_name = dev->driver->name;
	edac_dev->ctl_name = name;
	edac_dev->dev_name = name;
	edac_dev->edac_check = NULL;

	ret = edac_device_add_device(edac_dev);
	if (ret)
		goto err0;

	for (i = 0; i < ghes_list.count; i++) {
		ghes = &ghes_list.ghes[i];
		if (strncmp(name, ghes->name, 4))
			continue;

		ret = octeontx_edac_map_resource(pdev, &ghes, ghes->name);
		if (ret)
			continue;

		ghes->edac_dev = edac_dev;

		INIT_DELAYED_WORK(&ghes->work, octeontx_edac_cpu_wq);
		edac_stop_work(&ghes->work);

		octeontx_sdei_register(ghes, octeontx_cpu_sdei_callback);
	}

	otx_printk(KERN_DEBUG, "Register %d %s\n", cores, edac_dev->ctl_name);

	return 0;

err0:
	dev_err(dev, "Unable register %d\n", ret);
	edac_device_free_ctl_info(edac_dev);

	return ret;
}

static int octeontx_device_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edac_dev = platform_get_drvdata(pdev);
	struct octeontx_edac_pvt *pvt = edac_dev->pvt_info;

	octeontx_sdei_unregister(pvt->ghes);
	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(edac_dev);
	platform_device_unregister(pdev);

	return 0;
}

static int octeontx_mc_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);
	struct octeontx_edac_pvt *pvt = mci->pvt_info;

	octeontx_sdei_unregister(pvt->ghes);
	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);
	platform_device_unregister(pdev);

	return 0;
}

static int octeontx_cpu_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edac_dev = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	char *name = (char *)dev->driver->of_match_table->name;
	struct octeontx_ghes *ghes = NULL;
	int i = 0;

	for (i = 0; i < ghes_list.count; i++) {
		ghes = &ghes_list.ghes[i];
		if (strncmp(name, ghes->name, 4))
			continue;
		octeontx_sdei_unregister(ghes);
	}
	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(edac_dev);
	platform_device_unregister(pdev);

	return 0;
}

static struct platform_driver tad_edac_drv = {
	.probe = octeontx_tad_probe,
	.remove = octeontx_device_remove,
	.driver = {
		.name = "tad_edac",
		.of_match_table = tad_of_match,
	},
};

static struct platform_driver dss_edac_drv = {
	.probe = octeontx_dss_probe,
	.remove = octeontx_mc_remove,
	.driver = {
		.name = "dss_edac",
		.of_match_table = dss_of_match,
	},
};

static struct platform_driver mdc_edac_drv = {
	.probe = octeontx_mdc_probe,
	.remove = octeontx_device_remove,
	.driver = {
		.name = "mdc_edac",
		.of_match_table = mdc_of_match,
	},
};

static struct platform_driver mcc_edac_drv = {
	.probe = octeontx_mcc_probe,
	.remove = octeontx_mc_remove,
	.driver = {
		.name = "mcc_edac",
		.of_match_table = mcc_of_match,
	}
};

static struct platform_driver cpu_edac_drv = {
	.probe = octeontx_cpu_probe,
	.remove = octeontx_cpu_remove,
	.driver = {
		.name = "cpu_edac",
		.of_match_table = cpu_of_match,
	}
};

static int __init octeontx_edac_init(void)
{
	struct platform_device *mdc = NULL;
	struct platform_device *dss = NULL;
	struct platform_device *tad = NULL;
	struct platform_device *mcc = NULL;
	struct platform_device *cpu = NULL;
	int ret = 0;

	ret = octeontx_ghes_of_match_resource(&ghes_list);
	if (ret)
		goto exit0;

	if (MIDR_PARTNUM(read_cpuid_id()) == CN10K_CPU_MODEL) {

		ret = platform_driver_register(&dss_edac_drv);
		if (!ret)
			dss = platform_device_register_simple(dss_edac_drv.driver.name,
					PLATFORM_DEVID_NONE, NULL, 0);
		if (!ret && IS_ERR(dss)) {
			pr_err("Unable register %s %ld\n", dss_edac_drv.driver.name, PTR_ERR(dss));
			platform_driver_unregister(&dss_edac_drv);
		}

		ret = platform_driver_register(&tad_edac_drv);
		if (!ret)
			tad = platform_device_register_simple(tad_edac_drv.driver.name,
					PLATFORM_DEVID_NONE, NULL, 0);
		if (!ret && IS_ERR(tad)) {
			pr_err("Unable register %s %ld\n", tad_edac_drv.driver.name, PTR_ERR(tad));
			platform_driver_unregister(&tad_edac_drv);
		}

		ret = platform_driver_register(&cpu_edac_drv);
		if (!ret)
			cpu = platform_device_register_simple(cpu_edac_drv.driver.name,
					PLATFORM_DEVID_NONE, NULL, 0);
		if (!ret && IS_ERR(cpu)) {
			pr_err("Unable register %s %ld\n", cpu_edac_drv.driver.name, PTR_ERR(cpu));
			platform_driver_unregister(&cpu_edac_drv);
		}

	} else {
		ret = platform_driver_register(&mcc_edac_drv);
		if (!ret)
			mcc = platform_device_register_simple(mcc_edac_drv.driver.name,
					PLATFORM_DEVID_NONE, NULL, 0);
		if (!ret && IS_ERR(mcc)) {
			pr_err("Unable register %s %ld\n", mcc_edac_drv.driver.name, PTR_ERR(mcc));
			platform_driver_unregister(&mcc_edac_drv);
		}

		octeontx_edac_msix_init();
	}

	ret = platform_driver_register(&mdc_edac_drv);
	if (!ret)
		mdc = platform_device_register_simple(mdc_edac_drv.driver.name,
				PLATFORM_DEVID_NONE, NULL, 0);
	if (!ret && IS_ERR(mdc)) {
		pr_err("Unable register %s %ld\n", mdc_edac_drv.driver.name, PTR_ERR(mdc));
		platform_driver_unregister(&mdc_edac_drv);
	}

	return 0;

exit0:
	pr_err("%s failed 0x%x\n", __func__, ret);
	kfree(ghes_list.ghes);

	return ret;
}

static void __exit octeontx_edac_exit(void)
{
	if (MIDR_PARTNUM(read_cpuid_id()) == CN10K_CPU_MODEL) {
		platform_driver_unregister(&dss_edac_drv);
		platform_driver_unregister(&tad_edac_drv);
		platform_driver_unregister(&cpu_edac_drv);
	} else {
		platform_driver_unregister(&mcc_edac_drv);
	}
	platform_driver_unregister(&mdc_edac_drv);
	kfree(ghes_list.ghes);
}


module_init(octeontx_edac_init);
module_exit(octeontx_edac_exit);

MODULE_AUTHOR("Vasyl Gomonovych <vgomonovych@marvell.com>");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("OcteonTX2 / CN10K EDAC driver");
MODULE_LICENSE("GPL");
