LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_C_INCLUDES)

LOCAL_SRC_FILES := #openavb_intf_mjpeg_opengl.c

LOCAL_MODULE := libopenavb_intf_mjpeg_opengl

LOCAL_SHARED_LIBRARIES := #libopenavb libopenavb_map_mjpeg libmjpegavbsink

LOCAL_REQUIRED_MODULES := mjpeg_opengl_listener.ini

LOCAL_MODULE_PATH_64 := $(TARGET_OUT_VENDOR)/lib64

include $(BUILD_SHARED_LIBRARY)
include $(CLEAR_VARS)
LOCAL_MODULE_CLASS := DATA
LOCAL_MODULE := mjpeg_opengl_listener.ini
LOCAL_SRC_FILES := $(LOCAL_MODULE)
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/etc
include $(BUILD_PREBUILT)
