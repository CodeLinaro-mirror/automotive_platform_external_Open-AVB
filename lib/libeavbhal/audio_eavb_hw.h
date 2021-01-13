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

/*****************************************************************************
 *
 *  Filename:      audio_eavb_hw.h
 *
 *  Description:
 *
 *****************************************************************************/

#ifndef AUDIO_EAVB_HW_H
#define AUDIO_EAVB_HW_H

//#include <mutex>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include <semaphore.h>

#include <unistd.h>
#include <pthread.h>

#ifndef PTHREAD_MUTEX_RECURSIVE
#define PTHREAD_MUTEX_RECURSIVE PTHREAD_MUTEX_RECURSIVE_NP
#endif

#define DEFAULT_READ_SIZE 320

#define KVPAIR_KEY_SOCKET_PATH "skt_name"
#define MAX_PATH_LEN 512
#define AUDIO_STREAM_OUTPUT_BUFFER_SZ (28 * 1024)
#define AUDIO_STREAM_OUTPUT_BUFFER_PERIODS 2

#define AUDIO_STREAM_INPUT_BUFFER_SZ (28 * 1024)
#define AUDIO_BUFF_SIZE 10*AUDIO_STREAM_INPUT_BUFFER_SZ

#define DEFAULT_AUDIO_FORMAT AUDIO_FORMAT_PCM_16_BIT
#define DEFAULT_SAMPLE_RATE 48000
#define DEFAULT_CHANNEL_MASK AUDIO_CHANNEL_OUT_STEREO

#define USE_ECNR_THREAD

typedef struct {
    struct audio_hw_device device;
    //std::recursive_mutex* mutex;
} eavb_audio_device;

#ifdef USE_ECNR_THREAD
typedef struct
{
    void *buffer;
    void *buffer_end;
    size_t total_buffer_size;
    size_t count;
    size_t elem_size;
    void *head;
    void *tail;
} circular_buffer_t;
#endif

typedef struct {
    //std::recursive_mutex* mutex;
    int eavbFd =-1;
    int printErrorOnce;
    uint32_t rate;
    audio_format_t format;
    audio_channel_mask_t channel_mask;
    uint32_t channels;
    size_t bufferSize;
    char eavbSocketPath[MAX_PATH_LEN];
#ifdef USE_ECNR_THREAD
    bool halPollingThreadRunning;
    pthread_t halPollingThread;
    circular_buffer_t circ_buff;
    sem_t circ_buff_count_sem;
    sem_t circ_buff_space_left_sem;
    pthread_mutex_t circ_buff_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
    int32_t bus;
} eavb_stream_ctx;

typedef struct {
    struct audio_stream_out stream;
    eavb_stream_ctx eavbCtx;
} eavb_stream_out;

typedef struct  {
    struct audio_stream_in stream;
    eavb_stream_ctx eavbCtx;
} eavb_stream_in;


int in_stream_init(eavb_stream_in *in, struct audio_config *config);
void in_stream_close(eavb_stream_in *in);
void in_stream_destroy(eavb_stream_in *in);

int out_stream_init(eavb_stream_out *out, struct audio_config *config);
void out_stream_close(eavb_stream_out *out);
void out_stream_destroy(eavb_stream_out *out);

int eavb_stream_ctx_init(eavb_stream_ctx *ctx, struct audio_config *config);
int eavb_stream_write(eavb_stream_ctx *ctx, const void* buffer, size_t bytes);
int eavb_stream_read(eavb_stream_ctx *ctx, void* buffer, size_t bytes);
void eavb_stream_ctx_destroy(eavb_stream_ctx *ctx);

#ifdef USE_ECNR_THREAD
void *in_eavbHalPollingThreadFn(void *pv);
void eavb_halPollThread_init(eavb_stream_ctx *halctx);
#endif

#endif
