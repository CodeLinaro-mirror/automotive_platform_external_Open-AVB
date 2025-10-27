LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CFLAGS := -DWITHOUT_IFADDRS -Wno-unused-parameter -frtti -Wno-unused-private-field
LOCAL_CFLAGS += -DPTP_SW_QTIMER=1 -DSYSTEMD

LOCAL_C_INCLUDES := $(LOCAL_PATH)/common \
                    $(LOCAL_PATH)/linux/src

LOCAL_SHARED_LIBRARIES += liblog libutils libcutils

LOCAL_SRC_FILES := linux/src/daemon_cl.cpp \
                   common/ptp_message.cpp \
                   common/avbts_osnet.cpp \
                   common/ap_message.cpp \
                   common/common_port.cpp \
                   common/ether_port.cpp \
                   common/ieee1588clock.cpp \
                   common/gptp_cfg.cpp \
                   common/gptp_log.cpp \
                   common/ini.c \
                   linux/src/linux_hal_common.cpp \
                   linux/src/linux_hal_persist_file.cpp \
                   linux/src/platform.cpp \
                   linux/src/linux_hal_generic.cpp \
                   linux/src/linux_hal_generic_adj.cpp \
                   linux/src/qgptp_rmgr.cpp

LOCAL_MODULE := qgptp

ifeq ($(TARGET_BOARD_DERIVATIVE_SUFFIX),_cdccomm)
LOCAL_INIT_RC := gptp_daemon.rc
endif

ifeq (gen5_gvm_gy, $(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX))
LOCAL_INIT_RC := gptp_daemon.rc
endif

ifeq (gen5_gvm, $(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX))
LOCAL_INIT_RC := gptp_daemon.rc
endif

LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/bin

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE_CLASS := DATA
LOCAL_MODULE := gptp_cfg.ini
ifeq ($(TARGET_BOARD_DERIVATIVE_SUFFIX),_cdccomm)
LOCAL_SRC_FILES := gptp_cfg_au.ini
else ifeq (gen5_gvm_gy, $(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX))
LOCAL_SRC_FILES := gptp_cfg_au_nord.ini
else ifeq (gen5_gvm, $(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX))
LOCAL_SRC_FILES := gptp_cfg_au_nord.ini
else
LOCAL_SRC_FILES := $(LOCAL_MODULE)
endif
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/etc
include $(BUILD_PREBUILT)
