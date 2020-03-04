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

#define LOG_TAG "eavb_audio_hal_in"
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


static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    eavb_stream_in* in = (eavb_stream_in*) stream;
    return in->eavbCtx.rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    return 320;
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    eavb_stream_in* in = (eavb_stream_in*) stream;
    return in->eavbCtx.channel_mask;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    eavb_stream_in* in = (eavb_stream_in*) stream;
    return in->eavbCtx.format;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    //struct stub_stream_in *in = (struct stub_stream_in *)stream;
    //in->last_read_time_us = 0;
    return 0;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs) {
    ALOGE("%s: kvpairs: %s", __func__, kvpairs);
    eavb_stream_in* in = (eavb_stream_in*) stream;

    char* str = strdup(kvpairs);

    char* saveptr = nullptr;
    char* kvpair = strtok_r(str, ";", &saveptr);

    while (kvpair) {
        char* eq = strchr(kvpair, '=');
        if (eq == kvpair) {
            // No key - skip to next pair
            goto next_pair;
        }
        eq++;
        if (*eq == '\0') {
            // No value - skip to next pair
            goto next_pair;
        }

        if (0 == strncmp(kvpair, KVPAIR_KEY_SOCKET_PATH, strlen(KVPAIR_KEY_SOCKET_PATH))) {
            snprintf(in->eavbCtx.eavbSocketPath, MAX_PATH_LEN, "/data/misc/eavb/.%s", eq);
        }

    next_pair:
        kvpair = strtok_r(NULL, ";", &saveptr);
    }
	ALOGE("in socket path (%s)",in->eavbCtx.eavbSocketPath);

    free(str);
    return 0;
}

static char * in_get_parameters(const struct audio_stream *stream,
        const char *keys) {
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream, float gain) {
    return 0;
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer, size_t bytes) {
    eavb_stream_in* in = (eavb_stream_in*) stream;
    ALOGD("in_read: in=%p, in->ctx=%p, bytes: %zu", in, &in->eavbCtx, bytes);
    return eavb_stream_read(&in->eavbCtx, buffer, bytes);
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream) {
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect) {
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect) {
    return 0;
}

int in_stream_init(eavb_stream_in *in, struct audio_config *config) {
    ALOGI("in_stream_init...");
    // initialize function table
    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    // initialize stream context
    return eavb_stream_ctx_init(&in->eavbCtx, config);
}

void in_stream_close(eavb_stream_in *in) {
    //close stream
    ALOGI("in_stream_close: in=%p, in->ctx=%p", in, &in->eavbCtx);
}

void in_stream_destroy(eavb_stream_in *in) {
    // clean up ctx
    ALOGI("in_stream_destroy: in=%p, in->ctx=%p", in, &in->eavbCtx);
    eavb_stream_ctx_destroy(&in->eavbCtx);
}
