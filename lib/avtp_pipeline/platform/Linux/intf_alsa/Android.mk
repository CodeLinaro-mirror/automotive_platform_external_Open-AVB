LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
        external/tinyalsa/include

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_C_INCLUDES)

LOCAL_SRC_FILES := openavb_intf_tinyalsa.c

LOCAL_MODULE := libopenavb_intf_tinyalsa

LOCAL_REQUIRED_MODULES := tinyalsa_talker.ini tinyalsa_listener.ini

LOCAL_SHARED_LIBRARIES := libopenavb \
        libopenavb_map_aaf_audio \
        libopenavb_map_uncmp_audio \
        libtinyalsa

LOCAL_MODULE_PATH_64 := $(TARGET_OUT_VENDOR)/lib64

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE_CLASS := DATA
LOCAL_MODULE := tinyalsa_talker.ini
LOCAL_SRC_FILES := $(LOCAL_MODULE)
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/etc
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE_CLASS := DATA
LOCAL_MODULE := tinyalsa_listener.ini
LOCAL_SRC_FILES := $(LOCAL_MODULE)
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/etc
include $(BUILD_PREBUILT)
