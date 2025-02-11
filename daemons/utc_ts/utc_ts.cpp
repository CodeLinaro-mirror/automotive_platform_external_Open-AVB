/*
Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*******************************************
*
*           utc_ts Application
*
*********************************************/
#include <cstdint>
#include <sys/timex.h>
#include <gptp_helper.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fenv.h>
#include <math.h>
#include <stdbool.h>



#ifdef ANDROID
#include <log/log.h>
#else
#include <syslog.h>
#endif

#ifdef ANDROID

#define LOGE(fmt, ...) __android_log_print (ANDROID_LOG_ERROR,"libgptp", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGW(fmt, ...) __android_log_print (ANDROID_LOG_WARN,"libgptp", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGI(fmt, ...) __android_log_print (ANDROID_LOG_INFO,"libgptp", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGD(fmt, ...) __android_log_print (ANDROID_LOG_DEBUG,"libgptp", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)

enum _LOGGER_SEVERITY {
    QCLOG_ERROR         = ANDROID_LOG_ERROR,
    QCLOG_WARNING       = ANDROID_LOG_WARN,
    QCLOG_INFO          = ANDROID_LOG_INFO,
    QCLOG_DEBUG2        = ANDROID_LOG_DEBUG
};
#endif


/* These 4 macros are used only when Syntonize mode is enabled */
#define INTEGRAL 0.0003             /*!< PI controller integral factor*/
#define PROPORTIONAL 1.0            /*!< PI controller proportional factor*/
#define UPPER_FREQ_LIMIT  250.0     /*!< Upper frequency limit */
#define LOWER_FREQ_LIMIT -250.0     /*!< Lower frequency limit */

#define UPPER_LIMIT_PPM 250
#define LOWER_LIMIT_PPM -250
#define PPM_OFFSET_TO_RATIO(ppm) ((ppm) / ((FrequencyRatio)US_PER_SEC) + 1)

#define MIN_LS_RATIO 0.5
#define MAX_LS_RATIO 2.0

/* This is the threshold in ns for which frequency adjustments will be made */
#define PHASE_ERROR_THRESHOLD (1000000000)

/* This is the maximum count of phase error, outside of the threshold before
   adjustment is performed */
#define PHASE_ERROR_MAX_COUNT (6)


typedef long double FrequencyRatio;

#define UTC_LOG_ERROR(fmt, ...) LOGE("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define UTC_LOG_WARNING(fmt, ...) LOGW("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define UTC_LOG_INFO(fmt, ...) LOGI("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define UTC_LOG_DEBUG(fmt, ...) LOGD("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)


bool hab_thread_running = false;
pthread_t hab_Thread;
int32_t hab_hdl;
uint64_t prev_gptp_time = 0;
uint64_t prev_utc_time = 0;

uint64_t prev_utc_ref = 0;
uint64_t prev_expected_utc_ref = 0;


#define HAB_MMID_CREATE(major, minor) ((major&0xFFFF) | ((minor&0xFF)<<16))
#define HABMM_SOCKET_RECV_FLAGS_NON_BLOCKING 0x00000001
#define HABMM_SOCKET_RECV_FLAGS_UNINTERRUPTIBLE 0x00000002
#define HABMM_VNW_1 1401
#define HAB_UTC_SUB_ID 1


extern "C" int32_t habmm_socket_open(int32_t *handle, uint32_t mm_ip_id,
                                     uint32_t timeout, uint32_t flags);
extern "C" int32_t habmm_socket_recv(int32_t handle, void *dst_buff,
                                     uint32_t *size_bytes, uint32_t timeout, uint32_t flags);
extern "C" int32_t habmm_socket_close(int32_t handle);

typedef enum {
    VALUE_STATE_UNAVAILABLE = 0,
    VALUE_STATE_VALID = 1,
    VALUE_STATE_INVALID = 2,
} ValueState;

typedef enum {
    VEHICLE_UTC_TIME_VALIDITY_TYPE_T_UNSPECIFIED = 0,
    VEHICLE_UTC_TIME_VALIDITY_TYPE_T_INVALID = 1,
    VEHICLE_UTC_TIME_VALIDITY_TYPE_T_VALID = 2,
} VehicleUtcTimeValidityTypeT;

struct utc_timeinfo_t {
    ValueState state;
    VehicleUtcTimeValidityTypeT sync_state;
    uint64_t curUtcTimeNanoSec;
    uint64_t curPtpTimeNanoSec;
};


int realtime_adjust_offset(long long offset)
{
    struct timex tx = {};
    int ret;
    memset(&tx, 0, sizeof(tx));
    tx.modes = ADJ_SETOFFSET | ADJ_NANO;
    tx.time.tv_sec = offset / 1000000000;
    tx.time.tv_usec = offset % 1000000000;

    if (offset < 0 && tx.time.tv_usec) {
        tx.time.tv_sec -= 1;
        tx.time.tv_usec += 1000000000;
    }

    ret = clock_adjtime(CLOCK_REALTIME, &tx);

    if (ret < 0) {
        UTC_LOG_ERROR("failed to realtime_adjust_offset %s", strerror(errno));
        return ret;
    }

    return 0;
}
int realtime_adjust_freq(float freq_offset)
{
    struct timex tx = {};
    memset(&tx, 0, sizeof(tx));
    tx.modes = ADJ_FREQUENCY;
    tx.freq  = long(freq_offset) << 16;
    tx.freq += long(fmodf( freq_offset, 1.0 ) * 65536.0);

    if (clock_adjtime(CLOCK_REALTIME, &tx) < 0) {
        UTC_LOG_ERROR("failed to realtime_adjust_freq %s", strerror(errno));
        return -1;
    }

    return 0;
}




void updateTime(utc_timeinfo_t* update)
{
    uint64_t curr_gptp = 0;
    bool sync_status = false;
    static int ppm_miss_count = 0;
    uint64_t curr_utc = 0;
    uint64_t curr_expected_utc = 0;
    int64_t delta_utc = 0;
    static float _ppm = 0;
    struct timespec real;
    long double phase_error;
    static float time_ratio = 1.0;
    static uint64_t cnt = 0;

    if (prev_utc_time != 0) {
        time_ratio = (update->curUtcTimeNanoSec -  prev_utc_time) /
                     (update->curPtpTimeNanoSec - prev_gptp_time);
    }

    gptpGetCurPtpTime_s(&curr_gptp, NULL);
    sync_status = gptpGetSyncStatus();
    clock_gettime(CLOCK_REALTIME, &real);

    if (!sync_status) {
        UTC_LOG_ERROR("Ignoring the UTC sync as gptp is not in sync");
        return;
    }

    curr_expected_utc = update->curUtcTimeNanoSec + (curr_gptp -
                        update->curPtpTimeNanoSec) * time_ratio;
    curr_utc = (real.tv_sec) * 1000000000LL + real.tv_nsec;
    delta_utc = curr_utc - curr_expected_utc;
    phase_error = (long double) - delta_utc;

    if ((fabsl(phase_error) > PHASE_ERROR_THRESHOLD) || prev_utc_time == 0
            || ppm_miss_count > 10) {
        realtime_adjust_offset(phase_error);
    } else {
        FrequencyRatio freq_offset = 0;
        freq_offset = ((FrequencyRatio)(curr_expected_utc - prev_utc_ref)) /
                      (curr_utc - prev_utc_ref);

        // Check for jumps in REAL time or gptp time
        if ((fabs(freq_offset) < MIN_LS_RATIO) || (fabs(freq_offset) > MAX_LS_RATIO)) {
            UTC_LOG_WARNING("Real to UTC clock ratio (%Lf) exceeding threshold %lld %lld",
                            freq_offset, (curr_utc - prev_utc_ref),
                            (curr_expected_utc - prev_utc_ref));
            freq_offset = 1.0;
        } else {
            UTC_LOG_DEBUG("Real to UTC clock ratio (%Lf) delta %lld %lld",
                          freq_offset, (curr_utc - prev_utc_ref),
                          (curr_expected_utc - prev_utc_ref));
        }

        float syncPerSec = (float)(1.0 / pow((float)2,
                                             (update->curUtcTimeNanoSec - prev_utc_time)));
        _ppm += (float) ((INTEGRAL * syncPerSec * phase_error) + PROPORTIONAL * ((
                             freq_offset - 1.0) * 1000000));
        UTC_LOG_DEBUG("phase_error = %Lf, ppm = %f", phase_error, _ppm );

        if ( _ppm < LOWER_FREQ_LIMIT ) {
            _ppm = LOWER_FREQ_LIMIT;
            ppm_miss_count++;
        } else if ( _ppm > UPPER_FREQ_LIMIT ) {
            _ppm = UPPER_FREQ_LIMIT;
            ppm_miss_count++;
        } else {
            ppm_miss_count = 0;
        }

        realtime_adjust_freq(_ppm);
    }

    prev_utc_time = update->curUtcTimeNanoSec;
    prev_gptp_time = update->curPtpTimeNanoSec;
    prev_utc_ref = curr_utc;
    prev_expected_utc_ref = curr_expected_utc;
    UTC_LOG_INFO("[%lu]curr_utc %lld curr_expected_utc %lld delta_utc %lld state %d",
                  cnt, curr_utc, curr_expected_utc, delta_utc, update->state);
    cnt++;
}


void* habLoop(void* param)
{
    int32_t ret;
    struct utc_timeinfo_t update;
    uint32_t len = sizeof(update);

    while (hab_thread_running) {
        memset(&update, 0, sizeof(update));

        do {
            ret = habmm_socket_recv(hab_hdl, &update, &len, 0,
                                    HABMM_SOCKET_RECV_FLAGS_UNINTERRUPTIBLE);
        } while (-EINTR == ret);

        if (ret) {
            UTC_LOG_ERROR("habmm_socket_recv failed, ret= 0x%x\n", ret);
            return NULL;
        }

        if (update.state == VALUE_STATE_VALID
                && update.sync_state == VEHICLE_UTC_TIME_VALIDITY_TYPE_T_VALID ) {
            updateTime(&update);
        } else {
            UTC_LOG_ERROR("Ignoring UTC update as status or time is not valid\n");
        }
    }

    return NULL;
}


int main(int argc, char **argv)
{
    int32_t ret;
    int sig;
    sigset_t set;
    int err = 0;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset( &set, SIGTERM );
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGUSR2);

    while (!gptpInit()) {
        UTC_LOG_WARNING("waiting for Gptp Init to  succeed\n");
        usleep(50000);
    }

    ret = habmm_socket_open(&hab_hdl, HAB_MMID_CREATE(HABMM_VNW_1, HAB_UTC_SUB_ID),
                            0, 0);

    if (ret < 0) {
        UTC_LOG_ERROR("habmm_socket_open: socket create failed\n");
        return 0;
    }

    hab_thread_running = true;

    if ((err = pthread_create(&hab_Thread, NULL, habLoop, (void *) NULL))
            < 0) {
        hab_thread_running = false;
        UTC_LOG_ERROR("Error during creation of the thread %d\n", err);
        return 0;
    } else {
        hab_thread_running = true;
    }

    do {
        sig = 0;

        if (sigwait(&set, &sig) != 0) {
            perror("sigwait()");
            break;
        }
    } while (sig == SIGHUP || sig == SIGUSR2);

    hab_thread_running = false;
    habmm_socket_close(hab_hdl);
    gptpDeinit();
    return 0;
}


