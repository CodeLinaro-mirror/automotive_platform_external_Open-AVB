/******************************************************************************
 * Copyright (C) 2018, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ******************************************************************************/

#define LOG_TAG "eavb_audio_hal_dev"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <log/log.h>

#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include "audio_eavb_hw.h"

#define MAX_NUM_STREAMS 16

static int adev_open_input_stream(struct audio_hw_device *device,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags __unused,
                                  const char *address __unused,
                                  audio_source_t source __unused)
{
    //eavb_audio_device* dev = (eavb_audio_device*) device;
    ALOGI("adev_open_input_stream...");

    //std::lock_guard<std::recursive_mutex> lock(*dev->mutex);
    eavb_stream_in *in = (eavb_stream_in *)calloc(1, sizeof(eavb_stream_in));
    if (!in) {
        return -ENOMEM;
    }

    int err = in_stream_init(in, config);
    if (0 == err) {
        ALOGD("adev_open_input_stream - success - in=%p, in->stream=%p", in, &in->stream);
        *stream_in = &in->stream;
    } else {
        free(in);
    }

    return err;
}

static void adev_close_input_stream(struct audio_hw_device *device,
                                   struct audio_stream_in *stream)
{
    //eavb_audio_device* dev = (eavb_audio_device*) device;
    eavb_stream_in* in = (eavb_stream_in*) stream;
    ALOGI("adev_close_input_stream %p", stream);

    //std::lock_guard<std::recursive_mutex> lock(*dev->mutex);
    in_stream_close(in);
    in_stream_destroy(in);
    free(in);
    return;
}

static int adev_open_output_stream(struct audio_hw_device *device,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address)
{
    //eavb_audio_device* dev = (eavb_audio_device*) device;
    ALOGI("adev_open_output_stream...");

    *stream_out = NULL;
    eavb_stream_out *out =
            (eavb_stream_out *)calloc(1, sizeof(eavb_stream_out));
    if (!out)
        return -ENOMEM;

    /* set output config values */
    if (config != nullptr) {
        ALOGI("Output stream config: format=0x%x sample_rate=%d channel_mask=0x%x",
            config->format, config->sample_rate, config->channel_mask);
    }

    int err = out_stream_init(out, config);
    if (0 == err) {
        ALOGD("adev_open_output_stream - success - out=%p, out->stream=%p", out, &out->stream);
        *stream_out = &out->stream;
        return 0;
    } else {
        ALOGE("adev_open_output_stream - init failed");
        free(out);
        return -1;
    }



}

static void adev_close_output_stream(struct audio_hw_device *device,
                                     struct audio_stream_out *stream)
{
    //eavb_audio_device* dev = (eavb_audio_device*) device;
    eavb_stream_out* out = (eavb_stream_out*) stream;
    ALOGI("adev_close_output_stream - %p", stream);

    //std::lock_guard<std::recursive_mutex> lock(*dev->mutex);
    out_stream_close(out);
    out_stream_destroy(out);

    free(out);
}

static int adev_set_parameters(struct audio_hw_device *device, const char *kvpairs)
{
    return -ENOSYS;
}

static char * adev_get_parameters(const struct audio_hw_device *device,
                                  const char *keys)
{
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *device, float volume)
{
    return -ENOSYS;
}

static int adev_set_master_volume(struct audio_hw_device *device, float volume)
{
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *device, float *volume)
{
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device *device, bool muted)
{
    return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *device, bool *muted)
{
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *device, audio_mode_t mode)
{
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *device, bool state)
{
    return -ENOSYS;
}

static int adev_get_mic_mute(const struct audio_hw_device *device, bool *state)
{
    return -ENOSYS;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *device,
                                         const struct audio_config *config)
{
    return 320;
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    return 0;
}

static int adev_close(hw_device_t *device)
{
    ALOGI("adev_close");

    free(device);
    return 0;
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    ALOGI("adev_open: %s", name);

    eavb_audio_device *adev;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) {
        ALOGE("adev_open: error invalid name, expected %s", AUDIO_HARDWARE_INTERFACE);
        return -EINVAL;
    }

    adev = (eavb_audio_device*)calloc(1, sizeof(eavb_audio_device));
    if (!adev)
        return -ENOMEM;

    adev->device.common.tag = HARDWARE_DEVICE_TAG;
    adev->device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->device.common.module = (struct hw_module_t *) module;
    adev->device.common.close = adev_close;

    adev->device.init_check = adev_init_check;
    adev->device.set_voice_volume = adev_set_voice_volume;
    adev->device.set_master_volume = adev_set_master_volume;
    adev->device.get_master_volume = adev_get_master_volume;
    adev->device.set_master_mute = adev_set_master_mute;
    adev->device.get_master_mute = adev_get_master_mute;
    adev->device.set_mode = adev_set_mode;
    adev->device.set_mic_mute = adev_set_mic_mute;
    adev->device.get_mic_mute = adev_get_mic_mute;
    adev->device.set_parameters = adev_set_parameters;
    adev->device.get_parameters = adev_get_parameters;
    adev->device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->device.open_output_stream = adev_open_output_stream;
    adev->device.close_output_stream = adev_close_output_stream;
    adev->device.open_input_stream = adev_open_input_stream;
    adev->device.close_input_stream = adev_close_input_stream;
    adev->device.dump = adev_dump;

    *device = &adev->device.common;

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "eavb audio HW HAL",
        .author = "The Android Open Source Project",
        .methods = &hal_module_methods,
    },
};
