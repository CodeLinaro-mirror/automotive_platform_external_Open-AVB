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
    ret = (t.tv_sec)*1000000000LL + t.tv_nsec;
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

void do_some_tests_qtimer() {
    int i=0;
    struct timespec ts = { 0, 1000000 };
    uint64_t prev_vec_time;
    uint64_t prev_gptp_time;
    uint64_t test_vec_time;
    uint64_t test_gptp_time;
    int64_t delta_vec_time;
    int64_t delta_gptp_time;
    prev_vec_time = test_vec_time = getQtimerTime();
    gptpGetPtpTimeFromQTimeNs(&prev_gptp_time, prev_vec_time);
    for(i=0;i<1000;i++)
        if (gptpGetPtpTimeFromQTimeNs(&test_gptp_time, test_vec_time)) {
            delta_vec_time = test_vec_time;
            delta_vec_time -= prev_vec_time;
            delta_gptp_time = test_gptp_time;
            delta_gptp_time -= prev_gptp_time;

            printf("ns qtimer_time %" PRIi64 "  gptp_time %" PRIi64 "\n",
                            delta_vec_time,delta_gptp_time);
            prev_vec_time = test_vec_time;
            prev_gptp_time = test_gptp_time;

            nanosleep(&ts,NULL);
            test_vec_time += 1000000UL;
        } else {
            printf("Qtimer time test failed\n");
    }


}

void do_some_tests_sys() {
    int i=0;
    struct timespec ts = { 0, 1000000 };
    uint64_t prev_vec_time;
    uint64_t prev_gptp_time;
    uint64_t test_vec_time;
    uint64_t test_gptp_time;
    int64_t delta_vec_time;
    int64_t delta_gptp_time;
    prev_vec_time = test_vec_time = systemTime(CLOCK_REALTIME);
    gptpGetTime(&prev_gptp_time, prev_vec_time);
    for(i=0;i<1000;i++)
        if (gptpGetTime(&test_gptp_time, test_vec_time)) {
            delta_vec_time = test_vec_time;
            delta_vec_time -= prev_vec_time;
            delta_gptp_time = test_gptp_time;
            delta_gptp_time -= prev_gptp_time;

            printf("ns sys_time %" PRIi64 "  gptp_time %" PRIi64 "\n",
                            delta_vec_time,delta_gptp_time);
            prev_vec_time = test_vec_time;
            prev_gptp_time = test_gptp_time;

            nanosleep(&ts,NULL);
            test_vec_time += 1000000UL;
        } else {
            printf("Qtimer time test failed\n");
    }


}

void do_some_tests_ptp() {
    int i=0;
    uint64_t ptp_time = 0;

    for(i=0;i<10;i++){
        if(gptpGetCurPtpTime(&ptp_time)){
            printf("ns ptp_time %" PRIu64 "\n",ptp_time);
        }
    }
}

#ifdef RGPTP_CLNT_ENABLED
static void rgptp_test(void) {
    bool rgptp_avail = false;
    uint64_t test_rgptp_time;

    rgptp_avail = rgptpInit();
    if (rgptp_avail) {
        printf("RGPTP Available\n");
        if (rgptpGetCurPtpTime(&test_rgptp_time)){
            printf("rgptp time %" PRIu64 ".%" PRIu64 "\n",
                    test_rgptp_time/1000000000UL, test_rgptp_time%1000000000UL);
        }
        else {
            printf("RGPTP time test failed\n");
        }
        if(!rgptpDeinit()) {
            printf("RGPTP deinit failed\n");
        }
    }
    else {
        printf("RGPTP Not Available\n");
    }
    return;
}

static void do_some_tests_rgptp_s(int time_s) {
    uint64_t rptp_time = 0;
    uint64_t ptp_time = 0;
    int i = 0;
    int64_t ns = 0;

    if (rgptpInit()) {
        for(i=0; i< 200; i++) {
            gptpGetCurPtpTime(&ptp_time);
            rgptpGetCurPtpTime(&rptp_time);
            ns = (ptp_time - rptp_time);
            printf("gptp time: %" PRIu64 "rgptp time: %"PRIu64 " diff:%" PRId64 "\n", ptp_time, rptp_time, ns);
            sleep(time_s);
        }
        if(!rgptpDeinit()) {
            printf("RGPTP deinit failed\n");
        }
    }
    else {
        printf("RGPTP Not Available\n");
    }
    return;
}

static void do_some_tests_rgptp_u(int time_us) {
    uint64_t rptp_time = 0;
    uint64_t ptp_time = 0;
    int i = 0;
    int64_t ns = 0;

    if (rgptpInit()) {
        for(i=0; i< 200; i++) {
            gptpGetCurPtpTime(&ptp_time);
            rgptpGetCurPtpTime(&rptp_time);
            ns = (ptp_time - rptp_time);
            printf("gptp time: %" PRIu64 "rgptp time: %"PRIu64 " diff:%" PRId64 "\n", ptp_time, rptp_time, ns);
            usleep(time_us);
        }
        if(!rgptpDeinit()) {
            printf("RGPTP deinit failed\n");
        }
    }
    else {
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
#ifdef GPTP_AUTO_START
    struct timespec ts = { 0, 1000000 };
#endif

    gptp_scaling_available = gptpInit();

    if (gptp_scaling_available)
        printf("GPTP Scaling Available\n");
    else {
        printf("GPTP Scaling Not Available\n");
        return 0;
    }

#ifdef GPTP_AUTO_START
    while(1){
        test_vec_time = systemTime(CLOCK_REALTIME);

        if (gptpGetTime(&test_gptp_time, test_vec_time)){
            printf("real_time %" PRIu64 ".%" PRIu64 "  gptp_time %" PRIu64 ".%" PRIu64 "\n",
                    test_vec_time/1000000000UL, test_vec_time%1000000000UL,
                    test_gptp_time/1000000000UL, test_gptp_time%1000000000UL);
            break;
        } else {
            printf("Real time test failed\n");
        }
        nanosleep(&ts,NULL);
    }
#else
    test_vec_time = systemTime(CLOCK_REALTIME);
    if (gptpGetTime(&test_gptp_time, test_vec_time)) {
            printf("real_time %" PRIu64 ".%" PRIu64 "  gptp_time %" PRIu64 ".%" PRIu64 "\n",
                    test_vec_time/1000000000UL, test_vec_time%1000000000UL,
                    test_gptp_time/1000000000UL, test_gptp_time%1000000000UL);

    } else {
        printf("Real time test failed\n");
    }
#endif
    test_vec_time = getQtimerTime();
    if (gptpGetPtpTimeFromQTimeNs(&test_gptp_time, test_vec_time)) {
            printf("qtimer_time %" PRIu64 ".%" PRIu64 "  gptp_time %" PRIu64 ".%" PRIu64 "\n",
                    test_vec_time/1000000000UL, test_vec_time%1000000000UL,
                    test_gptp_time/1000000000UL, test_gptp_time%1000000000UL);

    } else {
        printf("Qtimer time test failed\n");
    }

    test_vec_time = getQtimerTicks();
    if (gptpGetPtpTimeFromQTimeTickCount(&test_gptp_time, test_vec_time)) {
            printf("qtimer ticks %" PRIu64 "  gptp_time %" PRIu64 ".%" PRIu64"\n",
                    test_vec_time,  test_gptp_time/1000000000UL, test_gptp_time%1000000000UL);
    } else {
        printf("Qtimer time tick test failed\n");
    }

    test_vec_time = systemTime(CLOCK_MONOTONIC);
    if (gptpGetPtpTimeFromMonoTime(&test_gptp_time, test_vec_time)) {
            printf("mono_time %" PRIu64 ".%" PRIu64 "  gptp_time %" PRIu64 ".%" PRIu64 "\n",
                    test_vec_time/1000000000UL, test_vec_time%1000000000UL,
                    test_gptp_time/1000000000UL, test_gptp_time%1000000000UL);
    } else {
        printf("Monotonic time test failed\n");
    }

    if (gptpGetCurPtpTime(&test_gptp_time)) {
            printf("gptp time %" PRIu64 ".%" PRIu64 "\n",
					test_gptp_time/1000000000UL, test_gptp_time%1000000000UL);
    } else {
        printf("GPTP time test failed\n");
    }

    if (argc == 2) {
        if (argv[1][0] == 'q') {
            printf("\n\n\n====================QTIMER based test=====================\n\n\n");
            do_some_tests_qtimer();
        } else if (argv[1][0] == 's') {
            printf("\n\n\n====================SYSTEM based test=====================\n\n\n");
            do_some_tests_sys();
        }
        else if (argv[1][0] == 'p') {
            printf("\n\n\n====================PTP based test=====================\n\n\n");
            do_some_tests_ptp();
        }
#ifdef RGPTP_CLNT_ENABLED
        else if(argv[1][0] == 'r') {
            rgptp_test();
        }
#endif
    }
#ifdef RGPTP_CLNT_ENABLED
    if (argc == 3) {
        if(argv[1][0] == 's') {
            int time_s = 0;
            time_s = atoi(argv[2]);
            printf("\n\n====================RPTP based test========================");
            printf("\nsleep interval: %ds\n", time_s);
            do_some_tests_rgptp_s(time_s);
        }
        else if(argv[1][0] == 'u') {
            int time_us = 0;
            time_us = atoi(argv[2]);
            printf("\n\n====================RPTP based test=====================");
            printf("\nsleep interval: %dus\n", time_us);
            do_some_tests_rgptp_u(time_us);
        }
	}
#endif

    if(!gptpDeinit()) {
        printf("GPTP deinit failed\n");
    }

    return 0;
}
