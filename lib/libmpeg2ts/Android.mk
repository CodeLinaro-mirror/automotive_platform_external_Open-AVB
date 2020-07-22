LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=         \
	AvbMpeg2tsStream.cpp \

LOCAL_SHARED_LIBRARIES := \
        libstagefright liblog libutils libbinder libgui \
        libstagefright_foundation libmedia libcutils libui

LOCAL_C_INCLUDES:= \
        $(LOCAL_PATH) \
        frameworks/av/media/libstagefright \

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_C_INCLUDES)

LOCAL_CFLAGS += -Wall -Werror -Wunused -Wunreachable-code

ifeq ($(call is-platform-sdk-version-at-least,29),true)
LOCAL_CFLAGS += -DSURFACE_NO_GLOBAL_TRANSACTION -DPHYS_DISPLAY
else ifeq ($(call is-platform-sdk-version-at-least,28),true)
LOCAL_CFLAGS += -DSURFACE_NO_GLOBAL_TRANSACTION -DOMX_SPLIT
endif

#Android R
ifneq ( ,$(filter R 11, $(PLATFORM_VERSION)))
LOCAL_CFLAGS += -DANDROID_R
endif

#LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= libmpeg2ts

include $(BUILD_SHARED_LIBRARY)


