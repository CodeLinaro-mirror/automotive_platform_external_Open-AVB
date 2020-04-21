LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_C_INCLUDES)

LOCAL_CFLAGS := -Wno-unused-parameter

LOCAL_SRC_FILES := openavb_intf_logger.c

LOCAL_MODULE := libopenavb_intf_logger

LOCAL_SHARED_LIBRARIES := libopenavb

LOCAL_MODULE_PATH_64 := $(TARGET_OUT_VENDOR)/lib64

include $(BUILD_SHARED_LIBRARY)
