/*
 *  Copyright (C) 1994  Linus Torvalds
 *  Copyright (C) 2000  SuSE
 */

#include <linux/kernel.h>
#include <linux/init.h>
#ifdef CONFIG_SYSFS
#include <linux/device.h>
#endif
#include <linux/cpu.h>
#include <asm/alternative.h>
#include <asm/nospec-branch.h>
#include <asm/cmdline.h>
#include <asm/bugs.h>
#include <asm/processor.h>
#include <asm/mtrr.h>
#include <asm/cacheflush.h>
#include <asm/spec_ctrl.h>
#include <asm/cmdline.h>
#include <asm/intel-family.h>

/*
 * use_ibrs flags:
 * SPEC_CTRL_IBRS_INUSE			indicate if ibrs is currently in use
 * SPEC_CTRL_IBRS_SUPPORTED		indicate if system supports ibrs
 * SPEC_CTRL_IBRS_ADMIN_DISABLED	indicate if admin disables ibrs
 */
int use_ibrs;
EXPORT_SYMBOL(use_ibrs);

/*
 * use_ibpb flags:
 * SPEC_CTRL_IBPB_INUSE			indicate if ibpb is currently in use
 * SPEC_CTRL_IBPB_SUPPORTED		indicate if system supports ibpb
 * SPEC_CTRL_IBPB_ADMIN_DISABLED	indicate if admin disables ibpb
 */
int use_ibpb;
EXPORT_SYMBOL(use_ibpb);

/* mutex to serialize IBRS & IBPB control changes */
DEFINE_MUTEX(spec_ctrl_mutex);
EXPORT_SYMBOL(spec_ctrl_mutex);

bool use_ibrs_on_skylake = true;
EXPORT_SYMBOL(use_ibrs_on_skylake);

/*
 * retpoline_fallback flags:
 * SPEC_CTRL_USE_RETPOLINE_FALLBACK	pick retpoline fallback mitigation
 */
int retpoline_fallback = SPEC_CTRL_USE_RETPOLINE_FALLBACK;
EXPORT_SYMBOL(retpoline_fallback);


int __init spectre_v2_heuristics_setup(char *p)
{
	ssize_t len;

	while (*p) {
		/* Disable all heuristics. */
		if (!strncmp(p, "off", 3)) {
			use_ibrs_on_skylake = false;
			clear_retpoline_fallback();
			break;
		}
		len = strlen("skylake");
		if (!strncmp(p, "skylake", len)) {
			p += len;
			if (*p == '=')
				++p;
			if (*p == '\0')
				break;
			if (!strncmp(p, "off", 3))
				use_ibrs_on_skylake = false;
		}
		len = strlen("retpoline_fallback");
		if (!strncmp(p, "retpoline_fallback", len)) {
			p += len;
			if (*p == '=')
				++p;
			if (*p == '\0')
				break;
			if (!strncmp(p, "off", 3))
				clear_retpoline_fallback();
		}

		p = strpbrk(p, ",");
		if (!p)
			break;
		p++; /* skip ',' */
	}
	return 1;
}
__setup("spectre_v2_heuristics=", spectre_v2_heuristics_setup);

static void __init spectre_v2_select_mitigation(void);

void __init check_bugs(void)
{
	identify_boot_cpu();
#if !defined(CONFIG_SMP)
	printk(KERN_INFO "CPU: ");
	print_cpu_info(&boot_cpu_data);
#endif

	/* Select the proper spectre mitigation before patching alternatives */
	spectre_v2_select_mitigation();

	alternative_instructions();

	/*
	 * Make sure the first 2MB area is not mapped by huge pages
	 * There are typically fixed size MTRRs in there and overlapping
	 * MTRRs into large pages causes slow downs.
	 *
	 * Right now we don't do that with gbpages because there seems
	 * very little benefit for that case.
	 */
	if (!direct_gbpages)
		set_memory_4k((unsigned long)__va(0), 1);
}

/* Check for Skylake-like CPUs (for RSB handling) */
static bool __init is_skylake_era(void)
{
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
	    boot_cpu_data.x86 == 6) {
		switch (boot_cpu_data.x86_model) {
		case INTEL_FAM6_SKYLAKE_MOBILE:
		case INTEL_FAM6_SKYLAKE_DESKTOP:
		case INTEL_FAM6_SKYLAKE_X:
		case INTEL_FAM6_KABYLAKE_MOBILE:
		case INTEL_FAM6_KABYLAKE_DESKTOP:
			return true;
		}
	}
	return false;
}

/* The kernel command line selection */
enum spectre_v2_mitigation_cmd {
	SPECTRE_V2_CMD_NONE,
	SPECTRE_V2_CMD_AUTO,
	SPECTRE_V2_CMD_FORCE,
	SPECTRE_V2_CMD_RETPOLINE,
	SPECTRE_V2_CMD_RETPOLINE_GENERIC,
	SPECTRE_V2_CMD_RETPOLINE_AMD,
	SPECTRE_V2_CMD_IBRS,
};

static const char *spectre_v2_strings[] = {
	[SPECTRE_V2_NONE]			= "Vulnerable",
	[SPECTRE_V2_RETPOLINE_MINIMAL]		= "Vulnerable: Minimal generic ASM retpoline",
	[SPECTRE_V2_RETPOLINE_MINIMAL_AMD]	= "Vulnerable: Minimal AMD ASM retpoline",
	[SPECTRE_V2_RETPOLINE_GENERIC]		= "Mitigation: Full generic retpoline",
	[SPECTRE_V2_RETPOLINE_AMD]		= "Mitigation: Full AMD retpoline",
	[SPECTRE_V2_IBRS]			= "Mitigation: IBRS",

};

#undef pr_fmt
#define pr_fmt(fmt)     "Spectre V2 : " fmt

static enum spectre_v2_mitigation spectre_v2_enabled = SPECTRE_V2_NONE;

/*
 * Disable retpoline and attempt to fall back to another Spectre v2 mitigation.
 * If possible, fall back to IBRS and IBPB.
 * Failing that, indicate that we have no Spectre v2 mitigation.
 *
 * Obtains spec_ctrl_mutex before checking/changing Spectre v2 mitigation
 * state.
 */
void disable_retpoline(void)
{
	mutex_lock(&spec_ctrl_mutex);

	if (retpoline_enabled()) {
		pr_err("Disabling Spectre v2 mitigation retpoline.\n");
	} else {
		/* retpoline is not enabled.  Return */
		mutex_unlock(&spec_ctrl_mutex);
		return;
	}

	if (allow_retpoline_fallback) {
		if (!ibrs_inuse) {
			/* try to enable ibrs */
			if (set_ibrs_inuse()) {
				pr_err("Spectre v2 mitigation set to IBRS.\n");
				spectre_v2_enabled = SPECTRE_V2_IBRS;
				if (!ibpb_inuse && set_ibpb_inuse()) {
					pr_err("Spectre v2 mitigation IBPB enabled.\n");
				}
			} else {
				pr_err("Could not enable IBRS.\n");
				pr_err("No Spectre v2 mitigation to fall back to.\n");
				spectre_v2_enabled = SPECTRE_V2_NONE;
			}
		} else {
			pr_err("Spectre v2 mitigation IBRS is set.\n");
			spectre_v2_enabled = SPECTRE_V2_IBRS;
		}
	} else {
		pr_err("Cannot choose another Spectre v2 mitigation because retpoline_fallback is off.\n");
		spectre_v2_enabled = SPECTRE_V2_NONE;
	}

	if (spectre_v2_enabled == SPECTRE_V2_NONE)
		pr_err("system may be vulnerable to spectre\n");

	mutex_unlock(&spec_ctrl_mutex);
}

bool retpoline_enabled(void)
{
	switch (spectre_v2_enabled) {
	case SPECTRE_V2_RETPOLINE_MINIMAL:
	case SPECTRE_V2_RETPOLINE_MINIMAL_AMD:
	case SPECTRE_V2_RETPOLINE_GENERIC:
	case SPECTRE_V2_RETPOLINE_AMD:
		return true;
	default:
		break;
	}

	return false;
}

int refresh_set_spectre_v2_enabled(void)
{
	if (retpoline_enabled())
		return false;

	if (check_ibrs_inuse())
		spectre_v2_enabled = SPECTRE_V2_IBRS;
	else {
		spectre_v2_enabled = SPECTRE_V2_NONE;
	}

	return true;
}

static void __init spec2_print_if_insecure(const char *reason)
{
	if (boot_cpu_has_bug(X86_BUG_SPECTRE_V2))
		pr_info("%s\n", reason);
}

static void __init spec2_print_if_secure(const char *reason)
{
	if (!boot_cpu_has_bug(X86_BUG_SPECTRE_V2))
		pr_info("%s\n", reason);
}

static inline bool retp_compiler(void)
{
	return __is_defined(RETPOLINE);
}

static inline bool match_option(const char *arg, int arglen, const char *opt)
{
	int len = strlen(opt);

	return len == arglen && !strncmp(arg, opt, len);
}

static enum spectre_v2_mitigation_cmd __init spectre_v2_parse_cmdline(void)
{
	char arg[20];
	int ret;

	if (cmdline_find_option_bool(boot_command_line, "noibrs")) {
		set_ibrs_disabled();
	}

	if (cmdline_find_option_bool(boot_command_line, "noibpb")) {
		set_ibpb_disabled();
	}

	ret = cmdline_find_option(boot_command_line, "spectre_v2", arg,
				  sizeof(arg));
	if (ret > 0)  {
		if (match_option(arg, ret, "off")) {
			goto disable;
		} else if (match_option(arg, ret, "on")) {
			spec2_print_if_secure("force enabled on command line.");
			return SPECTRE_V2_CMD_FORCE;
		} else if (match_option(arg, ret, "retpoline")) {
			spec2_print_if_insecure("retpoline selected on command line.");
			return SPECTRE_V2_CMD_RETPOLINE;
		} else if (match_option(arg, ret, "retpoline,amd")) {
			if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD) {
				pr_err("retpoline,amd selected but CPU is not AMD. Switching to AUTO select\n");
				return SPECTRE_V2_CMD_AUTO;
			}
			spec2_print_if_insecure("AMD retpoline selected on command line.");
			return SPECTRE_V2_CMD_RETPOLINE_AMD;
		} else if (match_option(arg, ret, "retpoline,generic")) {
			spec2_print_if_insecure("generic retpoline selected on command line.");
			return SPECTRE_V2_CMD_RETPOLINE_GENERIC;
		} else if (match_option(arg, ret, "auto")) {
			return SPECTRE_V2_CMD_AUTO;
		} else if (match_option(arg, ret, "ibrs")) {
			return SPECTRE_V2_CMD_IBRS;
		}
	}

	if (!cmdline_find_option_bool(boot_command_line, "nospectre_v2"))
		return SPECTRE_V2_CMD_AUTO;
disable:
	spec2_print_if_insecure("disabled on command line.");
	return SPECTRE_V2_CMD_NONE;
}

static enum spectre_v2_mitigation __init ibrs_select(void)
{
	enum spectre_v2_mitigation mode = SPECTRE_V2_NONE;

	/* If it is ON, OK, lets use it.*/
	if (check_ibrs_inuse())
		mode = SPECTRE_V2_IBRS;

	if (mode == SPECTRE_V2_NONE)
		/* Well, fallback on automatic discovery. */
		pr_info("IBRS could not be enabled.\n");
	else {
		/* OK, some form of IBRS is enabled, lets see if we need to STUFF_RSB */
		if (!boot_cpu_has(X86_FEATURE_SMEP))
			setup_force_cpu_cap(X86_FEATURE_STUFF_RSB);
	}
	return mode;
}

static void __init disable_ibrs_and_friends(bool disable_ibpb)
{
	set_ibrs_disabled();
	if (use_ibrs & SPEC_CTRL_IBRS_SUPPORTED) {
		unsigned int cpu;

		get_online_cpus();
		for_each_online_cpu(cpu)
			wrmsrl_on_cpu(cpu, MSR_IA32_SPEC_CTRL, SPEC_CTRL_FEATURE_DISABLE_IBRS);

		put_online_cpus();
	}
	/*
	 * We need to use IBPB with retpoline if it is available.
	 * And also IBRS for firmware paths.
	 */
	if (disable_ibpb) {
		set_ibpb_disabled();
		disable_ibrs_firmware();
	} else
		set_ibrs_firmware();
}

static bool __init retpoline_selected(enum spectre_v2_mitigation_cmd cmd)
{
	switch (cmd) {
	case SPECTRE_V2_CMD_RETPOLINE_AMD:
	case SPECTRE_V2_CMD_RETPOLINE_GENERIC:
	case SPECTRE_V2_CMD_RETPOLINE:
		return true;
	default:
		return false;
		break;
	}
	return false;
}

static void __init spectre_v2_select_mitigation(void)
{
	enum spectre_v2_mitigation_cmd cmd = spectre_v2_parse_cmdline();
	enum spectre_v2_mitigation mode = SPECTRE_V2_NONE;

	/*
	 * If the CPU is not affected and the command line mode is NONE or AUTO
	 * then nothing to do.
	 */
	if (!boot_cpu_has_bug(X86_BUG_SPECTRE_V2) &&
	    (cmd == SPECTRE_V2_CMD_NONE || cmd == SPECTRE_V2_CMD_AUTO)) {
		disable_ibrs_and_friends(true);
		return;
	}

	switch (cmd) {
	case SPECTRE_V2_CMD_NONE:
		disable_ibrs_and_friends(true);
		return;

	case SPECTRE_V2_CMD_FORCE:
		/* FALLTRHU */
	case SPECTRE_V2_CMD_AUTO:
		goto retpoline_auto;

	case SPECTRE_V2_CMD_RETPOLINE_AMD:
		if (IS_ENABLED(CONFIG_RETPOLINE))
			goto retpoline_amd;
		break;
	case SPECTRE_V2_CMD_RETPOLINE_GENERIC:
		if (IS_ENABLED(CONFIG_RETPOLINE))
			goto retpoline_generic;
		break;
	case SPECTRE_V2_CMD_RETPOLINE:
		if (IS_ENABLED(CONFIG_RETPOLINE))
			goto retpoline_auto;
		break;
	case SPECTRE_V2_CMD_IBRS:
		mode = ibrs_select();
		if (mode == SPECTRE_V2_NONE)
			goto retpoline_auto;

		goto display;
		break; /* Not needed but compilers may complain otherwise. */
	}
	pr_err("kernel not compiled with retpoline; retpoline mitigation not available");
	goto out;

retpoline_auto:
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD) {
	retpoline_amd:
		if (!boot_cpu_has(X86_FEATURE_LFENCE_RDTSC)) {
			pr_err("LFENCE not serializing. Switching to generic retpoline\n");
			goto retpoline_generic;
		}
		mode = retp_compiler() ? SPECTRE_V2_RETPOLINE_AMD :
					 SPECTRE_V2_RETPOLINE_MINIMAL_AMD;
		/* On AMD we do not need IBRS, so lets use the ASM mitigation. */
		setup_force_cpu_cap(X86_FEATURE_RETPOLINE_AMD);
		setup_force_cpu_cap(X86_FEATURE_RETPOLINE);
	} else {
	retpoline_generic:
		mode = retp_compiler() ? SPECTRE_V2_RETPOLINE_GENERIC :
					 SPECTRE_V2_RETPOLINE_MINIMAL;

		pr_info("Options: %s%s\n",
			check_ibrs_inuse() ? "IBRS " : "",
			retp_compiler() ? "retpoline" : "");

		/* IBRS available. Lets see if we are compiled with retpoline. */
		if (check_ibrs_inuse()) {
			/*
			 * If we are on Skylake, use IBRS (if available). But if we
			 * are forced to use retpoline on Skylake then use that.
			 */
			if (!retp_compiler() /* prefer IBRS over minimal ASM */ ||
			    (retp_compiler() && !retpoline_selected(cmd) &&
			     is_skylake_era() && use_ibrs_on_skylake)) {
				mode = SPECTRE_V2_IBRS;
				/* OK, some form of IBRS is enabled, lets see if we need to STUFF_RSB */
				if (!boot_cpu_has(X86_FEATURE_SMEP))
					setup_force_cpu_cap(X86_FEATURE_STUFF_RSB);
				goto display;
			}
		}
		setup_force_cpu_cap(X86_FEATURE_RETPOLINE);
	}
display:
	spectre_v2_enabled = mode;
	pr_info("%s\n", spectre_v2_strings[mode]);

out:
	/*
	 * If neither SMEP or KPTI are available, there is a risk of
	 * hitting userspace addresses in the RSB after a context switch
	 * from a shallow call stack to a deeper one. To prevent this fill
	 * the entire RSB, even when using IBRS.
	 *
	 * Skylake era CPUs have a separate issue with *underflow* of the
	 * RSB, when they will predict 'ret' targets from the generic BTB.
	 * The proper mitigation for this is IBRS. If IBRS is not supported
	 * or deactivated in favour of retpolines the RSB fill on context
	 * switch is required.
	 */
	if (((mode != SPECTRE_V2_IBRS) && (mode != SPECTRE_V2_IBRS_LFENCE)) &&
	    ((!boot_cpu_has(X86_FEATURE_PTI) &&
	     !boot_cpu_has(X86_FEATURE_SMEP)) || is_skylake_era())) {
		setup_force_cpu_cap(X86_FEATURE_RSB_CTXSW);
		pr_info("Filling RSB on context switch\n");
	}

	/* IBRS is unnecessary with retpoline mitigation. */
	if (mode == SPECTRE_V2_RETPOLINE_GENERIC ||
	    mode == SPECTRE_V2_RETPOLINE_AMD) {
		disable_ibrs_and_friends(false /* Do use IPBP if possible */);
	}
	/* Future CPUs with IBRS_ALL might be able to avoid this. */
	setup_force_cpu_cap(X86_FEATURE_VMEXIT_RSB_FULL);

	/* Initialize Indirect Branch Prediction Barrier if supported */
	if (boot_cpu_has(X86_FEATURE_IBPB) && ibpb_inuse)
		pr_info("Enabling Indirect Branch Prediction Barrier\n");

	if (ibrs_firmware)
		pr_info("Enabling Restricted Speculation for firmware calls\n");
}

#undef pr_fmt

#ifdef CONFIG_SYSFS
ssize_t cpu_show_meltdown(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	if (!boot_cpu_has_bug(X86_BUG_CPU_MELTDOWN))
		return sprintf(buf, "Not affected\n");
	if (boot_cpu_has(X86_FEATURE_PTI))
		return sprintf(buf, "Mitigation: PTI\n");
	return sprintf(buf, "Vulnerable\n");
}

ssize_t cpu_show_spectre_v1(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	if (!boot_cpu_has_bug(X86_BUG_SPECTRE_V1))
		return sprintf(buf, "Not affected\n");
	/* At the moment, a single hard-wired mitigation */
	return sprintf(buf, "Mitigation: lfence\n");
}

ssize_t cpu_show_spectre_v2(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	if (!boot_cpu_has_bug(X86_BUG_SPECTRE_V2))
		return sprintf(buf, "Not affected\n");

	return sprintf(buf, "%s%s%s\n", spectre_v2_strings[spectre_v2_enabled],
					ibrs_firmware ? ", IBRS_FW" : "",
					ibpb_inuse ? ", IBPB" : "");
}
#endif
