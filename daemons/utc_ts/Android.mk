LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CFLAGS := -Wno-unused-parameter -frtti -Wno-unused-private-field

LOCAL_C_INCLUDES:= external/open-avb/lib/libgptp/ \
                   $(LOCAL_PATH)

LOCAL_SHARED_LIBRARIES += liblog libutils libcutils libcap

ifeq ($(TARGET_BOARD_SUFFIX),_gvm)

LOCAL_SHARED_LIBRARIES += libuhab libion libgptp

LOCAL_SRC_FILES := utc_ts.cpp

LOCAL_MODULE := utc_ts

LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/bin

ifneq (,$(filter _cdccomm _cdcsdv,$(TARGET_BOARD_DERIVATIVE_SUFFIX)))
LOCAL_INIT_RC := utc_daemon.rc
endif

include $(BUILD_EXECUTABLE)

endif


