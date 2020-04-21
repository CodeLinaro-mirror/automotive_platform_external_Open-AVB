LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=         \
	#AvbMpeg2tsStream.cpp \

LOCAL_SHARED_LIBRARIES := \
        #libstagefright liblog libutils libbinder libgui \
        libstagefright_foundation libmedia libcutils libui

LOCAL_C_INCLUDES:= \
        $(LOCAL_PATH) \
        frameworks/av/media/libstagefright \

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_C_INCLUDES)

LOCAL_CFLAGS += -Wall -Werror -Wunused -Wunreachable-code

ifeq ($(call is-platform-sdk-version-at-least,28),true)
LOCAL_CFLAGS += -DSURFACE_NO_GLOBAL_TRANSACTION -DOMX_SPLIT
endif

#LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= libmpeg2ts

LOCAL_MODULE_PATH_64 := $(TARGET_OUT_VENDOR)/lib64

include $(BUILD_SHARED_LIBRARY)


