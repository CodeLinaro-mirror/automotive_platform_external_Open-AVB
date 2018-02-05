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

#define SOCK_SEND_TIMEOUT_MS 2000 /* Timeout for sending */
#define SOCK_RECV_TIMEOUT_MS 5000 /* Timeout for receiving */

// set WRITE_POLL_MS to 0 for blocking sockets, nonzero for polled non-blocking
// sockets
#define WRITE_POLL_MS 20


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

    ALOGD("audio_eavb_hw_stream_compute_buffer_size - divisor=%zu, buffer_sz=%zu",
        divisor, buffer_sz);


    // Adjust the buffer size so it can be divided by the divisor
    const size_t remainder = buffer_sz % divisor;
    if (remainder != 0) {
        buffer_sz += divisor - remainder;
    }

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
    int ret;
    int skt_fd;
    int len;

    if (strlen(path) == 0) {
        ALOGE("Error: Socket path not set");
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

    ALOGD("connected to stack fd = %d", skt_fd);

    return skt_fd;
}

static int skt_read(int fd, void* p, size_t len) {
    ssize_t read = -1;

    do {
        read = recv(fd, p, len, MSG_NOSIGNAL);
    } while (read == -1 && errno == EINTR);

    if (read == -1) {
        ALOGE("read failed with errno=%d\n", errno);
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
    uint64_t time1, time2;
    time1 = systemTime(CLOCK_MONOTONIC);

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
        time2 = systemTime(CLOCK_MONOTONIC);
        int sleep_offset = (int) ((time2 - time1)/125);
        if (sleep_offset < us_delay) {
            usleep(us_delay - sleep_offset);
        }
    }
    return bytes;
}

int eavb_stream_read(eavb_stream_ctx *ctx, void* buffer, size_t bytes) {
    int read = -1;

    if (ctx->eavbFd < 0) {
        ctx->eavbFd = skt_connect(ctx->eavbSocketPath, AUDIO_STREAM_OUTPUT_BUFFER_SZ);
        if (ctx->eavbFd < 0) {
            if (ctx->printErrorOnce == 0) {
                ALOGE("Error opening data socket - check if openavb is running");
                ctx->printErrorOnce = 1;
            }
            return 0;
        } else {
            // reset once successfully connected
            ctx->printErrorOnce = 0;
        }
    }

    read = skt_read(ctx->eavbFd, buffer, bytes);

    if (read == -1) {
        ALOGE("read failed");
        memset(buffer, 0, bytes);
    }

    return bytes;
}

int eavb_stream_ctx_init(eavb_stream_ctx *ctx, struct audio_config *config) {
    ALOGD("eavb_stream_ctx_init - ctx = %p", ctx);

    ctx->printErrorOnce = 0;
    ctx->eavbSocketPath[0] = '\0';

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
}
