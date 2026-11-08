LOCAL_PATH := $(call my-dir)

# The 2018 WFD AAC wrapper is NOT an Android17 Miracast service.
# Avoid unverified audio/vendor display dependencies for mondrian. Keep the
# original QSSI/QMAA/low-RAM selection intact for other historic targets.
ifneq ($(TARGET_DEVICE),mondrian)

ifneq ($(filter $(QSSI_SUPPORTED_PLATFORMS),$(TARGET_BOARD_PLATFORM)),)

QMAA_DISABLES_WFD := false
ifeq ($(TARGET_USES_QMAA),true)
ifneq ($(TARGET_USES_QMAA_OVERRIDE_WFD),true)
QMAA_DISABLES_WFD := true
endif #TARGET_USES_QMAA_OVERRIDE_WFD
endif #TARGET_USES_QMAA

WFD_DISABLE_PLATFORM_LIST := neo

#Disable WFD for selected 32-bit targets
ifeq ($(call is-board-platform,bengal),true)
ifeq ($(TARGET_BOARD_SUFFIX),_32)
WFD_DISABLE_PLATFORM_LIST += bengal
endif
endif

ifneq ($(call is-board-platform-in-list,$(WFD_DISABLE_PLATFORM_LIST)),true)
ifneq ($(TARGET_HAS_LOW_RAM), true)
ifneq ($(QMAA_DISABLES_WFD),true)
include $(call all-makefiles-under, $(LOCAL_PATH))
endif #QMAA_DISABLES_WFD
endif #TARGET_HAS_LOW_RAM
endif #WFD_DISABLE_PLATFORM_LIST

endif

endif # TARGET_DEVICE != mondrian
