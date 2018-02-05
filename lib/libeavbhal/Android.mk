LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CFLAGS := -Wall -Wextra -Wno-unused-parameter
LOCAL_LDFLAGS += -Wl,--export-dynamic

LOCAL_SRC_FILES:= \
    audio_eavb_hw_dev.cpp \
    audio_eavb_hw_in.cpp \
    audio_eavb_hw_out.cpp \
    audio_eavb_hw_ctx.cpp

LOCAL_SHARED_LIBRARIES := \
            liblog \
            libutils

LOCAL_C_INCLUDES:= \
        $(LOCAL_PATH)

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_C_INCLUDES)

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= audio.eavb.default
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_SHARED_LIBRARY)

