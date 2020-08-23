LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_C_INCLUDES)

LOCAL_SRC_FILES:= \
    AvbMjpegSink.cpp \
    GlSink.cpp \
    main.cpp \
    MjpegFrame.cpp


LOCAL_CFLAGS += -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES

ifeq ($(call is-platform-sdk-version-at-least,25),true)
LOCAL_CFLAGS += -DUSE_SK_CODEC
endif

ifeq ($(call is-platform-sdk-version-at-least,27),true)
LOCAL_CFLAGS += -DNO_SK_LOCK
endif

ifeq ($(call is-platform-sdk-version-at-least,28),true)
LOCAL_CFLAGS += -DSKIA_MAKE_FROM_DATA -DSURFACE_NO_GLOBAL_TRANSACTION
endif

ifeq ($(call is-platform-sdk-version-at-least,29),true)
	LOCAL_CFLAGS += -DPHYS_DISPLAY
endif

#Android R
ifneq ( ,$(filter R 11, $(PLATFORM_VERSION)))
LOCAL_CFLAGS += -DANDROID_R
endif

LOCAL_CFLAGS += -Wall -Werror -Wunused -Wunreachable-code

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libEGL \
    libGLESv1_CM \
    libgui \
    liblog

ifeq ($(call is-platform-sdk-version-at-least,28),true)
#workaround to link to libskia
LOCAL_SHARED_LIBRARIES += libhwui libui
else
LOCAL_SHARED_LIBRARIES += libskia
endif

LOCAL_MODULE:= libmjpegavbsink

include $(BUILD_SHARED_LIBRARY)
