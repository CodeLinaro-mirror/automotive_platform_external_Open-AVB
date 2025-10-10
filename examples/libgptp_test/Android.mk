LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES += \
     libgptp_test.cpp \

LOCAL_MODULE:= libgptp_test

LOCAL_C_INCLUDES:= external/open-avb/lib/libgptp/
LOCAL_SHARED_LIBRARIES:= libgptp
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Wno-unused-parameter -Wno-unused-variable

ifeq ($(call is-board-platform,msmnile),true)
ifeq ($(TARGET_BOARD_SUFFIX),_gvmq)
LOCAL_CFLAGS += -DAVB_FEATURE_GVM_MODE=1
endif
endif

ifeq ($(call is-board-platform,msmnile),true)
ifeq ($(TARGET_BOARD_SUFFIX),_gvm)
LOCAL_CFLAGS += -DAVB_FEATURE_GVM_MODE=1
endif
endif

LOCAL_CFLAGS += -DANDROID
ifeq ($(TARGET_BOARD_SUFFIX),_gvm)
ifneq ($(TARGET_BOARD_DERIVATIVE_SUFFIX),_cdccomm)
ifneq (gen5_gvm_gy, $(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX))
ifneq (gen5_gvm, $(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX))
LOCAL_CFLAGS += -DAVB_FEATURE_GVM_MODE=1
endif
endif
endif
endif

LOCAL_SHARED_LIBRARIES += \
     libcutils \
     liblog

LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/bin

include $(BUILD_EXECUTABLE)
