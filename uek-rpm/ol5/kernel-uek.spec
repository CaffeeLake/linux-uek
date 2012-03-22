Summary: The Linux kernel


# For a stable, released kernel, released_kernel should be 1. For rawhide
# and/or a kernel built from an rc or git snapshot, released_kernel should
# be 0.
%define released_kernel 1

# Versions of various parts

# Polite request for people who spin their own kernel rpms:
# please modify the "buildid" define in a way that identifies
# that the kernel isn't the stock distribution kernel, for example,
# by setting the define to ".local" or ".bz123456"
#
# % define buildid .local

%define rhel 1
%if %{rhel}
%define distro_build 200
%define signmodules 1
%else

# fedora_build defines which build revision of this kernel version we're
# building. Rather than incrementing forever, as with the prior versioning
# setup, we set fedora_cvs_origin to the current cvs revision s/1.// of the
# kernel spec when the kernel is rebased, so fedora_build automatically
# works out to the offset from the rebase, so it doesn't get too ginormous.
#
# If you're building on a branch, the RCS revision will be something like
# 1.1205.1.1.  In this case we drop the initial 1, subtract fedora_cvs_origin
# from the second number, and then append the rest of the RCS string as is.
# Don't stare at the awk too long, you'll go blind.
%define fedora_cvs_origin   1462
%define fedora_cvs_revision() %2
%global fedora_build %(echo %{fedora_cvs_origin}.%{fedora_cvs_revision $Revision: 1.1504 $} | awk -F . '{ OFS = "."; ORS = ""; print $3 - $1 ; i = 4 ; OFS = ""; while (i <= NF) { print ".", $i ; i++} }')
%define distro_build %{fedora_build}
%define signmodules 0
%endif

# base_sublevel is the kernel version we're starting with and patching
# on top of -- for example, 2.6.22-rc7-git1 starts with a 2.6.21 base,
# which yields a base_sublevel of 21.
%define base_sublevel 39

## If this is a released kernel ##
%if 0%{?released_kernel}

# Do we have a -stable update to apply?
%define stable_update 0
# Is it a -stable RC?
%define stable_rc 0
# Set rpm version accordingly
%if 0%{?stable_update}
%define stablerev .%{stable_update}
%define stable_base %{stable_update}
%if 0%{?stable_rc}
# stable RCs are incremental patches, so we need the previous stable patch
%define stable_base %(echo $((%{stable_update} - 1)))
%endif
%endif
%define rpmversion 2.6.%{base_sublevel}%{?stablerev}

## The not-released-kernel case ##
%else
# The next upstream release sublevel (base_sublevel+1)
%define upstream_sublevel %(echo $((%{base_sublevel} + 1)))
# The rc snapshot level
%define rcrev 0
# The git snapshot level
%define gitrev 0
# Set rpm version accordingly
%define rpmversion 2.6.%{upstream_sublevel}
%endif
# Nb: The above rcrev and gitrev values automagically define Patch00 and Patch01 below.

# What parts do we want to build?  We must build at least one kernel.
# These are the kernels that are built IF the architecture allows it.
# All should default to 1 (enabled) and be flipped to 0 (disabled)
# by later arch-specific checks.

# The following build options are enabled by default.
# Use either --without <opt> in your rpmbuild command or force values
# to 0 in here to disable them.
#
# standard kernel
%define with_up        1
# kernel-smp (only valid for ppc 32-bit, sparc64)
%define with_smp       1
# kernel-kdump
%define with_kdump     0
# kernel-debug
%define with_debug     1
# kernel-doc
%define with_doc       1
# kernel-headers
%define with_headers   1
# kernel-firmware
%define with_firmware  0
# kernel-debuginfo
%define with_debuginfo %{?_without_debuginfo: 0} %{?!_without_debuginfo: 1}
# kernel-bootwrapper (for creating zImages from kernel + initrd)
%define with_bootwrapper %{?_without_bootwrapper: 0} %{?!_without_bootwrapper: 1}
# Want to build a the vsdo directories installed
%define with_vdso_install %{?_without_vdso_install: 0} %{?!_without_vdso_install: 1}
# Control whether we perform a compat. check against published ABI.
%define with_kabichk	0

# Build the kernel-doc package, but don't fail the build if it botches.
# Here "true" means "continue" and "false" means "fail the build".
%if 0%{?released_kernel}
%define doc_build_fail false
%else
%define doc_build_fail true
%endif

# Additional options for user-friendly one-off kernel building:
#
# Only build the base kernel (--with baseonly):
%define with_baseonly  %{?_with_baseonly:     1} %{?!_with_baseonly:     0}
# Only build the smp kernel (--with smponly):
%define with_smponly   %{?_with_smponly:      1} %{?!_with_smponly:      0}

# should we do C=1 builds with sparse
%define with_sparse	%{?_with_sparse:      1} %{?!_with_sparse:      0}

# Set debugbuildsenabled to 1 for production (build separate debug kernels)
#  and 0 for rawhide (all kernels are debug kernels).
# See also 'make debug' and 'make release'.
%define debugbuildsenabled 1

# Want to build a vanilla kernel build without any non-upstream patches?
# (well, almost none, we need nonintconfig for build purposes). Default to 0 (off).
%define with_vanilla %{?_with_vanilla: 1} %{?!_with_vanilla: 0}

# pkg_release is what we'll fill in for the rpm Release: field
%if 0%{?released_kernel}

%if 0%{?stable_rc}
%define stable_rctag .rc%{stable_rc}
%endif
%define pkg_release %{distro_build}%{?stable_rctag}%{?dist}%{?buildid}

%else

# non-released_kernel
%if 0%{?rcrev}
%define rctag .rc%rcrev
%else
%define rctag .rc0
%endif
%if 0%{?gitrev}
%define gittag .git%gitrev
%else
%define gittag .git0
%endif
%define pkg_release 0.%{distro_build}%{?rctag}%{?gittag}%{?dist}%{?buildid}

%endif

# The kernel tarball/base version
%define kversion 2.6.%{base_sublevel}

%define make_target bzImage

%define hdrarch %_target_cpu
%define asmarch %_target_cpu

%if 0%{!?nopatches:1}
%define nopatches 0
%endif

%if %{with_vanilla}
%define nopatches 1
%endif

%if %{nopatches}
%define with_bootwrapper 0
%define variant -vanilla
%else
%define variant_fedora -fedora
%endif

%define using_upstream_branch 0
%if 0%{?upstream_branch:1}
%define stable_update 0
%define using_upstream_branch 1
%define variant -%{upstream_branch}%{?variant_fedora}
%define pkg_release 0.%{distro_build}%{upstream_branch_tag}%{?dist}%{?buildid}
%endif

%if %{rhel}
%define pkg_release %{distro_build}.0.4%{?dist}uek%{?buildid}
%endif
%define KVERREL %{rpmversion}-%{pkg_release}

%if !%{debugbuildsenabled}
%define with_debug 0
%endif

%if !%{with_debuginfo}
%define _enable_debug_packages 0
%endif
%define debuginfodir /usr/lib/debug

# We only build -PAE for 686 as of Fedora 11.
%ifarch i686
%define with_up 1
%define with_pae 0
%else
%define with_pae 0
%endif

# if requested, only build base kernel
%if %{with_baseonly}
%define with_smp 0
%define with_kdump 0
%define with_debug 0
%endif

# if requested, only build smp kernel
%if %{with_smponly}
%define with_up 0
%define with_kdump 0
%define with_debug 0
%endif

%define all_x86 i386 i686

%if %{with_vdso_install}
# These arches install vdso/ directories.
%define vdso_arches %{all_x86} x86_64 ppc ppc64
%endif

# Overrides for generic default options

# only ppc and sparc64 need separate smp kernels
%ifnarch ppc sparc64 alphaev56
%define with_smp 0
%endif

# only build kernel-kdump on ppc64
# (no relocatable kernel support upstream yet)
#FIXME: Temporarily disabled to speed up builds.
#ifnarch ppc64
%define with_kdump 0
#endif

# don't do debug builds on anything but i686 and x86_64
%ifnarch i686 x86_64
%define with_debug 0
%endif

# only package docs noarch
%ifnarch noarch
%define with_doc 0
%endif

# no need to build headers again for these arches,
# they can just use i586 and ppc64 headers
%ifarch ppc64iseries
%define with_headers 0
%endif

# don't build noarch kernels or headers (duh)
%ifarch noarch
%define with_up 0
%define with_headers 0
%define with_paravirt 0
%define with_paravirt_debug 0
%define all_arch_configs kernel-%{version}-*.config
%define with_firmware  %{?_without_firmware:  0} %{?!_without_firmware:  1}
%endif

# bootwrapper is only on ppc
%ifnarch ppc ppc64
%define with_bootwrapper 0
%endif

# sparse blows up on ppc64 alpha and sparc64
%ifarch ppc64 ppc alpha sparc64
%define with_sparse 0
%endif

# Per-arch tweaks

%ifarch %{all_x86}
%define asmarch x86
%define hdrarch i386
%define all_arch_configs kernel-%{version}-i?86*.config
%define image_install_path boot
%define kernel_image arch/x86/boot/bzImage
%endif

%ifarch x86_64
%define asmarch x86
%define all_arch_configs kernel-%{version}-x86_64*.config
%define image_install_path boot
%define kernel_image arch/x86/boot/bzImage
%endif

%ifarch ppc64
%define asmarch powerpc
%define hdrarch powerpc
%define all_arch_configs kernel-%{version}-ppc64*.config
%define image_install_path boot
%define make_target vmlinux
%define kernel_image vmlinux
%define kernel_image_elf 1
%endif

%ifarch s390x
%define asmarch s390
%define hdrarch s390
%define all_arch_configs kernel-%{version}-s390x.config
%define image_install_path boot
%define make_target image
%define kernel_image arch/s390/boot/image
%endif

%ifarch sparc
# We only build sparc headers since we dont support sparc32 hardware
%endif

%ifarch sparc64
%define asmarch sparc
%define all_arch_configs kernel-%{version}-sparc64*.config
%define make_target image
%define kernel_image arch/sparc/boot/image
%define image_install_path boot
%endif

%ifarch ppc
%define asmarch powerpc
%define hdrarch powerpc
%define all_arch_configs kernel-%{version}-ppc{-,.}*config
%define image_install_path boot
%define make_target vmlinux
%define kernel_image vmlinux
%define kernel_image_elf 1
%endif

%ifarch ia64
%define all_arch_configs kernel-%{version}-ia64*.config
%define image_install_path boot/efi/EFI/redhat
%define make_target compressed
%define kernel_image vmlinux.gz
%endif

%ifarch alpha alphaev56
%define all_arch_configs kernel-%{version}-alpha*.config
%define image_install_path boot
%define make_target vmlinux
%define kernel_image vmlinux
%endif

%ifarch %{arm}
%define all_arch_configs kernel-%{version}-arm*.config
%define image_install_path boot
%define hdrarch arm
%define make_target vmlinux
%define kernel_image vmlinux
%endif

%if %{nopatches}
# XXX temporary until last vdso patches are upstream
%define vdso_arches ppc ppc64
%endif

%define oldconfig_target oldnoconfig

# To temporarily exclude an architecture from being built, add it to
# %nobuildarches. Do _NOT_ use the ExclusiveArch: line, because if we
# don't build kernel-headers then the new build system will no longer let
# us use the previous build of that package -- it'll just be completely AWOL.
# Which is a BadThing(tm).

# We don't build a kernel on i386; we only do kernel-headers there,
# and we no longer build for 31bit S390. Same for 32bit sparc and arm.
##%define nobuildarches i386 s390 sparc %{arm}
%define nobuildarches s390 sparc %{arm}

%ifarch %nobuildarches
%define with_up 0
%define with_smp 0
%define with_pae 0
%define with_kdump 0
%define with_debuginfo 0
%define _enable_debug_packages 0
%define with_paravirt 0
%define with_paravirt_debug 0
%endif

%define with_pae_debug 0
%if %{with_pae}
%define with_pae_debug %{with_debug}
%endif

#
# Three sets of minimum package version requirements in the form of Conflicts:
# to versions below the minimum
#

#
# First the general kernel 2.6 required versions as per
# Documentation/Changes
#
%define kernel_dot_org_conflicts  ppp < 2.4.3-3, isdn4k-utils < 3.2-32, nfs-utils < 1.0.7-12, e2fsprogs < 1.37-4, util-linux < 2.12, jfsutils < 1.1.7-2, reiserfs-utils < 3.6.19-2, xfsprogs < 2.6.13-4, procps < 3.2.5-6.3, oprofile < 0.9.1-2

#
# Then a series of requirements that are distribution specific, either
# because we add patches for something, or the older versions have
# problems with the newer kernel or lack certain things that make
# integration in the distro harder than needed.
#
##%define package_conflicts initscripts < 7.23, udev < 063-6, iptables < 1.3.2-1, ipw2200-firmware < 2.4, iwl4965-firmware < 228.57.2, selinux-policy-targeted < 1.25.3-14, squashfs-tools < 4.0, wireless-tools < 29-3
%define package_conflicts initscripts < 7.23, udev < 095-14.27.0.1.el5_7.1, iptables < 1.3.2-1, ipw2200-firmware < 2.4, selinux-policy-targeted < 1.25.3-14

#
# The ld.so.conf.d file we install uses syntax older ldconfig's don't grok.
#
%define kernel_xen_conflicts glibc < 2.3.5-1, xen < 3.0.1

# upto and including kernel 2.4.9 rpms, the 4Gb+ kernel was called kernel-enterprise
# now that the smp kernel offers this capability, obsolete the old kernel
%define kernel_smp_obsoletes kernel-enterprise < 2.4.10
%define kernel_PAE_obsoletes kernel-smp < 2.6.17
%define kernel_PAE_provides kernel-xen = %{rpmversion}-%{pkg_release}

%ifarch x86_64
%define kernel_provides kernel%{?variant}-xen = %{rpmversion}-%{pkg_release}
%endif

# We moved the drm include files into kernel-headers, make sure there's
# a recent enough libdrm-devel on the system that doesn't have those.
%define kernel_headers_conflicts libdrm-devel < 2.4.0-0.15

#
# Packages that need to be installed before the kernel is, because the %post
# scripts use them.
#
##%define kernel_prereq  fileutils, module-init-tools, initscripts >= 8.11.1-1, mkinitrd >= 6.0.61-1, kernel-firmware >= %{rpmversion}-%{pkg_release}, /sbin/new-kernel-pkg
%define kernel_prereq  fileutils, module-init-tools, initscripts >= 8.11.1-1, kernel-firmware = %{rpmversion}-%{pkg_release}, /sbin/new-kernel-pkg, mkinitrd >= 5.1.19.6-71.0.10, kexec-tools >= 1.102pre-96.1.1.el5_5.2, device-mapper-multipath >= 0.4.9-23.0.1.el5, ql2xxx-firmware, oraclelinux-release

#
# This macro does requires, provides, conflicts, obsoletes for a kernel package.
#	%%kernel_reqprovconf <subpackage>
# It uses any kernel_<subpackage>_conflicts and kernel_<subpackage>_obsoletes
# macros defined above.
#
%define kernel_reqprovconf \
Provides: kernel%{?variant} = %{rpmversion}-%{pkg_release}\
Provides: kernel%{?variant}-%{_target_cpu} = %{rpmversion}-%{pkg_release}%{?1:.%{1}}\
Provides: kernel%{?variant}-drm = 4.3.0\
Provides: kernel%{?variant}-drm-nouveau = 12\
Provides: kernel%{?variant}-modeset = 1\
Provides: kernel%{?variant}-uname-r = %{KVERREL}%{?1:.%{1}}\
Provides: oracleasm = 2.0.5\
Provides: perf = %{KVERREL}%{?1:.%{1}}\
Provides: libperf.a = %{KVERREL}%{?1:.%{1}}\
Requires(pre): %{kernel_prereq}\
Conflicts: %{kernel_dot_org_conflicts}\
Conflicts: %{package_conflicts}\
%{expand:%%{?kernel%{?1:_%{1}}_conflicts:Conflicts: %%{kernel%{?1:_%{1}}_conflicts}}}\
#%{expand:%%{?kernel%{?1:_%{1}}_obsoletes:Obsoletes: %%{kernel%{?1:_%{1}}_obsoletes}}}\
%{expand:%%{?kernel%{?1:_%{1}}_provides:Provides: %%{kernel%{?1:_%{1}}_provides}}}\
# We can't let RPM do the dependencies automatic because it'll then pick up\
# a correct but undesirable perl dependency from the module headers which\
# isn't required for the kernel proper to function\
AutoReq: no\
AutoProv: yes\
%{nil}

%define variant -uek
Name: kernel%{?variant}
Group: System Environment/Kernel
License: GPLv2
URL: http://www.kernel.org/
Version: %{rpmversion}
Release: %{pkg_release}
# DO NOT CHANGE THE 'ExclusiveArch' LINE TO TEMPORARILY EXCLUDE AN ARCHITECTURE BUILD.
# SET %%nobuildarches (ABOVE) INSTEAD
ExclusiveArch: noarch %{all_x86} x86_64 ppc ppc64 ia64 sparc sparc64 s390x alpha alphaev56 %{arm}
ExclusiveOS: Linux

%kernel_reqprovconf
%ifarch x86_64
Obsoletes: kernel-smp
%endif


#
# List the packages used during the kernel build
#
BuildRequires: module-init-tools, patch >= 2.5.4, bash >= 2.03, sh-utils, tar
BuildRequires: bzip2, findutils, gzip, m4, perl, make >= 3.78, diffutils, gawk
BuildRequires: gcc >= 3.4.2, binutils >= 2.12
BuildRequires: net-tools
BuildRequires: elfutils-libelf-devel
%if %{with_doc}
BuildRequires: xmlto
%endif
%if %{with_sparse}
BuildRequires: sparse >= 0.4.1
%endif
%if %{signmodules}
BuildRequires: gnupg
%endif
BuildConflicts: rhbuildsys(DiskFree) < 500Mb

Source0: ftp://ftp.kernel.org/pub/linux/kernel/v2.6/linux-%{kversion}.tar.bz2

Source11: genkey
Source14: find-provides
Source15: merge.pl
Source16: perf
Source17: kabitool
Source18: check-kabi
Source19: extrakeys.pub

Source20: Makefile.config
Source21: config-debug
Source22: config-nodebug
Source23: config-generic
Source24: config-rhel-generic
Source25: Module.kabi_i686
Source28: Module.kabi_x86_64

Source30: config-x86-generic
##Source31: config-i586
Source32: config-i686

Source40: config-x86_64-generic

Source50: config-powerpc-generic
Source51: config-powerpc32-generic
Source52: config-powerpc32-smp
Source53: config-powerpc64
##Source54: config-powerpc64-kdump

Source60: config-ia64-generic

Source70: config-s390x

Source90: config-sparc64-generic
##Source91: config-sparc64-smp

Source100: config-arm

Source101: kabi_whitelist_i686
Source102: kabi_whitelist_x86_64

# Here should be only the patches up to the upstream canonical Linus tree.

# For a stable release kernel
%if 0%{?stable_update}
%if 0%{?stable_base}
%define    stable_patch_00  patch-2.6.%{base_sublevel}.%{stable_base}.bz2
Patch00: %{stable_patch_00}
%endif
%if 0%{?stable_rc}
%define    stable_patch_01  patch-2.6.%{base_sublevel}.%{stable_update}-rc%{stable_rc}.bz2
Patch01: %{stable_patch_01}
%endif

# non-released_kernel case
# These are automagically defined by the rcrev and gitrev values set up
# near the top of this spec file.
%else
%if 0%{?rcrev}
Patch00: patch-2.6.%{upstream_sublevel}-rc%{rcrev}.bz2
%if 0%{?gitrev}
Patch01: patch-2.6.%{upstream_sublevel}-rc%{rcrev}-git%{gitrev}.bz2
%endif
%else
# pre-{base_sublevel+1}-rc1 case
%if 0%{?gitrev}
Patch00: patch-2.6.%{base_sublevel}-git%{gitrev}.bz2
%endif
%endif
%endif

%if %{using_upstream_branch}
### BRANCH PATCH ###
%endif

%if !%{nopatches}
# revert upstream patches we get via other methods
%endif

BuildRoot: %{_tmppath}/kernel-%{KVERREL}-root

# Override find_provides to use a script that provides "kernel(symbol) = hash".
# Pass path of the RPM temp dir containing kabideps to find-provides script.
%global _use_internal_dependency_generator 0
%define __find_provides %_sourcedir/find-provides %{_tmppath}
%define __find_requires /usr/lib/rpm/redhat/find-requires kernel


%description
The kernel package contains the Linux kernel (vmlinuz), the core of any
Linux operating system.  The kernel handles the basic functions
of the operating system: memory allocation, process allocation, device
input and output, etc.


%package doc
Summary: Various documentation bits found in the kernel source
Group: Documentation
%description doc
This package contains documentation files from the kernel
source. Various bits of information about the Linux kernel and the
device drivers shipped with it are documented in these files.

You'll want to install this package if you need a reference to the
options that can be passed to Linux kernel modules at load time.


%package headers
Summary: Header files for the Linux kernel for use by glibc
Group: Development/System
Obsoletes: glibc-kernheaders
Provides: glibc-kernheaders = 3.0-46
Obsoletes: kernel-headers
Provides: kernel-headers
%description headers
Kernel-headers includes the C header files that specify the interface
between the Linux kernel and userspace libraries and programs.  The
header files define structures and constants that are needed for
building most standard programs and are also needed for rebuilding the
glibc package.

%package firmware
Summary: Firmware files used by the Linux kernel
Group: Development/System
# This is... complicated.
# Look at the WHENCE file.
License: GPL+ and GPLv2+ and MIT and Redistributable, no modification permitted
%if "x%{?variant}" != "x"
Provides: kernel-firmware = %{rpmversion}-%{pkg_release}
%endif
%description firmware
Kernel-firmware includes firmware files required for some devices to
operate.

%package bootwrapper
Summary: Boot wrapper files for generating combined kernel + initrd images
Group: Development/System
Requires: gzip
%description bootwrapper
Kernel-bootwrapper contains the wrapper code which makes bootable "zImage"
files combining both kernel and initial ramdisk.

%package debuginfo-common
Summary: Kernel source files used by %{name}-debuginfo packages
Group: Development/Debug
Provides: %{name}-debuginfo-common-%{_target_cpu} = %{version}-%{release}
%description debuginfo-common
This package is required by %{name}-debuginfo subpackages.
It provides the kernel source files common to all builds.


#
# This macro creates a kernel-<subpackage>-debuginfo package.
#	%%kernel_debuginfo_package <subpackage>
#
%define kernel_debuginfo_package() \
%package %{?1:%{1}-}debuginfo\
Summary: Debug information for package %{name}%{?1:-%{1}}\
Group: Development/Debug\
Requires: %{name}-debuginfo-common-%{_target_cpu} = %{version}-%{release}\
Provides: %{name}%{?1:-%{1}}-debuginfo-%{_target_cpu} = %{version}-%{release}\
AutoReqProv: no\
%description -n %{name}%{?1:-%{1}}-debuginfo\
This package provides debug information for package %{name}%{?1:-%{1}}.\
This is required to use SystemTap with %{name}%{?1:-%{1}}-%{KVERREL}.\
%{expand:%%global debuginfo_args %{?debuginfo_args} -p '/.*/%%{KVERREL}%{?1:\.%{1}}/.*|/.*%%{KVERREL}%{?1:\.%{1}}(\.debug)?' -o debuginfo%{?1}.list}\
%{nil}

#
# This macro creates a kernel-<subpackage>-devel package.
#	%%kernel_devel_package <subpackage> <pretty-name>
#
%define kernel_devel_package() \
%package %{?1:%{1}-}devel\
Summary: Development package for building kernel modules to match the %{?2:%{2} }kernel\
Group: System Environment/Kernel\
Provides: kernel%{?variant}%{?1:-%{1}}-devel-%{_target_cpu} = %{version}-%{release}\
Provides: kernel%{?variant}-xen-devel = %{version}-%{release}%{?1:.%{1}}\
Provides: kernel%{?variant}-devel-%{_target_cpu} = %{version}-%{release}%{?1:.%{1}}\
Provides: kernel%{?variant}-devel = %{version}-%{release}%{?1:.%{1}}\
Provides: kernel%{?variant}-devel-uname-r = %{KVERREL}%{?1:.%{1}}\
AutoReqProv: no\
Requires(pre): /usr/bin/find\
%description -n kernel%{?variant}%{?1:-%{1}}-devel\
This package provides kernel headers and makefiles sufficient to build modules\
against the %{?2:%{2} }kernel package.\
%{nil}

#
# This macro creates a kernel-<subpackage> and its -devel and -debuginfo too.
#	%%define variant_summary The Linux kernel compiled for <configuration>
#	%%kernel_variant_package [-n <pretty-name>] <subpackage>
#
%define kernel_variant_package(n:) \
%package %1\
Summary: %{variant_summary}\
Group: System Environment/Kernel\
%kernel_reqprovconf\
%{expand:%%kernel_devel_package %1 %{!?-n:%1}%{?-n:%{-n*}}}\
%{expand:%%kernel_debuginfo_package %1}\
%{nil}


# First the auxiliary packages of the main kernel package.
%kernel_devel_package
%kernel_debuginfo_package


# Now, each variant package.

%define variant_summary The Linux kernel compiled for SMP machines
%kernel_variant_package -n SMP smp
%description smp
This package includes a SMP version of the Linux kernel. It is
required only on machines with two or more CPUs as well as machines with
hyperthreading technology.

Install the kernel-smp package if your machine uses two or more CPUs.


%define variant_summary The Linux kernel compiled for PAE capable machines
%kernel_variant_package PAE
%description PAE
This package includes a version of the Linux kernel with support for up to
64GB of high memory. It requires a CPU with Physical Address Extensions (PAE).
The non-PAE kernel can only address up to 4GB of memory.
Install the kernel-PAE package if your machine has more than 4GB of memory.


%define variant_summary The Linux kernel compiled with extra debugging enabled for PAE capable machines
%kernel_variant_package PAEdebug
Obsoletes: kernel-PAE-debug
%description PAEdebug
This package includes a version of the Linux kernel with support for up to
64GB of high memory. It requires a CPU with Physical Address Extensions (PAE).
The non-PAE kernel can only address up to 4GB of memory.
Install the kernel-PAE package if your machine has more than 4GB of memory.

This variant of the kernel has numerous debugging options enabled.
It should only be installed when trying to gather additional information
on kernel bugs, as some of these options impact performance noticably.


%define variant_summary The Linux kernel compiled with extra debugging enabled
%kernel_variant_package debug
%description debug
The kernel package contains the Linux kernel (vmlinuz), the core of any
Linux operating system.  The kernel handles the basic functions
of the operating system:  memory allocation, process allocation, device
input and output, etc.

This variant of the kernel has numerous debugging options enabled.
It should only be installed when trying to gather additional information
on kernel bugs, as some of these options impact performance noticably.


%define variant_summary A minimal Linux kernel compiled for crash dumps
%kernel_variant_package kdump
%description kdump
This package includes a kdump version of the Linux kernel. It is
required only on machines which will use the kexec-based kernel crash dump
mechanism.


%prep
# do a few sanity-checks for --with *only builds
%if %{with_baseonly}
%if !%{with_up}%{with_pae}
echo "Cannot build --with baseonly, up build is disabled"
exit 1
%endif
%endif

%if %{with_smponly}
%if !%{with_smp}
echo "Cannot build --with smponly, smp build is disabled"
exit 1
%endif
%endif

patch_command='patch -p1 -F1 -s'
ApplyPatch()
{
  local patch=$1
  shift
  if [ ! -f $RPM_SOURCE_DIR/$patch ]; then
    exit 1;
  fi
  if ! egrep "^Patch[0-9]+: $patch\$" %{_specdir}/%{name}.spec ; then
    [ "${patch:0:10}" != "patch-2.6." ] && echo "Patch $patch not listed in specfile" && exit 1;
  fi
  case "$patch" in
  *.bz2) bunzip2 < "$RPM_SOURCE_DIR/$patch" | $patch_command ${1+"$@"} ;;
  *.gz) gunzip < "$RPM_SOURCE_DIR/$patch" | $patch_command ${1+"$@"} ;;
  *) $patch_command ${1+"$@"} < "$RPM_SOURCE_DIR/$patch" ;;
  esac
}

test_config_file()
{
  TestConfig=$1
  Arch=`head -1 .config | cut -b 3-`
  if [ `make ARCH=$Arch listnewconfig 2>/dev/null | grep -c CONFIG`  -ne 0 ]; then 
	echo "Following config options are unconfigured"
	make ARCH=$Arch listnewconfig 2> /dev/null
	echo "WARNING: Kernel version and config file missmatch"
	echo "WARNING: This options will be unset by default in config file"
  fi
}

# First we unpack the kernel tarball.
# If this isn't the first make prep, we use links to the existing clean tarball
# which speeds things up quite a bit.

# Update to latest upstream.
%if 0%{?released_kernel}
%define vanillaversion 2.6.%{base_sublevel}
# non-released_kernel case
%else
%if 0%{?rcrev}
%define vanillaversion 2.6.%{upstream_sublevel}-rc%{rcrev}
%if 0%{?gitrev}
%define vanillaversion 2.6.%{upstream_sublevel}-rc%{rcrev}-git%{gitrev}
%endif
%else
# pre-{base_sublevel+1}-rc1 case
%if 0%{?gitrev}
%define vanillaversion 2.6.%{base_sublevel}-git%{gitrev}
%endif
%endif
%endif

# We can share hardlinked source trees by putting a list of
# directory names of the CVS checkouts that we want to share
# with in .shared-srctree. (Full pathnames are required.)
[ -f .shared-srctree ] && sharedirs=$(cat .shared-srctree)

if [ ! -d kernel-%{kversion}/vanilla-%{vanillaversion} ]; then

  if [ -d kernel-%{kversion}/vanilla-%{kversion} ]; then

    cd kernel-%{kversion}

    # Any vanilla-* directories other than the base one are stale.
    for dir in vanilla-*; do
      [ "$dir" = vanilla-%{kversion} ] || rm -rf $dir &
    done

  else

    # Ok, first time we do a make prep.
    rm -f pax_global_header
    for sharedir in $sharedirs ; do
      if [[ ! -z $sharedir  &&  -d $sharedir/kernel-%{kversion}/vanilla-%{kversion} ]] ; then
        break
      fi
    done
    if [[ ! -z $sharedir  &&  -d $sharedir/kernel-%{kversion}/vanilla-%{kversion} ]] ; then
%setup -q -n kernel-%{kversion} -c -T
      cp -rl $sharedir/kernel-%{kversion}/vanilla-%{kversion} .
    else
%setup -q -n kernel-%{kversion} -c
      mv linux-%{kversion} vanilla-%{kversion}
    fi

  fi

%if "%{kversion}" != "%{vanillaversion}"

  for sharedir in $sharedirs ; do
    if [[ ! -z $sharedir  &&  -d $sharedir/kernel-%{kversion}/vanilla-%{vanillaversion} ]] ; then
      break
    fi
  done
  if [[ ! -z $sharedir  &&  -d $sharedir/kernel-%{kversion}/vanilla-%{vanillaversion} ]] ; then

    cp -rl $sharedir/kernel-%{kversion}/vanilla-%{vanillaversion} .

  else

    cp -rl vanilla-%{kversion} vanilla-%{vanillaversion}
    cd vanilla-%{vanillaversion}

# Update vanilla to the latest upstream.
# (non-released_kernel case only)
%if 0%{?rcrev}
    ApplyPatch patch-2.6.%{upstream_sublevel}-rc%{rcrev}.bz2
%if 0%{?gitrev}
    ApplyPatch patch-2.6.%{upstream_sublevel}-rc%{rcrev}-git%{gitrev}.bz2
%endif
%else
# pre-{base_sublevel+1}-rc1 case
%if 0%{?gitrev}
    ApplyPatch patch-2.6.%{base_sublevel}-git%{gitrev}.bz2
%endif
%endif

    cd ..

  fi

%endif

else
  # We already have a vanilla dir.
  cd kernel-%{kversion}
fi

if [ -d linux-%{kversion}.%{_target_cpu} ]; then
  # Just in case we ctrl-c'd a prep already
  rm -rf deleteme.%{_target_cpu}
  # Move away the stale away, and delete in background.
  mv linux-%{kversion}.%{_target_cpu} deleteme.%{_target_cpu}
  rm -rf deleteme.%{_target_cpu} &
fi

cp -rl vanilla-%{vanillaversion} linux-%{kversion}.%{_target_cpu}

cd linux-%{kversion}.%{_target_cpu}

# released_kernel with possible stable updates
%if 0%{?stable_base}
ApplyPatch %{stable_patch_00}
%endif
%if 0%{?stable_rc}
ApplyPatch %{stable_patch_01}
%endif

%if %{using_upstream_branch}
### BRANCH APPLY ###
%endif

# Drop some necessary files from the source dir into the buildroot
cp $RPM_SOURCE_DIR/config-* .
cp %{SOURCE15} .

# Dynamically generate kernel .config files from config-* files
make -f %{SOURCE20} VERSION=%{version} configs

#if a rhel kernel, apply the rhel config options
%if 0%{?rhel}
  for i in %{all_arch_configs}
  do
    mv $i $i.tmp
    ./merge.pl config-rhel-generic $i.tmp > $i
    rm $i.tmp
  done
%endif

%if !%{nopatches}
# revert patches from upstream that conflict or that we get via other means
%endif

# Any further pre-build tree manipulations happen here.

chmod +x scripts/checkpatch.pl

# only deal with configs if we are going to build for the arch
%ifnarch %nobuildarches

mkdir configs

# Remove configs not for the buildarch
for cfg in kernel-%{version}-*.config; do
  if [ `echo %{all_arch_configs} | grep -c $cfg` -eq 0 ]; then
    rm -f $cfg
  fi
done

%if !%{debugbuildsenabled}
rm -f kernel-%{version}-*debug.config
%endif

# now run oldconfig over all the config files
for i in *.config
do
  mv $i .config
  test_config_file $i
  Arch=`head -1 .config | cut -b 3-`
  make ARCH=$Arch %{oldconfig_target} > /dev/null
  echo "# $Arch" > configs/$i
  cat .config >> configs/$i
done
# end of kernel config
%endif

# get rid of unwanted files resulting from patch fuzz
find . \( -name "*.orig" -o -name "*~" \) -exec rm -f {} \; >/dev/null
%if %{signmodules}
cp %{SOURCE19} .
cat <<EOF
###
### Now generating a PGP key pair to be used for signing modules.
###
### If this takes a long time, you might wish to run rngd in the background to
### keep the supply of entropy topped up.  It needs to be run as root, and
### should use a hardware random number generator if one is available, eg:
###
###     rngd -r /dev/hwrandom
###
### If one isn't available, the pseudo-random number generator can be used:
###
###     rngd -r /dev/urandom
###
EOF
gpg --homedir . --batch --gen-key %{SOURCE11}
cat <<EOF
###
### Key pair generated.
###
EOF

if [ -s %{SOURCE19} ]; then
        gpg --homedir . --no-default-keyring --keyring kernel.pub --import %{SOURCE19}
fi
gpg --homedir . --export --keyring ./kernel.pub Oracle > extract.pub
gcc -o scripts/bin2c scripts/bin2c.c
scripts/bin2c ksign_def_public_key __initdata <extract.pub >crypto/signature/key.h
%endif

###
### build
###
%build

%if %{with_sparse}
%define sparse_mflags	C=1
%endif

cp_vmlinux()
{
  eu-strip --remove-comment -o "$2" "$1"
}

BuildKernel() {
    MakeTarget=$1
    KernelImage=$2
    Flavour=$3
    InstallName=${4:-vmlinuz}

    # Pick the right config file for the kernel we're building
    Config=kernel-%{version}-%{_target_cpu}${Flavour:+-${Flavour}}.config
    DevelDir=/usr/src/kernels/%{KVERREL}${Flavour:+.${Flavour}}

    # When the bootable image is just the ELF kernel, strip it.
    # We already copy the unstripped file into the debuginfo package.
    if [ "$KernelImage" = vmlinux ]; then
      CopyKernel=cp_vmlinux
    else
      CopyKernel=cp
    fi

    KernelVer=%{version}-%{release}${Flavour:+.${Flavour}}
    echo BUILDING A KERNEL FOR ${Flavour} %{_target_cpu}...

    # make sure EXTRAVERSION says what we want it to say
    perl -p -i -e "s/^EXTRAVERSION.*/EXTRAVERSION = %{?stablerev}-%{release}${Flavour:+.${Flavour}}/" Makefile

    # if pre-rc1 devel kernel, must fix up SUBLEVEL for our versioning scheme
    %if !0%{?rcrev}
    %if 0%{?gitrev}
    perl -p -i -e 's/^SUBLEVEL.*/SUBLEVEL = %{upstream_sublevel}/' Makefile
    %endif
    %endif

    # and now to start the build process

    make -s mrproper
    cp configs/$Config .config

    Arch=`head -1 .config | cut -b 3-`
    echo USING ARCH=$Arch

    make -s ARCH=$Arch %{oldconfig_target} > /dev/null
    make -s ARCH=$Arch V=1 %{?_smp_mflags} $MakeTarget %{?sparse_mflags}
    make -s ARCH=$Arch V=1 %{?_smp_mflags} modules %{?sparse_mflags} || exit 1

    # Start installing the results
%if %{with_debuginfo}
    mkdir -p $RPM_BUILD_ROOT%{debuginfodir}/boot
    mkdir -p $RPM_BUILD_ROOT%{debuginfodir}/%{image_install_path}
%endif
    mkdir -p $RPM_BUILD_ROOT/%{image_install_path}
    install -m 644 .config $RPM_BUILD_ROOT/boot/config-$KernelVer
    install -m 644 System.map $RPM_BUILD_ROOT/boot/System.map-$KernelVer
    touch $RPM_BUILD_ROOT/boot/initrd-$KernelVer.img
    if [ -f arch/$Arch/boot/zImage.stub ]; then
      cp arch/$Arch/boot/zImage.stub $RPM_BUILD_ROOT/%{image_install_path}/zImage.stub-$KernelVer || :
    fi
    $CopyKernel $KernelImage \
    		$RPM_BUILD_ROOT/%{image_install_path}/$InstallName-$KernelVer
    chmod 755 $RPM_BUILD_ROOT/%{image_install_path}/$InstallName-$KernelVer

    mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer
    # Override $(mod-fw) because we don't want it to install any firmware
    # We'll do that ourselves with 'make firmware_install'
    make -s ARCH=$Arch INSTALL_MOD_PATH=$RPM_BUILD_ROOT modules_install KERNELRELEASE=$KernelVer mod-fw=
    # check if the modules are being signed
%if %{signmodules}
      if [ -z "$(readelf -n $(find fs/ -name \*.ko | head -n 1) | grep module.sig)" ]; then
        echo "ERROR: modules are NOT signed" >&2;
	    exit 1;
      fi
%endif

	%ifarch %{vdso_arches}
    make -s ARCH=$Arch INSTALL_MOD_PATH=$RPM_BUILD_ROOT vdso_install KERNELRELEASE=$KernelVer
    if grep '^CONFIG_XEN=y$' .config >/dev/null; then
      echo > ldconfig-kernel.conf "\
# This directive teaches ldconfig to search in nosegneg subdirectories
# and cache the DSOs there with extra bit 0 set in their hwcap match
# fields.  In Xen guest kernels, the vDSO tells the dynamic linker to
# search in nosegneg subdirectories and to match this extra hwcap bit
# in the ld.so.cache file.
hwcap 0 nosegneg"
    fi
    if [ ! -s ldconfig-kernel.conf ]; then
      echo > ldconfig-kernel.conf "\
# Placeholder file, no vDSO hwcap entries used in this kernel."
    fi
    %{__install} -D -m 444 ldconfig-kernel.conf \
        $RPM_BUILD_ROOT/etc/ld.so.conf.d/kernel-$KernelVer.conf
%ifnarch noarch
# build tools/perf:
    if [ -d tools/perf ]; then
	cd tools/perf
	make all
# and install it:
	mkdir -p $RPM_BUILD_ROOT/usr/bin/$KernelVer/
	install -m 755 perf $RPM_BUILD_ROOT/usr/bin/$KernelVer/perf
	#install -m 755 libperf.a $RPM_BUILD_ROOT/lib/modules/$KernelVer/bin/%{_target_cpu}/libperf.a
	cd ../..
    fi
%endif
%endif

    # And save the headers/makefiles etc for building modules against
    #
    # This all looks scary, but the end result is supposed to be:
    # * all arch relevant include/ files
    # * all Makefile/Kconfig files
    # * all script/ files

    rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/source
    mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    (cd $RPM_BUILD_ROOT/lib/modules/$KernelVer ; ln -s build source)
    # dirs for additional modules per module-init-tools, kbuild/modules.txt
    mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/extra
    mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/updates
    mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/weak-updates
    # first copy everything
    cp --parents `find  -type f -name "Makefile*" -o -name "Kconfig*"` $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    cp Module.symvers $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    cp System.map $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    if [ -s Module.markers ]; then
      cp Module.markers $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    fi

    # create the kABI metadata for use in packaging
    echo "**** GENERATING kernel ABI metadata ****"
    gzip -c9 < Module.symvers > $RPM_BUILD_ROOT/boot/symvers-$KernelVer.gz
    chmod 0755 %_sourcedir/kabitool
    if [ -e $RPM_SOURCE_DIR/kabi_whitelist_%{_target_cpu}$Flavour ]; then
      cp $RPM_SOURCE_DIR/kabi_whitelist_%{_target_cpu}$Flavour $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/kabi_whitelist 
    fi 
    rm -f %{_tmppath}/kernel-$KernelVer-kabideps
    %_sourcedir/kabitool -s Module.symvers -o %{_tmppath}/kernel-$KernelVer-kabideps

%if %{with_kabichk}
    echo "**** kABI checking is enabled in kernel SPEC file. ****"
    chmod 0755 $RPM_SOURCE_DIR/check-kabi
    if [ -e $RPM_SOURCE_DIR/Module.kabi_%{_target_cpu}$Flavour ]; then
        cp $RPM_SOURCE_DIR/Module.kabi_%{_target_cpu}$Flavour $RPM_BUILD_ROOT/Module.kabi
        $RPM_SOURCE_DIR/check-kabi -k $RPM_BUILD_ROOT/Module.kabi -s Module.symvers || exit 1
        rm $RPM_BUILD_ROOT/Module.kabi # for now, don't keep it around.
    else
        echo "**** NOTE: Cannot find reference Module.kabi file. ****"
    fi
%endif

    # then drop all but the needed Makefiles/Kconfig files
    rm -rf $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/Documentation
    rm -rf $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/scripts
    rm -rf $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include
    cp .config $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    cp -a scripts $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    if [ -d arch/$Arch/scripts ]; then
      cp -a arch/$Arch/scripts $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/arch/%{_arch} || :
    fi
    if [ -f arch/$Arch/*lds ]; then
      cp -a arch/$Arch/*lds $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/arch/%{_arch}/ || :
    fi
    rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/scripts/*.o
    rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/scripts/*/*.o
%ifarch ppc
    cp -a --parents arch/powerpc/lib/crtsavres.[So] $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/
%endif
    if [ -d arch/%{asmarch}/include ]; then
      cp -a --parents arch/%{asmarch}/include $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/
    fi
    cp -a --parents Kbuild $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/
    cp -a --parents kernel/bounds.c $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/
    cp -a --parents arch/%{asmarch}/kernel/asm-offsets.c $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/
    cp -a --parents arch/%{asmarch}/kernel/asm-offsets_64.c $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/
    cp -a --parents security/selinux/include $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/ 
    
    mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include
    cd include
    cp -a acpi config generated crypto keys linux math-emu media mtd net pcmcia rdma rxrpc scsi sound trace video asm-generic drm xen $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include
    asmdir=../arch/%{asmarch}/include/asm
    cp -a $asmdir $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/
    pushd $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include
    ln -s $asmdir asm
    popd
    # Make sure the Makefile and version.h have a matching timestamp so that
    # external modules can be built
    touch -r $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/Makefile $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/linux/version.h
    touch -r $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/.config $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/linux/autoconf.h
    # Copy .config to include/config/auto.conf so "make prepare" is unnecessary.
    cp $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/.config $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/config/auto.conf
    cd ..

    #
    # save the vmlinux file for kernel debugging into the kernel-debuginfo rpm
    #
%if %{with_debuginfo}
    mkdir -p $RPM_BUILD_ROOT%{debuginfodir}/lib/modules/$KernelVer
    cp vmlinux $RPM_BUILD_ROOT%{debuginfodir}/lib/modules/$KernelVer
%endif

    find $RPM_BUILD_ROOT/lib/modules/$KernelVer -name "*.ko" -type f >modnames

    # mark modules executable so that strip-to-file can strip them
    xargs --no-run-if-empty chmod u+x < modnames

    # Generate a list of modules for block and networking.

    fgrep /drivers/ modnames | xargs --no-run-if-empty nm -upA |
    sed -n 's,^.*/\([^/]*\.ko\):  *U \(.*\)$,\1 \2,p' > drivers.undef

    collect_modules_list()
    {
      sed -r -n -e "s/^([^ ]+) \\.?($2)\$/\\1/p" drivers.undef |
      LC_ALL=C sort -u > $RPM_BUILD_ROOT/lib/modules/$KernelVer/modules.$1
    }

    collect_modules_list networking \
    			 'register_netdev|ieee80211_register_hw|usbnet_probe'
    collect_modules_list block \
    			 'ata_scsi_ioctl|scsi_add_host|blk_init_queue|register_mtd_blktrans|scsi_esp_register'
    collect_modules_list drm \
    			 'drm_open|drm_init'
    collect_modules_list modesetting \
    			 'drm_crtc_init'

    # detect missing or incorrect license tags
    rm -f modinfo
    while read i
    do
      echo -n "${i#$RPM_BUILD_ROOT/lib/modules/$KernelVer/} " >> modinfo
      /sbin/modinfo -l $i >> modinfo
    done < modnames

    egrep -v \
    	  'GPL( v2)?$|Dual BSD/GPL$|Dual MPL/GPL$|GPL and additional rights$' \
	  modinfo && exit 1

    rm -f modinfo modnames

    # remove files that will be auto generated by depmod at rpm -i time
    for i in alias ccwmap dep ieee1394map inputmap isapnpmap ofmap pcimap seriomap symbols usbmap
    do
      rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/modules.$i
    done

    # Move the devel headers out of the root file system
    mkdir -p $RPM_BUILD_ROOT/usr/src/kernels
    mv $RPM_BUILD_ROOT/lib/modules/$KernelVer/build $RPM_BUILD_ROOT/$DevelDir
    ln -sf ../../..$DevelDir $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
}

###
# DO it...
###

# prepare directories
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/boot

cd linux-%{kversion}.%{_target_cpu}

%if %{with_debug}
%if %{with_up}
BuildKernel %make_target %kernel_image debug
%endif
%if %{with_pae}
BuildKernel %make_target %kernel_image PAEdebug
%endif
%endif

%if %{with_pae}
BuildKernel %make_target %kernel_image PAE
%endif

%if %{with_up}
BuildKernel %make_target %kernel_image
%endif

%if %{with_smp}
BuildKernel %make_target %kernel_image smp
%endif

%if %{with_kdump}
BuildKernel vmlinux vmlinux kdump vmlinux
%endif

%if %{with_doc}
# Make the HTML and man pages.
make -j1  htmldocs mandocs || %{doc_build_fail}

# sometimes non-world-readable files sneak into the kernel source tree
chmod -R a=rX Documentation
find Documentation -type d | xargs chmod u+w
%endif

###
### Special hacks for debuginfo subpackages.
###

# This macro is used by %%install, so we must redefine it before that.
%define debug_package %{nil}

%if %{with_debuginfo}
%ifnarch noarch
%global __debug_package 1
%files debuginfo-common
%defattr(-,root,root)
/usr/src/debug/kernel-%{version}/linux-%{kversion}.%{_target_cpu}
%dir /usr/src/debug
%dir %{debuginfodir}
%dir %{debuginfodir}/%{image_install_path}
%dir %{debuginfodir}/lib
%dir %{debuginfodir}/lib/modules
%dir %{debuginfodir}/usr/src/kernels
%endif
%endif

###
### install
###

%install

cd linux-%{kversion}.%{_target_cpu}

%if %{with_doc}
docdir=$RPM_BUILD_ROOT%{_datadir}/doc/kernel-doc-%{rpmversion}
man9dir=$RPM_BUILD_ROOT%{_datadir}/man/man9

# copy the source over
mkdir -p $docdir
tar -f - --exclude=man --exclude='.*' -c Documentation | tar xf - -C $docdir

# Install man pages for the kernel API.
mkdir -p $man9dir
find Documentation/DocBook/man -name '*.9.gz' -print0 |
xargs -0 --no-run-if-empty %{__install} -m 444 -t $man9dir $m
ls $man9dir | grep -q '' || > $man9dir/BROKEN
%endif

%ifnarch noarch
# perf shell wrapper
mkdir -p $RPM_BUILD_ROOT/usr/sbin/
cp $RPM_SOURCE_DIR/perf $RPM_BUILD_ROOT/usr/sbin/perf
chmod 0755 $RPM_BUILD_ROOT/usr/sbin/perf
%endif

%if %{with_headers}
# Install kernel headers
make ARCH=%{hdrarch} INSTALL_HDR_PATH=$RPM_BUILD_ROOT/usr headers_install

# Do headers_check but don't die if it fails.
make ARCH=%{hdrarch} INSTALL_HDR_PATH=$RPM_BUILD_ROOT/usr headers_check \
     > hdrwarnings.txt || :
if grep -q exist hdrwarnings.txt; then
   sed s:^$RPM_BUILD_ROOT/usr/include/:: hdrwarnings.txt
   # Temporarily cause a build failure if header inconsistencies.
   # exit 1
fi

find $RPM_BUILD_ROOT/usr/include \
     \( -name .install -o -name .check -o \
     	-name ..install.cmd -o -name ..check.cmd \) | xargs rm -f

# glibc provides scsi headers for itself, for now
rm -rf $RPM_BUILD_ROOT/usr/include/scsi
rm -f $RPM_BUILD_ROOT/usr/include/asm*/atomic.h
rm -f $RPM_BUILD_ROOT/usr/include/asm*/io.h
rm -f $RPM_BUILD_ROOT/usr/include/asm*/irq.h

# these are provided by drm-devel
rm -rf $RPM_BUILD_ROOT/usr/include/drm
%endif

%if %{with_firmware}
mkdir -p $RPM_BUILD_ROOT/lib/firmware/%{rpmversion}-%{pkg_release}
Arch=`head -n 3 .config |grep -e "Linux.*Kernel" |cut -d '/' -f 2 | cut -d ' ' -f 1`
make ARCH=$Arch INSTALL_FW_PATH=$RPM_BUILD_ROOT/lib/firmware/%{rpmversion}-%{pkg_release} firmware_install
%endif

%if %{with_bootwrapper}
Arch=`head -n 3 .config |grep -e "Linux.*Kernel" |cut -d '/' -f 2 | cut -d ' ' -f 1`
make ARCH=$Arch DESTDIR=$RPM_BUILD_ROOT bootwrapper_install WRAPPER_OBJDIR=%{_libdir}/kernel-wrapper WRAPPER_DTSDIR=%{_libdir}/kernel-wrapper/dts
%endif

###
### clean
###

%clean
rm -rf $RPM_BUILD_ROOT

###
### scripts
###

#
# This macro defines a %%post script for a kernel*-devel package.
#	%%kernel_devel_post [<subpackage>]
#
%define kernel_devel_post() \
%{expand:%%post %{?1:%{1}-}devel}\
if [ -f /etc/sysconfig/kernel ]\
then\
    . /etc/sysconfig/kernel || exit $?\
fi\
if [ "$HARDLINK" != "no" -a -x /usr/sbin/hardlink ]\
then\
    (cd /usr/src/kernels/%{KVERREL}%{?1:.%{1}} &&\
     /usr/bin/find . -type f | while read f; do\
       hardlink -c /usr/src/kernels/*.fc*.*/$f $f\
     done)\
fi\
%{nil}

# This macro defines a %%posttrans script for a kernel package.
#	%%kernel_variant_posttrans [<subpackage>]
# More text can follow to go at the end of this variant's %%post.
#
###%define kernel_variant_posttrans() \
###%{expand:%%posttrans %{?1}}\
###/sbin/new-kernel-pkg --package kernel%{?1:-%{1}} --rpmposttrans %{KVERREL}%{?1:.%{1}} || exit $?\
###%{nil}

#
# This macro defines a %%post script for a kernel package and its devel package.
#	%%kernel_variant_post [-v <subpackage>] [-r <replace>]
# More text can follow to go at the end of this variant's %%post.
#
%define kernel_variant_post(uv:r:) \
%{expand:%%kernel_devel_post %{!-u:%{?-v*}}}\
%{expand:%%kernel_variant_posttrans %{!-u:%{?-v*}}}\
%{expand:%%post %{!-u:%{?-v*}}}\
%{-r:\
if [ `uname -i` == "x86_64" -o `uname -i` == "i386" ] &&\
   [ -f /etc/sysconfig/kernel ]; then\
  /bin/sed -r -i -e 's/^DEFAULTKERNEL=%{-r*}$/DEFAULTKERNEL=kernel%{?-v:-%{-v*}}/' /etc/sysconfig/kernel || exit $?\
fi}\
/sbin/new-kernel-pkg --package kernel%{?-v:-%{-v*}} --mkinitrd --depmod --install %{KVERREL}%{!-u:%{?-v:.%{-v*}}} || exit $?\
if [ -x /sbin/weak-modules ]\
then\
    /sbin/weak-modules --add-kernel %{KVERREL}%{!-u:%{?-v:.%{-v*}}} || exit $?\
fi\
ln -sf /lib/firmware/%{rpmversion}-%{pkg_release} /lib/firmware/%{rpmversion}-%{pkg_release}.%{_target_cpu} \
%{nil}

#
# This macro defines a %%preun script for a kernel package.
#	%%kernel_variant_preun <subpackage>
#
%define kernel_variant_preun() \
%{expand:%%preun %{?1}}\
/sbin/new-kernel-pkg --rminitrd --rmmoddep --remove %{KVERREL}%{?1:.%{1}} || exit $?\
if [ -x /sbin/weak-modules ]\
then\
    /sbin/weak-modules --remove-kernel %{KVERREL}%{?1:.%{1}} || exit $?\
   rm -f /lib/firmware/%{rpmversion}-%{pkg_release}.%{_target_cpu} \
fi\
%{nil}

#
# This macro defines a %%pre script for a kernel package.
#	%%kernel_variant_pre <subpackage>
#
%define kernel_variant_pre() \
%{expand:%%pre %{?1}}\
message="Change references of /dev/hd in /etc/fstab to disk label"\
if [ -f /etc/fstab ]\
then\
awk '($2=="/boot")&&/^\\/dev\\/hd/{print $1}' /etc/fstab | egrep -q "^/dev/hd"\
bdretval=$?\
awk '($2=="/")&&/^\\/dev\\/hd/{print $1}' /etc/fstab | egrep -q "^/dev/hd"\
rdretval=$?\
awk '($2=="/boot")&&/^LABEL=/{print $1}' /etc/fstab | egrep -q "^LABEL="\
blretval=$?\
awk '($2=="/")&&/^LABEL=/{print $1}' /etc/fstab | egrep -q "^LABEL="\
rlretval=$?\
if [ $bdretval == 0 ] || [ $rdretval == 0 ]\
then\
echo -e $message\
exit 1\
elif [ $blretval == 0 ] && [ $rlretval == 0 ]\
then\
grep -v "^#" /etc/fstab | egrep -q "/dev/hd"\
if [ $? == 0 ]\
then\
echo -e $message\
fi\
elif [ $blretval == 0 ] && [ $rdretval != 0 ]\
then\
grep -v "^#" /etc/fstab | egrep -q "/dev/hd"\
if [ $? == 0 ]\
then\
echo -e $message\
fi\
elif [ $bdretval != 0 ] && [ $rlretval == 0 ]\
then\
grep -v "^#" /etc/fstab | egrep -q "/dev/hd"\
if [ $? == 0 ]\
then\
echo -e $message\
fi\
elif [ $bdretval != 0 ] && [ $rdretval != 0 ]\
then\
grep -v "^#" /etc/fstab | egrep -q "/dev/hd"\
if [ $? == 0 ]\
then\
echo -e $message\
fi\
fi\
fi\
%{nil}

%kernel_variant_pre
%kernel_variant_preun
%ifarch x86_64
%kernel_variant_post -u -v uek -r (kernel|kernel-smp|kernel-xen|kernel-debug|kernel-uek-debug)
%else
%kernel_variant_post -u -v uek -r (kernel|kernel-smp|kernel-PAE|kernel-xen|kernel-debug|kernel-uek-debug)
%endif

%kernel_variant_pre smp
%kernel_variant_preun smp
%kernel_variant_post -v smp

%kernel_variant_pre PAE
%kernel_variant_preun PAE
%kernel_variant_post -v PAE -r (kernel|kernel-smp|kernel-xen)

%kernel_variant_pre debug
%kernel_variant_preun debug
%kernel_variant_post -v debug

%kernel_variant_post -v PAEdebug -r (kernel|kernel-smp|kernel-xen)
%kernel_variant_preun PAEdebug
%kernel_variant_pre PAEdebug

if [ -x /sbin/ldconfig ]
then
    /sbin/ldconfig -X || exit $?
fi

###
### file lists
###

%if %{with_headers}
%files headers
%defattr(-,root,root)
/usr/include/*
%endif

%if %{with_firmware}
%files firmware
%defattr(-,root,root)
/lib/firmware/*
%doc linux-%{kversion}.%{_target_cpu}/firmware/WHENCE
%endif

%if %{with_bootwrapper}
%files bootwrapper
%defattr(-,root,root)
/usr/sbin/*
%{_libdir}/kernel-wrapper
%endif

# only some architecture builds need kernel-doc
%if %{with_doc}
%files doc
%defattr(-,root,root)
%{_datadir}/doc/kernel-doc-%{rpmversion}/Documentation/*
%dir %{_datadir}/doc/kernel-doc-%{rpmversion}/Documentation
%dir %{_datadir}/doc/kernel-doc-%{rpmversion}
%{_datadir}/man/man9/*
%endif

# This is %{image_install_path} on an arch where that includes ELF files,
# or empty otherwise.
%define elf_image_install_path %{?kernel_image_elf:%{image_install_path}}

#
# This macro defines the %%files sections for a kernel package
# and its devel and debuginfo packages.
#	%%kernel_variant_files [-k vmlinux] <condition> <subpackage>
#
%define kernel_variant_files(k:) \
%if %{1}\
%{expand:%%files %{?2}}\
%defattr(-,root,root)\
/%{image_install_path}/%{?-k:%{-k*}}%{!?-k:vmlinuz}-%{KVERREL}%{?2:.%{2}}\
/boot/System.map-%{KVERREL}%{?2:.%{2}}\
/boot/symvers-%{KVERREL}%{?2:.%{2}}.gz\
/boot/config-%{KVERREL}%{?2:.%{2}}\
%dir /lib/modules/%{KVERREL}%{?2:.%{2}}\
/lib/modules/%{KVERREL}%{?2:.%{2}}/kernel\
/lib/modules/%{KVERREL}%{?2:.%{2}}/build\
/lib/modules/%{KVERREL}%{?2:.%{2}}/source\
/lib/modules/%{KVERREL}%{?2:.%{2}}/extra\
/lib/modules/%{KVERREL}%{?2:.%{2}}/updates\
/lib/modules/%{KVERREL}%{?2:.%{2}}/weak-updates\
%ifarch %{vdso_arches}\
/lib/modules/%{KVERREL}%{?2:.%{2}}/vdso\
/etc/ld.so.conf.d/kernel-%{KVERREL}%{?2:.%{2}}.conf\
%endif\
/lib/modules/%{KVERREL}%{?2:.%{2}}/modules.*\
/usr/bin/%{KVERREL}%{?2:.%{2}}/perf\
/usr/sbin/perf\
%ghost /boot/initrd-%{KVERREL}%{?2:.%{2}}.img\
%{expand:%%files %{?2:%{2}-}devel}\
%defattr(-,root,root)\
%dir /usr/src/kernels\
%verify(not mtime) /usr/src/kernels/%{KVERREL}%{?2:.%{2}}\
/usr/src/kernels/%{KVERREL}%{?2:.%{2}}\
%if %{with_debuginfo}\
%ifnarch noarch\
%{expand:%%files %{?2:%{2}-}debuginfo}\
%defattr(-,root,root)\
%{debuginfodir}/lib/modules/%{KVERREL}%{?2:.%{2}}\
%{debuginfodir}/usr/src/kernels/%{KVERREL}%{?2:.%{2}}\
%{debuginfodir}/usr/bin/%{KVERREL}%{?2:.%{2}}\
%endif\
%endif\
%endif\
%{nil}


%kernel_variant_files %{with_up}
%kernel_variant_files %{with_smp} smp
%if %{with_up}
%kernel_variant_files %{with_debug} debug
%endif
%kernel_variant_files %{with_pae} PAE
%kernel_variant_files %{with_pae_debug} PAEdebug
%kernel_variant_files -k vmlinux %{with_kdump} kdump

%changelog
* Mon Mar 21 2012 Maxim Uvarov <maxim.uvarov@oracle.com> [2.6.39-200.0.4.el5uek]
- ocfs2/trivial: Print message indicating unaligned aio+dio write (Sunil
  Mushran)
- ocfs2: Avoid livelock in ocfs2_readpage() (Jan Kara)
- ocfs2: serialize unaligned aio (Mark Fasheh)
- ocfs2: null deref on allocation error (Dan Carpenter)
- ocfs2: Bugfix for hard readonly mount (Tiger Yang)

* Mon Mar 19 2012 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-200.0.3.el5uek]
- xen/blkback: Disable DISCARD support for loopback device (but leave for phy).
  (Konrad Rzeszutek Wilk)
- block: fix patch import error in max_discard_sectors check (Jens Axboe)
- block: eliminate potential for infinite loop in blkdev_issue_discard (Mike
  Snitzer)
- config: Use the xen-acpi-processor instead of the cpufreq-xen driver. (Konrad
  Rzeszutek Wilk)
- xen/acpi-processor: C and P-state driver that uploads said data to
  hypervisor. (Konrad Rzeszutek Wilk)
- Revert "Merge branch 'stable/cpufreq-xen.v6.rebased' into uek2-merge" (Konrad
  Rzeszutek Wilk)

* Wed Mar 14 2012 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-200.0.2.el5uek]
- xen: make page table walk hugepages aware (Dave McCracken) [Orabug: 13719997]
- x86/PCI: Preserve existing pci=bfsort whitelist for Dell systems (Narendra_K)

* Sun Mar 11 2012 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-200.0.1.el5uek]
- disable kabicheck for uek2 update 1 beta
- nfs: only dirty user pages in direct read code (Dave Kleikamp)
- config: Enable Xen's PV USB, SCSI, MCE and Xen CPU freq driver (Konrad
  Rzeszutek Wilk)
- [CPUFREQ] xen: governor for Xen hypervisor frequency scaling. (Konrad
  Rzeszutek Wilk)
- xen/enlighten: Expose MWAIT and MWAIT_LEAF if hypervisor OKs it. (Konrad
  Rzeszutek Wilk)
- Revert "Merge branch 'stable/processor-passthru.v5.rebased' into uek2-merge"
  (Konrad Rzeszutek Wilk)
- xen/processor-passthru: threads aren't suppose to leave on their own. (Konrad
  Rzeszutek Wilk)
- config: Enable Xen's PV USB, SCSI, MCE and Processor-Passthru (Konrad
  Rzeszutek Wilk)
- Xen: Export host physical CPU information to dom0 (Liu Jinsong)
- xen/mce: Change the machine check point (Liu Jinsong)
- Add mcelog support from xen platform (Liu Jinsong)
- usb: xen pvusb driver (Nathanael Rensen)
- xen/processor-passthru: Provide an driver that passes struct acpi_processor
  data to the hypervisor. (Konrad Rzeszutek Wilk)
- xen/enlighten: Expose MWAIT and MWAIT_LEAF if hypervisor OKs it. (Konrad
  Rzeszutek Wilk)
- xen/setup/pm/acpi: Remove the call to boot_option_idle_override. (Konrad
  Rzeszutek Wilk)
- xen/acpi: Domain0 acpi parser related platform hypercall (Yu Ke)
- xen/pm_idle: Make pm_idle be default_idle under Xen. (Konrad Rzeszutek Wilk)
- cpuidle: stop depending on pm_idle (Len Brown)
- cpuidle: replace xen access to x86 pm_idle and default_idle (Len Brown)
- cpuidle: create bootparam "cpuidle.off=1" (Len Brown)
- Revert "Merge branch 'stable/acpi-cpufreq.v3.rebased' into uek2-merge"
  (Konrad Rzeszutek Wilk)
- x86/microcode: check proper return code. (Ben Guthro)
- xen: add CPU microcode update driver (Jeremy Fitzhardinge)
- xen: add dom0_op hypercall (Jeremy Fitzhardinge)
- xen/acpi: Domain0 acpi parser related platform hypercall (Yu Ke)
- nfs: add support for read_iter, write_iter (Dave Kleikamp)
- xenbus_dev: add missing error check to watch handling (Jan Beulich)
- xen/pci[front|back]: Use %d instead of %1x for displaying PCI devfn. (Konrad
  Rzeszutek Wilk)
- xen pvhvm: do not remap pirqs onto evtchns if !xen_have_vector_callback
  (Stefano Stabellini)
- xen/smp: Fix CPU online/offline bug triggering a BUG: scheduling while
  atomic. (Konrad Rzeszutek Wilk)
- xen/bootup: During bootup suppress XENBUS: Unable to read cpu state (Konrad
  Rzeszutek Wilk)
- Merge conflict resolved. Somehow the letter 's' slipped in the Makefile. This
  fixes the compile issues. (Konrad Rzeszutek Wilk)
- xen/events: BUG() when we can't allocate our event->irq array. (Konrad
  Rzeszutek Wilk)
- xen/granttable: Disable grant v2 for HVM domains. (Konrad Rzeszutek Wilk)
- xen-blkfront: Use kcalloc instead of kzalloc to allocate array (Thomas Meyer)
- xen/pciback: Expand the warning message to include domain id. (Konrad
  Rzeszutek Wilk)
- xen/pciback: Fix "device has been assigned to X domain!" warning (Konrad
  Rzeszutek Wilk)
- xen/xenbus: don't reimplement kvasprintf via a fixed size buffer (Ian
  Campbell)
- xenbus: maximum buffer size is XENSTORE_PAYLOAD_MAX (Ian Campbell)
- xen/xenbus: Reject replies with payload > XENSTORE_PAYLOAD_MAX. (Ian
  Campbell)
- Xen: consolidate and simplify struct xenbus_driver instantiation (Jan
  Beulich)
- xen-gntalloc: introduce missing kfree (Julia Lawall)
- xen/xenbus: Fix compile error - missing header for xen_initial_domain()
  (Konrad Rzeszutek Wilk)
- xen/netback: Enable netback on HVM guests (Daniel De Graaf)
- xen/grant-table: Support mappings required by blkback (Daniel De Graaf)
- xenbus: Use grant-table wrapper functions (Daniel De Graaf)
- xenbus: Support HVM backends (Daniel De Graaf)
- xen/xenbus-frontend: Fix compile error with randconfig (Konrad Rzeszutek
  Wilk)
- xen/xenbus-frontend: Make error message more clear (Bastian Blank)
- xen/privcmd: Remove unused support for arch specific privcmp mmap (Bastian
  Blank)
- xen: Add xenbus_backend device (Bastian Blank)
- xen: Add xenbus device driver (Bastian Blank)
- xen: Add privcmd device driver (Bastian Blank)
- xen/gntalloc: fix reference counts on multi-page mappings (Daniel De Graaf)
- xen/gntalloc: release grant references on page free (Daniel De Graaf)
- xen/events: prevent calling evtchn_get on invalid channels (Daniel De Graaf)
- xen/granttable: Support transitive grants (Annie Li)
- xen/granttable: Support sub-page grants (Annie Li)
- xen/granttable: Improve comments for function pointers (Annie Li)
- xen/ia64: fix build breakage because of conflicting u64 guest handles (Tony
  Luck)
- xen/granttable: Keep code format clean (Annie Li)
- xen/granttable: Grant tables V2 implementation (Annie Li)
- xen/granttable: Refactor some code (Annie Li)
- xen/granttable: Introducing grant table V2 stucture (Annie Li)
- Xen: update MAINTAINER info (Jeremy Fitzhardinge)
- xen/event: Add reference counting to event channels (Daniel De Graaf)
- xen/gnt{dev,alloc}: reserve event channels for notify (Daniel De Graaf)
- xen/gntalloc: Change gref_lock to a mutex (Daniel De Graaf)
- xen: document backend sysfs files (David Vrabel)
- xen: document balloon driver sysfs files (David Vrabel)
- btrfs: add support for read_iter, write_iter, and direct_IO_bvec (Dave
  Kleikamp)
- ext4: add support for read_iter, write_iter, and direct_IO_bvec (Dave
  Kleikamp)
- ocfs2: add support for read_iter, write_iter, and direct_IO_bvec (Dave
  Kleikamp)
- ext3: add support for .read_iter and .write_iter (Dave Kleikamp)
- bio: add bvec_length(), like iov_length() (Dave Kleikamp)
- aio: add aio support for iov_iter arguments (Zach Brown)
- aio: add aio_kernel_() interface (Dave Kleikamp)
- fs: pull iov_iter use higher up the stack (Dave Kleikamp)
- dio: add __blockdev_direct_IO_bdev() (Dave Kleikamp)
- dio: add dio_post_submission() helper function (Dave Kleikamp)
- dio: add dio_lock_and_flush() helper (Dave Kleikamp)
- dio: add sdio_init() helper function (Dave Kleikamp)
- dio: add dio_alloc_init() helper function (Dave Kleikamp)
- dio: create a dio_aligned() helper function (Zach Brown)
- iov_iter: let callers extract iovecs and bio_vecs (Zach Brown)
- iov_iter: add a shorten call (Zach Brown)
- iov_iter: add bvec support (Zach Brown)
- iov_iter: hide iovec details behind ops function pointers (Zach Brown)
- fuse: convert fuse to use iov_iter_copy_[to|from]_user (Dave Kleikamp)
- iov_iter: add copy_to_user support (Zach Brown)
- iov_iter: move into its own file (Zach Brown)
- xen/scsi[front|back]: consolidate and simplify struct xenbus_driver
  instantiation (Konrad Rzeszutek Wilk)
- xen/scsiback: allow RESERVE/RELEASE commands (James Harper)
- xen/scsiback: vscsi >2TB patch (Samuel Kvasnica)
- xen-scsi[front|back]: Fix warnings and bugs. (Konrad Rzeszutek Wilk)
- xen/scsi[front|back]: Forgot .owner attribute. (Konrad Rzeszutek Wilk)
- xen/scsi[front|back]: Initial commit from Novell SLES11SP1 2.6.32 tree.
  (Konrad Rzeszutek Wilk)
- xen/pci:use hypercall PHYSDEVOP_restore_msi_ext to restore MSI/MSI-X vectors
  (Liang Tang)
- xen/acpi/sleep: Register to the acpi_suspend_lowlevel a callback. (Konrad
  Rzeszutek Wilk)
- xen/acpi/sleep: Enable ACPI sleep via the __acpi_override_sleep (Konrad
  Rzeszutek Wilk)
- xen/acpi: Domain0 acpi parser related platform hypercall (Yu Ke)
- xen: Utilize the restore_msi_irqs hook. (Konrad Rzeszutek Wilk)
- x86/acpi/sleep: Provide registration for acpi_suspend_lowlevel. (Liang Tang)
- x86, acpi, tboot: Have a ACPI sleep override instead of calling tboot_sleep.
  (Konrad Rzeszutek Wilk)
- x86: Expand the x86_msi_ops to have a restore MSIs. (Konrad Rzeszutek Wilk)

* Tue Mar  6 2012 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.5.1.el5uek]
- proc: make sure mem_open() doesn't pin the target's memory (Oleg Nesterov) 
- proc: mem_release() should check mm != NULL (Oleg Nesterov) [orabug 13811116]
- proc: unify mem_read() and mem_write() (Oleg Nesterov)

* Thu Mar  1 2012 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.4.1.el5uek]
- sysfs: restore upstream sysfs code (Guru Anbalagane)
- rpm: remove symlink on uninstall (Maxim Uvarov) [Orabug: 13791936]
- Btrfs: fix casting error in scrub reada code (Chris Mason)

* Wed Feb 22 2012 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.3.1.el5uek]
- Btrfs: clear the extent uptodate bits during parent transid failures (Chris
  Mason)
- Btrfs: add extra sanity checks on the path names in btrfs_mksubvol (Chris
  Mason)
- Btrfs: make sure we update latest_bdev (Chris Mason)
- Btrfs: improve error handling for btrfs_insert_dir_item callers (Chris Mason)
- fnic: return zero on fnic_reset() success (Joe Jin)
- [SCSI] libfc: improve flogi retries to avoid lport stuck (Vasu Dev)
- [SCSI] libfc: avoid exchanges collision during lport reset (Vasu Dev)
- igbvf: update version number (Williams, Mitch A)

* Tue Feb 14 2012 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.2.1.el5uek]
- config: enable dm-nfs (Guru Anbalagane)
- Update lpfc version for 8.3.5.58.2p driver release (Chuck Anderson)
- Fix build warning with uninitialized variable (Chuck Anderson)
- Fix warning on i386 system (CR 123966) (Chuck Anderson)
- Fix mailbox and vpi memory leaks causing crashes (CR 126818) (Chuck Anderson)
- Fixed unbounded firmware revision string from port caused the system panic
  (CR 126560) (Chuck Anderson)

* Tue Feb  7 2012 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.1.1.el5uek]
- mm: compaction: check pfn_valid when entering a new MAX_ORDER_NR_PAGES block
  during isolation for migration (Mel Gorman)
- block: Disable autoprotect (Martin K. Petersen)
- enic: do vlan cleanup (Jiri Pirko)
- enic: Add support to configure hardware interrupt coalesce timers in a
  platform independent way (Vasanthy Kolluri)
- enic: Partial: Bug Fix: Fix hardware transmit queue indexing in
  enic_poll_controller (Vasanthy Kolluri)
- enic: Get/Set interrupt resource index for transmit and receive queues
  (Vasanthy Kolluri)
- enic: Log device configuration in detail during driver load (Vasanthy
  Kolluri)
- enic: Pass 802.1p bits for packets tagged with vlan zero (Vasanthy Kolluri)
- enic: update to support 64 bit stats (stephen hemminger)
- enic: Add support for MTU change via port profile on a dynamic vnic (Roopa
  Prabhu)
- drivers/net: Remove unnecessary semicolons (Joe Perches)
- [SCSI] megaraid_sas Version to 5.40-rc1 and Changelog update (Adam Radford)
- [SCSI] megaraid_sas: Add .change_queue_depth support (Adam Radford)
- [SCSI] megaraid_sas: Fix FastPath I/O to work on degraded raid 1 (Adam
  Radford)
- bnx2x: add missing break in bnx2x_dcbnl_get_cap (Shmulik Ravid)
- bnx2x: fix hw attention handling (Dmitry Kravkov)
- bnx2x: prevent flooded warnning kernel info (Joe Jin)
- Btrfs: don't reserve data with extents locked in btrfs_fallocate (Chris
  Mason)
- watchdog: hpwdt: prevent multiple "NMI occurred" messages (Naga Chumbalkar)
- watchdog: hpwdt: add next gen HP servers (Thomas Mingarelli)
- bnx2fc: Update copyright and bump version to 1.0.4 (Bhanu Prakash Gollapudi)
- bnx2fc: Tx BDs cache in write tasks (Bhanu Prakash Gollapudi)
- bnx2fc: Do not arm CQ when there are no CQEs (Bhanu Prakash Gollapudi)
- bnx2fc: hold tgt lock when calling cmd_release (Bhanu Prakash Gollapudi)
- bnx2fc: Enable support for sequence level error recovery (Bhanu Prakash
  Gollapudi)
- bnx2fc: HSI changes for tape (Bhanu Prakash Gollapudi)
- bnx2fc: Handle REC_TOV error code from firmware (Bhanu Prakash Gollapudi)
- bnx2fc: REC/SRR link service request and response handling (Bhanu Prakash
  Gollapudi)
- bnx2fc: Support 'sequence cleanup' task (Bhanu Prakash Gollapudi)
- bnx2fc: Enable REC & CONF support for the session (Bhanu Prakash Gollapudi)
- bnx2fc: Introduce interface structure for each vlan interface (Bhanu Prakash
  Gollapudi)
- bnx2fc: Replace printks with KERN_ALERT to KERN_ERR/KERN_INFO (Bhanu Prakash
  Gollapudi)
- cnic: Add VLAN ID as a parameter during netevent upcall (Michael Chan)
- bnx2i: Updated copyright and bump version (Eddie Wai)
- bnx2i: Modified to skip CNIC registration if iSCSI is not supported (Eddie
  Wai)
- bnx2i: Added the use of kthreads to handle SCSI cmd completion (Eddie Wai)
- iscsi: Use struct scsi_lun in iscsi structs instead of u8[8] (Andy Grover)
- cnic: Wait for all Context IDs to be deleted before sending FCOE_DESTROY_FUNC
  (Michael Chan)
- cnic: Fix Context ID space calculation (Michael Chan)
- bnx2x: Implementation for netdev->ndo_fcoe_get_wwn (Vladislav Zolotarov)
- bnx2: Fix endian swapping on firmware version string (Michael Chan)
- bnx2: Close device if tx_timeout reset fails (Michael Chan)
- bnx2: Read iSCSI config from shared memory during ->probe() (Michael Chan)
- bnx2: Add MCP dump (Jeffrey Huang)
- bnx2: remove unnecessary read of PCI_CAP_ID_EXP (Jon Mason)
- cnic: Return proper error code if we fail to send netlink message (Michael
  Chan)
- cnic: Fix ring setup/shutdown code (Michael Chan)
- cnic: Fix port_mode setting (Michael Chan)
- cnic: Replace get_random_bytes() with random32() (Michael Chan)
- cnic, bnx2i: Add support for new devices - 57800, 57810, and 57840 (Michael
  Chan)
- drivers/net: Remove casts of void * (Joe Perches)
- bnx2fc: Fix kernel panic when deleting NPIV ports (Bhanu Prakash Gollapudi)
- bnx2fc: scsi_dma_unmap() not invoked on IO completions (Bhanu Prakash
  Gollapudi)
- bnx2fc: host stats show the link speed 'unknown' on NIC partitioned
  interfaces (Bhanu Prakash Gollapudi)
- bnx2x: Update date to 2011/06/13 and version to 1.70.00-0 (Vladislav
  Zolotarov)
- bnx2x: PFC support for 578xx (Dmitry Kravkov)
- bnx2x: Rename LASI registers to definitions in mdio.h (Yaniv Rosner)
- bnx2x: Add a periodic task for link PHY events (Yaniv Rosner)
- bnx2x: Adjust BCM84833 to BCM578xx (Yaniv Rosner)
- bnx2x: Adjust ETS to 578xx (Yaniv Rosner)
- bnx2x: Add new PHY 54616s (Yaniv Rosner)
- bnx2x: Add Warpcore support for 578xx (Yaniv Rosner)
- bnx2x: Add new MAC support for 578xx (Yaniv Rosner)
- bnx2x: Cosmetic changes. (Dmitry Kravkov)
- bnx2x: update DCB data during PMF migration (Dmitry Kravkov)
- bnx2x: 57712 parity handling (Vladislav Zolotarov)
- New 7.0 FW: bnx2x, cnic, bnx2i, bnx2fc (Vlad Zolotarov)
- cnic: Move indexing function pointers to struct kcq_info (Michael Chan)
- linux-firmware: Add a new FW 7.0.20.0 (Vladislav Zolotarov)
- ixgbe: Fix FCOE memory leak for DDP packets (Alexander Duyck)
- ixgbe: fix PHY link setup for 82599 (Emil Tantilov)
- ixgbe: fix __ixgbe_notify_dca() bail out code (Don Skidmore)
- ixgbe: convert to ndo_fix_features (Don Skidmore)
- ixgbe: only enable WoL for magic packet by default (Andy Gospodarek)
- ixgbe: remove ifdef check for non-existent define (Emil Tantilov)
- ixgbe: Pass staterr instead of re-reading status and error bits from
  descriptor (Alexander Duyck)
- ixgbe: Move interrupt related values out of ring and into q_vector (Alexander
  Duyck)
- ixgbe: add structure for containing RX/TX rings to q_vector (Alexander Duyck)
- ixgbe: inline the ixgbe_maybe_stop_tx function (Alexander Duyck)
- ixgbe: Update ATR to use recorded TX queues instead of CPU for routing
  (Alexander Duyck)
- ixgbe: Make certain to initialize the fdir_perfect_lock in all cases
  (Alexander Duyck)
- e1000: always call e1000_check_for_link() on e1000_ce4100 MACs. (Nicolas
  Schichan)
- e1000: do vlan cleanup (Jiri Pirko)
- e1000: convert to ndo_fix_features (Michał Mirosław)
- e1000: remove unnecessary code (Greg Dietsche)
- igbvf: do vlan cleanup (Jiri Pirko)
- ixgbe: A fix to VF TX rate limit (Lior Levy)
- ixgbe: Update method used for determining descriptor count for an skb
  (Alexander Duyck)
- ixgbe: Add one function that handles most of context descriptor setup
  (Alexander Duyck)
- ixgbe: Move all values that deal with count, next_to_use, next_to_clean to
  u16 (Alexander Duyck)
- ixgbe: Convert IXGBE_DESC_UNUSED from macro to static inline function
  (Alexander Duyck)
- ixgbe: pass adapter struct instead of netdev for interrupt data (Alexander
  Duyck)
- ixgbe: update driver version string (Don Skidmore)
- ixgbe: fix ring assignment issues for SR-IOV and drop cases (Alexander Duyck)
- ixgbe: disable RSC when Rx checksum is off (Emil Tantilov)
- ixgbe: move reset code into a separate function (Emil Tantilov)
- ixgbe: move setting RSC into a separate function (Emil Tantilov)
- ixgbe: add support for nfc addition and removal of filters (Alexander Duyck)
- ixgbe: add support for displaying ntuple filters via the nfc interface
  (Alexander Duyck)
- ixgbe: add basic support for setting and getting nfc controls (Alexander
  Duyck)
- ixgbe: update perfect filter framework to support retaining filters
  (Alexander Duyck)
- ixgbe: fix flags relating to perfect filters to support coexistence
  (Alexander Duyck)
- ixgbe: remove ntuple filtering (Alexander Duyck)
- ixgbe: setup per CPU PCI pool for FCoE DDP (Vasu Dev)
- ixgbe: add support for Dell CEM (Emil Tantilov)
- bnx2x: Created bnx2x_sp (Vladislav Zolotarov)
- bnx2x: removed unused variables (Dmitry Kravkov)
- bnx2x: use bnx2x_reload_if_running (Dmitry Kravkov)
- bnx2x: dump FW memory when appropriate msglvl is raised (Dmitry Kravkov)
- bnx2x: do not call link update without HW notification (Yaniv Rosner)
- bnx2x: disable fairness if ETS is enabled (Dmitry Kravkov)
- bnx2x: avoid release of unrequested irqs (Dmitry Kravkov)
- bnx2x: put start bd csum in separate function (Dmitry Kravkov)
- bnx2x: remove references to intr_sem (Dmitry Kravkov)
- bnx2x: do not allocate FCoE ring if disabled (Dmitry Kravkov)
- bnx2x: Improve cl45 access methods (Yaniv Rosner)
- bnx2x: Modify XGXS functions (Yaniv Rosner)
- bnx2x: Fix link status sync (Yaniv Rosner)
- bnx2x: Adjust BCM8726 module detection settings (Yaniv Rosner)
- bnx2x: Fix grammar and relocate code (Yaniv Rosner)
- bnx2x: Fix BCM84833 settings (Yaniv Rosner)
- bnx2x: Fix over current port display (Yaniv Rosner)
- bnx2x: Add TX fault check for fiber PHYs (Yaniv Rosner)
- bnx2x: Change return status type (Yaniv Rosner)
- bnx2x: Fix port type display (Yaniv Rosner)
- bnx2x: Add new phy BCM8722 (Yaniv Rosner)
- Revert "bnx2fc: Fix kernel panic when deleting NPIV ports" (Bob Picco)
- Revert "bnx2fc: scsi_dma_unmap() not invoked on IO completions" (Bob Picco)
- Revert "bnx2x: prevent flooded warnning kernel info" (Bob Picco)
- Revert "bnx2x: fix hw attention handling" (Bob Picco)
- Revert "bnx2x: add missing break in bnx2x_dcbnl_get_cap" (Bob Picco)
- ixgbe: DCB and perfect filters can coexist (John Fastabend)
- ixgbe: fix bit mask for DCB version (John Fastabend)
- ixgbe: setup redirection table for multiple packet buffers (John Fastabend)
- ixgbe: DCB 82598 devices, tx_idx and rx_idx swapped (John Fastabend)
- ixgbe: DCB use existing TX and RX queues (John Fastabend)
- ixgbe: configure minimal packet buffers to support TC (John Fastabend)
- ixgbe: consolidate MRQC and MTQC handling (John Fastabend)
- ixgbe: consolidate packet buffer allocation (John Fastabend)
- ixgbe: dcbnl reduce duplicated code and indentation (John Fastabend)
- ixgbevf: do vlan cleanup (Jiri Pirko)
- ixgbevf: remove unnecessary ampersands (Stephen Hemminger)
- ixgbevf: Fix bungled declaration of ixgbevf_mbx_ops (Greg Rose)
- ixgbevf: Update the driver string (Greg Rose)
- e1000e: workaround invalid Tx/Rx tail descriptor register write (Bruce Allan)
- e1000e: Spurious interrupts & dropped packets with 82577/8/9 in half-duplex
  (Bruce Allan)
- e1000e: increase driver version number (Bruce Allan)
- e1000e: alternate MAC address update (Bruce Allan)
- e1000e: do not disable receiver on 82574/82583 (Bruce Allan)
- e1000e: minor re-order of #include files (Bruce Allan)
- e1000e: remove unnecessary check for NULL pointer (Bruce Allan)
- intel drivers: repair missing flush operations (Jesse Brandeburg)
- e1000e: use GFP_KERNEL allocations at init time (Jeff Kirsher)
- e1000e: Add Jumbo Frame support to 82583 devices (Carolyn Wyborny)
- e1000e: remove e1000_queue_stats (Eric Dumazet)
- net: e1000e: Use is_multicast_ether_addr helper (Tobias Klauser)
- e1000e: remove unnecessary reads of PCI_CAP_ID_EXP (Jon Mason)
- e1000e: update driver version (Bruce Allan)
- e1000e: Clear host wakeup bit on 82577/8 without touching PHY page 800 (Bruce
  Allan)
- e1000e: access multiple PHY registers on same page at the same time (Bruce
  Allan)
- e1000e: do not schedule the Tx queue until ready (Bruce Allan)
- e1000e: log when swflag is cleared unexpectedly on ICH/PCH devices (Bruce
  Allan)
- e1000e: 82579 intermittently disabled during S0->Sx (Bruce Allan)
- e1000e: disable far-end loopback mode on ESB2 (Bruce Allan)
- net: introduce __netdev_alloc_skb_ip_align (Eric Dumazet)
- update modsign (Maxim Uvarov) [Orabug: 13615815]

* Tue Jan 31 2012 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.22.el5uek]
- xfs: fix acl count validation in xfs_acl_from_disk() (Dan Carpenter)
  CVE-2012-0038
- Updated driver version to 5.02.00.00.06.02-uek2 (Tej Parkash)
- ocfs2: use spinlock irqsave for downconvert lock.patch (Srinivas Eeda)
- dm-nfs-for-uek2 (Adnan Misherfi)

* Thu Jan 26 2012 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.21.el5uek]
- git-changelog: add Orabug and CVE (Maxim Uvarov) [Add parsing Orabug and
  CVE.]
- qla2xxx: Update the driver version to 8.03.07.12.39.0-k. (Giridhar Malavali)
- Add support for pv hugepages and support for huge balloon pages. (Dave
  McCracken)
- Btrfs: remove some verbose warnings (Chris Mason)
- Btrfs: fix reservations in btrfs_page_mkwrite (Chris Mason)
- Btrfs: use larger system chunks (Chris Mason)
- Btrfs: add a delalloc mutex to inodes for delalloc reservations (Josef Bacik)
- Btrfs: protect orphan block rsv with spin_lock (Josef Bacik)
- Btrfs: don't call btrfs_throttle in file write (Josef Bacik)
- Btrfs: release space on error in page_mkwrite (Josef Bacik)
- Btrfs: fix btrfsck error 400 when truncating a compressed (Miao Xie)
- Btrfs: do not use btrfs_end_transaction_throttle everywhere (Josef Bacik)
- Btrfs: fix possible deadlock when opening a seed device (Li Zefan)
- Btrfs: update global block_rsv when creating a new block group (Li Zefan)
- Btrfs: rewrite btrfs_trim_block_group() (Li Zefan)
- Btrfs: simplfy calculation of stripe length for discard operation (Li Zefan)
- Btrfs: don't pre-allocate btrfs bio (Li Zefan)
- Btrfs: don't pass a trans handle unnecessarily in volumes.c (Li Zefan)
- Btrfs: reserve metadata space in btrfs_ioctl_setflags() (Li Zefan)
- Btrfs: remove BUG_ON()s in btrfs_ioctl_setflags() (Li Zefan)
- Btrfs: check the return value of io_ctl_init() (Li Zefan)
- Btrfs: avoid possible NULL deref in io_ctl_drop_pages() (Li Zefan)
- Btrfs: add pinned extents to on-disk free space cache correctly (Li Zefan)
- Btrfs: revamp clustered allocation logic (Alexandre Oliva)
- Btrfs: don't set up allocation result twice (Alexandre Oliva)
- Btrfs: test free space only for unclustered allocation (Alexandre Oliva)
- Btrfs: use bigger metadata chunks on bigger filesystems (Chris Mason)
- Btrfs: lower the bar for chunk allocation (Chris Mason)
- Btrfs: run chunk allocations while we do delayed refs (Chris Mason)
- Btrfs: call d_instantiate after all ops are setup (Al Viro)
- Btrfs: fix worker lock misuse in find_worker (Chris Mason)
- xen/config: turn CONFIG_XEN_DEBUG_FS off. (Konrad Rzeszutek Wilk)
- proc: clean up and fix /proc/<pid>/mem handling (Maxim Uvarov) [Orabug:
  13618927] CVE-2012-0056
- set XEN_MAX_DOMAIN_MEMORY for 512 (Maxim Uvarov)
- add __init arguments to init functions (Maxim Uvarov)
- hpwdt: clean up set_memory_x call for 32 bit (Maxim Uvarov)

* Tue Jan 12 2012 Maxim Uvarov <maxim.uvarov@oracle.com> [2.6.39-100.0.20.el5uek]
- Enable Kabi Check (Guru Anbalagane)
- net/bna driver update from 2.3.2.3 to 3.0.2.2 (Maxim Uvarov)
- scsi/bfa driver update from 2.3.2.3 to 3.0.2.2 (Maxim Uvarov)
- Updated version to 5.02.00.00.06.02-uek1 (Tej Parkash)
- qla4xxx: Added error logging for firmware abort (Nilesh Javali)
- qla4xxx: Disable generating pause frames in case of FW hung (Giridhar
  Malavali)
- qla4xxx: Temperature monitoring for ISP82XX core. (Mike Hernandez)
- qla4xxx: check for FW alive before calling chip_reset (Shyam Sunder)
- qla4xxx: Remove the unused macros (Tej Parkash)
- qla4xxx: cleanup, make qla4xxx_build_ddb_list short (Lalit Chandivade)
- qla4xxx: clear the RISC interrupt bit during firmware init (Sarang Radke)
- qla4xxx: clear the SCSI COMPLETION INTERRUPT bit during firmware init
  (Prasanna Mumbai)
- qla4xxx: Fixed BFS with sendtargets as boot index. (Manish Rangankar)
- qla4xxx: Correct the default relogin timeout value (Nilesh Javali)
- qla4xxx: Limit the ACB Default Timeout value to 12s (Nilesh Javali)
- bond_alb: don't disable softirq under bond_alb_xmit (Maxim Uvarov)
- fix kernel version (Guru Anbalagane)

* Tue Jan 10 2012 Maxim Uvarov <maxim.uvarov@oracle.com> [2.6.39-100.0.19.el5uek]
- scripts/git-changelog: generate rpm changelog script (Maxim Uvarov)
- Revert "hpwd watchdog mark page executable" (Maxim Uvarov)
- Partial revert of mainline removal of deprecated sysfs interface for 13568528
  (Chuck Anderson)
- scsi:lpfc update to 8.3.5.58 (Maxim Uvarov)
- Let KERNEL_VERSION be 3.0.x, and override UTSNAME (Nelson Elhage)
- qla4xxx: Fix qla4xxx_dump_buffer to dump buffer correctly (Vikas Chaudhary)
- qla4xxx: Fix the IDC locking mechanism (Nilesh Javali)
- qla4xxx: Wait for disable_acb before doing set_acb (Vikas Chaudhary)
- qla4xxx: Don't recover adapter if device state is FAILED (Sarang Radke)
- qla4xxx: fix call trace on rmmod with ql4xdontresethba=1 (Sarang Radke)
- qla4xxx: Fix CPU lockups when ql4xdontresethba set (Mike Hernandez)
- qla4xxx: Perform context resets in case of context failures. (Vikas
  Chaudhary)
- do not obsolete firmware (Maxim Uvarov)
- Revert "xen/pv-on-hvm kexec: add xs_reset_watches to shutdown watches from
  old kernel" (Konrad Rzeszutek Wilk)
- Revert "xen-blkback: convert hole punching to discard request on loop
  devices" (Maxim Uvarov)
- ath9k: Fix kernel panic in AR2427 in AP mode (Mohammed Shafi Shajakhan)
- ptrace: partially fix the do_wait(WEXITED) vs EXIT_DEAD->EXIT_ZOMBIE race
  (Oleg Nesterov)
- Revert "rtc: Disable the alarm in the hardware" (Linus Torvalds)
- hung_task: fix false positive during vfork (Mandeep Singh Baines)
- drm/radeon/kms/atom: fix possible segfault in pm setup (Alexander Müller)
- xfs: log all dirty inodes in xfs_fs_sync_fs (Christoph Hellwig)
- xfs: log the inode in ->write_inode calls for kupdate (Christoph Hellwig)
- mfd: Turn on the twl4030-madc MADC clock (Kyle Manna)
- mfd: Check for twl4030-madc NULL pointer (Kyle Manna)
- mfd: Copy the device pointer to the twl4030-madc structure (Kyle Manna)
- mfd: Fix mismatch in twl4030 mutex lock-unlock (Sanjeev Premi)
- iwlwifi: update SCD BC table for all SCD queues (Emmanuel Grumbach)
- ipv4: using prefetch requires including prefetch.h (Stephen Rothwell)
- ipv4: reintroduce route cache garbage collector (Eric Dumazet)
- ipv4: flush route cache after change accept_local (Weiping Pan)
- sctp: Do not account for sizeof(struct sk_buff) in estimated rwnd (Thomas
  Graf)
- sctp: fix incorrect overflow check on autoclose (Xi Wang)
- sch_gred: should not use GFP_KERNEL while holding a spinlock (Eric Dumazet)
- net: have ipconfig not wait if no dev is available (Gerlando Falauto)
- mqprio: Avoid panic if no options are provided (Thomas Graf)
- llc: llc_cmsg_rcv was getting called after sk_eat_skb. (Alex Juncu)
- ppp: fix pptp double release_sock in pptp_bind() (Djalal Harouni)
- net: bpf_jit: fix an off-one bug in x86_64 cond jump target (Markus Kötter)
- sparc: Fix handling of orig_i0 wrt. debugging when restarting syscalls.
  (David S. Miller)
- sparc64: Fix masking and shifting in VIS fpcmp emulation. (David S. Miller)
- sparc32: Correct the return value of memcpy. (David S. Miller)
- sparc32: Remove uses of %g7 in memcpy implementation. (David S. Miller)
- sparc32: Remove non-kernel code from memcpy implementation. (David S. Miller)
- sparc: Kill custom io_remap_pfn_range(). (David S. Miller)
- sparc64: Patch sun4v code sequences properly on module load. (David S.
  Miller)
- sparc32: Be less strict in matching %lo part of relocation. (David S. Miller)
- sparc64: Fix MSIQ HV call ordering in pci_sun4v_msiq_build_irq(). (David S.
  Miller)
- mm: hugetlb: fix non-atomic enqueue of huge page (Hillf Danton)
- drm/radeon/kms: bail on BTC parts if MC ucode is missing (Alex Deucher)
- watchdog: hpwdt: Changes to handle NX secure bit in 32bit path (Mingarelli,
  Thomas)
- futex: Fix uninterruptible loop due to gate_area (Hugh Dickins)
- oprofile, arm/sh: Fix oprofile_arch_exit() linkage issue (Vladimir Zapolskiy)
- ARM: 7220/1: mmc: mmci: Fixup error handling for dma (Ulf Hansson)
- ARM: 7214/1: mmc: mmci: Fixup handling of MCI_STARTBITERR (Ulf Hansson)
- ARM:imx:fix pwm period value (Jason Chen)
- VFS: Fix race between CPU hotplug and lglocks (Srivatsa S. Bhat)
- memcg: keep root group unchanged if creation fails (Hillf Danton)
- iwlwifi: allow to switch to HT40 if not associated (Wey-Yi Guy)
- iwlwifi: do not set the sequence control bit is not needed (Wey-Yi Guy)
- ath9k: fix max phy rate at rate control init (Rajkumar Manoharan)
- media: s5p-fimc: Use correct fourcc for RGB565 colour format (Sylwester
  Nawrocki)
- vfs: __read_cache_page should use gfp argument rather than GFP_KERNEL (Dave
  Kleikamp)
- mfd: Fix twl-core oops while calling twl_i2c_* for unbound driver (Ilya
  Yanok)
- cgroups: fix a css_set not found bug in cgroup_attach_proc (Mandeep Singh
  Baines)
- mmc: vub300: fix type of firmware_rom_wait_states module parameter (Rusty
  Russell)
- nilfs2: unbreak compat ioctl (Thomas Meyer)
- SELinux: Fix RCU deref check warning in sel_netport_insert() (David Howells)
- NFSv4.1: Ensure that we handle _all_ SEQUENCE status bits. (Trond Myklebust)
- oprofile: Fix uninitialized memory access when writing to writing to
  oprofilefs (Robert Richter)
- oom: fix integer overflow of points in oom_badness (Frantisek Hrbata)
- binary_sysctl(): fix memory leak (Michel Lespinasse)
- percpu: fix per_cpu_ptr_to_phys() handling of non-page-aligned addresses
  (Eugene Surovegin)
- Input: synaptics - fix touchpad not working after S2R on Vostro V13 (Dmitry
  Torokhov)
- MXC PWM: should active during DOZE/WAIT/DBG mode (Jason Chen)
- ssb: fix init regression with SoCs (Hauke Mehrtens)
- block: initialize request_queue's numa node during (Mike Snitzer)
- mac80211: fix another race in aggregation start (Johannes Berg)
- SCSI: fcoe: Fix preempt count leak in fcoe_filter_frames() (Thomas Gleixner)
- SCSI: zfcp: return early from slave_destroy if slave_alloc returned early
  (Steffen Maier)
- cfq-iosched: fix cfq_cic_link() race confition (Yasuaki Ishimatsu)
- cfq-iosched: free cic_index if blkio_alloc_blkg_stats fails (majianpeng)
- drm/i915: prevent division by zero when asking for chipset power (Eugeni
  Dodonov)
- rtc: m41t80: Workaround broken alarm functionality (John Stultz)
- ipip, sit: copy parms.name after register_netdevice (Ted Feng)
- ARM: OMAP: rx51: fix USB (Felipe Contreras)
- Revert "clockevents: Set noop handler in clockevents_exchange_device()"
  (Linus Torvalds)
- ASoC: core: Don't schedule deferred_resume_work twice (Stephen Warren)
- USB: option: Removing one bogus and adding some new Huawei combinations
  (Bjørn Mork)
- usb: option: Add Huawei E398 controlling interfaces (Alex Hermann)
- USB: cdc-acm: add IDs for Motorola H24 HSPA USB module. (Krzysztof Hałasa)
- ibft: Fix finding IBFT ACPI table on UEFI (Yinghai Lu)
- drm/radeon/kms: add some new pci ids (Alex Deucher)
- staging: r8712u: Add new USB ID (Larry Finger)
- fuse: fix fuse_retrieve (Miklos Szeredi)
- ext4: handle EOF correctly in ext4_bio_write_page() (Yongqiang Yang)
- ext4: avoid potential hang in mpage_submit_io() when blocksize < pagesize
  (Yongqiang Yang)
- ext4: avoid hangs in ext4_da_should_update_i_disksize() (Andrea Arcangeli)
- ext4: display the correct mount option in /proc/mounts for [no]init_itable
  (Theodore Ts'o)
- md/raid5: fix bug that could result in reads from a failed device.
  (NeilBrown)
- xfs: avoid synchronous transactions when deleting attr blocks (Christoph
  Hellwig)
- xfs: fix nfs export of 64-bit inodes numbers on 32-bit kernels (Christoph
  Hellwig)
- hwmon: (coretemp) Fix oops on CPU offlining (Jean Delvare)
- hfs: fix hfs_find_init() sb->ext_tree NULL ptr oops (Phillip Lougher)
- Make TASKSTATS require root access (Linus Torvalds)
- jbd/jbd2: validate sb->s_first in journal_get_superblock() (Eryu Guan)
- x86, hpet: Immediately disable HPET timer 1 if rtc irq is masked (Mark
  Langsdorf)
- mmc: mxcmmc: fix falling back to PIO (Sascha Hauer)
- hwmon: (jz4740) fix signedness bug (Axel Lin)
- linux/log2.h: Fix rounddown_pow_of_two(1) (Linus Torvalds)
- mac80211: fix race condition caused by late addBA response (Nikolay Martynov)
- iwlwifi: do not re-configure HT40 after associated (Wey-Yi Guy)
- percpu: fix chunk range calculation (Tejun Heo)
- intel-iommu: fix superpage support in pfn_to_dma_pte() (Allen Kay)
- intel-iommu: set iommu_superpage on VM domains to lowest common denominator
  (Allen Kay)
- intel-iommu: fix return value of iommu_unmap() API (Allen Kay)
- target: Handle 0 correctly in transport_get_sectors_6() (Roland Dreier)
- fix apparmor dereferencing potentially freed dentry, sanitize __d_path() API
  (Al Viro)
- mm: vmalloc: check for page allocation failure before vmlist insertion (Mel
  Gorman)
- mm: Ensure that pfn_valid() is called once per pageblock when reserving
  pageblocks (Michal Hocko)
- ptp: Fix clock_getres() implementation (Thomas Gleixner)
- thp: set compound tail page _count to zero (Youquan Song)
- thp: add compound tail page _mapcount when mapped (Youquan Song)
- fs/proc/meminfo.c: fix compilation error (Claudio Scordino)
- ASoC: Provide a more complete DMA driver stub (Mark Brown)
- ARM: davinci: dm646x evm: wrong register used in
  setup_vpif_input_channel_mode (Hans Verkuil)
- ARM: at91: fix clock conid for atmel_tcb.1 on 9260/9g20 (Jean-Christophe
  PLAGNIOL-VILLARD)
- arm: mx23: recognise stmp378x as mx23 (Wolfram Sang)
- ARM: davinci: da850 evm: change audio edma event queue to EVENTQ_0
  (Manjunathappa, Prakash)
- alarmtimers: Fix time comparison (Thomas Gleixner)
- ALSA: hda/realtek - Fix Oops in alc_mux_select() (Takashi Iwai)
- ALSA: sis7019 - give slow codecs more time to reset (David Dillow)
- netconsole support for netfront (Zhenzhong Duan)
- oracleasm: Fix two merge errors (Martin K. Petersen)
- x86/numa: Add constraints check for nid parameters (Petr Holasek)

* Sun Dec 18 2011 Guru Anbalagane<guru.anbalagane@oracle.com> [2.6.39-100.0.18.el5uek]
- xen/acpi: Domain0 acpi parser related platform hypercall (Yu Ke)
- xen: add dom0_op hypercall (Jeremy Fitzhardinge)
- xen: add CPU microcode update driver (Jeremy Fitzhardinge)
- xen/v86d: Fix /dev/mem to access memory below 1MB (Konrad Rzeszutek Wilk)
- x86/microcode: check proper return code. (Ben Guthro)
- Oracle ASM Kernel Driver (Martin K. Petersen)
- modsign: no sign if keys are missing (Maxim Uvarov)
- Set panic_on_oops to default to true (Maxim Uvarov)
  (Andi Kleen)
- direct-io: separate fields only used in the submission path from struct dio
- direct-io: fix a wrong comment (Andi Kleen)
- direct-io: rearrange fields in dio/dio_submit to avoid holes (Andi Kleen)
- direct-io: use a slab cache for struct dio (Andi Kleen)
- direct-io: separate map_bh from dio (Andi Kleen)
- direct-io: inline the complete submission path (Andi Kleen)
- direct-io: merge direct_io_walker into __blockdev_direct_IO (Andi Kleen)
- Install include/drm headers (Maxim Uvarov)
- VFS: Cache request_queue in struct block_device (Andi Kleen)
- DIO: optimize cache misses in the submission path (Andi Kleen)
- put firmware to kernel version specific location (Maxim Uvarov)
- hpwd watchdog mark page executable (Maxim Uvarov)
- SPEC: el5 mkinird more then 5.1.19.6-71.0.10 (Maxim Uvarov)
- SPEC: req udev-095-14.27.0.1.el5_7.1 or more (Maxim Uvarov)
- SPEC: ol6 req dracut-kernel-004-242.0.3 (Maxim Uvarov)
  S. Miller)
- sparc64: Force the execute bit in OpenFirmware's translation entries. (David
- sched/rt: Migrate equal priority tasks to available CPUs (Shawn Bohrer)
- sched: Fix up wchan borkage (Simon Kirby)
- ide-disk: Fix request requeuing (Borislav Petkov)
- posix-cpu-timers: Cure SMP wobbles (Peter Zijlstra)
- lis3: fix regression of HP DriveGuard with 8bit chip (Takashi Iwai)
- ASoC: use a valid device for dev_err() in Zylonite (Arnd Bergmann)
- ASoC: Fix setting update bits for WM8753_LADC and WM8753_RADC (Axel Lin)
  calculation. (Michel Dänzer)
- drm/radeon: Update AVIVO cursor coordinate origin before x/yorigin
- drm/radeon/kms: Fix logic error in DP HPD handler (Alex Deucher)
- drm/radeon/kms: fix regression in DP aux defer handling (Alex Deucher)
- drm/radeon/kms: add retry limits for native DP aux defer (Alex Deucher)
- drm/radeon/kms: fix channel_remap setup (v2) (Alex Deucher)
- ptp: fix L2 event message recognition (Richard Cochran)
- rt2x00: Serialize TX operations on a queue. (Gertjan van Wingerde)
- x86/PCI: use host bridge _CRS info on ASUS M2V-MX SE (Paul Menzel)
- qla2xxx: Fix crash in qla2x00_abort_all_cmds() on unload (Roland Dreier)
- libsas: fix panic when single phy is disabled on a wide port (Mark Salyzyn)
- md: Avoid waking up a thread after it has been freed. (NeilBrown)
- dm table: avoid crash if integrity profile changes (Mike Snitzer)
- mmc: mxs-mmc: fix clock rate setting (Koen Beel)
- ARM: mach-ux500: enable fix for ARM errata 754322 (srinidhi kasagar)
  Deucher)
- drm/radeon/kms: retry aux transactions if there are status flags (Alex
  (Alex Deucher)
- drm/radeon/kms: use hardcoded dig encoder to transmitter mapping for DCE4.1
- ipv6: fix NULL dereference in udp6_ufo_fragment() (Jason Wang)
- ahci: Enable SB600 64bit DMA on Asus M3A (Mark Nelson)
  Wysocki)
- MIPS: PM: Use struct syscore_ops instead of sysdevs for PM (v2) (Rafael J.
- ftrace: Fix regression of :mod:module function enabling (Steven Rostedt)
  Rostedt)
- ftrace: Fix regression where ftrace breaks when modules are loaded (Steven
  Rostedt)
- ftrace: Fix warning when CONFIG_FUNCTION_TRACER is not defined (Steven
- e1000e: workaround for packet drop on 82579 at 100Mbps (Bruce Allan)
- ARM: 7113/1: mm: Align bank start to MAX_ORDER_NR_PAGES (Linus Walleij)
- ARM: 7117/1: perf: fix HW_CACHE_* events on Cortex-A9 (Will Deacon)
- HID: usbhid: Add support for SiGma Micro chip (Jeremiah Matthey)
- hwmon: (w83627ehf) Properly report thermal diode sensors (Jean Delvare)
- Avoid using variable-length arrays in kernel/sys.c (Linus Torvalds)
- drm/radeon/kms/atom: fix handling of FB scratch indices (Alex Deucher)
- cputimer: Cure lock inversion (Peter Zijlstra)
- fuse: fix memory leak (Miklos Szeredi)
  (Thomas Courbon)
- Platform: fix samsung-laptop DMI identification for N150/N210/220/N230
  Kosina)
- HID: magicmouse: ignore 'ivalid report id' while switching modes, v2 (Jiri
- uvcvideo: Fix crash when linking entities (Laurent Pinchart)
  Forshee)
- hfsplus: ensure bio requests are not smaller than the hardware sectors (Seth
  Skeggs)
- drm/ttm: ensure ttm for new node is bound before calling move_notify() (Ben
- drm/ttm: unbind ttm before destroying node in accel move cleanup (Ben Skeggs)
- CIFS: Fix ERR_PTR dereference in cifs_get_root (Pavel Shilovsky)
- xfs: start periodic workers later (Christoph Hellwig)
- xfs: use a cursor for bulk AIL insertion (Dave Chinner)
- xfs: do not update xa_last_pushed_lsn for locked items (Christoph Hellwig)
  Hellwig)
- xfs: force the log if we encounter pinned buffers in .iop_pushbuf (Christoph
- xfs: revert to using a kthread for AIL pushing (Christoph Hellwig)
- firewire: sbp2: fix panic after rmmod with slow targets (Chris Boot)
- x86: Fix S4 regression (Takashi Iwai)
- hwmon: (w83627ehf) Fix negative 8-bit temperature values (Jean Delvare)
- mm: fix race between mremap and removing migration entry (Hugh Dickins)
- x25: Prevent skb overreads when checking call user data (Matthew Daley)
- crypto: ghash - Avoid null pointer dereference if no key is set (Nick Bowler)
- ALSA: HDA: conexant support for Lenovo T520/W520 (Daniel Suchy)
- ALSA: hda - Add position_fix quirk for Dell Inspiron 1010 (Takashi Iwai)
  Forshee)
- hfsplus: Fix kfree of wrong pointers in hfsplus_fill_super() error path (Seth
- CIFS: Fix incorrect max RFC1002 write size value (Pavel Shilovsky)
- CIFS: Fix DFS handling in cifs_get_file_info (Pavel Shilovsky)
- cris: fix a build error in drivers/tty/serial/crisv10.c (WANG Cong)
- TTY: drop driver reference in tty_open fail path (Jiri Slaby)
- TTY: make tty_add_file non-failing (Jiri Slaby)
- TTY: pty, release tty in all ptmx_open fail paths (Jiri Slaby)
  Wylder)
- USB: for usb_autopm_get_interface_async -EINPROGRESS is not an error (Jim
- staging: serqt_usb2: remove ssu100 from supported devices (Bill Pemberton)
  Consul)
- staging: quatech_usb2: Potential lost wakeup scenario in TIOCMIWAIT (Kautuk
- Staging: hv: Add support for >2 TB LUN in storage driver. (Mike Sterling)
  Hartmann)
- USB: qcserial: Add support for Sierra Wireless MC8355/Gobi 3000 (Richard
  Hamisch)
- USB: qcserial: add device ID for "HP un2430 Mobile Broadband Module" (Rigbert
- serial: pxa: work around for errata #20 (Marcus Folkesson)
  (Ning Jiang)
- serial-core: power up uart port early before we do set_termios when resuming
- EHCI : introduce a common ehci_setup (Matthieu CASTET)
- USB: fix ehci alignment error (Harro Haan)
- EHCI: workaround for MosChip controller bug (Alan Stern)
- xhci-mem.c: Check for ring->first_seg != NULL (Kautuk Consul)
- xHCI: AMD isoc link TRB chain bit quirk (Andiry Xu)
  Packard)
- drm/i915: Wrap DP EDID fetch functions to enable eDP panel power (Keith
  (Takashi Iwai)
- drm/i915/panel: Always record the backlight level again (but cleverly)
  Deucher)
- drm/radeon/kms: bail early in dvi_detect for digital only connectors (Alex
  Deucher)
- drm/radeon/kms: handle !force case in connector detect more gracefully (Alex
- drm/radeon/kms: Fix I2C mask definitions (Jean Delvare)
- mmc: core: Fix hangs related to insert/remove of cards (Ulf Hansson)
- mmc: core: ext_csd.raw_* used in comparison but never set (Andrei Warkentin)
  1180:e823 (Josh Boyer)
- PCI quirk: mmc: Always check for lower base frequency quirk for Ricoh
- megaraid_sas: Fix instance access in megasas_reset_timer (Adam Radford)
- ipr: Always initiate hard reset in kdump kernel (Anton Blanchard)
- libsas: set sas_address and device type of rphy (Jack Wang)
- isci: fix support for large smp requests (Dan Williams)
- isci: fix missed unlock in apc_agent_timeout() (Jeff Skirvin)
- ALSA: hda - Remove bad code for IDT 92HD83 family patch (Charles Chin)
- ALSA: HDA: Add new revision for ALC662 (David Henningsson)
  (Nicholas Bellinger)
- target: Fix REPORT TARGET PORT GROUPS handling with small allocation length
  Steiner)
- x86: uv2: Workaround for UV2 Hub bug (system global address format) (Jack
- x86: Fix compilation bug in kprobes' twobyte_is_boostable (Josh Stone)
- epoll: fix spurious lockdep warnings (Nelson Elhage)
  Ospite)
- leds: save the delay values after a successful call to blink_set() (Antonio
- leds: turn the blink_timer off before starting to blink (Antonio Ospite)
- usbmon vs. tcpdump: fix dropped packet count (Johannes Stezenbach)
- USB: storage: Use normalized sense when emulating autosense (Luben Tuikov)
- USB: Fix runtime wakeup on OHCI (Matthew Garrett)
- USB: g_printer: fix bug in unregistration (Fabian Godehardt)
- usb/core/devio.c: Check for printer class specific request (Matthias Dellweg)
  Hallyn)
- USB: pid_ns: ensure pid is not freed during kill_pid_info_as_uid (Serge
- usb: cdc-acm: Owen SI-30 support (Denis Pershin)
- USB: add RESET_RESUME for webcams shown to be quirky (Oliver Neukum)
- USB: add quirk for Logitech C300 web cam (Jon Levell)
- USB: pl2303: add id for SMART device (Eric Benoit)
- USB: ftdi_sio: add PID for Sony Ericsson Urban (Hakan Kvist)
  Stuge)
- USB: ftdi_sio: Support TI/Luminary Micro Stellaris BD-ICDI Board (Peter
- USB: option: convert interface blacklisting to bitfields (Dan Williams)
  blacklist (Dan Williams)
- USB: option: convert Huawei K3765, K4505, K4605 reservered interface to
- USB: option: add ZTE product 0x0037 to sendsetup blacklist (Dan Williams)
  Williams)
- USB: option: add various ZTE device network interfaces to the blacklist (Dan
- MAINTANERS: update Qualcomm Atheros addresses (Luis R. Rodriguez)
- ath9k_hw: Fix descriptor status of TxOpExceeded (Rajkumar Manoharan)
- ath9k_hw: Fix magnitude/phase coeff correction (Rajkumar Manoharan)
- ath9k_htc: add AVM FRITZ!WLAN 11N v2 support (Luis R. Rodriguez)
- ath9k_hw: Fix number of GPIO pins for AR9287/9300 (Mohammed Shafi Shajakhan)
- ath9k: disable unnecessary PHY error reporting (Felix Fietkau)
- HID: ACRUX - fix enabling force feedback support (Sergei Kolzun)
- QE/FHCI: fixed the CONTROL bug (Jerry Huang)
- Update email address for stable patch submission (Josh Boyer)
- kobj_uevent: Ignore if some listeners cannot handle message (Milan Broz)
- caif: Fix BUG() with network namespaces (David Woodhouse)
  (Stefan Beller)
- platform: samsung_laptop: add dmi information for Samsung R700 laptops
- Platform: samsung_laptop: add support for X520 machines. (Tommaso Massimi)
- Platform: samsung_laptop: samsung backlight for R528/R728 (Smelov Andrey)
  Stubbs)
- platform: samsung_laptop: fix samsung brightness min/max calculations (Jason
- Platform: Fix error path in samsung-laptop init (David Herrmann)
- kmod: prevent kmod_loop_msg overflow in __request_module() (Jiri Kosina)
  syncing" (Trond Myklebust)
- Revert "NFS: Ensure that writeback_single_inode() calls write_inode() when
  Layton)
- nfs: don't redirty inode when ncommit == 0 in nfs_commit_unstable_pages (Jeff
  chipsets(v2) (Ming Lei)
- ata_piix: make DVD Drive recognisable on systems with Intel Sandybridge
  dev_change_net_namespaces (Eric W. Biederman)
- rtnetlink: Add missing manual netlink notification in
- dp83640: free packet queues on remove (Richard Cochran)
- mac80211: fix offchannel TX cookie matching (Johannes Berg)
- net: hold sock reference while processing tx timestamps (Richard Cochran)
- wl12xx: fix forced passive scans (Luciano Coelho)
- time: Change jiffies_to_clock_t() argument type to unsigned long (hank)
  (NeilBrown)
- md/raid5: fix bug that could result in reads from a failed device.
- perf probe: Fix to show correct error string (Masami Hiramatsu)
  Rostedt)
- tracing: Fix returning of duplicate data after EOF in trace_pipe_raw (Steven
- genirq: Add IRQF_RESUME_EARLY and resume such IRQs earlier (Ian Campbell)
- nfs: don't try to migrate pages with active requests (Jeff Layton)
- nfsd4: Remove check for a 32-bit cookie in nfsd4_readdir() (Bernd Schubert)
- nfsd4: stop using nfserr_resource for transitory errors (J. Bruce Fields)
- nfsd4: fix seqid_mutating_error (J. Bruce Fields)
- nfsd4: permit read opens of executable-only files (J. Bruce Fields)
- nfsd4: fix open downgrade, again (J. Bruce Fields)
- nfsd4: ignore WANT bits in open downgrade (J. Bruce Fields)
- hppfs: missing include (Al Viro)
- vfs: add "device" tag to /proc/self/mountstats (Bryan Schumaker)
- io-mapping: ensure io_mapping_map_atomic _is_ atomic (Daniel Vetter)
- ASoC: wm8940: Properly set codec->dapm.bias_level (Axel Lin)
- ASoC: wm8741: Fix setting interface format for DSP modes (Axel Lin)
- ASoC: ak4642: fixup cache register table (Kuninori Morimoto)
- ASoC: ak4535: fixup cache register table (Axel Lin)
- ASoC: wm8994: Use SND_SOC_DAPM_AIF_OUT for AIF3 Capture (Axel Lin)
- ASoC: Remove direct register cache accesses from WM8962 driver (Mark Brown)
- ASoC: Fix a bug in WM8962 DSP_A and DSP_B settings (Susan Gao)
- KVM: s390: check cpu_id prior to using it (Carsten Otte)
- user per registers vs. ptrace single stepping (Martin Schwidefsky)
- memory leak with RCU_TABLE_FREE (Martin Schwidefsky)
- ccwgroup: move attributes to attribute group (Sebastian Ott)
- WMI: properly cleanup devices to avoid crashes (Dmitry Torokhov)
- iommu/amd: Fix wrong shift direction (Joerg Roedel)
- carminefb: Fix module parameters permissions (Jean Delvare)
  Krzesinski)
- fb: avoid possible deadlock caused by fb_set_suspend (Herton Ronaldo
  (Bruno Prémont)
- fb: sh-mobile: Fix deadlock risk between lock_fb_info() and console_lock()
  Schandinat)
- viafb: use display information in info not in var for panning (Florian Tobias
- viafb: improve pitch handling (Florian Tobias Schandinat)
  Lei)
- uvcvideo: Set alternate setting 0 on resume if the bus has been reset (Ming
- DiBcom: protect the I2C bufer access (Patrick Boettcher)
- dib0700: protect the dib0700 buffer access (Olivier Grenie)
  (Mauro Carvalho Chehab)
- tuner_xc2028: Allow selection of the frequency adjustment code for XC3028
- /proc/self/numa_maps: restore "huge" tag for hugetlb vmas (Andrew Morton)
  (Paul Fertser)
- plat-mxc: iomux-v3.h: implicitly enable pull-up/down when that's desired
- ARM: mach-ux500: unlock I&D l2x0 caches before init (Linus Walleij)
  Hayasaka)
- mm: avoid null pointer access in vm_struct via /proc/vmallocinfo (Mitsuo
- ALSA: hda - Fix ADC input-amp handling for Cx20549 codec (Takashi Iwai)
- um: fix ubd cow size (Richard Weinberger)
  (Andy Whitcroft)
- readlinkat: ensure we return ENOENT for the empty pathname for normal lookups
- VFS: Fix automount for negative autofs dentries (David Howells)
- vfs: automount should ignore LOOKUP_FOLLOW (Miklos Szeredi)
- VFS: Fix the remaining automounter semantics regressions (Trond Myklebust)
- vfs pathname lookup: Add LOOKUP_AUTOMOUNT flag (Linus Torvalds)
- ptrace: don't clear GROUP_STOP_SIGMASK on double-stop (Oleg Nesterov)
- jsm: remove buggy write queue (Thadeu Lima de Souza Cascardo)
  (Mitsuo Hayasaka)
- bonding: use local function pointer of bond->recv_probe in bond_handle_frame
- bridge: fix hang on removal of bridge via netlink (stephen hemminger)
- can bcm: fix tx_setup off-by-one errors (Oliver Hartkopp)
- ipv4: fix ipsec forward performance regression (Yan, Zheng)
- l2tp: fix a potential skb leak in l2tp_xmit_skb() (Eric Dumazet)
  Ward)
- macvlan/macvtap: Fix unicast between macvtap interfaces in bridge mode (David
- netconsole: enable netconsole can make net_device refcnt incorrent (Gao feng)
- tcp: properly handle md5sig_pool references (Yan, Zheng)
- tcp: properly update lost_cnt_hint during shifting (Yan, Zheng)
- tg3: negate USE_PHYLIB flag check (Jiri Pirko)
  Zheng)
- ipv6: nullify ipv6_ac_list and ipv6_fl_list when creating new socket (Yan,
  ring (Willem de Bruijn)
- make PACKET_STATISTICS getsockopt report consistently between ring and non-
  Vrabel)
- net: xen-netback: correctly restart Tx after a VM restore/migrate (David
- mm: thp: tail page refcounting fix (Andrea Arcangeli)
- binfmt_elf: fix PIE execution with randomization disabled (Jiri Kosina)
  Torvalds)
- vfs: show O_CLOEXE bit properly in /proc/<pid>/fdinfo/<fd> files (Linus
- iwlagn: do not use interruptible waits (Johannes Berg)
  Bounine)
- drivers/net/rionet.c: fix ethernet address macros for LE platforms (Alexandre
- hwspinlock/core: use a mutex to protect the radix tree (Juan Gutierrez)
  (Clifton Barnes)
- drivers/power/ds2780_battery.c: create central point for calling w1 interface
  (Clifton Barnes)
- drivers/power/ds2780_battery.c: add a nolock function to w1 interface
  (Clifton Barnes)
- drivers/power/ds2780_battery.c: fix deadlock upon insertion and removal
  (Theodore Ts'o)
- ext2,ext3,ext4: don't inherit APPEND_FL or IMMUTABLE_FL for new inodes
  Wong)
- ext4: ext4_rename should dirty dir_bh with the correct directory (Darrick J.
  (Darrick J. Wong)
- ext4: ext4_mkdir should dirty dir_block with newly created directory inode
  (Theodore Ts'o)
- ext4: call ext4_handle_dirty_metadata with correct inode in ext4_dx_add_entry
- ext4: fix race in xattr block allocation path (Eric Sandeen)
  (Vasanthy Kolluri)
- enic: Bug Fix: Fix hardware transmit queue indexing in enic_poll_controller
  li)
- rtl8150: rtl8150_disconnect(...) does not need tasklet_disable(...) (huajun
- USB: EHCI: Fix test mode sequence (Boris Todorov)
  (Arvid Brodin)
- usb/isp1760: Added missing call to usb_hcd_check_unlink_urb() during unlink
- USB: Serial: Add device ID for Sierra Wireless MC8305 (Florian Echtler)
  (Artur Zimmer)
- USB: Serial: Add PID(0xF7C0) to FTDI SIO driver for a zeitcontrol-device
- usbnet/cdc_ncm: Don't use stack variables for DMA (Josh Boyer)
- USB: Avoid NULL pointer deref in usb_hcd_alloc_bandwidth. (Sarah Sharp)
- ipheth: iPhone 4 Verizon CDMA USB Product ID add (Kavan Smith)
- USB: xHCI: prevent infinite loop when processing MSE event (Andiry Xu)
- ASIX: Simplify condition in rx_fixup() (Marek Vasut)
- ASIX: Use only 11 bits of header for data size (Marek Vasut)
- Bluetooth: Add MacBookAir4,1 support (Pieter-Augustijn Van Malleghem)
- Bluetooth: Add Toshiba laptops AR30XX device ID (Ricardo Mendoza)
- Bluetooth: Add Atheros AR3012 one PID/VID supported (Steven.Li)
- Bluetooth: add support for 2011 mac mini (Jurgen Kramer)
- btusb: add device entry for Broadcom SoftSailing (Oliver Neukum)
- usb_storage: Don't freeze in usb-stor-scan (Seth Forshee)
- xhci: If no endpoints changed, don't issue BW command. (Sarah Sharp)
- xHCI: test and clear RWC bit (Andiry Xu)
- xHCI: Clear PLC for USB2 root hub ports (Andiry Xu)
- can bcm: fix incomplete tx_setup fix (Oliver Hartkopp)
  Arcangeli)
- powerpc: remove superfluous PageTail checks on the pte gup_fast (Andrea
- powerpc: get_hugepte() don't put_page() the wrong page (Andrea Arcangeli)
  Arcangeli)
- powerpc: gup_hugepte() avoid freeing the head page too many times (Andrea
- powerpc: gup_hugepte() support THP based tail recounting (Andrea Arcangeli)
- powerpc: gup_huge_pmd() return 0 if pte changes (Andrea Arcangeli)
- s390: gup_huge_pmd() support THP tail recounting (Andrea Arcangeli)
- s390: gup_huge_pmd() return 0 if pte changes (Andrea Arcangeli)
- thp: share get_huge_page_tail() (Andrea Arcangeli)
- bridge: leave carrier on for empty bridge (stephen hemminger)
- net: Unlock sock before calling sk_free() (Thomas Gleixner)
- ALSA: ua101: fix crash when unplugging (Clemens Ladisch)
  (Charles Chin)
- ALSA: hda - Disable power-widget control for IDT 92HD83/93 as default
- ALSA: hda - Add support for 92HD65 / 92HD66 family of codecs (Charles Chin)
- ALSA: hda/realtek - Skip invalid digital out pins (Takashi Iwai)
  (Jerome Glisse)
- drm/radeon: avoid bouncing connector status btw disconnected & unknown
- drm/radeon/kms: split MSI check into a separate function (Alex Deucher)
- drm/radeon/kms: Add MSI quirk for HP RS690 (Alex Deucher)
  Glisse)
- drm/radeon: set hpd polarity at init time so hotplug detect works (Jerome
- drm/radeon/kms: properly set panel mode for eDP (Alex Deucher)
- drm/radeon/kms: Add MSI quirk for Dell RS690 (Alex Deucher)
- drm/radeon/kms: add MSI module parameter (Alex Deucher)
- drm/radeon/kms: set HPD polarity in hpd_init() (Alex Deucher)
- kbuild: Fix help text not displayed in choice option. (Srinivas Kandagatla)
- PM / Runtime: Automatically retry failed autosuspends (Alan Stern)
- USB: Update last_busy time after autosuspend fails (Alan Stern)
  (Mike Miller)
- cciss: add small delay when using PCI Power Management to reset for kump
- hwmon: (coretemp) Fix for non-SMP builds (Jean Delvare)
  Delvare)
- hwmon: (w83627ehf) Properly report PECI and AMD-SI sensor types (Jean
- hwmon: (w83627ehf) Fix broken driver init (Guenter Roeck)
  (Nicholas Bellinger)
- tcm_loop: Add explict read buffer memset for SCF_SCSI_CONTROL_SG_IO_CDB
- st: fix race in st_scsi_execute_end (Petr Uzel)
- scsi_dh: check queuedata pointer before proceeding further (Moger, Babu)
- Make scsi_free_queue() kill pending SCSI commands (Bart Van Assche)
- Fix block queue and elevator memory leak in scsi_alloc_sdev (Anton Blanchard)
  (nagalakshmi.nandigama)
- mpt2sas: Fix for system hang when discovery in progress
  (Axel Lin)
- ASoC: wm8711: Fix wrong mask for setting input audio data bit length select
  wm8711_set_dai_fmt (Axel Lin)
- ASoC: Leave input audio data bit length settings untouched in
- ASoC: WM8904: Set `invert' bit for Capture Switch (Hong Xu)
- ASoC: Ensure WM8962 PLL registers are reset (Mark Brown)
- ASoC: Ensure the WM8962 oscillator and PLLs start up disabled (Mark Brown)
- NFS/sunrpc: don't use a credential with extra groups. (NeilBrown)
- block: make gendisk hold a reference to its queue (Tejun Heo)
- VFS: fix statfs() automounter semantics regression (Dan McGee)
  Miller)
- hpsa: add small delay when using PCI Power Management to reset for kump (Mike
- VFS: we need to set LOOKUP_JUMPED on mountpoint crossing (Al Viro)
  Blanchard)
- powerpc/numa: Remove double of_node_put in hot_add_node_scn_to_nid (Anton
  (Anton Blanchard)
- powerpc: Fix oops when echoing bad values to /sys/devices/system/memory/probe
  Blanchard)
- powerpc/pseries: Avoid spurious error during hotplug CPU add (Anton
- powerpc/eeh: Fix /proc/ppc64/eeh creation (Thadeu Lima de Souza Cascardo)
- powerpc: Fix deadlock in icswx code (Anton Blanchard)
  Luck)
- ACPI atomicio: Convert width in bits to bytes in __acpi_ioremap_fast() (Tony
- netlink: validate NLA_MSECS length (Johannes Berg)
  Manoharan)
- ath9k_hw: Update AR9485 initvals to fix system hang issue (Rajkumar
- mac80211: fix remain_off_channel regression (Eliad Peller)
- mac80211: config hw when going back on-channel (Eliad Peller)
- mac80211: disable powersave for broken APs (Johannes Berg)
- mtd: mtdchar: add missing initializer on raw write (Peter Wippich)
- mtd: provide an alias for the redboot module name (Andres Salomon)
- mtd: pxa3xx_nand: fix nand detection issue (Lei Wen)
- mtd: pxa3xx_nand: Fix blank page ECC mismatch (Daniel Mack)
  (Jiaying Zhang)
- ext4: remove i_mutex lock in ext4_evict_inode to fix lockdep complaining
- net: Align AF-specific flowi structs to long (David Ward)
  (dpward)
- net: Handle different key sizes between address families in flow cache
- PM / Suspend: Off by one in pm_suspend() (Dan Carpenter)
- crypto: cryptd - Use subsys_initcall to prevent races with aesni (Herbert Xu)
  Cochran)
- dp83640: use proper function to free transmit time stamping packets (Richard
  Manoharan)
- ath9k_hw: Fix regression of register offset for AR9003 chips (Rajkumar
  Adam (Adam))
- mtd: nand_base: always initialise oob_poi before writing OOB data (THOMSON,
- HID: add support for new revision of Apple aluminum keyboard (Dan Bastone)
- HID: add support for HuiJia USB Gamepad connector (Clemens Werther)
- HID: add support for MacBookAir4,2 keyboard. (Joshua V. Dillon)
- HID: hid-multitouch: Add LG Display Multitouch device. (Jeff Brown)
- HID: add MacBookAir4,2 to hid_have_special_driver[] (Jiri Kosina)
- HID: Add support MacbookAir 4,1 keyboard (Nobuhiro Iwamatsu)
- HID: Add device IDs for Macbook Pro 8 keyboards (Gökçen Eraslan)
- HID: hid-apple: add device ID of another wireless aluminium (Andreas Krist)
- HID: consolidate MacbookAir 4,1 mappings (Jiri Kosina)
  translations (Linus Torvalds)
- hid/apple: modern macbook airs use the standard apple function key
  Iwai)
- ALSA: hda - Don't add elements of other codecs to vmaster slave (Takashi
- virtio-pci: fix use after free (Michael S. Tsirkin)
  Brown)
- ASoC: Don't use wm8994->control_data in wm8994_readable_register() (Mark
- sh: Fix cached/uncaced address calculation in 29bit mode (Nobuhiro Iwamatsu)
  Anholt)
- drm/i915: Fix object refcount leak on mmappable size limit error path. (Eric
- drm/nouveau: initialize chan->fence.lock before use (Marcin Slusarz)
- drm/radeon/kms: make an aux failure debug only (Alex Deucher)
  Iwai)
- ALSA: usb-audio - Check the dB-range validity in the later read, too (Takashi
  Iwai)
- ALSA: usb-audio - Fix the missing volume quirks at delayed init (Takashi
- KEYS: Fix a NULL pointer deref in the user-defined key type (David Howells)
- hfs: add sanity check for file name length (Dan Carpenter)
  (Johan Hovold)
- Revert "leds: save the delay values after a successful call to blink_set()"
- drm/radeon: add some missing FireMV pci ids (Alex Deucher)
  (Jesse Barnes)
- drm/i915: enable ring freq scaling, RC6 and graphics turbo on Ivy Bridge v3
- sfi: table irq 0xFF means 'no interrupt' (Kirill A. Shutemov)
- x86, mrst: use a temporary variable for SFI irq (Mika Westerberg)
- b43: refuse to load unsupported firmware (Rafał Miłecki)
- md/raid5: abort any pending parity operations when array fails. (NeilBrown)
- mfd: Fix twl4030 dependencies for audio codec (Thomas Weber)
- powerpc/ps3: Fix lost SMP IPIs (Geoff Levand)
- powerpc: Copy down exception vectors after feature fixups (Anton Blanchard)
- backing-dev: ensure wakeup_timer is deleted (Rabin Vincent)
  Hutchings)
- block: Always check length of all iov entries in blk_rq_map_user_iov() (Ben
- genirq: Fix irqfixup, irqpoll regression (Edward Donovan)
- fix WARNING: at drivers/scsi/scsi_lib.c:1704 (James Bottomley)
- hpsa: Disable ASPM (Matthew Garrett)
  Averin)
- aacraid: controller hangs if kernel uses non-default ASPM policy (Vasily
- saa7164: Add support for another HVR2200 hardware revision (Tony Jago)
- drm/i915/pch: Save/restore PCH_PORT_HOTPLUG across suspend (Adam Jackson)
  Marinas)
- ARM: 7150/1: Allow kernel unaligned accesses on ARMv6+ processors (Catalin
- Net, libertas: Resolve memory leak in if_spi_host_to_card() (Jesper Juhl)
  Wingerde)
- rt2x00: Fix sleep-while-atomic bug in powersaving code. (Gertjan van
- mac80211: fix NULL dereference in radiotap code (Johannes Berg)
- mac80211: fix bug in ieee80211_build_probe_req (Johannes Berg)
- nl80211: fix HT capability attribute validation (Johannes Berg)
  Rodriguez)
- cfg80211: fix bug on regulatory core exit on access to last_request (Luis R.
- ip6_tunnel: copy parms.name after register_netdevice (Josh Boyer)
- PM / driver core: disable device's runtime PM during shutdown (Peter Chen)
- pch_phub: Support new device LAPIS Semiconductor ML7831 IOH (Tomoya MORINAGA)
- pch_phub: Fix MAC address writing issue for LAPIS ML7831 (Tomoya MORINAGA)
- pch_uart: Fix hw-flow control issue (Tomoya MORINAGA)
- pch_uart: Fix DMA resource leak issue (Tomoya MORINAGA)
- pch_uart: Support new device LAPIS Semiconductor ML7831 IOH (Tomoya MORINAGA)
- tty: hvc_dcc: Fix duplicate character inputs (Stephen Boyd)
- TTY: ldisc, allow waiting for ldisc arbitrarily long (Jiri Slaby)
- TTY: ldisc, move wait idle to caller (Jiri Slaby)
- TTY: ldisc, wait for ldisc infinitely in hangup (Jiri Slaby)
  (Jeff Layton)
- nfs: when attempting to open a directory, fall back on normal lookup (try #5)
- pcie-gadget-spear: Add "platform:" prefix for platform modalias (Axel Lin)
  (Claudio Scordino)
- drivers/base/node.c: fix compilation error with older versions of gcc
- xhci: Set slot and ep0 flags for address command. (Sarah Sharp)
- usb, xhci: Clear warm reset change event during init (Don Zickus)
- usb, xhci: fix lockdep warning on endpoint timeout (Don Zickus)
- USB: XHCI: resume root hubs when the controller resumes (Alan Stern)
- USB: option: release new PID for ZTE 3G modem (zheng.zhijian)
- USB: option: add PID of Huawei E173s 3G modem (Ferenc Wagner)
- USB: serial: pl2303: rm duplicate id (wangyanqing)
- USB: cdc-acm: Fix disconnect() vs close() race (Havard Skinnemoen)
- USB: workaround for bug in old version of GCC (Alan Stern)
- USB: ark3116 initialisation fix (Bart Hartgers)
- USB: Fix Corruption issue in USB ftdi driver ftdi_sio.c (Andrew Worsley)
- usb-storage: Accept 8020i-protocol commands longer than 12 bytes (Alan Stern)
- USB: EHCI: fix HUB TT scheduling issue with iso transfer (Thomas Poussevin)
- USB: add quirk for Logitech C600 web cam (Josh Boyer)
- USB: quirks: adding more quirky webcams to avoid squeaky audio (sordna)
- xfs: fix error handling for synchronous writes (Christoph Hellwig)
- xfs: fix xfs_mark_inode_dirty during umount (Christoph Hellwig)
- xfs: dont serialise direct IO reads on page cache (Dave Chinner)
- xfs: avoid direct I/O write vs buffered I/O race (Christoph Hellwig)
- xfs: Return -EIO when xfs_vn_getattr() failed (Mitsuo Hayasaka)
- xfs: fix buffer flushing during unmount (Christoph Hellwig)
- xfs: Fix possible memory corruption in xfs_readlink (Carlos Maiolino)
- xfs: use doalloc flag in xfs_qm_dqattach_one() (Mitsuo Hayasaka)
- xfs: fix ->write_inode return values (Christoph Hellwig)
- drm/i915: fix IVB cursor support (Jesse Barnes)
- drm/i915: always set FDI composite sync bit (Jesse Barnes)
  Kroah-Hartman)
- Revert "USB: EHCI: fix HUB TT scheduling issue with iso transfer" (Greg
  supported on the card (Somnath Kotur)
- be2net: Fallback to the older opcode if MCC_CREATE_EXT opcode is not
- be2net: Fix Rx pause counter for lancer (Selvin Xavier)
- be2net: Enable NETIF_F_TSO6 for VLAN traffic for BE (Padmanabh Ratnakar)
- be2net: support multiple TX queues (Sathya Perla)
- be2net: fix netdev_stats_update (Sathya Perla)
- be2net: get rid of multi_rxq module param (Sathya Perla)
- be2net: fix initialization of vlan_prio_bmap (Sathya Perla)
- be2net: fix certain cmd failure logging (Sathya Perla)
- be2net: create/destroy rx-queues on interface open/close (Sathya Perla)
- be2net: clear intr bit in be_probe() (Sathya Perla)
- benet: Add missing comma between constant string array (Joe Perches)
- be2net: account for skb allocation failures (Eric Dumazet)
- be2net: move to new vlan model (Ajit Khaparde)
- be2net: request native mode each time the card is reset (Sathya Perla)
- be2net: cleanup and refactor stats code (Sathya Perla)
- be2net: use stats-sync to read/write 64-bit stats (Sathya Perla)
  Perla)
- be2net: remove wrong and unnecessary calls to netif_carrier_off() (Sathya
- be2net: no need to query link status (Sathya Perla)
- be2net: non-member vlan pkts not received in promiscous mode (Sathya Perla)
- be2net: use RX_FILTER cmd to program multicast addresses (Sathya Perla)
- be2net: add support for flashing Teranetics PHY firmware (Sathya Perla)
- be2net: drop pkts that do not belong to the port (Sathya Perla)
- be2net: fix cmd-rx-filter not notifying MCC (Sathya Perla)
- benet: fix build error on 32bit arch (Eric Dumazet)
  vlan_tag (Somnath Kotur)
- be2net: Storing the 'vid' got by the grp5 event instead of storing the
- be2net: Fix race in posting rx buffers. (Sathya Perla)
- be2net: get rid of memory mapped pci-cfg space address (Sathya Perla)
- be2net: fix erx->rx_drops_no_frags wrap around (Sathya Perla)
- be2net: increase FW update completion timeout (Sathya Perla)
- be2net: remove unused variable (Sathya Perla)
- benet: remove bogus "unlikely" on vlan check (Jiri Pirko)
- be2net: fix multicast filter programming (Sathya Perla)
- be2net: Show newly flashed FW ver in ethtool (Sathya Perla)
  (Somnath Kotur)
- be2net: Add 60 second delay to allow FAT dump completion on recovery from EEH
  Kotur)
- be2net: Change the data type of the 'on die temperature' stat. (Somnath
  retrieving FAT data (Somnath Kotur)
- be2net: Fixed Endianness issues in the response read log length field while
- be2net: Modified PCI MaxReadReq size to 4096 bytes (Somnath Kotur)
- be2net: Making die temperature ioctl call async (Somnath Kotur)
- be2net: fix truesize errors (Eric Dumazet)
- be2net: add vlan/rx-mode/flow-control config to be_setup() (Sathya Perla)
  Perla)
- be2net: refactor VF setup/teardown code into be_vf_setup/clear() (Sathya
- be2net: don't create multiple TXQs in BE2 (Sathya Perla)
  Perla)
- be2net: don't create multiple RX/TX rings in multi channel mode (Sathya
- be2net: Refactored be_cmds.c file. (Somnath Kotur)
- be2net: Changing MAC Address of a VF was broken. (Somnath Kotur)
- be2net: Fix endian issue in RX filter command (Padmanabh Ratnakar)
- be2net: Fix disabling multicast promiscous mode (Padmanabh Ratnakar)
- be2net: Prevent CQ full condition for Lancer (Padmanabh Ratnakar)
- be2net: Add detect UE feature for Lancer (Padmanabh Ratnakar)
  Perla)
- be2net: init (vf)_if_handle/vf_pmac_id to handle failure scenarios (Sathya
- be2net: stop checking the UE registers after an EEH error (Sathya Perla)
  Perla)
- be2net: don't log more than one error on detecting EEH/UE errors (Sathya
- be2net: stop issuing FW cmds if any cmd times out (Sathya Perla)
- be2net: Fix TX queue create for Lancer (Padmanabh Ratnakar)
- be2net: add register dump feature for Lancer (Padmanabh Ratnakar)
- be2net: Add EEPROM dump feature for Lancer (Padmanabh Ratnakar)
- be2net: Fix VLAN promiscous mode for Lancer (Padmanabh Ratnakar)
- be2net: Use V1 query link status command for lancer (Padmanabh Ratnakar)
- be2net: Move to new SR-IOV implementation in Lancer (Padmanabh Ratnakar)
- be2net: Fix error recovery paths (Padmanabh Ratnakar)
- be2net: Add error handling for Lancer (Padmanabh Ratnakar)
- be2net: Use new hash key (Padmanabh Ratnakar)
- be2net: Fix non utilization of RX queues (Padmanabh Ratnakar)
- be2net: Changed version number to 4.1.297o (Somnath Kotur)
- Enable BG by default (Maxim Uvarov)
  Uvarov)
- Fixed compiler warning for putting large amount of memory on stack (Maxim
- Fixed mailbox double free panic (Maxim Uvarov)
  Uvarov)
- Merge from upstream: Silence DEBUG_STRICT_USER_COPY_CHECKS=y warning (Maxim
  together (Konrad Rzeszutek Wilk)
- xen/blk[front|back]: Squash blkif_request_rw and blkif_request_discard
  (Konrad Rzeszutek Wilk)
- xen/blk[front|back]: Enhance discard support with secure erasing support.
  (Konrad Rzeszutek Wilk)
- xen/blkback: Move processing of BLKIF_OP_DISCARD from dispatch_rw_block_io
  Dongyang)
- xen-blkback: convert hole punching to discard request on loop devices (Li
  Vrabel)
- block: xen-blkback: use API provided by xenbus module to map rings (David
  Vrabel)
- xen: use generic functions instead of xen_{alloc, free}_vm_area() (David
  Vrabel)
- block: xen-blkback: use API provided by xenbus module to map rings (David
  Vrabel)
- net: xen-netback: use API provided by xenbus module to map rings (David
  Vrabel)
- xen: map foreign pages for shared rings by updating the PTEs directly (David
- xen/pm_idle: Make pm_idle be default_idle under Xen. (Konrad Rzeszutek Wilk)
  (Konrad Rzeszutek Wilk)
- x86/cpa: Use pte_attrs instead of pte_flags on CPA/set_p.._wb/wc operations.
  Rzeszutek Wilk)
- x86/paravirt: Use pte_val instead of pte_flags on CPA pageattr_test (Konrad
  XenbusStateClosed. (Joe Jin)
- xen-blkback: Don't disconnect backend until state switched to
- xen/acpi: Domain0 acpi parser related platform hypercall (Yu Ke)
- ACPI: processor: export necessary interfaces (Kevin Tian)
  (Kevin Tian)
- ACPI: processor: Don't setup cpu idle handler when we do not want them.
- ACPI: processor: cache acpi_power_register in cx structure (Kevin Tian)
  Liang)
- ACPI: processor: add __acpi_processor_[un]register_driver helpers. (Tang
- ACPI: add processor driver for Xen virtual CPUs. (Kevin Tian)
  for Xen vcpu (Tang Liang)
- ACPI: processor: override the interface of register acpi processor handler
- ACPI: xen processor: add PM notification interfaces. (Kevin Tian)
  Tian)
- ACPI: xen processor: set ignore_ppc to handle PPC event for Xen vcpu. (Kevin
  Rzeszutek Wilk)
- Revert "xen/pm_idle: Make pm_idle be default_idle under Xen." (Konrad
- AIO: Don't plug the I/O queue in do_io_submit() (Dave Kleikamp)
- mlx4: use pci_dev->revision (Sergei Shtylyov)
- mlx4_core: Extend capability flags to 64 bits (Or Gerlitz)
- mlx4_core: Read extended capabilities into the flags field (Or Gerlitz)
- mlx4: do vlan cleanup (Jiri Pirko)
- mlx4: Fixing Ethernet unicast packet steering (Yevgeny Petrilin)
- mlx4: decreasing ref count when removing mac (Yevgeny Petrilin)
- mlx4_core: Clean up error flow in mlx4_register_mac() (Roland Dreier)
- mlx4_en: Assigning TX irq per ring (Joe Jin)
- mlx4_en: Removing reserve vectors (Joe Jin)
- mlx4_en: Adjusting moderation per each ring (Joe Jin)
- mlx4_en: Added missing iounmap upon releasing a device (Joe Jin)
- mlx4_en: Fix QP number calculation according to module param (Joe Jin)
- mlx4_en: Fix crash upon device initialization error (Joe Jin)
- mlx4_en: Adding 40gb speed report for ethtool (Joe Jin)
- mlx4: Fix vlan table overflow (Joe Jin)
- mlx4_en: Controlling FCS header removal (Joe Jin)
- mlx4_en: Checksum counters per ring (Joe Jin)
- mlx4_en: Recording rx queue for gro packets (Joe Jin)
- mlx4_en: Adding rxhash support (Joe Jin)
- mlx4_en: Updating driver version (Joe Jin)
- mlx4_en: fix skb truesize underestimation (Joe Jin)
- mlx4_en: Remove FCS bytes from packet length. (Joe Jin)
- mlx4_en: using non collapsed CQ on TX (Joe Jin)
  Jin)
- mlx4_en: fix WOL handlers were always looking at port2 capability bit (Joe
- mlx4_en: adding loopback support (Joe Jin)
- netxen: Upgrade netxen_nic driver to v4.0.77 (Joe Jin)
- [firmware] radeon: Add License for raedon firmware files (Joe Jin)
  hanged. (Konrad Rzeszutek Wilk)
- xen: Enable CONFIG_XEN_WDT so that we can reboot the box in case the dom0 is
  Campbell)
- xen: only limit memory map to maximum reservation for domain 0. (Ian
  Rzeszutek Wilk)
- xen/swiotlb: Use page alignment for early buffer allocation. (Konrad
- eCryptfs: Flush file in vma close (Tyler Hicks)
  (Jeffrey (Sheng-Hui) Chu)
- i2c-algo-bit: Generate correct i2c address sequence for 10-bit target
- eCryptfs: Extend array bounds for all filename chars (Tyler Hicks)
- crypto: mv_cesa - fix hashing of chunks > 1920 bytes (Phil Sutter)
- drm: integer overflow in drm_mode_dirtyfb_ioctl() (Xi Wang)
- drm/radeon/kms: fix up gpio i2c mask bits for r4xx for real (Alex Deucher)
- drm/i915: Ivybridge still has fences! (Daniel Vetter)
  Anholt)
- drm/i915: Turn on a required 3D clock gating bit on Sandybridge. (Eric
- drm/i915: Turn on another required clock gating bit on gen6. (Eric Anholt)
  Skeggs)
- drm/ttm: request zeroed system memory pages for new TT buffer objects (Ben
- drm/i915: fix CB tuning check for ILK+ (Jesse Barnes)
  Helgaas)
- PCI hotplug: shpchp: don't blindly claim non-AMD 0x7450 device IDs (Bjorn
- drm/radeon/kms: fix up gpio i2c mask bits for r4xx (Alex Deucher)
- viafb: correct sync polarity for OLPC DCON (Daniel Drake)
- ARM: pxa: fix inconsistent CONFIG_USB_PXA27X (Haojian Zhuang)
- arm: mx28: fix bit operation in clock setting (Wolfram Sang)
- ARM: OMAP: smartreflex: fix IRQ handling bug (Felipe Balbi)
- ARM: OMAP2: select ARM_AMBA if OMAP3_EMU is defined (Ming Lei)
- ARM: 7161/1: errata: no automatic store buffer drain (Will Deacon)
- ALSA: lx6464es - fix device communication via command bus (Tim Blechmann)
- ASoC: fsl_ssi: properly initialize the sysfs attribute object (Timur Tabi)
- ASoC: wm8753: Skip noop reconfiguration of DAI mode (Timo Juhani Lindfors)
  (Mark Brown)
- ASoC: Ensure WM8731 register cache is synced when resuming from disabled
  Myklebust)
- SUNRPC: Ensure we return EAGAIN in xs_nospace if congestion is cleared (Trond
- genirq: fix regression in irqfixup, irqpoll (Edward Donovan)
- cgroup_freezer: fix freezing groups with stopped tasks (Michal Hocko)
- timekeeping: add arch_offset hook to ktime_get functions (Hector Palacios)
- hrtimer: Fix extra wakeups from __remove_hrtimer() (Jeff Ohlstein)
- p54spi: Add missing spin_lock_init (Michael Büsch)
- p54spi: Fix workqueue deadlock (Michael Büsch)
- rt2x00: Fix efuse EEPROM reading on PPC32. (Gertjan van Wingerde)
- nl80211: fix MAC address validation (Eliad Peller)
- cfg80211: fix regulatory NULL dereference (Johannes Berg)
- mac80211: don't stop a single aggregation session twice (Johannes Berg)
  Grumbach)
- mac80211: fix race between the AGG SM and the Tx data path (Emmanuel
- hwmon: (coretemp) Fix oops on driver load (Jean Delvare)
- revert "mfd: Fix twl4030 dependencies for audio codec" (Greg Kroah-Hartman)
- SCSI: Silencing 'killing requests for dead queue' (Hannes Reinecke)
- hugetlb: release pages in the error path of hugetlb_cow() (Hillf Danton)
- drm/radeon/kms: add some new pci ids (Alex Deucher)
- drm/radeon/kms: add some loop timeouts in pageflip code (Alex Deucher)
- firmware: Sigma: Prevent out of bounds memory access (Lars-Peter Clausen)
- firmware: Sigma: Skip header during CRC generation (Lars-Peter Clausen)
- firmware: Sigma: Fix endianess issues (Lars-Peter Clausen)
- staging: usbip: bugfix for deadlock (Bart Westgeest)
- staging: comedi: fix oops for USB DAQ devices. (Bernd Porr)
- Staging: comedi: fix mmap_count (Federico Vaga)
- Staging: comedi: fix signal handling in read and write (Federico Vaga)
- USB: whci-hcd: fix endian conversion in qset_clear() (Dan Carpenter)
- HID: Correct General touch PID (Benjamin Tissoires)
- usb: ftdi_sio: add PID for Propox ISPcable III (Marcin Kościelnicki)
- usb: option: add Huawei E353 controlling interfaces (Dirk Nehring)
- usb: option: add SIMCom SIM5218 (Veli-Pekka Peltola)
- USB: usb-storage: unusual_devs entry for Kingston DT 101 G2 (Qinglin Ye)
- EHCI : Fix a regression in the ISO scheduler (Matthieu CASTET)
- xHCI: fix bug in xhci_clear_command_ring() (Andiry Xu)
- sched, x86: Avoid unnecessary overflow in sched_clock (Salman Qazi)
- x86/mpparse: Account for bus types other than ISA and PCI (Bjorn Helgaas)
- x86: Fix "Acer Aspire 1" reboot hang (Peter Chubb)
- perf/x86: Fix PEBS instruction unwind (Peter Zijlstra)
  Richter)
- oprofile, x86: Fix crash when unloading module (nmi timer mode) (Robert
- add missing .set function for NT_S390_LAST_BREAK regset (Martin Schwidefsky)
- cfg80211: fix race on init and driver registration (Luis R. Rodriguez)
- cfg80211: amend regulatory NULL dereference fix (Luis R. Rodriguez)
- genirq: Fix race condition when stopping the irq thread (Ido Yariv)
  Myklebust)
- NFS: Prevent 3.0 from crashing if it receives a partial layout (Trond
- xfs: validate acl count (Christoph Hellwig)
  (Christoph Hellwig)
- xfs: force buffer writeback before blocking on the ilock in inode reclaim
- xfs: fix attr2 vs large data fork assert (Christoph Hellwig)
  ftrace_event_call->filter (Tejun Heo)
- trace_events_filter: Use rcu_assign_pointer() when setting
- rtc: Disable the alarm in the hardware (Rabin Vincent)
- tracing: fix event_subsystem ref counting (Ilya Dryomov)
  Gleixner)
- tick-broadcast: Stop active broadcast device when replacing it (Thomas
- perf: Fix parsing of __print_flags() in TP_printk() (Steven Rostedt)
  Natapov)
- jump_label: jump_label_inc may return before the code is patched (Gleb
- oprofile: Fix crash when unloading module (hr timer mode) (Robert Richter)
  (Joseph))
- clocksource: Fix bug with max_deferment margin calculation (Yang Honggang
  Gleixner)
- clockevents: Set noop handler in clockevents_exchange_device() (Thomas
  Christie)
- iscsi_boot_sysfs: have this module check for null on destruction (Mike
  (Mike Christie)
- iscsi_ibft, be2iscsi, iscsi_boot: fix boot kobj data lifetime management
- block: add bsg helper library (Mike Christie)
- bsg-lib: add module.h include (Jens Axboe)
- iscsi_transport: add support for net settings (Mike Christie)
- qla4xxx: add support for set_net_config (Mike Christie)
- qla4xxx: Added new "struct ipaddress_config" (Vikas Chaudhary)
- iscsi class: add iface representation (Mike Christie)
- qla4xxx: added support to show multiple iface in sysfs (Vikas Chaudhary)
- iscsi cls: sysfs group is_visible callout for conn attrs (Mike Christie)
- iscsi class: sysfs group is_visible callout for session attrs (Mike Christie)
- iscsi class: remove iface param mask (Mike Christie)
  Christie)
- iscsi class: sysfs group is_visible callout for iscsi host attrs (Mike
- iscsi class: expand vlan support (Mike Christie)
- qla4xxx: Add VLAN support (Vikas Chaudhary)
- iscsi class: add bsg support to iscsi class (Mike Christie)
- qla4xxx: add bsg support (Vikas Chaudhary)
  offload session login. (Manish Rangankar)
- scsi_transport_iscsi: Add conn login, kernel to user, event to support
- qla4xxx: support iscsiadm session mgmt (Manish Rangankar)
  Rangankar)
- qla4xxx: Remove reduandant code after open-iscsi integration. (Manish
- qla4xxx: Boot from SAN support for open-iscsi (Manish Rangankar)
- scsi_transport_iscsi: Added support to update mtu (Vikas Chaudhary)
- qla4xxx: Added support to update mtu (Vikas Chaudhary)
- qla4xxx: Code cleanup for read/update flash using BSG (Harish Zunjarrao)
- qla4xxx: Add get ACB state support using BSG (Harish Zunjarrao)
  Zunjarrao)
- qla4xxx: Add read/update NVRAM support for 40xx adapters using BSG (Harish
- qla4xxx: Added vendor specific sysfs attributes (Vikas Chaudhary)
  Chaudhary)
- scsi_transport_iscsi: Added support to update initiator iscsi port (Vikas
- qla4xxx: added support to update initiator iscsi port (Vikas Chaudhary)
- qla4xxx: Added restore factory defaults support using BSG (Harish Zunjarrao)
- qla4xxx: Added Get ACB support using BSG (Harish Zunjarrao)
- scsi: Added support for adapter and firmware reset (Vikas Chaudhary)
- qla4xxx: Added support for adapter and firmware reset (Vikas Chaudhary)
- qla4xxx: export iface name (Mike Christie)
- qla4xxx: Add new FLT firmware region (Nilesh Javali)
- qla4xxx: Fix bidirectional CHAP. (Lalit Chandivade)
- qla4xxx: Do not add duplicate CHAP entry in FLASH (Lalit Chandivade)
- qla4xxx: Fix exporting boot targets to sysfs (Lalit Chandivade)
- qla4xxx: Fix getting BIDI CHAP for boot targets (Lalit Chandivade)
- qla4xxx: Free Device Database (DDB) reserved by FW (Lalit Chandivade)
- qla4xxx: Clear DDB map index on the basis of AEN. (Manish Rangankar)
- qla4xxx: Fixed session destroy issue on link up-down. (Manish Rangankar)
- qla4xxx: Fixed device blocked issue on link up-down. (Manish Rangankar)
- qla4xxx: Fixed active session re-open issue. (Manish Rangankar)
- qla4xxx: Fixed target discovery failed issue. (Manish Rangankar)
- qla4xxx: updated device id check for BFS. (Manish Rangankar)
- qla4xxx: Update driver version to 5.02.00-k8 (Vikas Chaudhary)
- iscsi class: fix link local mispelling (Mike Christie)
- qla4xxx: fix data alignment and use nl helpers (Mike Christie)
- iscsi class: fix vlan configuration (Mike Christie)
  Christie)
- qla4xxx: export address/port of connection (fix udev disk names) (Mike
- scsi: qla4xxx driver depends on NET (Randy Dunlap)
- qla4xxx: select iscsi boot sysfs attrs (Mike Christie)
- qla4xxx: Autologin persisted target entries. (Manish Rangankar)
- iscsi class: export pid of process that created session (Mike Christie)
- qla4xxx: Updated version to 5.02.00.00.06.02-uek0 (Lalit Chandivade)
- [SCSI] mpt2sas MPI next revision header update (Kashyap, Desai)
- [SCSI] mpt2sas: Set max_sector count from module parameter (Kashyap, Desai)
- [SCSI] mpt2sas: fix broadcast AEN and task management issue (Kashyap, Desai)
- [SCSI] mpt2sas: Bump version 09.100.00.00 (Kashyap, Desai)
  entry in MPI message (Kashyap, Desai)
- [SCSI] mpt2sas: WarpDrive Infinite command retries due to wrong scsi command
  context (kashyap.desai)
- [SCSI] mpt2sas: Added missing mpt2sas_base_detach call from scsih_remove
- Remove unneeded version.h includes from drivers/scsi/ (Jesper Juhl)
  support of the HBA (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Added NUNA IO support in driver which uses multi-reply queue
- [SCSI] mpt2sas: Bump driver version 09.100.00.01 (nagalakshmi.nandigama)
- [SCSI] mpt2sas: take size of pointed value, not pointer (Julia Lawall)
- [SCSI] mpt2sas: MPI next revision header update (nagalakshmi.nandigama)
- [SCSI] mpt2sas: New feature - Fast Load Support (nagalakshmi.nandigama)
  (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Fix for system hang when discovery in progress
  (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Fix failure message displayed during diag reset
  removed while host reset is active (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Fix drives not getting properly deleted if sas cable is
  sas_device_lock (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Fix for dead lock occurring between host_lock and
  reset context (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Fix for deadlock between hot plug worker threads and host
  complete while issued during creating a volume (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Fix for issue Port Reset taking long time(around 5 mins) to
  (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Fix for Panic when inactive volume is tried deleting
- [SCSI] mpt2sas: Bump driver version to 10.100.00.00 (nagalakshmi.nandigama)
- [SCSI] mpt2sas: add missing allocation. (Dan Carpenter)
  context (Anton Blanchard)
- [SCSI] mpt2sas: _scsih_smart_predicted_fault uses GFP_KERNEL in interrupt
  (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Better handling DEAD IOC (PCI-E LInk down) error condition
  to avoid infinite resets (nagalakshmi.nandigama)
- [SCSI] mpt2sas: When IOs are terminated, update the result to DID_SOFT_ERROR
  (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Adding support for customer specific branding
- [SCSI] mpt2sas: MPI next revision header update (nagalakshmi.nandigama)
  callback when all the LUNS have been deleted (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Do not set sas_device->starget to NULL from the slave_destroy
  (nagalakshmi.nandigama)
  initialized prior to sending the request to controller firmware
- [SCSI] mpt2sas: Rearrange the the code so that the completion queues are
- [SCSI] mpt2sas: Bump driver version to 11.100.00.00 (nagalakshmi.nandigama)
  (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Support for greater than 2TB capacity WarpDrive
  (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Increase max transfer support from 4MB to 16MB
  (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Added support for customer specific branding
- [SCSI] mpt2sas: MPI next revision header update (nagalakshmi.nandigama)
  (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Release spinlock for the raid device list before blocking it
  (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Do not retry a timed out direct IO for warpdrive
  (nagalakshmi.nandigama)
- [SCSI] mpt2sas : Fix for memory allocation error for large host credits
- [SCSI] mpt2sas : Bump driver vesion to 12.100.00.00 (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Fix leak on mpt2sas_base_attach() error path (Roland Dreier)
- [SCSI] mpt2sas: Fix possible integer truncation of cpu_count (Roland Dreier)
  Dreier)
- [SCSI] mpt2sas: Remove unused duplicate diag_buffer_enable param (Roland
  _scsih_probe (nagalakshmi.nandigama)
- [SCSI] mpt2sas: Removed redundant calling of _scsih_probe_devices() from
- Btrfs: fix barrier flushes (Chris Mason)
- btrfs: Fix up 32/64-bit compatibility for new ioctls (Jeff Mahoney)
- btrfs: mirror_num should be int, not u64 (Jan Schmidt)
- Btrfs: fix to search one more bitmap for cluster setup (Li Zefan)
- Btrfs: avoid unnecessary bitmap search for cluster setup (Li Zefan)
- btrfs: fix stat blocks accounting (David Sterba)
- Btrfs: prefix resize related printks with btrfs: (Arnd Hannemann)
- Btrfs: wait on caching if we're loading the free space cache (Josef Bacik)
- Btrfs: clear pages dirty for io and set them extent mapped (Josef Bacik)
- Btrfs: sectorsize align offsets in fiemap (Josef Bacik)
- Btrfs: remove free-space-cache.c WARN during log replay (Chris Mason)
- btrfs scrub: handle -ENOMEM from init_ipath() (Dan Carpenter)
- Fix URL of btrfs-progs git repository in docs (Arnd Hannemann)
- Btrfs: fix deadlock on metadata reservation when evicting a inode (Miao Xie)
- Btrfs: Don't error on resizing FS to same size (Mike Fleetwood)
- Btrfs: fix oops when calling statfs on readonly device (Li Zefan)
- Btrfs: initialize new bitmaps' list (Alexandre Oliva)
- Btrfs: reset cluster's max_size when creating bitmap (Alexandre Oliva)
- Btrfs: start search for new cluster at the beginning (Alexandre Oliva)
- Btrfs: skip block groups without enough space for a cluster (Alexandre Oliva)
- Btrfs: skip allocation attempt from empty cluster (Alexandre Oliva)
- Btrfs: fix meta data raid-repair merge problem (Jan Schmidt)
  Oliva)
- Btrfs: try to allocate from cluster even at LOOP_NO_EMPTY_SIZE (Alexandre
- Btrfs: try cluster but don't advance in search list (Alexandre Oliva)
- Btrfs: check if the to-be-added device is writable (Li Zefan)
- Btrfs: drop spin lock when memory alloc fails (Liu Bo)
  Mason)
- Btrfs: fix btrfs_end_bio to deal with write errors to a single mirror (Chris
- Btrfs: fix wrong i_size when truncating a file to a larger size (Miao Xie)
- Btrfs: fix wrong disk space information of the files (Miao Xie)
- Btrfs: fix inaccurate available space on raid0 profile (Miao Xie)
- btrfs: keep orphans for subvolume deletion (Arne Jansen)
- Btrfs: fix ctime update of on-disk inode (Li Zefan)
- Btrfs: add a cond_resched() into the worker loop (Chris Mason)
- BTRFS: Establish i_ops before calling d_instantiate (Casey Schaufler)
  Bacik)
- Btrfs: fix num_workers_starting bug and other bugs in async thread (Josef
- Btrfs: deal with enospc from dirtying inodes properly (Chris Mason)
  error (Josef Bacik)
- Btrfs: fix how we do delalloc reservations and how we free reservations on
- Btrfs: fix leaked space in truncate (Josef Bacik)
- Btrfs: don't panic if orphan item already exists (Josef Bacik)
- Btrfs: only set cache_generation if we setup the block group (Josef Bacik)
  Mason)
- Btrfs: deal with NULL srv_rsv in the delalloc inode reservation code (Chris
- Btrfs: unplug every once and a while (Chris Mason)
- scsi: qla_isr.c: fix comment typo 'hammmer' (Justin P. Mattock)
- qla2xxx: Basic infrastructure for dynamic logging. (Saurav Kashyap)
  Kashyap)
- qla2xxx: Code changes to support new dynamic logging infrastructure. (Saurav
- qla2xxx: Cleanup of previous infrastructure. (Saurav Kashyap)
- qla2xxx: T10 DIF - Handle uninitalized sectors. (Arun Easi)
- qla2xxx: T10 DIF - Fix incorrect error reporting. (Arun Easi)
- qla2xxx: Fix qla24xx revision check while enabling interrupts. (Chad Dupuis)
- qla2xxx: Acquire hardware lock while manipulating dsd list. (Saurav Kashyap)
  (Chad Dupuis)
- qla2xxx: Double check for command completion if abort mailbox command fails.
  (Saurav Kashyap)
- qla2xxx: Save and restore irq in the response queue interrupt handler.
- qla2xxx: Set the task attributes after memsetting fcp cmnd. (Saurav Kashyap)
- qla2xxx: Update version number to 8.03.07.07-k. (Chad Dupuis)
  (Giridhar Malavali)
- qla2xxx: Add support for ISP82xx to capture dump (minidump) on failure.
- qla2xxx: Implemeted beacon on/off for ISP82XX. (Saurav Kashyap)
  (Saurav Kashyap)
- qla2xxx: Prevent CPU lockups when "ql2xdontresethba" module param is set.
  be changed dynamically. (Chad Dupuis)
- qla2xxx: Enable write permission to some debug related module parameters to
  Kashyap)
- qla2xxx: check for marker IOCB during response queue processing. (Saurav
- qla2xxx: Fix array out of bound warning. (Saurav Kashyap)
- qla2xxx: During loopdown perform Diagnostic loopback. (Saurav Kashyap)
- qla2xxx: Correction to sysfs edc interface. (Joe Carnuccio)
- qla2xxx: Provide method for updating I2C attached VPD. (Joe Carnuccio)
- qla2xxx: Return sysfs error codes appropriate to conditions. (Joe Carnuccio)
  ISP82xx. (Giridhar Malavali)
- qla2xxx: Issue mailbox command only when firmware hung bit is reset for
- qla2xxx: Fix "active_mask" may be used uninitialized warning. (Chad Dupuis)
- scsi: fix qla2xxx printk format warning (Randy Dunlap)
- qla2xxx: Fix crash in qla2x00_abort_all_cmds() on unload (Roland Dreier)
- qla2xxx: Correct inadvertent clearing of RISC_INTR status. (Andrew Vasquez)
- qla2xxx: Remove qla2x00_wait_for_loop_ready function. (Saurav Kashyap)
- qla2xxx: Check for SCSI status on underruns. (Arun Easi)
- qla2xxx: Don't call alloc_fw_dump for ISP82XX. (Saurav Kashyap)
  Malavali)
- qla2xxx: Revert back the request queue mapping to request queue 0. (Giridhar
  interrupt mode during firmware hang. (Giridhar Malavali)
- qla2xxx: Stop unconditional completion of mailbox commands issued in
  Malavali)
- qla2xxx: Enable Minidump by default with default capture mask 0x1f. (Giridhar
  recovery. (Andrew Vasquez)
- qla2xxx: Return the correct value for a mailbox command if 82xx is in reset
- qla2xxx: Display IPE error message for ISP82xx. (Chad Dupuis)
- qla2xxx: Correct fc_host port_state display. (Saurav Kashyap)
  0. (Giridhar Malavali)
- qla2xxx: Submit all chained IOCBs for passthrough commands on request queue
- qla2xxx: Update version number to 8.03.07.12-k. (Chad Dupuis)
- qla2xxx: Use less stack to emit logging messages. (Joe Perches)
  current broken uses as appropriate. (Joe Perches)
- qla2xxx: Make the logging functions verify their arguments and fixed the
- qla2xxx: Update to dynamic logging. (Chad Dupuis)
  (Giridhar Malavali)
- qla2xxx: Proper cleanup of pass through commands when firmware returns error.
- qla2xxx: Only read requested mailbox registers. (Andrew Vasquez)
- qla2xxx: Limit excessive DPC cycles. (Andrew Vasquez)
- qla2xxx: Fix to include FCE data as part of dump. (Giridhar Malavali)
- qla2xxx: Correct report-id acquisition check (Giridhar Malavali)
- qla2xxx: Corrections to returned sysfs error codes. (Joe Carnuccio)
  mask. (Giridhar Malavali)
- qla2xxx: Corrected the default setting of the help text of Minidump capture
  (Giridhar Malavali)
- qla2xxx: Corrected the display of firmware dump availability for ISP82xx.
  (Giridhar Malavali)
- qla2xxx: Added a new entry to ISP specific function pointers structure.
- qla2xxx: Process marker IOCB request on request queue 0. (Giridhar Malavali)
- qla2xxx: Consolidated IOCB processing routines. (Giridhar Malavali)
- qla2xxx: Implement FCP priority tagging for 82xx adapters. (Saurav Kashyap)
  (Andrew Vasquez)
- qla2xxx: Ensure there's enough request-queue space for passthru IOCBs.
  Dupuis)
- qla2xxx: Move initialization of some variables before iospace_config. (Chad
  (Chad Dupuis)
- qla2xxx: Do not check for minidump when device state is QLA82XX_DEV_READY.
- SCSI, qla2xxx: remove redundant semicolon (Jesper Juhl)
  Anderson)
- be2iscsi 4.1.239.0 [PATCH 01/10]   Remove host and session casts (Chuck
  (Chuck Anderson)
- be2iscsi 4.1.239.0 [PATCH 02/10]  Fixing the /proc/interrupts problem V3
- be2iscsi 4.1.239.0 [PATCH 03/10]  Adding a shutdown Routine (Chuck Anderson)
- be2iscsi 4.1.239.0 [PATCH 04/10]  Add pci_disable device (Chuck Anderson)
- be2iscsi 4.1.239.0 [PATCH 05/10]  Fix for kdump failure (Chuck Anderson)
  Anderson)
- be2iscsi 4.1.239.0 [PATCH 06/10]  Fix for wrong dmsg setting in wrb (Chuck
  earlier (Chuck Anderson)
- be2iscsi 4.1.239.0 [PATCH 07/10]  Fix for case where task->sc was cleanedup
  Anderson)
- be2iscsi 4.1.239.0 [PATCH 08/10]   memset wrb for ring create (Chuck
  Anderson)
- be2iscsi 4.1.239.0 [PATCH 09/10]  Move driver Version to 4.1.239.0 (Chuck
  (Chuck Anderson)
- be2iscsi 4.1.239.0 [PATCH 10/10]  Fix in the ASYNC PDU handling code path.
- qlcnic driver v5.0.25.1 for UEK2 2.6.39 (Chuck Anderson)
* Wed Nov 16 2011 Maxim Uvarov <maxim.uvarov@oracle.com> [2.6.39-100.0.17.el5uek]
- Update Btrfs
- turn on CONFIG_PARAVIRT_SPINLOCKS for bare metal
- xen-gntalloc: signedness bug in add_grefs()
- xen-gntalloc: integer overflow in gntalloc_ioctl_alloc()
- xen-gntdev: integer overflow in gntdev_alloc_map()
- xen:pvhvm: enable PVHVM VCPU placement when using more than 32 CPUs.
- xen/balloon: Avoid OOM when requesting highmem
- xen: Remove hanging references to CONFIG_XEN_PLATFORM_PCI

* Fri Nov 11 2011 Maxim Uvarov <maxim.uvarov@oracle.com> [2.6.39-100.0.16.el5uek]
- SPEC: fixes for spec file [orabugs 13359985, 13339700, 13348381]
- config: enable IP_PNP
- Merge branch 'uek2-merge' of git://oss.oracle.com/git/kwilk/xen into uek2-stable
- ocfs2: Fix cleancache initialization call to correctly pass uuid
- Merge branch 'stable/xen-block.rebase' into uek2-merge
- xen/blkback: Fix two races in the handling of barrier requests.
- xen/blkback: Check for proper operation.
- xen/blkback: Fix the inhibition to map pages when discarding sector ranges.
- xen/blkback: Report VBD_WSECT (wr_sect) properly.
- xen/blkback: Support 'feature-barrier' aka old-style BARRIER requests.
- xen-blkfront: plug device number leak in xlblk_init() error path
- xen-blkfront: If no barrier or flush is supported, use invalid operation.
- xen-blkback: use kzalloc() in favor of kmalloc()+memset()
- xen-blkback: fixed indentation and comments
- xen-blkfront: fix a deadlock while handling discard response
- xen-blkfront: Handle discard requests.
- xen-blkback: Implement discard requests ('feature-discard')
- xen-blkfront: add BLKIF_OP_DISCARD and discard request struct
- xen/blkback: Add module alias for autoloading
- xen/blkback: Don't let in-flight requests defer pending ones.
- Merge branch 'stable/xen-settime' into uek2-merge
- Merge branch 'stable/e820-3.2.rebased' into uek2-merge
- Merge branch 'stable/mmu.fixes.rebased' into uek2-merge
- Merge branch 'stable/drivers-3.2.rebased' into uek2-merge
- Merge branch 'stable/cleanups-3.2.rebased' into uek2-merge
- Merge branch 'stable/pci.fixes-3.2' of git://oss.oracle.com/git/kwilk/xen into uek2-merge
- Merge branch 'stable/bug.fixes-3.2.rebased' of git://oss.oracle.com/git/kwilk/xen into uek2-merge
- Merge branch 'stable/xen-pciback-0.6.3.bugfixes' of git://oss.oracle.com/git/kwilk/xen into uek2-merge
- xen/irq: If we fail during msi_capability_init return proper error code.
- xen: remove XEN_PLATFORM_PCI config option
- xen: XEN_PVHVM depends on PCI
- xen/p2m/debugfs: Make type_name more obvious.
- xen/p2m/debugfs: Fix potential pointer exception.
- xen/enlighten: Fix compile warnings and set cx to known value.
- xen/xenbus: Remove the unnecessary check.
- xen/events: Don't check the info for NULL as it is already done.
- xen/pci: Use 'acpi_gsi_to_irq' value unconditionally.
- xen/pci: Remove 'xen_allocate_pirq_gsi'.
- xen/pci: Retire unnecessary #ifdef CONFIG_ACPI
- xen/pci: Move the allocation of IRQs when there are no IOAPIC's to the end
- xen/pci: Squash pci_xen_initial_domain and xen_setup_pirqs together.
- xen/pci: Use the xen_register_pirq for HVM and initial domain users
- xen/pci: In xen_register_pirq bind the GSI to the IRQ after the hypercall.
- xen/pci: Provide #ifdef CONFIG_ACPI to easy code squashing.
- xen/pci: Update comments and fix empty spaces.
- xen/pci: Shuffle code around.
- xen/dom0: set wallclock time in Xen
- xen: add dom0_op hypercall
- xen/acpi: Domain0 acpi parser related platform hypercall
- xen: release all pages within 1-1 p2m mappings
- xen: allow extra memory to be in multiple regions
- xen: allow balloon driver to use more than one memory region
- xen/balloon: simplify test for the end of usable RAM
- xen/balloon: account for pages released during memory setup
- xen/e820: if there is no dom0_mem=, don't tweak extra_pages.
- Revert "xen/e820: if there is no dom0_mem=, don't tweak extra_pages."
- xen/e820: if there is no dom0_mem=, don't tweak extra_pages.
- xen: use maximum reservation to limit amount of usable RAM
- xen: Fix misleading WARN message at xen_release_chunk
- xen: Fix printk() format in xen/setup.c
- xen/gntdev: Fix sleep-inside-spinlock
- xen: modify kernel mappings corresponding to granted pages
- xen: add an "highmem" parameter to alloc_xenballooned_pages
- xen/p2m: Use SetPagePrivate and its friends for M2P overrides.
- xen/p2m: Make debug/xen/mmu/p2m visible again.
- Revert "xen/debug: WARN_ON when identity PFN has no _PAGE_IOMAP flag set."
- xen/pciback: Check if the device is found instead of blindly assuming so.
- xen/pciback: Do not dereference psdev during printk when it is NULL.
- xen/pciback: double lock typo
- xen/pciback: use mutex rather than spinlock in vpci backend
- xen/pciback: Use mutexes when working with Xenbus state transitions.
- xen/pciback: miscellaneous adjustments
- xen/pciback: use mutex rather than spinlock in passthrough backend
- xen/pciback: use resource_size()
- xen: use static initializers in xen-balloon.c
- Xen: fix braces and tabs coding style issue in xenbus_probe.c
- Xen: fix braces coding style issue in xenbus_probe.h
- Xen: fix whitespaces,tabs coding style issue in drivers/xen/pci.c
- Xen: fix braces coding style issue in gntdev.c and grant-table.c
- Xen: fix whitespaces,tabs coding style issue in drivers/xen/events.c
- Xen: fix whitespaces,tabs coding style issue in drivers/xen/balloon.c

* Wed Oct 19 2011 Joe Jin <joe.jin@oracle.com> [2.6.39-100.0.15.el5uek]
- [scsi] cciss: Use cciss for some Smart Array controller when build for OL5
- [Kconfig]: Add CONFIG_UEK5 option.

* Wed Oct 12 2011 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.14.el5uek]
- Apply signature checking to modules on module load (David Howells)
- Don't include .note.gnu.build-id in the digest (David Howells)
- config: turn on module sign (Maxim Uvarov)
- fix modpost port bug for module signatures  (Maxim Uvarov)
- xen: Fix selfballooning and ensure it doesn't go too far (Dan Magenheimer)
- config: disable XEN_BALLOON_MEMORY_HOTPLUG
- apic, i386/bigsmp: Fix false warnings regarding logical APIC ID mismatches (Jan Beulich)

* Mon Oct 10 2011 Maxim Uvarov <maxim.uvarov@oracle.com> [2.6.39-100.0.13.el5uek]
- fix btrfs compilation for 32 bit
- ext4 turn on CONFIG_LBDAF for 32bit kernel [orabug 12965485]
- exec: do not call request_module() twice from search_binary_handler()
- merge 3.0.6 patches

* Thu Sep 29 2011 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.12.el5uek]
- audit: dynamically allocate audit_names when not enough spaceis in the names array [orabug 13038425]
- update btrfs 3.0

* Wed Sep 28 2011 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.11.el5uek]
- xen:  Add  bootmem.h in xen-selfballoon.c

* Wed Sep 28 2011 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.10.el5uek]
- mpt2sas: Add a module parameter that permits overriding protection capabilities
- mpt2sas: Return the correct sense key for DIF errors
- mpt2sas: Do not check DIF for unwritten blocks
- Revert "xen/e820: if there is no dom0_mem=, don't tweak extra_pages."
- Revert "[AUDIT/workaround] Increase AUDIT_NAMES array len" [orabug 13034299]
- block: Rate-limit failed I/O error message  [orabug 13007648]
- config: disable panic on hardlockup [orabug 13007648]
- ocfs2: update ocfs2 version [orabug 13017352]
- x86/paravirt: PTE updates in k(un)map_atomic need to be synchronous, regardless of lazy_mmu mode
- bnx2x: prevent flooded warnning kernel info [orabug 12687487]
- tg3: Dont dump DMA error when interface not ready [orabug 12981473]
- xen: Fix selfballooning and ensure it doesn't go too far
- ocfs2: Add datavolume mount option [orabug 13017352]

* Thu Sep 22 2011 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.9
.el5uek]
- generate -paravirt configs more accurate [orabug 13002151]
- radeon: add missed firmwares [orabug 12981553]
- ksplice: Clear garbage data on the kernel stack when handling signals
- Add devel headers [orabug 13000607]

* Wed Sep 21 2011 Kevin Lyons [2.6.39-100.0.8.el5uek]
- Add -u parameter to kernel_variant_post to make it work
  properly for uek [orabug 12965870]

* Tue Sep 20 2011 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.7.el5uek]
- fix --noarch build
- CONFIG: Add support for Large files - 32bit orabug 12984979

* Mon Sep 19 2011 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.6.el5uek]
- rebase to 3.0.4
- Build paravirt and paravirt-debug kernels
- Remove commented out patches from spec
- Specfile: build OCFS2
- make XEN_MAX_DOMAIN_MEMORY selectable
- config-debug: enable LOCKDEP and more debug options
- Turn on CONFIG_CRYPTO_FIPS (Maxim Uvarov)
- CONFIG: enable sysfs(el5) and xen memory hotplug
- scsi: bump up SD_MAX_DISKS (Dave Kleikamp)
- x86, acpi: Handle xapic/x2apic entries in MADT at same time (Yinghai Lu)

* Tue Sep 13 2011 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.5.el5uek]
- xen: allow enable use of VGA console on dom0
- xen: prepare tmem shim to handle frontswap
- xen: Add __attribute__((format(printf... where appropriate
- xen: Populate xenbus device attributes
- xen: Add module alias to autoload backend drivers
- xen: tmem: self-ballooning and frontswap-selfshrinking
- xen/pci: Shuffle code around.
- xen/pci: Update comments and fix empty spaces.
- xen/pci: Provide #ifdef CONFIG_ACPI to easy code squashing.
- xen/pci: In xen_register_pirq bind the GSI to the IRQ after the hypercall.
- xen/pci: Use the xen_register_pirq for HVM and initial domain users
- xen/pci: Squash pci_xen_initial_domain and xen_setup_pirqs together.
- xen/pci: Move the allocation of IRQs when there are no IOAPIC's to the end
- xen/pci: Retire unnecessary #ifdef CONFIG_ACPI
- xen/pci: Remove 'xen_allocate_pirq_gsi'.
- xen/pci: Use 'acpi_gsi_to_irq' value unconditionally.
- xen/pciback: xen pci backend driver.
- xen/pciback: Cleanup the driver based on checkpatch warnings and errors.
- xen/pciback: Register the owner (domain) of the PCI device.
- xen/pciback: guest SR-IOV support for PV guest
- xen/pciback: Disable MSI/MSI-X when reseting a device
- xen/pciback: Allocate IRQ handler for device that is shared with guest.
- xen/pciback: Fine-grain the spinlocks and fix BUG: scheduling while atomic cases.
- xen: rename pciback module to xen-pciback.
- xen/pciback: Don't setup an fake IRQ handler for SR-IOV devices.
- xen/pciback: Print out the MSI/MSI-X (PIRQ) values
- xen/pciback: Drop two backends, squash and cleanup some code.
- xen/pciback: Remove the DEBUG option.
- xen/pciback: Have 'passthrough' option instead of XEN_PCIDEV_BACKEND_PASS and XEN_PCIDEV_BACKEND_VPCI
- mm: frontswap: swap data structure changes
- mm: frontswap: core code
- mm: frontswap: add swap hooks and extend try_to_unuse
- mm: frontswap: config and doc files
- xen:pvhvm: Modpost section mismatch fix
- xen/pciback: remove duplicated #include
- trace/xen: add skeleton for Xen trace events
- xen/multicalls: remove debugfs stats
- xen/trace: set up tracepoint skeleton
- xen/trace: add multicall tracing
- xen/trace: add mmu tracepoints
- xen/trace: add ptpage alloc/release tracepoints
- xen/trace: add xen_pgd_(un)pin tracepoints
- xen/trace: add segment desc tracing
- xen/trace: add tlb flush tracepoints
- xen/mmu: use extend_args for more mmuext updates
- xen/mmu: tune pgtable alloc/release
- xen/multicalls: disable MC_DEBUG
- xen/multicalls: add unlikely around slowpath in __xen_mc_entry()
- xen/multicall: special-case singleton hypercalls
- xen/multicall: move *idx fields to start of mc_buffer
- xen/trace: convert mmu events to use DECLARE_EVENT_CLASS()/DEFINE_EVENT()
- xen/trace: use class for multicall trace
- xen/tracing: fix compile errors when tracing is disabled.
- xen/tracing: it looks like we wanted CONFIG_FTRACE
- xen/trace: Fix compile error when CONFIG_XEN_PRIVILEGED_GUEST is not set
- xen/tracing: Fix tracing config option properly
- Input: xen-kbdfront - enable driver for HVM guests
- xen/balloon: memory hotplug support for Xen balloon driver
- mm: extend memory hotplug API to allow memory hotplug in virtual machines
- xen/blkback: Add module alias for autoloading
- xen/blkback: Don't let in-flight requests defer pending ones.
- xen/netback: Add module alias for autoloading
- xen: convert to 64 bit stats interface
- xen/balloon: Fix compile errors - missing header files.
- xen/self-balloon: Add dependency on tmem.
- xen: xen-selfballoon.c needs more header files
- xen/grant: Fix compile warning.
- xen: Fix printk() format in xen/setup.c
- xen: Fix misleading WARN message at xen_release_chunk
- xen/x86: replace order-based range checking of M2P table by linear one
- xen: Do not enable PV IPIs when vector callback not present
- xen-blkfront: Fix one off warning about name clash
- xen-blkfront: Drop name and minor adjustments for emulated scsi devices
- xen/blkback: Make description more obvious.
- xen-blkback: fixed indentation and comments
- SCSI: Fix oops dereferencing queue
- xen: use maximum reservation to limit amount of usable RAM
- xen: x86_32: do not enable iterrupts when returning from exception in interrupt context
- xen/smp: Warn user why they keel over - nosmp or noapic and what to use instead.
- xen: disable PV spinlocks on HVM
- xen/e820: if there is no dom0_mem=, don't tweak extra_pages.
- config: from 6.1 and review
- Revert "IPC reduce lock contention in semctl"
- Revert "IPC lock reduction corners"
- Revert "use rwlocks for ipc"
- Revert "ipc semaphores: order wakeups based on waiter CPU"
- Revert "ipc semaphores: reduce ipc_lock contention in semtimedop

* Thu Aug 25 2011 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.4.el5uek]
- revert makefile to 2.6.39

* Wed  Aug 24 2011 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.3.el5uek]
- Rebase to linux-3.0.3

* Thu  Aug 11 2011 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.2.el5uek]
- [AUDIT/workaround] Increase AUDIT_NAMES array len (Maxim Uvarov)
- Increase kernel log buffer to 1MB (SHIFT=20)
- export list of msi irqs into sysfs (Chris Mason)
- memcg: mark init_section_page_cgroup() properly (Namhyung Kim)
- memcg: fix init_page_cgroup nid with sparsemem (KAMEZAWA Hiroyuki)

* Thu Jul 29 2011 Guru Anbalagane <guru.anbalagane@oracle.com> [2.6.39-100.0.1.el5uek]
- Linux 2.6.39.3
- ipc semaphores: reduce ipc_lock contention in semtimedop
- ipc semaphores: order wakeups based on waiter CPU
- use rwlocks for ipc
- IPC lock reduction corners
- IPC reduce lock contention in semctl
- Batched wakeups from ipc
