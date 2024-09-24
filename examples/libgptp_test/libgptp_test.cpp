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

============================================================================ */

/* ============================================================================
Changes from Qualcomm Innovation Center, Inc. are provided under the following license:

Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
============================================================================ */

#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <gptp_helper.h>

#define CLOCKFD 3
#define FD_TO_CLOCKID(fd)   ((~(clockid_t) (fd) << 3) | CLOCKFD)

uint64_t systemTime(int clock)
{
    uint64_t ret;
    static const clockid_t clocks[] = {
        CLOCK_REALTIME,
        CLOCK_MONOTONIC,
    };
    struct timespec t;
    t.tv_sec = t.tv_nsec = 0;
    clock_gettime(clocks[clock], &t);
    ret = (t.tv_sec) * 1000000000LL + t.tv_nsec;
    return ret;
}

uint64_t getQtimerTime()
{
    uint64_t qTimerCount = 0, qTimerFreq = 0, qTimerSec = 0, qTimerNanosNSec = 0;
#if __aarch64__
    asm volatile("mrs %0, cntvct_el0" : "=r" (qTimerCount));
    asm volatile("mrs %0, cntfrq_el0" : "=r"(qTimerFreq));
#else
    asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (qTimerCount));
    qTimerFreq =  19200000; //19.2 MHz TBD: find right asm instruction
#endif
    qTimerSec = (qTimerCount / qTimerFreq);
    qTimerNanosNSec = (qTimerCount % qTimerFreq);
    qTimerNanosNSec *= 1000000000;
    qTimerNanosNSec /= qTimerFreq;
    return (qTimerSec * 1000000000 + qTimerNanosNSec) ;
}

uint64_t getQtimerTicks()
{
    uint64_t qTimerCount = 0, qTimerFreq = 0, qTimerSec = 0, qTimerNanosNSec = 0;
#if __aarch64__
    asm volatile("mrs %0, cntvct_el0" : "=r" (qTimerCount));
#else
    asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (qTimerCount));
#endif
    return (qTimerCount) ;
}

void do_some_tests_qtimer()
{
    int i = 0;
    struct timespec ts = { 0, 1000000 };
    uint64_t prev_vec_time;
    uint64_t prev_gptp_time;
    uint64_t test_vec_time;
    uint64_t test_gptp_time;
    int64_t delta_vec_time;
    int64_t delta_gptp_time;
    prev_vec_time = test_vec_time = getQtimerTime();
    gptpGetPtpTimeFromQTimeNs(&prev_gptp_time, prev_vec_time);

    for (i = 0; i < 100000; i++)
        if (gptpGetPtpTimeFromQTimeNs(&test_gptp_time, test_vec_time)) {
            delta_vec_time = test_vec_time;
            delta_vec_time -= prev_vec_time;
            delta_gptp_time = test_gptp_time;
            delta_gptp_time -= prev_gptp_time;
            printf("ns qtimer_time %" PRIi64 "  gptp_time %" PRIi64 "\n",
                   delta_vec_time, delta_gptp_time);
            prev_vec_time = test_vec_time;
            prev_gptp_time = test_gptp_time;
            nanosleep(&ts, NULL);
            test_vec_time += 1000000UL;
        } else {
            printf("Qtimer time test failed\n");
        }
}

void do_some_tests_sys()
{
    int i = 0;
    struct timespec ts = { 0, 1000000 };
    uint64_t prev_vec_time;
    uint64_t prev_gptp_time;
    uint64_t test_vec_time;
    uint64_t test_gptp_time;
    int64_t delta_vec_time;
    int64_t delta_gptp_time;
    prev_vec_time = test_vec_time = systemTime(CLOCK_REALTIME);
    gptpGetPtpTimefromSystime(&prev_gptp_time, prev_vec_time);

    for (i = 0; i < 100000; i++) {
        if (gptpGetPtpTimefromSystime(&test_gptp_time, test_vec_time)) {
            delta_vec_time = test_vec_time;
            delta_vec_time -= prev_vec_time;
            delta_gptp_time = test_gptp_time;
            delta_gptp_time -= prev_gptp_time;
            printf("ns sys_time %" PRIi64 "  gptp_time %" PRIi64 "\n",
                   delta_vec_time, delta_gptp_time);
            prev_vec_time = test_vec_time;
            prev_gptp_time = test_gptp_time;
            nanosleep(&ts, NULL);
            test_vec_time += 1000000UL;
        } else {
            printf("Qtimer time test failed\n");
        }
    }
}

void loop_test(int time_us)
{
    uint64_t ptp_time;
    syncMesaurementData_t syncData;
    pDelayMeasurementData_t delayData;
    gptpStatsType_t status;
    uint64_t qtimer_time;
    uint64_t prev_qtimer_time;
    uint64_t prev_ptp_time;
    int64_t delta_qtimer_time;
    int64_t delta_ptp_time;
    int64_t delta_qtimer_ptp;
    int64_t time_error;
    int rcvid = 0;
    prev_qtimer_time = getQtimerTime();
    gptpGetCurPtpTime(&prev_ptp_time);

    for (int i = 0; i < 100000; i++) {
        qtimer_time = getQtimerTime();

        if (gptpGetCurPtpTime(&ptp_time)) {
            delta_qtimer_time = qtimer_time - prev_qtimer_time;
            delta_ptp_time = ptp_time - prev_ptp_time;
            delta_qtimer_ptp = qtimer_time - ptp_time;
            printf("loop_test: qtimer_time %" PRIu64 " ptp_time %" PRIu64 " qtimer_delta %"
                   PRIi64 "  ptp_delta %" PRIi64  " qtimer_ptp_delta %" PRIi64 "\n",
                   qtimer_time, ptp_time, delta_qtimer_time, delta_ptp_time, delta_qtimer_ptp);
            prev_qtimer_time = qtimer_time;
            prev_ptp_time = ptp_time;
        } else {
            printf("Failed to get PTP time\n");
        }

        memset(&syncData, 0, sizeof(syncData));

        if (gptpGetSyncMeasurementData(&syncData)) {
            printf("loop_test: *************** Sync Measurement Data *****************\n");
            printf("loop_test: precise_origin_timestamp %" PRIu64 "\n",
                   syncData.precise_origin_timestamp);
            printf("loop_test: reference_local_timestamp %" PRIu64 "\n",
                   syncData.reference_local_timestamp);
            printf("loop_test: reference_global_timestamp %" PRIu64 "\n",
                   syncData.reference_global_timestamp);
            printf("loop_test: sync_ingress_timestamp %" PRIu64 "\n",
                   syncData.sync_ingress_timestamp);
            printf("loop_test: correction_field %" PRIu64 "\n", syncData.correction_field);
            printf("loop_test: sequence_id %" PRIu64 "\n", syncData.sequence_id);
            printf("loop_test: pDelay %" PRIu64 "\n", syncData.pDelay);
            printf("loop_test: portNumber %d\n", syncData.portNumber);
            printf("loop_test: clockIdentity " CLK_STR "\n",
                   CLK_TO_STR(syncData.clockIdentity));
        } else {
            printf("Failed to get Sync Measurement Data\n");
        }

        memset(&status, 0, sizeof(status));

        if (getgPTPStatus(&status)) {
            printf("loop_test: *********************** Status Data ************************\n");
            printf("loop_test: gptp_status %d\n", status.gptp_status);
            printf("loop_test: rate_deviation %f\n", status.rate_deviation);
            printf("loop_test: IsMaster %d\n", status.IsMaster);
            printf("loop_test: offset %" PRId64 "\n", status.offset);
        } else {
            printf("Failed to get GPTP Stat Data\n");
        }

        memset(&delayData, 0, sizeof(delayData));

        if (gptpGetPDelayMeasurementData(&delayData)) {
            printf("loop_test: ********************** PDelay Measurement Data ********************\n");
            printf("loop_test: request_origin_timestamp %" PRIu64 "\n",
                   delayData.request_origin_timestamp);
            printf("loop_test: request_receipt_timestamp %" PRIu64 "\n",
                   delayData.request_receipt_timestamp);
            printf("loop_test: response_origin_timestamp %" PRIu64 "\n",
                   delayData.response_origin_timestamp);
            printf("loop_test: response_receipt_timestamp %" PRIu64 "\n",
                   delayData.response_receipt_timestamp);
            printf("loop_test: reference_local_timestamp %" PRIu64 "\n",
                   delayData.reference_local_timestamp);
            printf("loop_test: reference_global_timestamp %" PRIu64 "\n",
                   delayData.reference_global_timestamp);
            printf("loop_test: sequence_id %" PRIu64 "\n", delayData.sequence_id);
            printf("loop_test: pDelay %" PRIu64 "\n", delayData.pDelay);
            printf("loop_test: req_portNumber %d\n", delayData.req_portNumber);
            printf("loop_test: req_clockIdentity " CLK_STR "\n",
                   CLK_TO_STR(delayData.req_clockIdentity));
            printf("loop_test: resp_portNumber %d\n", delayData.resp_portNumber);
            printf("loop_test: resp_clockIdentity " CLK_STR "\n",
                   CLK_TO_STR(delayData.resp_clockIdentity));
        } else {
            printf("Failed to get Path Delay Measurement Data\n");
        }

        rcvid = getTimeError(&time_error);
        if ( rcvid == 0) {
            printf("loop_test: ********************** Reverse sync - Slave clock offset ********************\n");
            printf("loop_test: time error %d\n", time_error);
        } else if (rcvid < 0) {
            printf("Failed to get time error\n");
        }
        usleep(time_us);
    }
}


void do_some_tests_ptp()
{
    int i = 0;
    uint64_t ptp_time = 0;

    for (i = 0; i < 100000; i++) {
        if (gptpGetCurPtpTime(&ptp_time)) {
            printf("ns ptp_time %" PRIu64 "\n", ptp_time);
        }
    }
}

void callback_handler(struct gptp_update update)
{
    printf("callback_handler:: got callback %" PRIu64 " %" PRId64 " \n",
           update.curr_gptp_time, update.clock_adjust);
}

void do_some_tests_gptp_mono()
{
    int i = 0;
    uint64_t ptp_time = 0;
    uint64_t mono_time = 0;
    printf("do_some_tests_gptp_mono");

    for (i = 0; i < 100000; i++) {
        if (gptpGetCurgPtpMonotonicPair(&ptp_time, &mono_time)) {
            printf("ns ptp_time %" PRIu64 "ns mono_time %" PRIu64 "\n", ptp_time,
                   mono_time);
        }
    }
}
void get_gptp_time()
{
    struct timespec ts;
    static clockid_t gPtpClockid = -1;
    uint64_t curr_gptp_time;
#ifdef AVB_FEATURE_GVM_MODE
    int gptp_phc_fd = open("/dev/ptp0", O_RDWR );

    if ( gptp_phc_fd == -1 ||
            (gPtpClockid = FD_TO_CLOCKID(gptp_phc_fd)) == -1 ) {
        printf("Failed to open PTP clock device error 0x%x(%s)\n", errno,
               strerror(errno));
        return;
    }

    if (clock_gettime(gPtpClockid, &ts)) {
        printf("clock_gettime failed 0x%x (%s)\n", errno, strerror(errno));
        close(gptp_phc_fd);
        return;
    }

    if (ts.tv_sec == 0 && ts.tv_nsec == 0) {
        printf("gptp time read taking longer time\n");
        close(gptp_phc_fd);
        return;
    }

    curr_gptp_time = (ts.tv_sec) * 1000000000LL + ts.tv_nsec;
    printf("current gptp time = %ld\n", curr_gptp_time);
    close(gptp_phc_fd);
#endif
    return;
}
void do_some_tests_gptp_boot()
{
    int i = 0;
    uint64_t ptp_time = 0;
    uint64_t boot_time_ns = 0;
    struct timespec boot;
    printf("do_some_tests_gptp_boot");

    for (i = 0; i < 100000; i++) {
        gptpGetCurPtpTime(&ptp_time);
        clock_gettime(CLOCK_BOOTTIME, &boot);
        boot_time_ns = boot.tv_sec * 1000000000LL + boot.tv_nsec;
        printf("current ptp_time %" PRIu64 " ns boot_time_ns %" PRIu64 "\n", ptp_time,
               boot_time_ns);

        if (gptpGetBootTimeFromPtpTime(&boot_time_ns, ptp_time)) {
            printf("gptpGetBootTimeFromPtpTime ptp_time %" PRIu64 " ns boot_time_ns %"
                   PRIu64 "\n", ptp_time,
                   boot_time_ns);
        }

        if (gptpGetPtpTimeFromBootTime(&ptp_time, boot_time_ns)) {
            printf("gptpGetPtpTimeFromBootTime ptp_time %" PRIu64 " ns boot_time_ns %"
                   PRIu64 "\n", ptp_time,
                   boot_time_ns);
        }

        gptpGetCurPtpTime(&ptp_time);
        clock_gettime(CLOCK_BOOTTIME, &boot);
        boot_time_ns = boot.tv_sec * 1000000000LL + boot.tv_nsec;
        ptp_time -= 1000000000LL; //just asking for boot time a second before
        boot_time_ns -= 1000000000LL;
        printf("current -1s ptp_time %" PRIu64 " ns boot_time_ns %" PRIu64 "\n",
               ptp_time,
               boot_time_ns);

        if (gptpGetBootTimeFromPtpTime(&boot_time_ns, ptp_time)) {
            printf("gptpGetBootTimeFromPtpTime ptp_time %" PRIu64 " ns boot_time_ns %"
                   PRIu64 "\n", ptp_time,
                   boot_time_ns);
        }

        if (gptpGetPtpTimeFromBootTime(&ptp_time, boot_time_ns)) {
            printf("gptpGetPtpTimeFromBootTime ptp_time %" PRIu64 " ns boot_time_ns %"
                   PRIu64 "\n", ptp_time,
                   boot_time_ns);
        }

        sleep(1);
    }
}

#ifdef RGPTP_CLNT_ENABLED
static void rgptp_test(void)
{
    bool rgptp_avail = false;
    uint64_t test_rgptp_time;
    rgptp_avail = rgptpInit();

    if (rgptp_avail) {
        printf("RGPTP Available\n");

        if (rgptpGetCurPtpTime(&test_rgptp_time)) {
            printf("rgptp time %" PRIu64 ".%" PRIu64 "\n",
                   test_rgptp_time / 1000000000UL, test_rgptp_time % 1000000000UL);
        } else {
            printf("RGPTP time test failed\n");
        }

        if (!rgptpDeinit()) {
            printf("RGPTP deinit failed\n");
        }
    } else {
        printf("RGPTP Not Available\n");
    }

    return;
}

static void do_some_tests_rgptp_s(int time_s)
{
    uint64_t rptp_time = 0;
    uint64_t ptp_time = 0;
    int i = 0;
    int64_t ns = 0;

    if (rgptpInit()) {
        for (i = 0; i < 200; i++) {
            gptpGetCurPtpTime(&ptp_time);
            rgptpGetCurPtpTime(&rptp_time);
            ns = (ptp_time - rptp_time);
            printf("gptp time: %" PRIu64 "rgptp time: %"PRIu64 " diff:%" PRId64 "\n",
                   ptp_time, rptp_time, ns);
            sleep(time_s);
        }

        if (!rgptpDeinit()) {
            printf("RGPTP deinit failed\n");
        }
    } else {
        printf("RGPTP Not Available\n");
    }

    return;
}

static void do_some_tests_rgptp_u(int time_us)
{
    uint64_t rptp_time = 0;
    uint64_t ptp_time = 0;
    int i = 0;
    int64_t ns = 0;

    if (rgptpInit()) {
        for (i = 0; i < 200; i++) {
            gptpGetCurPtpTime(&ptp_time);
            rgptpGetCurPtpTime(&rptp_time);
            ns = (ptp_time - rptp_time);
            printf("gptp time: %" PRIu64 "rgptp time: %"PRIu64 " diff:%" PRId64 "\n",
                   ptp_time, rptp_time, ns);
            usleep(time_us);
        }

        if (!rgptpDeinit()) {
            printf("RGPTP deinit failed\n");
        }
    } else {
        printf("RGPTP Not Available\n");
    }

    return;
}
#endif

int main(int argc, char *argv[])
{
    uint64_t test_vec_time;
    uint64_t test_gptp_time;
    bool gptp_scaling_available = false;
    gptpTimeInfo_t ptp_data;
    int retry = 0;
    RsyncStatus_t Rsync;
    gptp_scaling_available = gptpInit();

    if (gptp_scaling_available) {
        printf("Gptp Init Success\n");
    } else {
        printf("GPTP Init Failure\n");
        return 0;
    }

    if (gptpGetStatusAndCurPtpTime(&ptp_data)) {
        if (ptp_data.status) {
            printf("gptp status %d port status %d gptp time %" PRIu64 ".%" PRIu64 "\n",
                   ptp_data.status, ptp_data.port_status, ptp_data.tv_sec, ptp_data.tv_nsec);
        } else {
            printf("gptp status %d port status %d\n", ptp_data.status,
                   ptp_data.port_status);
        }
    } else {
        printf("GPTP time test failed\n");
    }

#ifndef LE_GVM
    test_vec_time = systemTime(CLOCK_REALTIME);

    if (gptpGetPtpTimefromSystime(&test_gptp_time, test_vec_time)) {
        printf("real_time %" PRIu64 ".%" PRIu64 "  gptp_time %" PRIu64 ".%" PRIu64 "\n",
               test_vec_time / 1000000000UL, test_vec_time % 1000000000UL,
               test_gptp_time / 1000000000UL, test_gptp_time % 1000000000UL);
    } else {
        printf("Real time test failed\n");
    }

    test_vec_time = getQtimerTime();

    if (gptpGetPtpTimeFromQTimeNs(&test_gptp_time, test_vec_time)) {
        printf("qtimer_time %" PRIu64 ".%" PRIu64 "  gptp_time %" PRIu64 ".%" PRIu64
               "\n",
               test_vec_time / 1000000000UL, test_vec_time % 1000000000UL,
               test_gptp_time / 1000000000UL, test_gptp_time % 1000000000UL);
    } else {
        printf("Qtimer time test failed\n");
    }

    test_vec_time = getQtimerTicks();

    if (gptpGetPtpTimeFromQTimeTickCount(&test_gptp_time, test_vec_time)) {
        printf("qtimer ticks %" PRIu64 "  gptp_time %" PRIu64 ".%" PRIu64"\n",
               test_vec_time,  test_gptp_time / 1000000000UL, test_gptp_time % 1000000000UL);
    } else {
        printf("Qtimer time tick test failed\n");
    }

    test_vec_time = systemTime(CLOCK_MONOTONIC);

    if (gptpGetPtpTimeFromMonoTime(&test_gptp_time, test_vec_time)) {
        printf("mono_time %" PRIu64 ".%" PRIu64 "  gptp_time %" PRIu64 ".%" PRIu64 "\n",
               test_vec_time / 1000000000UL, test_vec_time % 1000000000UL,
               test_gptp_time / 1000000000UL, test_gptp_time % 1000000000UL);
    } else {
        printf("Monotonic time test failed\n");
    }

#endif // END LE_GVM
#ifndef LE_GVM

    if (argc == 2) {
        if (argv[1][0] == 'q') {
            printf("\n\n\n====================QTIMER based test=====================\n\n\n");
            do_some_tests_qtimer();
        } else if (argv[1][0] == 's') {
            printf("\n\n\n====================SYSTEM based test=====================\n\n\n");
            do_some_tests_sys();
        } else if (argv[1][0] == 'p') {
            printf("\n\n\n====================PTP based test=====================\n\n\n");
            do_some_tests_ptp();
        } else if (argv[1][0] == 'm') {
            printf("\n\n\n====================gPTP Monotonic pair based test=====================\n\n\n");
            do_some_tests_gptp_mono();
        } else if (argv[1][0] == 'l') {
            printf("\n\n\n====================gPTP loop test=====================\n\n\n");
            loop_test(1000000);
        } else if (argv[1][0] == 'g') {
            printf("\n\n=======================clock_gettime based test=========================\n\n");
            get_gptp_time();
        } else if (argv[1][0] == 'b') {
            printf("\n\n\n====================gPTP time boot time test=====================\n\n\n");
            do_some_tests_gptp_boot();
        }

#ifdef RGPTP_CLNT_ENABLED
        else if (argv[1][0] == 'r') {
            rgptp_test();
        }

#endif
    } else if (argc == 3 || argc == 5) {
        if (argv[1][0] == 'm') {
            printf("\n\n\n====================gPTP Monotonic pair based test=====================\n\n\n");
            int sleepduration = atoi(argv[2]);
            gptpRegisterCallback(&callback_handler);
            do_some_tests_gptp_mono();
            sleep(sleepduration);
            do_some_tests_gptp_mono();
            gptpRegisterCallback(NULL);
        } else if (argv[1][0] == 'l') {
            printf("\n\n\n====================gPTP loop test=====================\n\n\n");
            int sleepduration = atoi(argv[2]);
            loop_test(sleepduration);
        } else if(argv[1][0] == 'R') {
            printf("\n\n\n====================gPTP Reverse sync test=====================\n\n\n");
            Rsync.reverseSyncEnabled = atoi(argv[2]);
            if (Rsync.reverseSyncEnabled && argc == 5) {
                Rsync.reverseSyncDomain = atoi(argv[3]);
                Rsync.reverseSyncRate = atof(argv[4]);
            }
            printf("RSYNC: %d, RSYNCDOMAIN %d, RSYNCRATE %f", Rsync.reverseSyncEnabled, Rsync.reverseSyncDomain, Rsync.reverseSyncRate);

            if(setRsyncStatus(&Rsync))
            {
                printf("Error while setting reverse sync status");
                return 0;
            }
        }
    }
#ifdef RGPTP_CLNT_ENABLED

    if (argc == 3) {
        if (argv[1][0] == 's') {
            int time_s = 0;
            time_s = atoi(argv[2]);
            printf("\n\n====================RPTP based test========================");
            printf("\nsleep interval: %ds\n", time_s);
            do_some_tests_rgptp_s(time_s);
        } else if (argv[1][0] == 'u') {
            int time_us = 0;
            time_us = atoi(argv[2]);
            printf("\n\n====================RPTP based test=====================");
            printf("\nsleep interval: %dus\n", time_us);
            do_some_tests_rgptp_u(time_us);
        }
    }

#endif
#endif // END LE_GVM

    if (!gptpDeinit()) {
        printf("GPTP deinit failed\n");
    }

    return 0;
}
