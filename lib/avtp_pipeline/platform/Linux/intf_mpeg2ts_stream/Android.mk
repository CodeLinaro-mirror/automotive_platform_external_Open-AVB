LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_C_INCLUDES)

LOCAL_SRC_FILES := #openavb_intf_mpeg2ts_stream.c

LOCAL_MODULE := libopenavb_intf_mpeg2ts_stream

LOCAL_SHARED_LIBRARIES := #libopenavb libopenavb_map_mpeg2ts libmpeg2ts

LOCAL_REQUIRED_MODULES := mpeg2ts_stream_listener.ini

LOCAL_MODULE_PATH_64 := $(TARGET_OUT_VENDOR)/lib64

include $(BUILD_SHARED_LIBRARY)
include $(CLEAR_VARS)
LOCAL_MODULE_CLASS := DATA
LOCAL_MODULE := mpeg2ts_stream_listener.ini
LOCAL_SRC_FILES := $(LOCAL_MODULE)
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/etc
include $(BUILD_PREBUILT)
