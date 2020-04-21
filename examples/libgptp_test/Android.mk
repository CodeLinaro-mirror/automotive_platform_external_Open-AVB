LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES += \
     libgptp_test.cpp \

LOCAL_MODULE:= libgptp_test

LOCAL_C_INCLUDES:= external/open-avb/lib/libgptp/
LOCAL_SHARED_LIBRARIES:= libgptp
LOCAL_MODULE_TAGS := optional debug
LOCAL_CFLAGS := -Wno-unused-parameter -Wno-unused-variable

LOCAL_SHARED_LIBRARIES += \
     libcutils \

LOCAL_MODULE_PATH_64 := $(TARGET_OUT_VENDOR)/lib64

include $(BUILD_EXECUTABLE)
