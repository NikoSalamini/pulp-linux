################################################################################
#
# memstresser
#
################################################################################

MEMSTRESSER_VERSION = 1.0
MEMSTRESSER_LICENSE = Apache-2.0
MEMSTRESSER_LICENSE_FILES = COPYING
MEMSTRESSER_SITE = "$(BR2_EXTERNAL_CVA6_LINUX_PATH)/package/memstresser/src"
MEMSTRESSER_SITE_METHOD = local

define MEMSTRESSER_BUILD_CMDS
    $(MAKE) CC="$(TARGET_CC)" LD="$(TARGET_LD)" -C $(@D) memstresser
endef

define MEMSTRESSER_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/memstresser $(TARGET_DIR)/usr/bin
endef

$(eval $(generic-package))