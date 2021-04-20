LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CFLAGS := -Wall -Wextra -Wno-unused-parameter

LOCAL_C_INCLUDES := $(LOCAL_PATH)

LOCAL_SRC_FILES := \
    WAVFileWriter.cpp \
    EAVBListenerStream.cpp \
    EAVBRecord.cpp

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libhardware

LOCAL_MODULE := EAVBRecord
LOCAL_REQUIRED_MODULES := audio.eavb.default
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/bin
LOCAL_MODULE_TAGS := optional debug

include $(BUILD_EXECUTABLE)
