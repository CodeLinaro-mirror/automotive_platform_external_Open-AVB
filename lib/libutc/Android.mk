LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        utc_helper.cpp\

LOCAL_SHARED_LIBRARIES := libcutils liblog

LOCAL_C_INCLUDES += \
        $(LOCAL_PATH) \
	external/open-avb/daemons/utc_ts/ \

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_C_INCLUDES)

LOCAL_CFLAGS += -Wno-multichar -Werror -Wall -Wno-unused-parameter

LOCAL_CFLAGS += -DANDROID -DSYSTEMD

LOCAL_CLANG := true

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= libutc

LOCAL_MODULE_PATH_64 := $(TARGET_OUT_VENDOR)/lib64

include $(BUILD_SHARED_LIBRARY)

