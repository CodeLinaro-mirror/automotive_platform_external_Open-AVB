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

#define LOG_TAG "eavb_audio_hal_out"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <cutils/sockets.h>

#include <log/log.h>


#include "audio_eavb_hw.h"


static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    eavb_stream_out* out = (eavb_stream_out*) stream;
    return out->eavbCtx.rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    eavb_stream_out* out = (eavb_stream_out*) stream;
    const size_t period_size =
          out->eavbCtx.bufferSize / AUDIO_STREAM_OUTPUT_BUFFER_PERIODS;

    ALOGI("socket buffer size: %zu  period size: %zu", out->eavbCtx.bufferSize,
        period_size);

    return period_size;
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    eavb_stream_out* out = (eavb_stream_out*) stream;
    return out->eavbCtx.channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    eavb_stream_out* out = (eavb_stream_out*) stream;
    return out->eavbCtx.format;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}

static int out_standby(struct audio_stream *stream)
{
    eavb_stream_out* out = (eavb_stream_out*) stream;
    ALOGI("out_standby");
    if (out->eavbCtx.eavbFd >= 0) {
        ALOGI("Closing HAL socket (%d)", out->eavbCtx.eavbFd);
        shutdown(out->eavbCtx.eavbFd, SHUT_RDWR);
        close(out->eavbCtx.eavbFd);
        out->eavbCtx.eavbFd = -1;
    }
    return 0;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    eavb_stream_out* out = (eavb_stream_out*) stream;

    char* str = strdup(kvpairs);

    char* saveptr = nullptr;
    char* kvpair = strtok_r(str, ";", &saveptr);
    char* str_bus = NULL;
    char* last_r;
    int32_t bus_num = -1;

    while (kvpair) {
        char* eq = strchr(kvpair, '=');
        if (eq == kvpair) {
            // No key - skip to next pair
            goto next_pair;
        }
        eq++;
        if (*eq == '\0') {
            // No value - skip to next pair
            // Extract bus number from address
            str_bus = strtok_r(kvpair, "BUS_", &last_r);
            if (str_bus != NULL) {
                bus_num = (int32_t)strtol(str_bus, (char **)NULL, 10);
                ALOGI("%s: Bus number %d identified for the address", __func__, bus_num);
                out->eavbCtx.bus = bus_num;
                snprintf(out->eavbCtx.eavbSocketPath, MAX_PATH_LEN, "/data/misc/eavb/.eavb_out_%d", bus_num + 1);
            }
            goto next_pair;
        }

        if (0 == strncmp(kvpair, KVPAIR_KEY_SOCKET_PATH, strlen(KVPAIR_KEY_SOCKET_PATH))) {
            snprintf(out->eavbCtx.eavbSocketPath, MAX_PATH_LEN, "/data/misc/eavb/.%s", eq);
        }

    next_pair:
        kvpair = strtok_r(NULL, ";", &saveptr);
    }

    free(str);
    return 0;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    return 10;
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    /* volume controlled in audioflinger mixer (digital) */
    return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    eavb_stream_out* out = (eavb_stream_out*) stream;
    //ALOGD("out_write: out=%p, out->ctx=%p, bytes: %zu  bus_num: %d", out, &out->eavbCtx, bytes, out->eavbCtx.bus);
    return eavb_stream_write(&out->eavbCtx, buffer, bytes);
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    *dsp_frames = 0;
    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    *timestamp = 0;
    return -EINVAL;
}

int out_stream_init(eavb_stream_out *out, struct audio_config *config) {
    // initialize function table
    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;

    // initialize stream context
    return eavb_stream_ctx_init(&out->eavbCtx, config);
}

void out_stream_close(eavb_stream_out *out) {
    //std::lock_guard<std::recursive_mutex> lock(*out->eavbCtx.mutex);
    //close stream
    ALOGI("out_stream_close: out=%p, out->ctx=%p", out, &out->eavbCtx);
}

void out_stream_destroy(eavb_stream_out *out) {
    // clean up ctx
    ALOGI("out_stream_destroy: out=%p, out->ctx=%p", out, &out->eavbCtx);
    eavb_stream_ctx_destroy(&out->eavbCtx);
}
