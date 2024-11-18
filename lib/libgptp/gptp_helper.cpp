/*===========================================================================
Copyright (c) 2019, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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

============================================================================ */

/* ============================================================================
Changes from Qualcomm Innovation Center, Inc. are provided under the following license:

Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
============================================================================ */
#include <linux/ptp_clock.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>           /* For O_* constants */
#include <linux_ipc.hpp>
#include <signal.h>
#include <sys/un.h>
#include "gptp_helper.h"
#include <atomic>
#include <limits.h>
#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef ANDROID
#include <log/log.h>
#else
#include <syslog.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
pthread_mutex_t gInitMutex = PTHREAD_MUTEX_INITIALIZER;
#define LOCK()      pthread_mutex_lock(&gInitMutex)
#define UNLOCK()    pthread_mutex_unlock(&gInitMutex)

#define CLOCKFD 3
#define FD_TO_CLOCKID(fd)   ((~(clockid_t) (fd) << 3) | CLOCKFD)
#define BUF_SIZE 500
#define GPTP_BOOTTIME_VALIDTY_RANGE 10000000000LL //using 10 sec max time difference to find the boot time ratio

#ifdef SYSTEMD
#ifdef ANDROID
#define ADDRESS     "/dev/socket/gptp_socket"
#else
#define ADDRESS     "/dev/socket/gptp/gptp_socket"
#endif // END OF ANDROID
#else
#define ADDRESS     "/tmp/gptp_socket"
#endif
#define CONNECT_RETRY_PERIOD_us  1000


#ifdef AVB_FEATURE_GVM_MODE
#define SCT_SHM_NAME  "/dev/sct_gptp_shm"     /*!< Shared memory name*/
#else
#ifdef ANDROID
#define SCT_SHM_NAME  "/dev/sctptpshm"     /*!< Shared memory name*/
#else
#define SCT_SHM_NAME  "/sct_ptp"        /*!< Shared memory name*/
#endif
#endif
#define SCT_SHM_SIZE 0x2000
#ifndef LOG_ERROR
#define LOG_ERROR    1
#endif
#ifndef LOG_WARNING
#define LOG_WARNING     2
#endif
#ifndef LOG_INFO
#define LOG_INFO     3
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG    4
#endif
#define GPTP_LOG_LEVEL LOG_INFO
#ifdef ANDROID

#define LOGE(fmt, ...) __android_log_print (ANDROID_LOG_ERROR,"libgptp", fmt, __VA_ARGS__)
#define LOGW(fmt, ...) __android_log_print (ANDROID_LOG_WARN,"libgptp", fmt, __VA_ARGS__)
#define LOGI(fmt, ...) __android_log_print (ANDROID_LOG_INFO,"libgptp", fmt, __VA_ARGS__)
#define LOGD(fmt, ...) __android_log_print (ANDROID_LOG_DEBUG,"libgptp", fmt, __VA_ARGS__)

enum _LOGGER_SEVERITY {
    QCLOG_ERROR         = ANDROID_LOG_ERROR,
    QCLOG_WARNING       = ANDROID_LOG_WARN,
    QCLOG_INFO          = ANDROID_LOG_INFO,
    QCLOG_DEBUG2        = ANDROID_LOG_DEBUG
};

#endif
#ifndef ANDROID

#define GPTP_LOG_ERROR(fmt, ...) system_log(LOG_ERROR, "[%d:%s:%d] " fmt ,gettid(),  __FUNCTION__, __LINE__,##__VA_ARGS__)
#define GPTP_LOG_WARNING(fmt, ...) system_log(LOG_WARNING, "[%d:%s:%d] " fmt ,gettid(),  __FUNCTION__, __LINE__,##__VA_ARGS__)
#define GPTP_LOG_INFO(fmt, ...) system_log(LOG_INFO, "[%d:%s:%d] " fmt ,gettid(),  __FUNCTION__, __LINE__,##__VA_ARGS__)
#define GPTP_LOG_DEBUG(fmt, ...) system_log(LOG_DEBUG, "[%d:%s:%d] " fmt ,gettid(),  __FUNCTION__, __LINE__,##__VA_ARGS__)

#else

#define GPTP_LOG_ERROR(fmt, ...) LOGE("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define GPTP_LOG_WARNING(fmt, ...) LOGW("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define GPTP_LOG_INFO(fmt, ...) LOGI("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define GPTP_LOG_DEBUG(fmt, ...) LOGD("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)

#endif

#define PTP_DEVICE "/dev/ptpXX"         /*!< Default PTP device */
#define PTP_DEVICE_IDX_OFFS 8           /*!< PTP device index offset*/

#define DAEMON_STATUS_UP 0xabcdef

static bool bInitialized = false;
static bool bServiceConnect = false;

/* Pipe file descriptors for cleanup the loop */
int pipefd[2];
fd_set readfds;
static int gPtpShmFd = -1;
static char *gPtpMmap = NULL;
static int gPtpSCTShmFd = -1;
static char *gPtpSCTMmap = NULL;
static gPtpTimeData gPtpTD;
static int gptpPhcFd = -1;
static clockid_t gPtpClockid = -1;
#ifdef  RGPTP_CLNT_ENABLED
static int rptp_fd = 0;
static clockid_t rgptp_clkid = -1;
#endif

static pthread_t thread_id;
static int sock = -1;

#ifdef LE_GVM
static int gptp_fd = -1;
#endif

GPTP_UPDATE_NOTIFY_CALLBACK gptp_update_callback = NULL;

#ifdef AVB_FEATURE_GVM_MODE

bool hab_thread_running = false;
pthread_t hab_Thread;
int32_t hab_hdl;

#define HABMM_SOCKET_RECV_FLAGS_NON_BLOCKING 0x00000001
#define HABMM_SOCKET_RECV_FLAGS_UNINTERRUPTIBLE 0x00000002
#define HABMM_VNW_1 1401

extern "C" int32_t habmm_socket_open(int32_t *handle, uint32_t mm_ip_id,
                                     uint32_t timeout, uint32_t flags);
extern "C" int32_t habmm_socket_recv(int32_t handle, void *dst_buff,
                                     uint32_t *size_bytes, uint32_t timeout, uint32_t flags);
extern "C" int32_t habmm_socket_close(int32_t handle);

#endif


typedef struct {
    pthread_mutex_t lock;
    syncMesaurementData_t syncData;
    pDelayMeasurementData_t delayData;
    gptpStatsType_t status;
    syncInterval_t syncInterval;
} sct_gptp_data;

void system_log(int loglevel, const char *s, ...)
{
    va_list arg = {};

    if (loglevel == LOG_ERROR || loglevel <= GPTP_LOG_LEVEL) {
        va_start(arg, s);
        vsyslog(loglevel, s, arg);
        va_end(arg);
    }
}

static int gptpClkInit(int *gptp_phc_fd)
{
#ifdef AVB_FEATURE_GVM_MODE
    *gptp_phc_fd = open("/dev/ptp0", O_RDWR );
#else
    char ptp_device[] = PTP_DEVICE;
    memcpy( ptp_device + PTP_DEVICE_IDX_OFFS,
            gPtpTD.ptp_dev_index,  sizeof(ptp_device) - PTP_DEVICE_IDX_OFFS);
    GPTP_LOG_INFO("opening clock device: %s", ptp_device);
    *gptp_phc_fd = open(ptp_device, O_RDWR );
#endif

    if ( *gptp_phc_fd == -1 ||
            (gPtpClockid = FD_TO_CLOCKID(*gptp_phc_fd)) == -1 ) {
        GPTP_LOG_ERROR("Failed to open PTP clock device\n");
        return false;
    }

    return true;
}

static void gptpClkDeInit(int gptp_phc_fd)
{
    if (gptp_phc_fd > 0) {
        close(gptp_phc_fd);
    }

    gPtpClockid = -1;
}


static bool gptpSCTMemInit()
{
    if (gPtpSCTShmFd  == -1) {
#ifdef AVB_FEATURE_GVM_MODE
        gPtpSCTShmFd = open( SCT_SHM_NAME, O_RDWR, 0);
#else
#ifdef ANDROID
        gPtpSCTShmFd = open( SCT_SHM_NAME, O_RDWR, 0);
#else
        gPtpSCTShmFd = shm_open(SCT_SHM_NAME, O_RDWR, 0);
#endif
#endif
        GPTP_LOG_INFO("gptpSCTMemInit %s %d\n", SCT_SHM_NAME, gPtpSCTShmFd);

        if (gPtpSCTShmFd == -1) {
            perror("shm_open()");
            return false;
        }

        gPtpSCTMmap =
            (char *)mmap(NULL, SCT_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                         gPtpSCTShmFd, 0);
        GPTP_LOG_INFO("gptpMemInit mmap pointer %p\n", gPtpSCTMmap);

        if (gPtpSCTMmap == (char *) -1) {
            perror("mmap()");
            gPtpSCTMmap = NULL;
#ifdef AVB_FEATURE_GVM_MODE

            if (gPtpSCTShmFd  != -1) {
                close(gPtpSCTShmFd);
                gPtpSCTShmFd = -1;
            }

            // unlink(SCT_SHM_NAME );
#else
#ifdef ANDROID

            if (gPtpSCTShmFd  != -1) {
                close(gPtpSCTShmFd);
                gPtpSCTShmFd = -1;
            }

            // unlink(SCT_SHM_NAME );
#else

            if (gPtpSCTShmFd  != -1) {
                close(gPtpSCTShmFd);
                gPtpSCTShmFd = -1;
            }

#endif
#endif
            GPTP_LOG_ERROR("gptpSCTMemInit failed %s\n", SCT_SHM_NAME);
            gPtpSCTShmFd = -1;
            return false;
        }
    }

    return true;
}

static void gptpSCTMemDeinit()
{
    if (gPtpSCTShmFd != -1) {
        if (gPtpSCTMmap != NULL) {
            munmap(gPtpSCTMmap, SCT_SHM_SIZE);
            gPtpSCTMmap = NULL;
        }

        if (gPtpSCTShmFd != -1) {
            close(gPtpSCTShmFd);
        }

        GPTP_LOG_INFO("gptpSCTMemDeinit %s\n", SCT_SHM_NAME);
        gPtpSCTShmFd = -1;
    }
}



/* gptp core function to init gptp scaling */
static int gptpMemInit(int *gptp_shm_fd, char **gptp_mmap)
{
    if (NULL == gptp_shm_fd) {
        return false;
    }

#ifdef AVB_FEATURE_GVM_MODE
    *gptp_shm_fd = open( SHM_NAME, O_RDWR, 0);
#else
#ifdef ANDROID
    *gptp_shm_fd = open( SHM_NAME, O_RDWR, 0);
#else
    *gptp_shm_fd = shm_open(SHM_NAME, O_RDWR, 0);
#endif
#endif
    GPTP_LOG_INFO("gptpMemInit %s %d\n", SHM_NAME, *gptp_shm_fd);

    if (*gptp_shm_fd == -1) {
        perror("shm_open()");
        return false;
    }

    *gptp_mmap =
        (char *)mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                     *gptp_shm_fd, 0);

    if (*gptp_mmap == (char *) -1) {
        perror("mmap()");
        GPTP_LOG_ERROR("gptpMemInit mmap pointer %p\n", *gptp_mmap);
        *gptp_mmap = NULL;
#ifdef AVB_FEATURE_GVM_MODE

        if (*gptp_shm_fd != -1) {
            close(*gptp_shm_fd);
            *gptp_shm_fd = -1;
        }

#else
#ifdef ANDROID

        if (*gptp_shm_fd != -1) {
            close(*gptp_shm_fd);
            *gptp_shm_fd = -1;
        }

        //unlink(SHM_NAME );
#else

        if (*gptp_shm_fd != -1) {
            close(*gptp_shm_fd);
            *gptp_shm_fd = -1;
        }

#endif
#endif
        GPTP_LOG_ERROR("gptpMemInit failed %s\n", SHM_NAME);
        return false;
    }

    gptpSCTMemInit();
    return true;
}

/* gptp core function to deinit gptp scaling */
static void gptpMemDeinit(int gptp_shm_fd, char *gptp_mmap)
{
    if (gptp_mmap != NULL) {
        munmap(gptp_mmap, SHM_SIZE);
        gptp_mmap = NULL;
    }

    if (gptp_shm_fd != -1) {
        close(gptp_shm_fd);
        gptp_shm_fd = -1;
    }

    GPTP_LOG_INFO("gptpMemDeinit %s\n", SHM_NAME);
    gptpSCTMemDeinit();
}

/* gptp core function to copy gptp offset data from shared memory */
static int gptpScaling(gPtpTimeData * td, char *memory_offset_buffer)
{
    if ((td == NULL) || (memory_offset_buffer == NULL)) {
        GPTP_LOG_ERROR("gptpScaling failure %p %p\n", td, memory_offset_buffer);
        return false;
    }

#ifndef AVB_FEATURE_GVM_MODE
    gPtpTimeData * checkstatus = (gPtpTimeData *) (memory_offset_buffer + sizeof(
                                     pthread_mutex_t));

    if (checkstatus->d_status != 0xabcdef ) {
        GPTP_LOG_WARNING("gptp daemon is not up");
        return false;
    }

    pthread_mutex_lock((pthread_mutex_t *) memory_offset_buffer);
    memcpy(td, memory_offset_buffer + sizeof(pthread_mutex_t), sizeof(*td));
    pthread_mutex_unlock((pthread_mutex_t *) memory_offset_buffer);
#else
    int buf_offset = 0;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;
    uint32_t a, b;
    gPtpTimeData *ptimedata;
    int count = 0;
    char *dest = (char*)td;
    char *src = NULL;
    buf_offset += (2 * sizeof(std::atomic<uint32_t>));
    seq0 = (std::atomic<uint32_t> *)memory_offset_buffer;
    seq1 = (std::atomic<uint32_t> *)(memory_offset_buffer + sizeof(
                                         std::atomic<uint32_t>));
    ptimedata   = (gPtpTimeData *) (memory_offset_buffer + buf_offset);
    src = (char *)ptimedata;

    do {
        a = seq0->load();
        b = seq1->load();

        //memcpy(td, ptimedata, sizeof(*td)); //commented due to bus error issue
        for (int i = 0; i < sizeof(gPtpTimeData); i++ ) {
            dest[i] = *(volatile char *)(&src[i]);
        }

        count++;
    } while ((a != b || a != seq0->load() || b != seq1->load()) && count < 3);

    if (count >= 3) {
        return false;
    }

#endif
    return true;
}

/* gptp core function to copy gptp offset data from shared memory */
static int updateGptpRsync(RsyncStatus_t *rSync, char *memory_offset_buffer)
{
    if ((rSync == NULL) || (memory_offset_buffer == NULL)) {
        GPTP_LOG_ERROR("updateGptpRsync failure %p %p\n", rSync, memory_offset_buffer);
        return false;
    }

#ifndef AVB_FEATURE_GVM_MODE
    gPtpTimeData *ptimedata;
    ptimedata = (gPtpTimeData *) (memory_offset_buffer + sizeof(pthread_mutex_t));
    pthread_mutex_lock((pthread_mutex_t *) memory_offset_buffer);
    ptimedata->reverseSyncEnabled = rSync->reverseSyncEnabled;
    ptimedata->reverseSyncDomain = rSync->reverseSyncDomain;
    ptimedata->reverseSyncRate = rSync->reverseSyncRate;
    pthread_mutex_unlock((pthread_mutex_t *) memory_offset_buffer);
#else
    int buf_offset = 0;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;
    uint32_t a, b;
    gPtpTimeData *ptimedata;
    int count = 0;
    buf_offset += (2 * sizeof(std::atomic<uint32_t>));
    seq0 = (std::atomic<uint32_t> *)memory_offset_buffer;
    seq1 = (std::atomic<uint32_t> *)(memory_offset_buffer + sizeof(
                                         std::atomic<uint32_t>));
    ptimedata   = (gPtpTimeData *) (memory_offset_buffer + buf_offset);

    do {
        a = seq0->load();
        b = seq1->load();
        ptimedata->reverseSyncEnabled = rSync->reverseSyncEnabled;
        ptimedata->reverseSyncDomain = rSync->reverseSyncDomain;
        ptimedata->reverseSyncRate = rSync->reverseSyncRate;
        count++;
    } while ((a != b || a != seq0->load() || b != seq1->load()) && count < 3);

#endif
    return true;
}

#ifdef AVB_FEATURE_GVM_MODE

void* habLoop(void* param)
{
    int32_t ret;
    struct gptp_update update;
    uint32_t len = sizeof(update);

    while (hab_thread_running) {
        memset(&update, 0, sizeof(update));

        do {
            ret = habmm_socket_recv(hab_hdl, &update, &len, 0,
                                    HABMM_SOCKET_RECV_FLAGS_UNINTERRUPTIBLE);
        } while (-EINTR == ret);

        if (ret) {
            GPTP_LOG_ERROR("habmm_socket_recv failed, ret= 0x%x\n", ret);
            return NULL;
        }

        if (gptp_update_callback) {
            gptp_update_callback(update);
        }
    }

    return NULL;
}

#endif

bool gptpRegisterCallback(GPTP_UPDATE_NOTIFY_CALLBACK fn_ptr)
{
    if (fn_ptr == NULL && gptp_update_callback == NULL) {
        return false;
    }

#ifdef AVB_FEATURE_GVM_MODE
    int err = 0;
    int32_t ret;

    if (gptp_update_callback != NULL && hab_thread_running) {
        hab_thread_running = false;
        habmm_socket_close(hab_hdl);
    }

    gptp_update_callback = fn_ptr;

    if (fn_ptr != NULL) {
        ret = habmm_socket_open(&hab_hdl, HABMM_VNW_1, 0, 0);

        if (ret < 0) {
            GPTP_LOG_ERROR("habmm_socket_open: socket create failed\n");
            return false;
        }

        hab_thread_running = true;

        if ((err = pthread_create(&hab_Thread, NULL, habLoop, (void *) NULL))
                < 0) {
            hab_thread_running = false;
            GPTP_LOG_ERROR("Error during creation of the thread %d\n", err);
            return false;
        } else {
            hab_thread_running = true;
        }
    }

#endif
    return true;
}

/* gptp core function query gptp time */
static bool gptpLocalTime(const gPtpTimeData *td, uint64_t *now_local,
                          uint64_t *time_sys_ns)
{
    uint64_t system_time = 0;
    int64_t delta_local = 0;
    int64_t delta_system = 0;

    if (!td || !now_local || !time_sys_ns) {
        return false;
    }

    system_time = td->local_time + td->ls_phoffset;
    delta_system = *time_sys_ns - system_time;
    delta_local = td->ls_freqoffset * delta_system;
    /* now_local contains the scaled gptp local time
    corresponding to system time */
    *now_local = td->local_time + delta_local;

    if (*now_local == 0) {
        return false;
    }

    return true;
}


/* gptp core function query gptp time */
static bool gptpLocalQTime(const gPtpTimeData *td, uint64_t *now_local,
                           uint64_t *time_qtime_ns)
{
    uint64_t qtimer_time = 0;
    int64_t delta_local = 0;
    int64_t delta_qtimer = 0;

    if (!td || !now_local || !time_qtime_ns) {
        return false;
    }

    qtimer_time = td->local_time + td->lq_phoffset;
    delta_qtimer = *time_qtime_ns - qtimer_time;
    delta_local = td->lq_freqoffset * delta_qtimer;
    /* now_local contains the scaled gptp local time
    corresponding to qtimer time*/
    *now_local = td->local_time + delta_local;

    if (*now_local == 0) {
        return false;
    }

    return true;
}


/* gptp core function query gptp time */
static bool gptpLocalBTime(const gPtpTimeData *td, uint64_t *now_local,
                           uint64_t *time_btime_ns)
{
    uint64_t boot_time = 0;
    int64_t delta_local = 0;
    int64_t delta_btimer = 0;

    if (!td || !now_local || !time_btime_ns) {
        return false;
    }

    boot_time = td->local_time + td->lb_phoffset;
    delta_btimer = *time_btime_ns - boot_time;
    delta_local = td->lb_freqoffset * delta_btimer;
    /* now_local contains the scaled gptp local time
    corresponding to qtimer time*/
    *now_local = td->local_time + delta_local;

    if (*now_local == 0) {
        return false;
    }

    return true;
}

/* intermediate wrapper to init gptp scaling */
static bool gptpTimeInit(void)
{
    if (!gptpMemInit(&gPtpShmFd, &gPtpMmap)) {
        return false;
    }

    if (!gptpScaling(&gPtpTD, gPtpMmap)) {
        gptpMemDeinit(gPtpShmFd, gPtpMmap);
        return false;
    }

    if (!gptpClkInit(&gptpPhcFd)) {
        gptpMemDeinit(gPtpShmFd, gPtpMmap);
        return false;
    }

    return true;
}

static void *gptpDaemonSrvConnect(void *arg)
{
    int ret = -1;
    int len = 0;
    struct sockaddr_un saun;
    int bytes_read = 0;
    int retry_count = 0;
    int gptp_state = 0;
    char buf[BUF_SIZE];

    while (bServiceConnect) {
        if (sock == -1) {
            if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
                GPTP_LOG_ERROR("gptpDaemonSrvConnect: socket create failed\n");
                return NULL;
            }

            memset(&saun, 0, sizeof(sockaddr_un));
            saun.sun_family = AF_UNIX;
            snprintf(saun.sun_path, (sizeof(saun.sun_path) - 1), ADDRESS);
            len = sizeof(saun.sun_family) + strlen(saun.sun_path);
        }

        ret = connect(sock, (struct sockaddr*) &saun, len);

        /* EISCONN -- Transport endpoint is already connected */
        if ((ret == 0) || ((ret == -1) && (errno == EISCONN))) {
            LOCK();

            if (!bInitialized) {
                if (gptpTimeInit()) {
                    GPTP_LOG_INFO("gptpDaemonSrvConnect: success\n");
                    bInitialized = true;
                } else {
                    gptpMemDeinit(gPtpShmFd, gPtpMmap);
                    gptpClkDeInit(gptpPhcFd);
                    memset(&gPtpTD, 0, sizeof(gPtpTimeData));
                    UNLOCK();
                    GPTP_LOG_INFO("gptpDaemonSrvConnect: initialization failed\n");
                    close(sock);
                    sock = -1;
                    usleep(1000000);
                    continue;
                }
            }

            UNLOCK();
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            FD_SET(pipefd[0], &readfds);

            do {
                ret = select((pipefd[0] > sock ? pipefd[0] : sock) + 1, &readfds, NULL, NULL,
                             NULL);
            } while ((ret == -1) && (errno == EINTR));

            if (ret != -1) {
                if (FD_ISSET(pipefd[0], &readfds)) {
                    char pipebuf;
                    read(pipefd[0], &pipebuf, 1);

                    if (pipebuf == '1') {
                        GPTP_LOG_INFO("clean up thread\n");
                        ret = -1;
                    }
                } else if (FD_ISSET(sock, &readfds)) {
                    ret = read(sock, buf, BUF_SIZE);

                    if (ret == 0) {
                        close(sock);
                        sock = -1;
                    }
                }
            } else {
                GPTP_LOG_ERROR("gptpDaemonSrvConnect: select errno %d\n", errno);
            }
        }

        if ((ret == -1) && bInitialized) {
            GPTP_LOG_ERROR("gptpDaemonSrvConnect: cleanup errno %d %d\n", errno,
                           bInitialized);
            LOCK();
            gptpMemDeinit(gPtpShmFd, gPtpMmap);
            gptpClkDeInit(gptpPhcFd);
            memset(&gPtpTD, 0, sizeof(gPtpTimeData));
            bInitialized = false;
            UNLOCK();
        }

        usleep(CONNECT_RETRY_PERIOD_us);
    }

    return NULL;
}

static int gptpDaemonClientInit(void)
{
    int ret = 0;

    if (bServiceConnect == true || sock != -1) {
        GPTP_LOG_INFO("gptpDaemonClientInit: already initialized\n");
        return true;
    }

    pipefd[0] = -1;
    pipefd[1] = -1;

    if (pipe(pipefd) == -1) {
        GPTP_LOG_ERROR("pipe create error\n");
        return false;
    }

    if (gptpTimeInit()) {
        GPTP_LOG_INFO("gptpDaemonSrvConnect: success\n");
        bInitialized = true;
    } else {
        if (pipefd[0] != -1) {
            close(pipefd[0]);
            pipefd[0] = -1;
        }

        if (pipefd[1] != -1) {
            close(pipefd[1]);
            pipefd[1] = -1;
        }

        return false;
    }

#ifndef AVB_FEATURE_GVM_MODE
    ret = pthread_create(&thread_id, NULL, gptpDaemonSrvConnect, NULL);

    if (ret != 0) {
        GPTP_LOG_ERROR("gptpDaemonClientInit: failed -->%s\n", strerror(errno));

        if (pipefd[0] != -1) {
            close(pipefd[0]);
        }

        if (pipefd[1] != -1) {
            close(pipefd[1]);
        }

        return false;
    }

    ret = pthread_setname_np(thread_id, "GPTP-HELPER");

    if (ret != 0) {
        GPTP_LOG_ERROR("Failed to set thread name \n");
    }

#endif
    return true;
}

static void gptpDaemonClientDeInit(void)
{
#ifndef AVB_FEATURE_GVM_MODE
    char data = '1';
    int ret = 0;
    bServiceConnect = false;

    if (pipefd[1] != -1) {
        write(pipefd[1], &data, 1);
    }

    ret = pthread_join(thread_id, NULL);

    if (ret != 0) {
        GPTP_LOG_ERROR("gptpDaemonClientDeInit: failed -->%s\n", strerror(errno));
    }

    if (sock > 0) {
        close(sock);
        sock = -1;
    }

#endif

    // Release the Pipe
    if (pipefd[0] != -1) {
        close(pipefd[0]);
        pipefd[0] = -1;
    }

    if (pipefd[1] != -1) {
        close(pipefd[1]);
        pipefd[1] = -1;
    }

    return;
}

/* public API to query gptp time */
bool gptpGetPtpTimeFromMonoTime_s(uint64_t *gptp_time_sys,
                                  uint64_t time_mono_ns, bool* inSync)
{
#ifndef AVB_FEATURE_GVM_MODE
    uint64_t now_local = 0;
    uint64_t update_8021as = 0;
    int64_t delta_8021as = 0;
    int64_t delta_local = 0;
    uint64_t time_mono_qtime_ns  = 0;

    if (!bInitialized) {
        return false;
    }

    if (!gptpScaling(&gPtpTD, gPtpMmap)) {
        return false;
    }

    if (gPtpTD.d_status != DAEMON_STATUS_UP) {
        GPTP_LOG_WARNING("Daemon not up!!");
        return false;
    }

    if (inSync) {
        *inSync = gPtpTD.sync_status;
    }

    time_mono_qtime_ns =  time_mono_ns +
                          gPtpTD.qtime_to_mono_offset; //Qtimer is ahead from monotonic

    if (gptpLocalQTime(&gPtpTD, &now_local, &time_mono_qtime_ns)) {
        update_8021as = gPtpTD.local_time - gPtpTD.ml_phoffset;
        delta_local = now_local - gPtpTD.local_time;
        delta_8021as = gPtpTD.ml_freqoffset * delta_local;
        *gptp_time_sys = update_8021as + delta_8021as;
        return true;
    }

#endif
    return false;
}


bool gptpGetPtpTimeFromMonoTime(uint64_t *gptp_time_sys, uint64_t time_mono_ns)
{
    return gptpGetPtpTimeFromMonoTime_s(gptp_time_sys, time_mono_ns, NULL);
}


/* public API to query gptp time */
bool gptpGetPtpTimeFromQTimeNs_s(uint64_t *gptp_time_qt,
                                 uint64_t time_qtimer_ns, bool* inSync)
{
#ifndef AVB_FEATURE_GVM_MODE
    uint64_t now_local = 0;
    uint64_t update_8021as = 0;
    int64_t delta_8021as = 0;
    int64_t delta_local = 0;
    uint64_t time_ns = time_qtimer_ns;

    if (!bInitialized) {
        return false;
    }

    if (!gptpScaling(&gPtpTD, gPtpMmap)) {
        return false;
    }

    if (gPtpTD.d_status != DAEMON_STATUS_UP) {
        GPTP_LOG_WARNING("Daemon not up!!");
        return false;
    }

    if (inSync) {
        *inSync = gPtpTD.sync_status;
    }

    if (gptpLocalQTime(&gPtpTD, &now_local, &time_ns)) {
        update_8021as = gPtpTD.local_time - gPtpTD.ml_phoffset;
        delta_local = now_local - gPtpTD.local_time;
        delta_8021as = gPtpTD.ml_freqoffset * delta_local;
        *gptp_time_qt = update_8021as + delta_8021as;
        return true;
    }

#endif
    return false;
}

bool gptpGetPtpTimeFromQTimeNs(uint64_t *gptp_time_qt, uint64_t time_qtimer_ns)
{
    return gptpGetPtpTimeFromQTimeNs_s(gptp_time_qt, time_qtimer_ns, NULL);
}


/* public API to query gptp time */
bool gptpGetPtpTimeFromBootTime_s(uint64_t *gptp_time_bt, uint64_t time_boot_ns,
                                  bool* inSync)
{
    if (!gptp_time_bt || !bInitialized) {
        return false;
    }

    *gptp_time_bt = 0;
#ifndef AVB_FEATURE_GVM_MODE
    uint64_t now_local = 0;
    uint64_t update_8021as = 0;
    int64_t delta_8021as = 0;
    int64_t delta_local = 0;
    uint64_t time_ns = time_boot_ns;

    if (!gptpScaling(&gPtpTD, gPtpMmap)) {
        return false;
    }

    GPTP_LOG_DEBUG("gptpGetPtpTimeFromBootTime offset %lld freqoffset %Lf qtimeoffset %lld \n",
                   gPtpTD.lb_phoffset, gPtpTD.lb_freqoffset, gPtpTD.qtime_to_mono_offset);

    if (gPtpTD.d_status != DAEMON_STATUS_UP) {
        GPTP_LOG_WARNING("Daemon not up!!");
        return false;
    }

    if (inSync) {
        *inSync = gPtpTD.sync_status;
    }

    if (gptpLocalBTime(&gPtpTD, &now_local, &time_ns)) {
        update_8021as = gPtpTD.local_time - gPtpTD.ml_phoffset;
        delta_local = now_local - gPtpTD.local_time;
        delta_8021as = gPtpTD.ml_freqoffset * delta_local;
        *gptp_time_bt = update_8021as + delta_8021as;
        return true;
    }

    return false;
#else
    uint64_t *gptp_mem;
    uint64_t *boot_time_mem;
    static double boot_gptp_ratio = 1.0;
    static uint64_t prev_gptp = 0;
    static uint64_t prev_boot = 0;
    struct timespec ts;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;
    uint32_t a, b;
    int count = 0;
    gPtpTimeData *ptimedata;
    int buf_offset = 0;
    buf_offset += (2 * sizeof(std::atomic<uint32_t>));
    ptimedata   = (gPtpTimeData *) (gPtpMmap + buf_offset);
    seq0 = (std::atomic<uint32_t> *)gPtpMmap;
    seq1 = (std::atomic<uint32_t> *)(gPtpMmap + sizeof(std::atomic<uint32_t>));
    ts.tv_sec = ts.tv_nsec = 0;

    do {
        a = seq0->load();
        b = seq1->load();

        if (clock_gettime(gPtpClockid, &ts)) {
            GPTP_LOG_ERROR("clock_gettime failed");
            return false;
        }

        if (ts.tv_sec == 0 && ts.tv_nsec == 0) {
            GPTP_LOG_WARNING("gptp time read taking longer time\n");
            return false;
        }

        gptp_mem = (uint64_t *) (gPtpMmap + 0x1000 - 3 * sizeof(uint64_t));
        boot_time_mem = (uint64_t *) (gPtpMmap + 0x1000 - 9 * sizeof(uint64_t));
        count++;
    } while ((a != b || a != seq0->load() || b != seq1->load()) && count < 3);

    if (count < 3) {
        int gptpdiff = *gptp_mem - time_boot_ns;

        if (gptpdiff > GPTP_BOOTTIME_VALIDTY_RANGE
                || gptpdiff < -GPTP_BOOTTIME_VALIDTY_RANGE) {
            GPTP_LOG_INFO("gptp time provided beyond range for better accuracy\n");
            return false;
        }

        gptpdiff = *gptp_mem - prev_gptp;

        if (gptpdiff > GPTP_BOOTTIME_VALIDTY_RANGE || gptpdiff < 0) {
            prev_boot = 0;
            boot_gptp_ratio = 1.0;
        }

        if (prev_gptp != 0 && prev_boot != 0) {
            boot_gptp_ratio = (2 * boot_gptp_ratio + ((double)(*boot_time_mem -
                               prev_boot)) / (*gptp_mem - prev_gptp) ) / 3;
        }

        *gptp_time_bt = *gptp_mem - ((int64_t)(*boot_time_mem - time_boot_ns)) /
                        boot_gptp_ratio;
        prev_boot = *boot_time_mem;
        prev_gptp = *gptp_mem;
    } else {
        GPTP_LOG_INFO("dint get valid values\n");
        return false;
    }

    if (inSync) {
        *inSync = ptimedata->sync_status;
    }

    return true;
#endif
}

bool gptpGetPtpTimeFromBootTime(uint64_t *gptp_time_bt, uint64_t time_boot_ns)
{
    return gptpGetPtpTimeFromBootTime_s(gptp_time_bt, time_boot_ns, NULL);
}


bool gptpGetBootTimeFromPtpTime_s(uint64_t *boot_time_ns, uint64_t ptp_time_ns,
                                  bool* inSync)
{
    if (!boot_time_ns || !bInitialized) {
        return false;
    }

    *boot_time_ns = 0;
#ifndef AVB_FEATURE_GVM_MODE

    if (!gptpScaling(&gPtpTD, gPtpMmap)) {
        return false;
    }

    if (gPtpTD.d_status != DAEMON_STATUS_UP) {
        GPTP_LOG_WARNING("Daemon not up!!");
        return false;
    }

    if (inSync) {
        *inSync = gPtpTD.sync_status;
    }

    int gptpdiff = 0;
    gptpdiff =  ptp_time_ns - gPtpTD.local_time;

    if (gptpdiff > GPTP_BOOTTIME_VALIDTY_RANGE
            || gptpdiff < -GPTP_BOOTTIME_VALIDTY_RANGE) {
        GPTP_LOG_ERROR("gptp time provided beyond range for better accuracy\n");
        return false;
    }

    GPTP_LOG_DEBUG("gptpGetBootTimeFromPtpTime offset %lld freqoffset %Lf qtimeoffset %lld \n",
                   gPtpTD.lb_phoffset, gPtpTD.lb_freqoffset, gPtpTD.qtime_to_mono_offset);
    *boot_time_ns = gPtpTD.local_time + gPtpTD.lb_phoffset; //curr boot time

    if (gPtpTD.lb_freqoffset) {
        *boot_time_ns += (gptpdiff / gPtpTD.lb_freqoffset);
    } else {
        *boot_time_ns += gptpdiff;
    }

#else
    uint64_t *gptp_mem;
    uint64_t *boot_time_mem;
    static double boot_gptp_ratio = 1.0;
    static uint64_t prev_gptp = 0;
    static uint64_t prev_boot = 0;
    struct timespec ts;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;
    uint32_t a, b;
    int count = 0;
    ts.tv_sec = ts.tv_nsec = 0;
    gPtpTimeData *ptimedata;
    int buf_offset = 0;
    buf_offset += (2 * sizeof(std::atomic<uint32_t>));
    ptimedata   = (gPtpTimeData *) (gPtpMmap + buf_offset);
    seq0 = (std::atomic<uint32_t> *)gPtpMmap;
    seq1 = (std::atomic<uint32_t> *)(gPtpMmap + sizeof(std::atomic<uint32_t>));

    do {
        a = seq0->load();
        b = seq1->load();

        if (clock_gettime(gPtpClockid, &ts)) {
            GPTP_LOG_ERROR("clock_gettime failed");
            return false;
        }

        if (ts.tv_sec == 0 && ts.tv_nsec == 0) {
            GPTP_LOG_INFO("gptp time read taking longer time\n");
            return false;
        }

        gptp_mem = (uint64_t *) (gPtpMmap + 0x1000 - 3 * sizeof(uint64_t));
        boot_time_mem = (uint64_t *) (gPtpMmap + 0x1000 - 9 * sizeof(uint64_t));
        count++;
    } while ((a != b || a != seq0->load() || b != seq1->load()) && count < 3);

    if (count < 3) {
        int gptpdiff = *gptp_mem - ptp_time_ns;

        if (gptpdiff > GPTP_BOOTTIME_VALIDTY_RANGE
                || gptpdiff < -GPTP_BOOTTIME_VALIDTY_RANGE) {
            GPTP_LOG_ERROR("gptp time provided beyond range for better accuracy\n");
            return false;
        }

        gptpdiff = *gptp_mem - prev_gptp;

        if (gptpdiff > GPTP_BOOTTIME_VALIDTY_RANGE || gptpdiff < 0) {
            prev_boot = 0;
            boot_gptp_ratio = 1.0;
        }

        if (prev_gptp != 0 && prev_boot != 0) {
            boot_gptp_ratio = (2 * boot_gptp_ratio + ((double)(*boot_time_mem -
                               prev_boot)) / (*gptp_mem - prev_gptp) ) / 3;
        }

        *boot_time_ns = *boot_time_mem - ((int64_t)(*gptp_mem - ptp_time_ns)) *
                        boot_gptp_ratio;
        prev_boot = *boot_time_mem;
        prev_gptp = *gptp_mem;
    } else {
        GPTP_LOG_ERROR("dint get valid values\n");
        return false;
    }

    if (inSync) {
        *inSync = ptimedata->sync_status;
    }

#endif
    return true;
}

bool gptpGetBootTimeFromPtpTime(uint64_t *boot_time_ns, uint64_t ptp_time_ns)
{
    return gptpGetBootTimeFromPtpTime_s(boot_time_ns, ptp_time_ns, NULL);
}


bool gptpGetPtpTimeFromQTimeTickCount_s(uint64_t *gptp_time_sys,
                                        uint64_t qtime_ticks, bool* inSync)
{
    bool ret = false;
#ifndef AVB_FEATURE_GVM_MODE
    uint64_t qTimerFreq = 0, qtimer_sec = 0, qtimer_nanos_NSec = 0,
             time_qtimer_ns = 0;
#if __aarch64__
    asm volatile("mrs %0, cntfrq_el0" : "=r"(qTimerFreq));
#else
    qTimerFreq = 19200000; //19.2 MHz TBD: find right asm instruction
#endif
    qtimer_sec = (qtime_ticks / qTimerFreq);
    qtimer_nanos_NSec = (qtime_ticks % qTimerFreq);
    qtimer_nanos_NSec *= 1000000000;
    qtimer_nanos_NSec /= qTimerFreq;
    time_qtimer_ns = qtimer_sec * 1000000000 + qtimer_nanos_NSec;
    ret = gptpGetPtpTimeFromQTimeNs(gptp_time_sys, time_qtimer_ns);
#endif
    return ret;
}

bool gptpGetPtpTimeFromQTimeTickCount(uint64_t *gptp_time_sys,
                                      uint64_t qtime_ticks)
{
    return gptpGetPtpTimeFromQTimeTickCount_s(gptp_time_sys, qtime_ticks, NULL);
}

/* public API to query gptp time */
bool gptpGetPtpTimefromSystime_s(uint64_t *gptp_time_sys, uint64_t time_sys_ns,
                                 bool* inSync)
{
#ifndef AVB_FEATURE_GVM_MODE
    uint64_t now_local = 0;
    uint64_t update_8021as = 0;
    int64_t delta_8021as = 0;
    int64_t delta_local = 0;
    uint64_t time_ns = time_sys_ns;
    gPtpTimeData gPtpTD;

    if (!bInitialized) {
        return false;
    }

    if (!gptpScaling(&gPtpTD, gPtpMmap)) {
        return false;
    }

    if (gPtpTD.d_status != DAEMON_STATUS_UP) {
        GPTP_LOG_WARNING("Daemon not up!!");
        return false;
    }

    if (inSync) {
        *inSync = gPtpTD.sync_status;
    }

    if (gptpLocalTime(&gPtpTD, &now_local, &time_ns)) {
        update_8021as = gPtpTD.local_time - gPtpTD.ml_phoffset;
        delta_local = now_local - gPtpTD.local_time;
        delta_8021as = gPtpTD.ml_freqoffset * delta_local;
        *gptp_time_sys = update_8021as + delta_8021as;
        return true;
    }

#endif
    return false;
}


bool gptpGetPtpTimefromSystime(uint64_t *gptp_time_sys, uint64_t time_sys_ns)
{
    return gptpGetPtpTimefromSystime_s(gptp_time_sys, time_sys_ns, NULL);
}


/* public API to query gptp time */
bool gptpGetTime(uint64_t *gptp_time_sys,
                 uint64_t time_sys_ns) //just an alias for backward compatibility
{
    return gptpGetPtpTimefromSystime_s(gptp_time_sys, time_sys_ns, NULL);
}



/* public API to query gptp Port State */
int gptpGetPortState(void)
{
    gPtpTimeData gPtpTD;

    if (!bInitialized) {
        return false;
    }

    if (!gptpScaling(&gPtpTD, gPtpMmap)) {
        return false;
    }

    return gPtpTD.port_state;
}


/* public API to enable/disable reverse sync */
int setRsyncStatus(RsyncStatus_t *status)
{
    GPTP_LOG_ERROR("%s : ENTER \n", __func__);

    if (!bInitialized) {
        return -1;
    }

    if (!updateGptpRsync(status, gPtpMmap)) {
        return -1;
    }

    GPTP_LOG_ERROR("%s : EXIT \n", __func__);
    return 0;
}

int getTimeError(int64_t *timeError)
{
    gPtpTimeData gPtpTD;

    if (!bInitialized) {
        return -1;
    }

    if (!gptpScaling(&gPtpTD, gPtpMmap)) {
        return -1;
    }

    if (gPtpTD.port_state == PTP_MASTER) {
        *timeError = gPtpTD.ml_phoffset;
    } else {
        return 1;
    }

    return 0;
}

/* public API to query gptp sync status */
bool gptpGetSyncStatus(void)
{
    gPtpTimeData gPtpTD;

    if (!bInitialized) {
        return false;
    }

    if (!gptpScaling(&gPtpTD, gPtpMmap)) {
        return false;
    }

    return gPtpTD.sync_status;
}

/* public API to query current gptp time */
bool gptpGetCurPtpTime_s(uint64_t *gptp_time_cur, bool* inSync)
{
#ifdef LE_GVM
    int ret = 0;
    gptpTimeInfo_t ptp_data;

    if (gptp_fd != -1) {
        ret = ioctl(gptp_fd, GET_PTP_DATA, &ptp_data);

        if (ret) {
            GPTP_LOG_ERROR(" Ioctl failed to get ptp data 0x%x (%s)\n", errno,
                           strerror(errno));
            close(gptp_fd);
            gptp_fd = -1;
            return false;
        }
    } else {
        return false;
    }

    *gptp_time_cur = (ptp_data.tv_sec) * 1000000000LL + ptp_data.tv_nsec;

    if (inSync) {
        *inSync = ptp_data.status;
    }

#else
    struct timespec ts;
    ts.tv_sec = ts.tv_nsec = 0;
    *gptp_time_cur = 0;

    if (!bInitialized) {
        return false;
    }

    if (clock_gettime(gPtpClockid, &ts)) {
        GPTP_LOG_ERROR("clock_gettime failed");
        return false;
    }

    *gptp_time_cur = (ts.tv_sec) * 1000000000LL + ts.tv_nsec;
#endif
    return true;
}

bool gptpGetCurPtpTime(uint64_t *gptp_time_cur)
{
    return gptpGetCurPtpTime_s(gptp_time_cur, NULL);
}


bool gptpGetSyncMeasurementData(syncMesaurementData_t *syncData)
{
    int ret = false;
#ifndef AVB_FEATURE_GVM_MODE

    if (syncData == NULL) {
        GPTP_LOG_ERROR("Invalid SyncData parameter");
        return ret;
    }

    if (gPtpSCTMmap == NULL) {
        GPTP_LOG_ERROR("gptpGetSyncMeasurementData memory failure %p\n", gPtpSCTMmap);
        gptpSCTMemInit();
        return false;
    }

    sct_gptp_data* data = (sct_gptp_data*)gPtpSCTMmap;
    pthread_mutex_lock((pthread_mutex_t *) &data->lock);
    memcpy(syncData, &data->syncData,
           sizeof(syncMesaurementData_t));
    pthread_mutex_unlock((pthread_mutex_t *) &data->lock);
    ret = true;
#ifdef LIBGPTP_DEBUG
    GPTP_LOG_INFO("qgptp Sync Measurement Data: precise_origin_timestamp %"PRIu64" reference_local_timestamp %"PRIu64" \
			sync_ingress_timestamp %"PRIu64" correction_field %"PRIu64" sequence_id %d pDelay %"PRIu64" portNumber %d \
			clockIdentity "CLK_STR"\n",
                  syncData->precise_origin_timestamp,
                  syncData->reference_local_timestamp,
                  syncData->sync_ingress_timestamp,
                  syncData->correction_field,
                  syncData->sequence_id,
                  syncData->pDelay,
                  syncData->portNumber,
                  CLK_TO_STR(syncData->clockIdentity));
#endif
#endif
    return ret;
}

bool gptpGetPDelayMeasurementData(pDelayMeasurementData_t *delayData)
{
    int ret = false;
#ifndef AVB_FEATURE_GVM_MODE

    if (delayData == NULL) {
        GPTP_LOG_INFO("Invalid SyncData parameter");
        return false;
    }

    if (gPtpSCTMmap == NULL) {
        GPTP_LOG_ERROR("gptpGetPDelayMeasurementData memory failure %p\n", gPtpSCTMmap);
        gptpSCTMemInit();
        return false;
    }

    sct_gptp_data* data = (sct_gptp_data*)gPtpSCTMmap;
    pthread_mutex_lock((pthread_mutex_t *) &data->lock);
    memcpy(delayData, &data->delayData,
           sizeof(pDelayMeasurementData_t));
    pthread_mutex_unlock((pthread_mutex_t *) &data->lock);
    ret = true;
    GPTP_LOG_INFO("libgptp library: resp_clockIdentity " CLK_STR "",
                  CLK_TO_STR(delayData->resp_clockIdentity));
#ifdef LIBGPTP_DEBUG
    GPTP_LOG_INFO("qgptp PDelay Measurement Data: request_origin_timestamp %"PRIu64" request_receipt_timestamp %"PRIu64"\
			response_origin_timestamp %"PRIu64" response_receipt_timestamp %"PRIu64" reference_local_timestamp %"PRIu64"\
			sequence_id %d pDelay %"PRIu64" req_portNumber %d req_clockIdentity "CLK_STR" resp_portNumber %d resp_clockIdentity "CLK_STR"\n",
                  delayData->request_origin_timestamp,
                  delayData->request_receipt_timestamp,
                  delayData->response_origin_timestamp,
                  delayData->response_receipt_timestamp,
                  delayData->reference_local_timestamp,
                  delayData->sequence_id,
                  delayData->pDelay,
                  delayData->req_portNumber,
                  CLK_TO_STR(delayData->req_clockIdentity),
                  delayData->resp_portNumber,
                  CLK_TO_STR(delayData->resp_clockIdentity));
#endif
#endif
    return ret;
}

bool getgPTPStatus(gptpStatsType_t *status)
{
    int ret = false;
#ifndef AVB_FEATURE_GVM_MODE

    if (status == NULL) {
        GPTP_LOG_ERROR("Invalid SyncData parameter");
        return false;
    }

    if (gPtpSCTMmap == NULL) {
        GPTP_LOG_ERROR("getgPTPStatus memory failure %p\n", gPtpSCTMmap);
        gptpSCTMemInit();
        return false;
    }

    sct_gptp_data* data = (sct_gptp_data*)gPtpSCTMmap;
    pthread_mutex_lock((pthread_mutex_t *) &data->lock);
    memcpy(status, &data->status,
           sizeof(gptpStatsType_t));
    pthread_mutex_unlock((pthread_mutex_t *) &data->lock);
    ret = true;
#ifdef LIBGPTP_DEBUG
    GPTP_LOG_INFO("qgptp Status Data: gptp_status %d rate_deviation %f IsMaster %d offset %"PRIu64" ",
                  status->gptp_status,
                  status->rate_deviation,
                  status->IsMaster,
                  status->offset);
#endif
#endif
    return ret;
}

/* public API to query gptp status, port status and current gptp time */
bool gptpGetStatusAndCurPtpTime(gptpTimeInfo_t *ptp_data)
{
#ifdef LE_GVM
    int ret = 0;

    if (gptp_fd != -1) {
        ret = ioctl(gptp_fd, GET_PTP_DATA, ptp_data);

        if (ret) {
            GPTP_LOG_ERROR(" Ioctl failed to get ptp data 0x%x (%s)\n", errno,
                           strerror(errno));
            close(gptp_fd);
            gptp_fd = -1;
            return false;
        }
    } else {
        return false;
    }

    return true;
#else
    uint64_t gptp_time = 0;
    ptp_data->status = gptpGetSyncStatus();
    ptp_data->port_status = gptpGetPortState();

    if (gptpGetCurPtpTime(&gptp_time)) {
        ptp_data->tv_sec = gptp_time / 1000000000UL;
        ptp_data->tv_nsec = gptp_time % 1000000000UL;
    } else {
        return false;
    }

    return true;
#endif
}


bool gptpGetCurgPtpMonotonicPair_s(uint64_t *gptp_time_cur,
                                   uint64_t *mono_time_cur, bool* inSync)
{
    *gptp_time_cur = 0;
    *mono_time_cur = 0;
#ifdef AVB_FEATURE_GVM_MODE
    uint64_t *gptp_mem;
    uint64_t *mono_mem;
    struct timespec ts;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;
    uint32_t a, b;
    int count = 0;
    gPtpTimeData *ptimedata;
    int buf_offset = 0;
    buf_offset += (2 * sizeof(std::atomic<uint32_t>));
    ptimedata   = (gPtpTimeData *) (gPtpMmap + buf_offset);
    ts.tv_sec = ts.tv_nsec = 0;
    *gptp_time_cur = 0;
    *mono_time_cur = 0;

    if (!bInitialized) {
        return false;
    }

    seq0 = (std::atomic<uint32_t> *)gPtpMmap;
    seq1 = (std::atomic<uint32_t> *)(gPtpMmap + sizeof(std::atomic<uint32_t>));

    do {
        a = seq0->load();
        b = seq1->load();

        if (clock_gettime(gPtpClockid, &ts)) {
            GPTP_LOG_ERROR("clock_gettime failed 0x%x (%s)\n", errno, strerror(errno));
            return false;
        }

        if (ts.tv_sec == 0 && ts.tv_nsec == 0) {
            GPTP_LOG_WARNING("gptp time read taking longer time\n");
            return false;
        }

        gptp_mem = (uint64_t *) (gPtpMmap + 0x1000 - 3 * sizeof(uint64_t));
        mono_mem = (uint64_t *) (gPtpMmap + 0x1000 - 4 * sizeof(uint64_t));
        *gptp_time_cur = *gptp_mem;
        *mono_time_cur = *mono_mem;
        count++;
    } while ((a != b || a != seq0->load() || b != seq1->load()) && count < 3);

    if (count >= 3) {
        return false;
    }

    if (inSync) {
        *inSync = ptimedata->sync_status;
    }

#else
    struct timespec t;
    t.tv_sec = t.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &t);
    *mono_time_cur = (t.tv_sec) * 1000000000LL + t.tv_nsec;
    return gptpGetPtpTimeFromMonoTime_s(gptp_time_cur, *mono_time_cur, inSync);
#endif
    return true;
}

bool gptpGetCurgPtpMonotonicPair(uint64_t *gptp_time_cur,
                                 uint64_t *mono_time_cur)
{
    return gptpGetCurgPtpMonotonicPair_s(gptp_time_cur, mono_time_cur, NULL);
}


/* public API to init gptp time scaling */
bool gptpInit(void)
{
#ifdef LE_GVM
    gptp_fd = open("/dev/gptp", O_RDWR);

    if ( gptp_fd == -1 ) {
        GPTP_LOG_ERROR("Failed to open /dev/gptp error 0x%x(%s)\n", errno,
                       strerror(errno));
        return false;
    }

    return true;
#else
    return gptpDaemonClientInit();
#endif
}

/* public API to deinit gptp time scaling */
bool gptpDeinit(void)
{
#ifdef LE_GVM

    if (gptp_fd != -1) {
        close(gptp_fd);
        gptp_fd = -1;
    }

#else
    gptpMemDeinit(gPtpShmFd, gPtpMmap);
    gptpClkDeInit(gptpPhcFd);
    gptpDaemonClientDeInit();
    bInitialized = false;
#endif
    return true;
}

#ifdef  RGPTP_CLNT_ENABLED
/* public API to query current rgptp time */
bool rgptpGetCurPtpTime(uint64_t *rgptp_time)
{
    struct timespec ts;
    ts.tv_sec = ts.tv_nsec = 0;
    *rgptp_time = 0;

    if (clock_gettime(rgptp_clkid, &ts)) {
        GPTP_LOG_ERROR("clock_gettime failed");
        return false;
    }

    *rgptp_time = (ts.tv_sec) * 1000000000LL + ts.tv_nsec;
    return true;
}

/* public API to init rgptp time scaling */
bool rgptpInit(void)
{
    rptp_fd = open("/dev/ptp1", O_RDWR );

    if ( rptp_fd == -1 ||
            (rgptp_clkid = FD_TO_CLOCKID(rptp_fd)) == -1 ) {
        GPTP_LOG_ERROR("%s, Failed to open PTP clock device\n", __func__);
        return false;
    }

    return true;
}

/* public API to deinit rgptp time scaling */
bool rgptpDeinit(void)
{
    if (rptp_fd < 0) {
        close(rptp_fd);
    }

    rgptp_clkid = -1;
    return true;
}
#endif

bool handleGptpGetTimeIf(uint64_t *gptp_time_ns, uint64_t time_sys_ns)
{
    return gptpGetTime(gptp_time_ns, time_sys_ns);
}
bool handleGptpGetPtpTimeFromQTimeNsIf(uint64_t *gptp_time_ns,
                                       uint64_t time_qtimer_ns)
{
    return gptpGetPtpTimeFromQTimeNs(gptp_time_ns, time_qtimer_ns);
}
bool handleGptpGetPtpTimeFromQTimeTickCountIf(uint64_t *gptp_time_ns,
        uint64_t qtime_ticks)
{
    return gptpGetPtpTimeFromQTimeTickCount(gptp_time_ns, qtime_ticks);
}
bool handleGptpGetPtpTimeFromMonoTimeIf(uint64_t *gptp_time_ns,
                                        uint64_t time_mono_ns)
{
    return gptpGetPtpTimeFromMonoTime(gptp_time_ns, time_mono_ns);
}
bool handleGptpGetCurPtpTimeIf(uint64_t *gptp_time_ns)
{
    return gptpGetCurPtpTime(gptp_time_ns);
}
bool handleGptpGetCurgPtpMonotonicPairIf(uint64_t *gptp_time_cur,
        uint64_t *mono_time_cur)
{
    return gptpGetCurgPtpMonotonicPair(gptp_time_cur, mono_time_cur);
}
bool handleGptpGetBootTimeFromPtpTimeIf(uint64_t *boot_time_ns,
                                        uint64_t ptp_time_ns)
{
    return gptpGetBootTimeFromPtpTime(boot_time_ns, ptp_time_ns);
}
bool handleGptpGetPtpTimeFromBootTimeIf(uint64_t *ptp_time_ns,
                                        uint64_t boot_time_ns)
{
    return gptpGetPtpTimeFromBootTime(ptp_time_ns, boot_time_ns);
}
bool handleGetgPTPStatusIf(gptpStatsType_t *status)
{
    return getgPTPStatus(status);
}

bool handleGPTPGetSyncStatusIf(void)
{
    return gptpGetSyncStatus();
}

bool handleGPTPGetPortStateIf(void)
{
    return gptpGetPortState();
}


bool handleGptpInitIf(void)
{
    return gptpInit();
}
bool handleGptpDeinitIf(void)
{
    return gptpDeinit();
}
const gPTPLibInterfaceEvent* gPTPEventIf = nullptr;
bool handleGptpRegisterEvent(void)
{
    gptpRegisterCallback(gPTPEventIf->gPTP_Update_Event);
    return true;
}
bool handleGptpUnregisterEvent(void)
{
    gptpRegisterCallback(nullptr);
    return true;
}
const static gPTPLibInterfaceReq gPTPReqIf {
    handleGptpGetTimeIf,
    handleGptpGetPtpTimeFromQTimeNsIf,
    handleGptpGetPtpTimeFromQTimeTickCountIf,
    handleGptpGetPtpTimeFromMonoTimeIf,
    handleGptpGetCurPtpTimeIf,
    handleGptpGetCurgPtpMonotonicPairIf,
    handleGptpGetBootTimeFromPtpTimeIf,
    handleGptpGetPtpTimeFromBootTimeIf,
    handleGetgPTPStatusIf,
    handleGptpInitIf,
    handleGptpDeinitIf,
    handleGptpRegisterEvent,
    handleGptpUnregisterEvent,
    handleGPTPGetSyncStatusIf,
    handleGPTPGetPortStateIf
};
const gPTPLibInterfaceReq* get_gPTPLib_if(const gPTPLibInterfaceEvent*
        eventCallback)
{
    if ((nullptr != eventCallback) &&
            (nullptr != eventCallback->gPTP_Update_Event)) {
        gPTPEventIf = eventCallback;
    }

    return (&gPTPReqIf);
}
#ifdef __cplusplus
}
#endif
