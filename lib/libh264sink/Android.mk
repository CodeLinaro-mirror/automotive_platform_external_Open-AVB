LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        Avbh264Stream.cpp\

LOCAL_SHARED_LIBRARIES := \
        libstagefright liblog libutils libbinder libstagefright_foundation \
        libmedia libgui libcutils libui

LOCAL_C_INCLUDES:= \
        $(LOCAL_PATH) \
        frameworks/av/media/libstagefright \
        $(TOP)/frameworks/native/include/media/openmax

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_C_INCLUDES)

LOCAL_CFLAGS += -Wno-multichar -Werror -Wall
LOCAL_CLANG := true

#Android R
ifneq ( ,$(filter R 11, $(PLATFORM_VERSION)))
LOCAL_HEADER_LIBRARIES := libmediadrm_headers libmediametrics_headers
LOCAL_CFLAGS += -DANDROID_R
endif

ifeq ($(call is-platform-sdk-version-at-least,25),true)
LOCAL_CFLAGS += -DUSE_MEDIA_CODEC_BUFFER
endif

ifeq ($(call is-platform-sdk-version-at-least,28),true)
LOCAL_CFLAGS += -DSURFACE_NO_GLOBAL_TRANSACTION
LOCAL_SHARED_LIBRARIES += libmedia_omx
endif

ifeq ($(call is-platform-sdk-version-at-least,29),true)
	LOCAL_CFLAGS += -DPHYS_DISPLAY
endif

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= libh264sink

include $(BUILD_SHARED_LIBRARY)

