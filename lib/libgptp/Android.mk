LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        gptp_helper.cpp\

LOCAL_SHARED_LIBRARIES := libcutils

LOCAL_C_INCLUDES += \
        $(LOCAL_PATH) \
		external/open-avb/daemons/gptp/linux/src/ \
		external/open-avb/daemons/gptp/common

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_C_INCLUDES)

LOCAL_CFLAGS += -Wno-multichar -Werror -Wall -Wno-unused-parameter
ifeq ($(call is-board-platform,msmnile),true)
ifeq ($(TARGET_BOARD_SUFFIX),_gvmq)
LOCAL_CFLAGS += -DAVB_FEATURE_GVM_MODE=1

LOCAL_SHARED_LIBRARIES := libuhab libion
endif
endif
LOCAL_CLANG := true

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= libgptp

LOCAL_MODULE_PATH_64 := $(TARGET_OUT_VENDOR)/lib64

include $(BUILD_SHARED_LIBRARY)

