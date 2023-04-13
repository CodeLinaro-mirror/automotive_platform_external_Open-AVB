AVB_FEATURE_ENDPOINT ?= 1
AVB_FEATURE_GSTREAMER ?= 0
AVB_FEATURE_NEUTRINO ?= 0
AVB_FEATURE_INTF_ALSA2 ?= 1
AVB_FEATURE_GVM_MODE ?= 0

ifeq ($(AUDIO_INC_DIR),)
AUDIO_INC_DIR := "/usr/include/alsa"
endif

ifeq ($(AUDIO_LIB),)
AUDIO_LIB := asound
endif

.PHONY: all clean

all: build/Makefile
	$(MAKE) -s -C build install

doc: build/Makefile
	$(MAKE) -s -C build doc
	@echo "\n\nTo display documentation use:\n\n" \
	      "\txdg-open $(abspath build/documents/api_docs/index.html)\n"

clean:
	$(RM) -r build

build/Makefile:
	mkdir -p build && \
	cd build && \
	cmake -DCMAKE_TOOLCHAIN_FILE=../platform/Linux/arm_ntn_linux.cmake \
	      -DAVB_FEATURE_GVM_MODE=$(AVB_FEATURE_GVM_MODE) \
	      -DAVB_FEATURE_ENDPOINT=$(AVB_FEATURE_ENDPOINT) \
	      -DAVB_FEATURE_GSTREAMER=$(AVB_FEATURE_GSTREAMER) \
	      -DGSTREAMER_1_0=$(GSTREAMER_1_0) \
	      -DAVB_FEATURE_NEUTRINO=$(AVB_FEATURE_NEUTRINO) \
	      -DAVB_FEATURE_INTF_ALSA2=$(AVB_FEATURE_INTF_ALSA2) \
	      -DAVB_FEATURE_INTF_PAL=$(PAL_SUPPORT_INCLUDED) \
	      -DTINYALSA_SUPPORT_INCLUDED=$(TINYALSA_SUPPORT_INCLUDED) \
	      -DALSA_SUPPORT_INCLUDED=$(ALSA_SUPPORT_INCLUDED) \
	      -DCMAKE_FRAMEWORK_PATH=$(PKG_CONFIG_SYSROOT_DIR)/usr/include \
	      -DCMAKE_LIBRARY_PATH="$(PKG_CONFIG_SYSROOT_DIR)/usr/lib;$(PKG_CONFIG_SYSROOT_DIR)/usr/lib64" \
	      -DLINUX_KERNEL_DIR=$(PKG_CONFIG_SYSROOT_DIR)/usr/src/kernel \
	      -DCROSS_PREFIX=arm-oe-linux-gnueabi- \
	      -DARCH=arm \
	      -DGLIB_PKG_INCLUDE_DIRS="$(PKG_CONFIG_SYSROOT_DIR)/usr/include/glib-2.0;$(PKG_CONFIG_SYSROOT_DIR)/usr/lib/glib-2.0/include;$(PKG_CONFIG_SYSROOT_DIR)/usr/lib64/glib-2.0/include" \
	      -DGLIB_PKG_LIBRARIES=glib-2.0 gobject-2.0 \
	      -DALSA_INCLUDE_DIRS=$(PKG_CONFIG_SYSROOT_DIR)$(AUDIO_INC_DIR) \
	      -DALSA_LIBRARIES=$(AUDIO_LIB) \
	      -DSDX_ALSA_LIB_INCLUDED=$(SDX_ALSA_LIB_INCLUDED) \
              ..
