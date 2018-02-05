/*
* Copyright (c) 2018, The Linux Foundation. All rights reserved.
*/

/*************************************************************************************************************
Copyright (c) 2012-2015, Symphony Teleca Corporation, a Harman International Industries, Incorporated company
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS LISTED "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS LISTED BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Attributions: The inih library portion of the source code is licensed from 
Brush Technology and Ben Hoyt - Copyright (c) 2009, Brush Technology and Copyright (c) 2009, Ben Hoyt. 
Complete license and copyright information can be found at 
https://github.com/benhoyt/inih/commit/74d2ca064fb293bc60a77b0bd068075b293cf175.
*************************************************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "openavb_types_pub.h"
#include "openavb_trace_pub.h"
#include "openavb_mediaq_pub.h"
#include "openavb_map_uncmp_audio_pub.h"
#include "openavb_map_aaf_audio_pub.h"
#include "openavb_intf_pub.h"
#include "openavb_platform_pub.h"
#include "openavb_os_services_osal.h"
#include <cutils/sockets.h>
#include <cutils/sched_policy.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <pthread.h>


#define    AVB_LOG_COMPONENT    "Audio Stream Interface"
#include "openavb_log_pub.h"

#define SOCK_SEND_TIMEOUT_MS 20 /* Timeout for sending */
#define SOCK_RECV_TIMEOUT_MS 10 /* Timeout for receiving */
#define SOCKET_POLL_TIMEOUT -1 /* Timeout for connecting */
#define SOCKET_BUFFER_SIZE (28 * 1024)
#define AUDIO_BUFF_SIZE 10*SOCKET_BUFFER_SIZE
#define MAX_PATH_LEN 512

// set WRITE_POLL_MS to 0 for blocking sockets, nonzero for polled non-blocking
// sockets
#define WRITE_POLL_MS 20
#define READ_POLL_MS 200

extern void *halPollingThreadFn(void *pv);
#define halPollingThread_THREAD_STK_SIZE THREAD_STACK_SIZE
THREAD_TYPE(halPollingThread);

typedef struct {
    // Ignore timestamp at listener.
    bool ignoreTimestamp;

    // ALSA read/write interval
    U32 intervalCounter;

    // map_nv_audio_rate
    avb_audio_rate_t audioRate;

    // map_nv_audio_bit_depth
    avb_audio_bit_depth_t audioBitDepth;

    // map_nv_channels
    avb_audio_channels_t audioChannels;

    // map_nv_audio_endian
    avb_audio_endian_t audioEndian;

    // intf_nv_number_of_data_bytes
    U32 numberOfDataBytes;

    int pollingThreadRunning;

    THREAD_DEFINITON(halPollingThread);
    int pServerSocket;
    int pHalSocket;
    char socketPath[MAX_PATH_LEN];
    uint8_t audioBuffer[AUDIO_BUFF_SIZE];
    MUTEX_HANDLE_ALT(audioBufferMutex);
    uint32_t audioBufferPos;
    uint32_t dataLeftInBuffer;
    uint64_t samplesIn;
    uint64_t samplesOut;
    uint64_t timestamp;

} pvt_data_t;

static int accept_server_socket(int sfd) {
  struct sockaddr_un remote;
  struct pollfd pfd;
  int fd;
  socklen_t len = sizeof(struct sockaddr_un);

  /* make sure there is data to process */
  pfd.fd = sfd;
  pfd.events = POLLIN;

  int poll_ret;
  do {
      poll_ret = poll(&pfd, 1, SOCKET_POLL_TIMEOUT);
  } while (poll_ret == -1 && errno == EINTR);

  if (poll_ret == 0) {
    AVB_LOG_DEBUG("accept poll timeout");
    return -1;
  }

  do {
      fd = accept(sfd, (struct sockaddr*)&remote, &len);
  } while ((fd == -1) && (errno == EINTR));

  if (fd == -1) {
    AVB_LOGF_DEBUG("sock accept failed (%s)", strerror(errno));
    return -1;
  }

  // match socket buffer size option with client
  const int size = SOCKET_BUFFER_SIZE;
  int ret =
      setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*)&size, (int)sizeof(size));
  if (ret < 0) {
    AVB_LOGF_ERROR("setsockopt failed (%s)", strerror(errno));
  }

  return fd;
}

static int skt_connect(const char* path, size_t buffer_sz) {
    int ret;
    int skt_fd;
    int len;

    AVB_LOGF_INFO("connect to %s (sz %zu)", path, buffer_sz);

    if ((path == NULL) || (strlen(path) == 0)) {
        AVB_LOG_ERROR("Error: Socket path not set");
        return -1;
    }

    skt_fd = socket(AF_LOCAL, SOCK_STREAM, 0);

    if (skt_fd < 0) {
        AVB_LOG_ERROR("failed to create socket");
        return -1;
    }

    if (socket_local_server_bind(skt_fd, path, ANDROID_SOCKET_NAMESPACE_ABSTRACT) < 0) {
        AVB_LOGF_ERROR("failed to connect (%s)", strerror(errno));
        close(skt_fd);
        return -1;
    }

    if (listen(skt_fd, 1) < 0) {
        AVB_LOGF_ERROR("listen failed", strerror(errno));
        close(skt_fd);
        return -1;
    }

    AVB_LOGF_DEBUG("connected to stack fd = %d", skt_fd);

    return skt_fd;
}

static int skt_read(pvt_data_t *pPvtData, uint8_t* p, size_t len) {
    int read = 0;
    struct pollfd pfd;

    if (pPvtData->pServerSocket < 0) {
        AVB_LOG_ERROR("invalid socket");
        return -1;
    }

    if (pPvtData->pHalSocket < 0) {
        pPvtData->pHalSocket = accept_server_socket(pPvtData->pServerSocket);
        if (pPvtData->pHalSocket < 0) {
            AVB_LOG_WARNING("No hal socket opened");
            return -1;
        }
    }


    int poll_ret;
    pfd.fd = pPvtData->pHalSocket;
    pfd.events = POLLIN | POLLHUP;

    do {
        poll_ret = poll(&pfd, 1, READ_POLL_MS);
    } while ((poll_ret == -1) && (errno == EINTR));

    if (poll_ret == 0) {
      AVB_LOGF_VERBOSE("poll timeout (%d ms)", READ_POLL_MS);
      //break;
      return read;
    } else if (poll_ret < 0) {
      AVB_LOGF_ERROR("%s(): poll() failed: return %d errno %d (%s)", __func__,
                       poll_ret, errno, strerror(errno));
      //break;
      return read;
    }

    if (pfd.revents & (POLLHUP | POLLNVAL)) {
        return 0;
    }

    do {
        read = recv(pPvtData->pHalSocket, p, len, 0);
    } while ((read == -1) && (errno == EINTR));

    if (read == 0) {
      return 0;
    }

    if (read < 0) {
        AVB_LOGF_ERROR("read failed with errno=%d, str=%s\n", errno, strerror(errno));
        return 0;
    }

    return (int)read;
}


static int skt_write(pvt_data_t *pPvtData, const void* p, size_t len) {
    ssize_t sent;

    if (pPvtData->pServerSocket < 0) {
        AVB_LOG_ERROR("invalid socket");
        return -1;
    }

    if (pPvtData->pHalSocket < 0) {
        pPvtData->pHalSocket = accept_server_socket(pPvtData->pServerSocket);
        if (pPvtData->pHalSocket < 0) {
            AVB_LOG_WARNING("No hal socket opened");
            return -1;
        }
    }

  if (WRITE_POLL_MS == 0) {
    // do not poll, use blocking send
    do {
        sent = send(pPvtData->pHalSocket, p, len, MSG_NOSIGNAL);
    } while (sent == -1 && errno == EINTR);

    if (sent == -1) {
        AVB_LOGF_ERROR("write failed with error(%s)", strerror(errno));
    }

    return (int)sent;
  }

  // use non-blocking send, poll
  int ms_timeout = SOCK_SEND_TIMEOUT_MS;
  size_t count = 0;
  while (count < len) {
    do {
        sent = send(pPvtData->pHalSocket, p, len - count, MSG_NOSIGNAL | MSG_DONTWAIT);
    } while (sent == -1 && errno == EINTR);

    if (sent == -1) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        AVB_LOGF_ERROR("write failed with error(%s)", strerror(errno));
        return -1;
      }
      if (ms_timeout >= WRITE_POLL_MS) {
        usleep(WRITE_POLL_MS * 1000);
        ms_timeout -= WRITE_POLL_MS;
        continue;
      }
      AVB_LOGF_ERROR("write timeout exceeded, sent %zu bytes", count);
      return -1;
    }
    count += sent;
    p = (const uint8_t*)p + sent;
  }
  return (int)count;
}

static int skt_disconnect(int fd) {
    AVB_LOGF_INFO("fd %d", fd);

    if (fd != -1) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
    return 0;
}


void *halPollingThreadFn(void *pv) {
    pvt_data_t *pPvtData = (pvt_data_t *) pv;
    int read = 0;
    pPvtData->pollingThreadRunning = 1;


    // make polling threads use RT scheduling policy since they are part of the
    // audio pipeline
    {
        int tid = gettid();
        int rc = set_sched_policy(tid, SP_AUDIO_SYS);
        if (rc) {
            AVB_LOG_ERROR("failed to change sched policy");
        }

        struct sched_param rt_params;
        rt_params.sched_priority = 1;

        rc = sched_setscheduler(tid, SCHED_FIFO, &rt_params);
        if (rc != 0) {
            AVB_LOGF_ERROR("unable to set SCHED_FIFO priority, error %s", strerror(errno));
        }
    }

    uint8_t* buff = (uint8_t*) malloc(SOCKET_BUFFER_SIZE);
    // keep polling until avb stream is stopped
    while (pPvtData->pollingThreadRunning) {
        // read new data
        read = skt_read(pPvtData, buff, SOCKET_BUFFER_SIZE);
        if (read == 0) {
            continue;
        } else if (read < 0) {
            AVB_LOGF_ERROR("halPollingThreadFn - skt_read() error: %d, %s",
                    read, strerror(read));
            continue;
        }

        MUTEX_LOCK_ALT(pPvtData->audioBufferMutex);

        // check if there is enough space in buffer to fit data from skt_read
        if (read > (int)(AUDIO_BUFF_SIZE - pPvtData->dataLeftInBuffer)) {
            AVB_LOG_DEBUG("halPollingThreadFn - buffer overflow");
            MUTEX_UNLOCK_ALT(pPvtData->audioBufferMutex);
            continue;
        }

        // check if there isn't enough space left at the end to fit in new data
        if ((read + pPvtData->audioBufferPos + pPvtData->dataLeftInBuffer) > AUDIO_BUFF_SIZE) {
            // move existing data to start of buffer
            memmove(pPvtData->audioBuffer, pPvtData->audioBuffer + pPvtData->audioBufferPos, pPvtData->dataLeftInBuffer);
            pPvtData->audioBufferPos = 0;
        }

        // Copy in the new data after existing data
        memcpy((pPvtData->audioBuffer + pPvtData->audioBufferPos + pPvtData->dataLeftInBuffer),
                buff, read);
        pPvtData->dataLeftInBuffer += read;

        MUTEX_UNLOCK_ALT(pPvtData->audioBufferMutex);

    }

    return NULL;
}

static void passParamToMapModule(media_q_t *pMediaQ) {
    if (pMediaQ) {
        media_q_pub_map_uncmp_audio_info_t *pPubMapUncmpAudioInfo = (media_q_pub_map_uncmp_audio_info_t *)pMediaQ->pPubMapInfo;
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;

        if (!pPubMapUncmpAudioInfo) {
            AVB_LOG_ERROR("Public interface module data not allocated.");
            return;
        }

        if (!pPvtData) {
            AVB_LOG_ERROR("Private interface module data not allocated.");
            return;
        }

       if (pMediaQ->pMediaQDataFormat) {
            if (strcmp(pMediaQ->pMediaQDataFormat, MapUncmpAudioMediaQDataFormat) == 0
                || strcmp(pMediaQ->pMediaQDataFormat, MapAVTPAudioMediaQDataFormat) == 0) {
                pPubMapUncmpAudioInfo->audioRate = pPvtData->audioRate;
                pPubMapUncmpAudioInfo->audioBitDepth = pPvtData->audioBitDepth;
                pPubMapUncmpAudioInfo->audioChannels = pPvtData->audioChannels;
                pPubMapUncmpAudioInfo->audioEndian = pPvtData->audioEndian;
            }
        }
    }
}


// Each configuration name value pair for this mapping will result in this callback being called.
void openavbIntfAudioStreamCfgCB(media_q_t *pMediaQ, const char *name, const char *value) {
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);

    if (!pMediaQ) {
        return;
    }

    char *pEnd;
    U32 val;
    pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
    if (!pPvtData) {
        AVB_LOG_ERROR("Private interface module data not allocated.");
        return;
    }

    if (strcmp(name, "intf_nv_audio_rate") == 0) {
        val = strtol(value, &pEnd, 10);
        if (val >= AVB_AUDIO_RATE_8KHZ && val <= AVB_AUDIO_RATE_192KHZ) {
            pPvtData->audioRate = val;
        }
        else {
            AVB_LOG_ERROR("Invalid audio rate configured for intf_nv_audio_rate.");
            pPvtData->audioRate = AVB_AUDIO_RATE_44_1KHZ;
        }
    }
    else if (strcmp(name, "intf_nv_audio_bit_depth") == 0) {
        val = strtol(value, &pEnd, 10);
        if (val >= AVB_AUDIO_BIT_DEPTH_1BIT && val <= AVB_AUDIO_BIT_DEPTH_64BIT) {
            pPvtData->audioBitDepth = val;
        }
        else {
            AVB_LOG_ERROR("Invalid audio type configured for intf_nv_audio_bits.");
            pPvtData->audioBitDepth = AVB_AUDIO_BIT_DEPTH_24BIT;
        }
    }
    else if (strcmp(name, "intf_nv_audio_channels") == 0) {
        val = strtol(value, &pEnd, 10);
        if (val >= AVB_AUDIO_CHANNELS_1 && val <= AVB_AUDIO_CHANNELS_8) {
            pPvtData->audioChannels = val;
        }
        else {
            AVB_LOG_ERROR("Invalid audio channels configured for intf_nv_audio_channels.");
            pPvtData->audioChannels = AVB_AUDIO_CHANNELS_2;
        }
    }
    else if (strcmp(name, "intf_nv_number_of_data_bytes") == 0) {
        val = strtol(value, &pEnd, 10);
        if (val > 0) {
            pPvtData->numberOfDataBytes = val;
        }
        else {
            AVB_LOG_ERROR("Invalid number of data bytes for intf_nv_number_of_data_bytes.");
        }
    }
    else if (strcmp(name, "intf_nv_socket_name") == 0) {
        snprintf(pPvtData->socketPath, MAX_PATH_LEN,
                "/data/misc/eavb/.%s", value);
        AVB_LOGF_INFO("Using socket: %s", pPvtData->socketPath);
    }
    else if (strcmp(name, "intf_nv_ignore_timestamp") == 0) {
        int tmp = strtol(value, &pEnd, 10);
        if (*pEnd == '\0' && tmp == 1) {
            pPvtData->ignoreTimestamp = (tmp == 1);
        }
    }

    passParamToMapModule(pMediaQ);

    AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

void openavbIntfAudioStreamGenInitCB(media_q_t *pMediaQ) {
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);

    if (pMediaQ) {
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
        if (!pPvtData) {
            AVB_LOG_ERROR("Private interface module data not allocated.");
            return;
        }
    }

    AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

// A call to this callback indicates that this interface module will be
// a talker. Any talker initialization can be done in this function.
void openavbIntfAudioStreamTxInitCB(media_q_t *pMediaQ) {
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);

    if (pMediaQ) {
        // U8 b;
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
        if (!pPvtData) {
            AVB_LOG_ERROR("Private interface module data not allocated.");
            return;
        }

        pPvtData->pServerSocket = skt_connect(pPvtData->socketPath, SOCKET_BUFFER_SIZE);
        if (pPvtData->pServerSocket < 0) {
             AVB_LOG_ERROR("Failed to create data socket");
             return;
        }

        int errResult;
        THREAD_CREATE(halPollingThread, pPvtData->halPollingThread, NULL, halPollingThreadFn, pPvtData);
        THREAD_CHECK_ERROR(pPvtData->halPollingThread, "HAL polling Thread creation failed", errResult);
    }

    AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

static bool readAudio(pvt_data_t *pPvtData, uint8_t *buffer, U32 buflen) {
    MUTEX_LOCK_ALT(pPvtData->audioBufferMutex);

    // Check if we have enough data to fill request
    if (pPvtData->dataLeftInBuffer < buflen) {
        if (pPvtData->dataLeftInBuffer == 0) {
            // No data available, don't try to send packets
            MUTEX_UNLOCK_ALT(pPvtData->audioBufferMutex);
            return FALSE;
        } else {
            IF_LOG_INTERVAL(100) {
                AVB_LOGF_ERROR("readAudio - buffer underflow - need %d bytes, have %d bytes",
                        buflen, pPvtData->dataLeftInBuffer);
            }
            memcpy(buffer, pPvtData->audioBuffer + pPvtData->audioBufferPos, pPvtData->dataLeftInBuffer);
            memset(buffer + pPvtData->dataLeftInBuffer, 0, buflen - pPvtData->dataLeftInBuffer);
            pPvtData->audioBufferPos = 0;
            pPvtData->dataLeftInBuffer = 0;
        }
        MUTEX_UNLOCK_ALT(pPvtData->audioBufferMutex);
        return TRUE;
    }

    // Still have enough data left in the buffer, use it
    memcpy(buffer, pPvtData->audioBuffer + pPvtData->audioBufferPos, buflen);
    pPvtData->audioBufferPos += buflen;
    pPvtData->dataLeftInBuffer -= buflen;

    MUTEX_UNLOCK_ALT(pPvtData->audioBufferMutex);

    return TRUE;
}

// This callback will be called for each AVB transmit interval.
bool openavbIntfAudioStreamTxCB(media_q_t *pMediaQ) {
    AVB_TRACE_ENTRY(AVB_TRACE_INTF_DETAIL);

    if (pMediaQ) {
        media_q_pub_map_uncmp_audio_info_t *pPubMapUncmpAudioInfo = pMediaQ->pPubMapInfo;
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
        media_q_item_t *pMediaQItem = NULL;
        if (!pPvtData) {
            AVB_LOG_ERROR("Private interface module data not allocated.");
            return FALSE;
        }
        //put current wall time into tail item used by AAF mapping module
        if ((pPubMapUncmpAudioInfo->sparseMode != TS_SPARSE_MODE_UNSPEC)) {
            pMediaQItem = openavbMediaQTailLock(pMediaQ, TRUE);
            if ((pMediaQItem) && (pPvtData->intervalCounter % pPubMapUncmpAudioInfo->sparseMode == 0)) {
                openavbAvtpTimeSetToWallTime(pMediaQItem->pAvtpTime);
            }
            openavbMediaQTailUnlock(pMediaQ);
            pMediaQItem = NULL;
        }

        if (pPvtData->intervalCounter++ % pPubMapUncmpAudioInfo->packingFactor != 0) {
            return TRUE;
        }

        pMediaQItem = openavbMediaQHeadLock(pMediaQ);
        if (pMediaQItem) {
            if (pMediaQItem->itemSize < pPubMapUncmpAudioInfo->itemSize) {
                AVB_LOG_ERROR("Media queue item not large enough for samples");
            }

            bool rslt = FALSE;
            rslt = readAudio(pPvtData, (uint8_t*)pMediaQItem->pPubData,
                    pMediaQItem->itemSize);

            if (!rslt) {
                openavbMediaQHeadUnlock(pMediaQ);
                AVB_TRACE_EXIT(AVB_TRACE_INTF);
                return FALSE;
            }

            pMediaQItem->dataLen = pPubMapUncmpAudioInfo->itemSize;
            if (pMediaQItem->dataLen != pPubMapUncmpAudioInfo->itemSize) {
                openavbMediaQHeadUnlock(pMediaQ);
                AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
                return TRUE;
            }
            else {
                openavbAvtpTimeSetToWallTime(pMediaQItem->pAvtpTime);
                openavbMediaQHeadPush(pMediaQ);

                AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
                return TRUE;
            }
        }
        else {
            AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
            return FALSE;    // Media queue full
        }
    }

    AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
    return FALSE;
}

// A call to this callback indicates that this interface module will be
// a listener. Any listener initialization can be done in this function.
void openavbIntfAudioStreamRxInitCB(media_q_t *pMediaQ) {
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);

    if (pMediaQ) {
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
        struct stat buf;
        if (!pPvtData) {
            AVB_LOG_ERROR("Private interface module data not allocated.");
            return;
        }

        // Open data socket
    }
    AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

static bool consumeAudio(pvt_data_t *pPvtData, uint8_t *buffer, U32 buflen)
{
    // Check buffer full
    if ((pPvtData->audioBufferPos + buflen) > AUDIO_BUFF_SIZE) {
        AVB_LOG_ERROR("consumeAudio - out of buff space");
        pPvtData->audioBufferPos = 0;
    }

    // Append data to buffer
    memcpy(pPvtData->audioBuffer + pPvtData->audioBufferPos, buffer, buflen);
    pPvtData->audioBufferPos += buflen;

    // Check if we've accumulated enough data to send to receiver
    if (pPvtData->audioBufferPos > SOCKET_BUFFER_SIZE) {
        int writen = skt_write(pPvtData, pPvtData->audioBuffer, SOCKET_BUFFER_SIZE);
        if (writen <= 0) {
            AVB_LOGF_ERROR("consumeAudio - skt_write() error: %d, %s",
                writen, strerror(writen));
            return FALSE;
        }
        // Move remaining data in buffer to beginning of buf.
        memmove(pPvtData->audioBuffer, pPvtData->audioBuffer+writen, pPvtData->audioBufferPos - writen);
        pPvtData->audioBufferPos -= writen;
    }

    return TRUE;
}

// This callback is called when acting as a listener.
bool openavbIntfAudioStreamRxCB(media_q_t *pMediaQ)
{
    AVB_TRACE_ENTRY(AVB_TRACE_INTF_DETAIL);

    if (pMediaQ) {
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;

        bool moreItems = TRUE;

        while (moreItems) {
            media_q_item_t *pMediaQItem = openavbMediaQTailLock(pMediaQ, pPvtData->ignoreTimestamp);
            if (pMediaQItem) {
                if (pMediaQItem->dataLen) {
                    consumeAudio(pPvtData, pMediaQItem->pPubData, pMediaQItem->dataLen);
                }
                openavbMediaQTailPull(pMediaQ);
            }
            else {
                moreItems = FALSE;
            }
        }
    }
    AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
    return TRUE;
}

// This callback will be called when the interface needs to be closed. All shutdown should
// occur in this function.
void openavbIntfAudioStreamEndCB(media_q_t *pMediaQ) {
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);

    if (pMediaQ) {
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
        if (!pPvtData) {
            AVB_LOG_ERROR("Private interface module data not allocated.");
            return;
        }

        if (pPvtData->pServerSocket) {
            close(pPvtData->pServerSocket);
            pPvtData->pServerSocket = 0;
        }

        if (pPvtData->pollingThreadRunning) {
            pPvtData->pollingThreadRunning = 0;
            THREAD_JOIN(pPvtData->halPollingThread, NULL);
        }

        if (pPvtData->pHalSocket) {
            close(pPvtData->pHalSocket);
            pPvtData->pHalSocket = 0;
        }
    }

    AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

void openavbIntfAudioStreamGenEndCB(media_q_t *pMediaQ) 
{
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);

    if (pMediaQ) {
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
        if (!pPvtData) {
            AVB_LOG_ERROR("Private interface module data not allocated.");
            return;
        }

        if (pPvtData->pServerSocket) {
            close(pPvtData->pServerSocket);
            pPvtData->pServerSocket = 0;
        }

        if (pPvtData->pollingThreadRunning) {
            pPvtData->pollingThreadRunning = 0;
            THREAD_JOIN(pPvtData->halPollingThread, NULL);
        }

        if (pPvtData->pHalSocket) {
            close(pPvtData->pHalSocket);
            pPvtData->pHalSocket = 0;
        }
    }
    AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

// Main initialization entry point into the interface module
extern DLL_EXPORT bool openavbIntfAudioStreamInitialize(media_q_t *pMediaQ, openavb_intf_cb_t *pIntfCB)
{
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);

    if (pMediaQ) {
        pMediaQ->pPvtIntfInfo = calloc(1, sizeof(pvt_data_t));        // Memory freed by the media queue when the media queue is destroyed.

        if (!pMediaQ->pPvtIntfInfo) {
            AVB_LOG_ERROR("Unable to allocate memory for AVTP interface module.");
            return FALSE;
        }

        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;

        pIntfCB->intf_cfg_cb = openavbIntfAudioStreamCfgCB;
        pIntfCB->intf_gen_init_cb = openavbIntfAudioStreamGenInitCB;
        pIntfCB->intf_tx_init_cb = openavbIntfAudioStreamTxInitCB;
        pIntfCB->intf_tx_cb = openavbIntfAudioStreamTxCB;
        pIntfCB->intf_rx_init_cb = openavbIntfAudioStreamRxInitCB;
        pIntfCB->intf_rx_cb = openavbIntfAudioStreamRxCB;
        pIntfCB->intf_end_cb = openavbIntfAudioStreamEndCB;
        pIntfCB->intf_gen_end_cb = openavbIntfAudioStreamGenEndCB;
        pPvtData->audioEndian = AVB_AUDIO_ENDIAN_LITTLE;
        pPvtData->ignoreTimestamp = TRUE;
        pPvtData->socketPath[0] = '\0';
        pPvtData->pServerSocket = -1;
        pPvtData->pHalSocket = -1;
        pPvtData->pollingThreadRunning = 0;

        pPvtData->intervalCounter = 0;
        pPvtData->dataLeftInBuffer = 0;
        pPvtData->samplesIn = 0;
        pPvtData->samplesOut = 0;
    }

    AVB_TRACE_EXIT(AVB_TRACE_INTF);
    return TRUE;
}
