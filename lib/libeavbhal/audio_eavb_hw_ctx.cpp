/******************************************************************************
 * Copyright (C) 2018, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 ******************************************************************************/
/******************************************************************************
 *
 *  Copyright (C) 2009-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

#define LOG_TAG "eavb_audio_hal_ctx"
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
#include <utils/Timers.h>

#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include <cutils/sockets.h>
#include "audio_eavb_hw.h"

#define USEC_PER_SEC 1000000L
#define NANOSECONDS_PER_MSEC (1000000L)
#define DEFAULT_SEM_WAIT_TIMEOUT_MSEC 50

#define SOCK_SEND_TIMEOUT_MS 2000 /* Timeout for sending */
#define SOCK_RECV_TIMEOUT_MS 5000 /* Timeout for receiving */

// set WRITE_POLL_MS to 0 for blocking sockets, nonzero for polled non-blocking
// sockets
#define WRITE_POLL_MS 20

#ifdef USE_ECNR_THREAD
int circ_buff_init(circular_buffer_t *circ_buff, size_t total_buffer_size, size_t elem_size) {
    if (circ_buff->buffer == NULL) {
        ALOGI("allocating memory for circular buffer");
        circ_buff->buffer = malloc(total_buffer_size * elem_size);
        if (circ_buff->buffer == NULL) {
            ALOGE("error allocating memory for circular buffer");
            return -1;
        }
    }
    circ_buff->buffer_end = (char *)circ_buff->buffer + total_buffer_size * elem_size;
    circ_buff->total_buffer_size = total_buffer_size;
    circ_buff->count = 0;
    circ_buff->elem_size = elem_size;
    circ_buff->head = circ_buff->buffer;
    circ_buff->tail = circ_buff->buffer;
    return 0;
}

void circ_buff_free(circular_buffer_t *circ_buff) {
    circ_buff->head = NULL;
    circ_buff->tail = NULL;
    circ_buff->buffer_end = NULL;

    free(circ_buff->buffer);
    circ_buff->buffer = NULL;
}

void circ_buff_push(circular_buffer_t *circ_buff, const void *element) {
    if (circ_buff->count == circ_buff->total_buffer_size){
        ALOGI("buffer overflow, doing reset");
        circ_buff->count = 0;
        circ_buff->head = circ_buff->buffer;
        circ_buff->tail = circ_buff->buffer;
    }

    memcpy(circ_buff->head, element, circ_buff->elem_size);

    circ_buff->head = (char*)circ_buff->head + circ_buff->elem_size;
    if (circ_buff->head == circ_buff->buffer_end) {
        circ_buff->head = circ_buff->buffer;
    }

    circ_buff->count++;
}

int circ_buff_pop(circular_buffer_t *circ_buff, void *element) {
    if (circ_buff->count == 0 || circ_buff->buffer == NULL){
        ALOGE("buffer underflow");
        memset(element, 0, circ_buff->elem_size);
        return -1;
    }

    memcpy(element, circ_buff->tail, circ_buff->elem_size);
    circ_buff->tail = (char*)circ_buff->tail + circ_buff->elem_size;

    if (circ_buff->tail == circ_buff->buffer_end) {
        circ_buff->tail = circ_buff->buffer;
    }

    circ_buff->count--;
    return circ_buff->elem_size;
}
#endif

static size_t audio_eavb_hw_stream_compute_buffer_size(int sampleRate,
        int format, int channels) {
    size_t buffer_sz = AUDIO_STREAM_OUTPUT_BUFFER_SZ;  // Default value
    const uint64_t time_period_ms = 20;                // Conservative 20ms
    int bitdepth = 0;

    switch (format) {
        case AUDIO_FORMAT_PCM_16_BIT:        bitdepth = 16; break;
        case AUDIO_FORMAT_PCM_24_BIT_PACKED: bitdepth = 24; break;
        case AUDIO_FORMAT_PCM_32_BIT:        bitdepth = 32; break;
        case AUDIO_FORMAT_PCM_8_24_BIT:      // FALLTHROUGH
            // All 24-bit audio is expected in 24_BIT_PACKED format
        default:
            ALOGE("Invalid audio format: 0x%x", format);
            return -1;
    }

    const size_t divisor = (AUDIO_STREAM_OUTPUT_BUFFER_PERIODS * 16 *
                          channels * bitdepth) / 8;

    buffer_sz = (time_period_ms * AUDIO_STREAM_OUTPUT_BUFFER_PERIODS *
               sampleRate * channels * (bitdepth / 8)) /
              1000;

    // Adjust the buffer size so it can be divided by the divisor
    const size_t remainder = buffer_sz % divisor;
    if (remainder != 0) {
        buffer_sz += divisor - remainder;
    }

    ALOGD("audio_eavb_hw_stream_compute_buffer_size - remainder = %zu, divisor=%zu, buffer_sz=%zu, channels =%d, sampleRate =%d, bitdepth =%d, format =%d",
        remainder, divisor, buffer_sz, channels, sampleRate, bitdepth, format);

    return buffer_sz;
}

static int calc_audiotime_usec(eavb_stream_ctx* ctx, int bytes) {
  int chan_count = ctx->channels;
  int bytes_per_sample;

  switch (ctx->format) {
    case AUDIO_FORMAT_PCM_8_BIT:
      bytes_per_sample = 1;
      break;
    case AUDIO_FORMAT_PCM_16_BIT:
      bytes_per_sample = 2;
      break;
    case AUDIO_FORMAT_PCM_24_BIT_PACKED:
      bytes_per_sample = 3;
      break;
    case AUDIO_FORMAT_PCM_8_24_BIT:
      bytes_per_sample = 4;
      break;
    case AUDIO_FORMAT_PCM_32_BIT:
      bytes_per_sample = 4;
      break;
    default:
      ALOGE("unsupported sample format %d", ctx->format);
      bytes_per_sample = 2;
      break;
  }

  //ALOGE("calc_audiotime_usec - ctx=%p, ctx->format=%d, ctx->rate=%d", ctx, ctx->format, ctx->rate);

  return (
      int)(((int64_t)bytes * (USEC_PER_SEC / (chan_count * bytes_per_sample))) /
           ctx->rate);
}

static int skt_connect(const char* path, size_t buffer_sz) {
    int ret = 0;
    int skt_fd = -1;
    int len;

    if (strlen(path) == 0) {
        //ALOGE("Error: Socket path (%s) not set",path);
        return -1;
    }

    skt_fd = socket(AF_LOCAL, SOCK_STREAM, 0);

    if (skt_fd < 0) {
        return -1;
    }

    if (socket_local_client_connect(skt_fd, path,
            ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM) < 0) {
        //ALOGD("failed to connect (%s)", strerror(errno));
        close(skt_fd);
        return -1;
    }

    len = buffer_sz;
    ret = setsockopt(skt_fd, SOL_SOCKET, SO_SNDBUF, (char*)&len, (int)sizeof(len));
    if (ret < 0) {
        ALOGE("setsockopt failed (%s)", strerror(errno));
    }

    ret = setsockopt(skt_fd, SOL_SOCKET, SO_RCVBUF, (char*)&len, (int)sizeof(len));
    if (ret < 0) {
        ALOGE("setsockopt failed (%s)", strerror(errno));
    }

    /* Socket send/receive timeout value */
    struct timeval tv;
    tv.tv_sec = SOCK_SEND_TIMEOUT_MS / 1000;
    tv.tv_usec = (SOCK_SEND_TIMEOUT_MS % 1000) * 1000;

    ret = setsockopt(skt_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (ret < 0) {
        ALOGE("setsockopt failed (%s)", strerror(errno));
    }

    tv.tv_sec = SOCK_RECV_TIMEOUT_MS / 1000;
    tv.tv_usec = (SOCK_RECV_TIMEOUT_MS % 1000) * 1000;

    ret = setsockopt(skt_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (ret < 0) {
        ALOGE("setsockopt failed (%s)", strerror(errno));
    }

    ALOGI("connected to server socket (%d), path (%s)",skt_fd, path);

    return skt_fd;
}

static int skt_read(int fd, void* p, size_t len) {
    ssize_t read = -1;

    do {
        read = recv(fd, p, len, MSG_NOSIGNAL | MSG_WAITALL);
    } while (read == -1 && errno == EINTR);

    if (read == -1) {
        ALOGE("read failed with len = %zu errno=%d, errno=(%s) socketfd=(%d)\n", len, errno, strerror(errno), fd);
    }

    return (int)read;
}


static int skt_write(int fd, const void* p, size_t len) {
    ssize_t sent = -1;

    if (WRITE_POLL_MS == 0) {
        // do not poll, use blocking send
        do {
            sent = send(fd, p, len, MSG_NOSIGNAL);
        } while (sent == -1 && errno == EINTR);

        if (sent == -1) {
            ALOGE("write failed with error(%s)", strerror(errno));
        }

        return (int)sent;
    }

    // use non-blocking send, poll
    int ms_timeout = SOCK_SEND_TIMEOUT_MS;
    size_t count = 0;
    while (count < len) {
        do {
            sent = send(fd, p, len - count, MSG_NOSIGNAL | MSG_DONTWAIT);
        } while (sent == -1 && errno == EINTR);

        if (sent == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ALOGE("write failed with error(%s)", strerror(errno));
                return -1;
            }
            if (ms_timeout >= WRITE_POLL_MS) {
                usleep(WRITE_POLL_MS * 1000);
                ms_timeout -= WRITE_POLL_MS;
                continue;
            }
            ALOGW("write timeout exceeded, sent %zu bytes", count);
            return -1;
        }
        count += sent;
        p = (const uint8_t*)p + sent;
    }
    return (int)count;
}

static int skt_disconnect(int fd) {
  ALOGI("fd %d", fd);

  if (fd != -1) {
    shutdown(fd, SHUT_RDWR);
    close(fd);
  }
  return 0;
}

int eavb_stream_write(eavb_stream_ctx *ctx, const void* buffer, size_t bytes) {
    int sent = -1;

    if (ctx->eavbFd < 0) {
        ctx->eavbFd = skt_connect(ctx->eavbSocketPath, AUDIO_STREAM_OUTPUT_BUFFER_SZ);
        if (ctx->eavbFd < 0) {
            if (ctx->printErrorOnce == 0) {
                ALOGD("Error opening data socket (%s) - check if openavb is running",
                    ctx->eavbSocketPath);
                ctx->printErrorOnce = 1;
            }
            goto finish;
        } else {
            // reset once successfully connected
            ctx->printErrorOnce = 0;
        }
    }

    sent = skt_write(ctx->eavbFd, buffer, bytes);

    if (sent == -1) {
        ALOGE("write failed - server might have disconnected");
        skt_disconnect(ctx->eavbFd);
        ctx->eavbFd = -1;
    }

finish:
    {
        const int us_delay = calc_audiotime_usec(ctx, bytes);
        ctx->time2 = systemTime(CLOCK_MONOTONIC);
        if (ctx->time1 && (ctx->time1 < ctx->time2)) {
            int sleep_offset = (int) ((ctx->time2 - ctx->time1)/1000);
            if (sleep_offset < us_delay) {
                usleep(us_delay - sleep_offset);
            }
        }
        ctx->time1 = systemTime(CLOCK_MONOTONIC);
    }
    return sent;
}

int eavb_stream_read(eavb_stream_ctx *ctx, void* buffer, size_t bytes) {
#ifndef USE_ECNR_THREAD
    int read = -1;
    if (ctx->eavbFd <= 0) {
        while (ctx->eavbFd <= 0) {
           ctx->eavbFd = skt_connect(ctx->eavbSocketPath, AUDIO_STREAM_INPUT_BUFFER_SZ);
           usleep(20);
        }
    }

    read = skt_read(ctx->eavbFd, buffer, bytes);

    if (read == -1) {
        ALOGE("read failed");
        memset(buffer, 0, bytes);
    }

    return read;
#else
    struct timespec semwait_timeout;

    if (clock_gettime(CLOCK_REALTIME, &semwait_timeout) == -1) {
        ALOGE("clock_gettime error");
        return 0;
    }
    semwait_timeout.tv_nsec += NANOSECONDS_PER_MSEC*DEFAULT_SEM_WAIT_TIMEOUT_MSEC;
    if (sem_timedwait(&ctx->circ_buff_count_sem, &semwait_timeout) < 0) {
        memset(buffer, 0, bytes);
        sem_post(&ctx->circ_buff_space_left_sem);
        return 0;
    }
    pthread_mutex_lock(&ctx->circ_buff_mutex);
    bytes = circ_buff_pop(&ctx->circ_buff, buffer);
    pthread_mutex_unlock(&ctx->circ_buff_mutex);
    sem_post(&ctx->circ_buff_space_left_sem);
    return bytes;
#endif
}

int eavb_stream_ctx_init(eavb_stream_ctx *ctx, struct audio_config *config) {
    ALOGD("eavb_stream_ctx_init - ctx = %p", ctx);

    ctx->printErrorOnce = 0;
    ctx->eavbSocketPath[0] = '\0';
    ctx->eavbFd = -1;
    ctx->time1 = 0;
    ctx->time2 = 0;

    if (config) {
        ctx->format = config->format;
        ctx->rate = config->sample_rate;
        ctx->channel_mask = config->channel_mask;
        ctx->channels = audio_channel_count_from_out_mask(config->channel_mask);
    } else {
        // Default values
        ctx->format = DEFAULT_AUDIO_FORMAT;
        ctx->rate = DEFAULT_SAMPLE_RATE;
        ctx->channel_mask = DEFAULT_CHANNEL_MASK;
        ctx->channels = audio_channel_count_from_out_mask(ctx->channel_mask);
    }
    ctx->bufferSize = audio_eavb_hw_stream_compute_buffer_size(ctx->rate, ctx->format, ctx->channels);
    ALOGD("Buffer size = %zu", ctx->bufferSize);

    return 0;
}

void eavb_stream_ctx_destroy(eavb_stream_ctx *ctx) {
    ALOGD("eavb_stream_ctx_destroy - ctx=%p", ctx);
#ifdef USE_ECNR_THREAD
    // exit ECNR Poll thread
    ctx->halPollingThreadRunning = false;
    usleep(1000);
#endif
}

#ifdef USE_ECNR_THREAD
void eavb_halPollThread_init(eavb_stream_ctx *halctx) {
    pthread_create(&halctx->halPollingThread, NULL, in_eavbHalPollingThreadFn, halctx);

    // make thread running
    halctx->halPollingThreadRunning = true;

    return;
}

void *in_eavbHalPollingThreadFn(void *pv) {
    eavb_stream_ctx *pctx = (eavb_stream_ctx *) pv;
    size_t len = -1;
    void *buffer;
    struct timespec semwait_timeout;

    while (pctx->eavbFd <= 0) {
        pctx->eavbFd = skt_connect(pctx->eavbSocketPath, AUDIO_STREAM_INPUT_BUFFER_SZ);
        usleep(20);
    }

    // read new data
    buffer = (void*) malloc(DEFAULT_READ_SIZE);
    if (!buffer){
        goto cleanup;
    }

    if (circ_buff_init(&pctx->circ_buff, AUDIO_STREAM_INPUT_BUFFER_SZ, DEFAULT_READ_SIZE) < 0) {
        goto cleanup;
	}
    sem_init(&pctx->circ_buff_count_sem, 0, 0);
    sem_init(&pctx->circ_buff_space_left_sem, 0, 1);

    ALOGI("creating ECNR poll thread");

    // keep polling until avb stream is stopped
    while (pctx->halPollingThreadRunning) {
        if (pctx->eavbFd > 0) {
            len = skt_read(pctx->eavbFd, buffer, DEFAULT_READ_SIZE);
            if (len <= 0) {
                continue;
            }

            if (len > 0) {
                if (clock_gettime(CLOCK_REALTIME, &semwait_timeout) == -1) {
                    ALOGE("clock_gettime error");
                    continue;
                }
                semwait_timeout.tv_nsec += NANOSECONDS_PER_MSEC;
                if (sem_timedwait(&pctx->circ_buff_space_left_sem, &semwait_timeout) < 0) {
                    continue;
                }
                pthread_mutex_lock(&pctx->circ_buff_mutex);
                circ_buff_push(&pctx->circ_buff, buffer);
                pthread_mutex_unlock(&pctx->circ_buff_mutex);
                sem_post(&pctx->circ_buff_count_sem);
                usleep(1000);
            }
        }
    }

    if (pctx->circ_buff.buffer != NULL) {
        circ_buff_free(&pctx->circ_buff);
    }

cleanup:
    ALOGI("closing ECNR poll thread");

    pctx->eavbFd = -1;
    close(pctx->eavbFd);
    if (buffer) {
        free(buffer);
    }

    sem_destroy(&pctx->circ_buff_space_left_sem);
    sem_destroy(&pctx->circ_buff_count_sem);

    pthread_exit(NULL);
    return NULL;
}
#endif
