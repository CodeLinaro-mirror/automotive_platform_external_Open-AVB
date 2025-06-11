LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES += \
     libutc_test.cpp \

LOCAL_MODULE:= libutc_test

LOCAL_C_INCLUDES:= external/open-avb/lib/libutc/
LOCAL_SHARED_LIBRARIES:= libutc
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Wno-unused-parameter -Wno-unused-variable
LOCAL_CFLAGS += -DANDROID

LOCAL_SHARED_LIBRARIES += \
     libcutils \
     liblog \
     libutc

LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/bin

include $(BUILD_EXECUTABLE)
