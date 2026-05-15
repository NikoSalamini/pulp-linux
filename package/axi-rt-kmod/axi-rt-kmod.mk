################################################################################
#
# axi-rt-kmod
#
################################################################################

AXI_RT_KMOD_VERSION = 1.0
AXI_RT_KMOD_SITE = $(BR2_EXTERNAL_CVA6_LINUX_PATH)/package/axi-rt-kmod
AXI_RT_KMOD_SITE_METHOD = local
AXI_RT_KMOD_LICENSE = GPL-2.0-or-later OR MIT
AXI_RT_KMOD_MODULE_SUBDIRS = src

$(eval $(kernel-module))
$(eval $(generic-package))