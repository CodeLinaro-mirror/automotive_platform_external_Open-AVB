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
#ifdef GPTP_AUTO_START
#include <signal.h>
#include <sys/un.h>
#endif
#include "gptp_helper.h"
#include <atomic>
#include <limits.h>

pthread_mutex_t gInitMutex = PTHREAD_MUTEX_INITIALIZER;
#define LOCK()  	pthread_mutex_lock(&gInitMutex)
#define UNLOCK()	pthread_mutex_unlock(&gInitMutex)

#define CLOCKFD 3
#define FD_TO_CLOCKID(fd)	((~(clockid_t) (fd) << 3) | CLOCKFD)
#define BUF_SIZE 500

#ifdef GPTP_AUTO_START
#ifdef SYSTEMD
#define ADDRESS     "/dev/socket/gptp/gptp_socket"
#else
#define ADDRESS     "/tmp/gptp_socket"
#endif
#define CONNECT_RETRY_PERIOD_us  1000
#endif

static bool bInitialized = false;
#ifdef GPTP_AUTO_START
static bool bServiceConnect = false;
#endif
/* Pipe file descriptors for cleanup the loop */
int pipefd[2];
fd_set readfds;
static int gPtpShmFd = -1;
static char *gPtpMmap = NULL;
static gPtpTimeData gPtpTD;
static int gptpPhcFd = -1;
static clockid_t gPtpClockid = -1;
#ifdef  RGPTP_CLNT_ENABLED
static int rptp_fd = 0;
static clockid_t rgptp_clkid = -1;
#endif

#ifdef GPTP_AUTO_START
static pthread_t thread_id;
static int sock = -1;
#endif

GPTP_UPDATE_NOTIFY_CALLBACK gptp_update_callback = NULL;

#ifdef AVB_FEATURE_GVM_MODE

bool hab_thread_running = false;
pthread_t hab_Thread;
int32_t hab_hdl;

#define HABMM_SOCKET_RECV_FLAGS_NON_BLOCKING 0x00000001
#define HABMM_SOCKET_RECV_FLAGS_UNINTERRUPTIBLE 0x00000002
#define HABMM_VNW_1 1401

extern "C" int32_t habmm_socket_open(int32_t *handle, uint32_t mm_ip_id,uint32_t timeout, uint32_t flags);
extern "C" int32_t habmm_socket_recv(int32_t handle, void *dst_buff, uint32_t *size_bytes,uint32_t timeout, uint32_t flags);
extern "C" int32_t habmm_socket_close(int32_t handle);

#endif


static int gptpClkInit(int *gptp_phc_fd)
{
    *gptp_phc_fd = open("/dev/ptp0", O_RDWR );

    if( *gptp_phc_fd == -1 ||
        (gPtpClockid = FD_TO_CLOCKID(*gptp_phc_fd)) == -1 ) {
        printf("Failed to open PTP clock device\n");
        return false;
    }
    return true;
}

static void gptpClkDeInit(int gptp_phc_fd)
{
    if (gptp_phc_fd < 0)
	close(gptp_phc_fd);

    gPtpClockid = -1;
}

/* gptp core function to init gptp scaling */
static int gptpMemInit(int *gptp_shm_fd, char **gptp_mmap)
{
	if (NULL == gptp_shm_fd)
		return false;
#ifdef AVB_FEATURE_GVM_MODE
	*gptp_shm_fd = open( SHM_NAME, O_RDWR, 0);
#else
#ifdef ANDROID
	*gptp_shm_fd = open( SHM_NAME, O_RDWR | O_CREAT, 0);
#else
	*gptp_shm_fd = shm_open(SHM_NAME, O_RDWR, 0);
#endif
#endif
	printf("gptpMemInit %s",SHM_NAME);
	if (*gptp_shm_fd == -1) {
		perror("shm_open()");
		return false;
	}

	*gptp_mmap =
	    (char *)mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
			 *gptp_shm_fd, 0);

	if (*gptp_mmap == (char *)-1) {
		perror("mmap()");
		*gptp_mmap = NULL;
#ifdef AVB_FEATURE_GVM_MODE
		close(*gptp_shm_fd);
		unlink(SHM_NAME );
#else
#ifdef ANDROID
		close(*gptp_shm_fd);
		unlink(SHM_NAME );
#else
		shm_unlink(SHM_NAME);
#endif
#endif
		return false;
	}
	return true;
}

/* gptp core function to deinit gptp scaling */
static void gptpMemDeinit(int gptp_shm_fd, char *gptp_mmap)
{
	if (gptp_mmap != NULL){
		munmap(gptp_mmap, SHM_SIZE);
		gptp_mmap = NULL;
    }

	if (gptp_shm_fd != -1)
		close(gptp_shm_fd);
}

/* gptp core function to copy gptp offset data from shared memory */
static int gptpScaling(gPtpTimeData * td, char *memory_offset_buffer)
{
	if ((td == NULL)||(memory_offset_buffer == NULL))
	{
		printf("gptpScaling failure %p %p\n",td,memory_offset_buffer);
		return false;
	}

#ifndef AVB_FEATURE_GVM_MODE
	pthread_mutex_lock((pthread_mutex_t *) memory_offset_buffer);
	memcpy(td, memory_offset_buffer + sizeof(pthread_mutex_t), sizeof(*td));
	pthread_mutex_unlock((pthread_mutex_t *) memory_offset_buffer);
#else
	int buf_offset = 0;
	std::atomic<uint32_t> *seq0;
	std::atomic<uint32_t> *seq1;
	uint32_t a,b;
	gPtpTimeData *ptimedata;
	int count = 0;
	char *dest = (char*)td;
	char *src = NULL;
	buf_offset += (2 * sizeof(std::atomic<uint32_t>));

	seq0 = (std::atomic<uint32_t> *)memory_offset_buffer;
	seq1 = (std::atomic<uint32_t> *)(memory_offset_buffer + sizeof(std::atomic<uint32_t>));
	ptimedata   = (gPtpTimeData *) (memory_offset_buffer + buf_offset);
	src = (char *)ptimedata;
	do {
	a = seq0->load();
	b = seq1->load();

	//memcpy(td, ptimedata, sizeof(*td)); //commented due to bus error issue
	for(int i=0; i<sizeof(gPtpTimeData); i++ )
	{
	    dest[i]=src[i];
	}
	count++;

	}while((a!=b || a!=seq0->load() || b != seq1->load())&&count<3);

#endif
	return true;
}


#ifdef AVB_FEATURE_GVM_MODE

void* habLoop(void* param) {

	int32_t ret;
	struct gptp_update update;
	uint32_t len = sizeof(update);

	while(hab_thread_running){

			memset(&update,0,sizeof(update));

			do {
			ret = habmm_socket_recv(hab_hdl,&update, &len, 0,
				HABMM_SOCKET_RECV_FLAGS_UNINTERRUPTIBLE);
			} while (-EINTR == ret);


		if (ret) {
			printf("habmm_socket_recv failed, ret= 0x%x\n", ret);
			return NULL;
		}


		if(gptp_update_callback) {
			gptp_update_callback(update);
		}

	}

	return NULL;

}

#endif

bool gptpRegisterCallback(GPTP_UPDATE_NOTIFY_CALLBACK fn_ptr)
{

	if(fn_ptr == NULL && gptp_update_callback == NULL)
	return false;

#ifdef AVB_FEATURE_GVM_MODE

	int err = 0;
	int32_t ret;


	if(gptp_update_callback!=NULL && hab_thread_running)
	{
		hab_thread_running = false;
		habmm_socket_close(hab_hdl);
	}

	gptp_update_callback = fn_ptr;

	if(fn_ptr!=NULL)
	{
		ret = habmm_socket_open(&hab_hdl, HABMM_VNW_1, 0, 0);

		if (ret < 0)
		{
			 printf("habmm_socket_open: socket create failed\n");
	         return false;
		}

		hab_thread_running = true;

		if ((err = pthread_create(&hab_Thread, NULL, habLoop, (void *) NULL))
				< 0) {
			hab_thread_running = false;
			printf("Error during creation of the thread %d\n",err);
			return false;
		}
		else
		{
			hab_thread_running = true;
		}
	}
#endif

	return true;

}

/* gptp core function query gptp time */
static bool gptpLocalTime(const gPtpTimeData *td, uint64_t *now_local, uint64_t *time_sys_ns)
{
	uint64_t system_time = 0;
	int64_t delta_local = 0;
	int64_t delta_system = 0;

	if (!td || !now_local || !time_sys_ns)
		return false;

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
static bool gptpLocalQTime(const gPtpTimeData *td, uint64_t *now_local, uint64_t *time_qtime_ns)
{
	uint64_t qtimer_time = 0;
	int64_t delta_local = 0;
	int64_t delta_qtimer = 0;

	if (!td || !now_local || !time_qtime_ns)
		return false;

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


/* intermediate wrapper to init gptp scaling */
static bool gptpTimeInit(void) {
	if (!gptpMemInit(&gPtpShmFd, &gPtpMmap))
		return false;

	if (!gptpScaling(&gPtpTD, gPtpMmap))
		return false;

	if(!gptpClkInit(&gptpPhcFd))
		return false;

	return true;
}

#ifdef GPTP_AUTO_START
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
                printf("gptpDaemonSrvConnect: socket create failed\n");
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
                    printf("gptpDaemonSrvConnect: success\n");
                    bInitialized = true;
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
                        printf("clean up thread\n");
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
                printf("gptpDaemonSrvConnect: select errno %d\n", errno);
            }
        }

        if (ret == -1) {
            printf("gptpDaemonSrvConnect: cleanup errno %d\n", errno);
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

static void gptpDaemonClientInit(void) {
    int ret = 0;

    if (bServiceConnect == true || sock != -1) {
        printf("gptpDaemonClientInit: already initialized\n");
        return;
    }

    bServiceConnect = true;
    pipefd[0] = -1;
    pipefd[1] = -1;

    if (pipe(pipefd) == -1) {
        printf("pipe create error\n");
        return;
    }
    ret = pthread_create(&thread_id, NULL, gptpDaemonSrvConnect, NULL);

    if(ret != 0){
        printf("gptpDaemonClientInit: failed -->%s\n", strerror(errno));
    }

    ret = pthread_setname_np(thread_id, "GPTP-HELPER");

    if (ret != 0) {
        printf("Failed to set thread name \n");
    }

    return;
}

static void gptpDaemonClientDeInit(void) {
    int ret = 0;
    char data = '1';
    bServiceConnect = false;
    write(pipefd[1], &data, 1);
    ret = pthread_join(thread_id, NULL);

    if(ret != 0){
        printf("gptpDaemonClientDeInit: failed -->%s\n", strerror(errno));
    }

    if (sock > 0) {
        close(sock);
        sock = -1;
    }

    // Release the Pipe
    if (pipefd[0] != -1) {
        close(pipefd[0]);
    }

    if (pipefd[1] != -1) {
        close(pipefd[1]);
    }

    return;
}

#endif

/* public API to query gptp time */
bool gptpGetPtpTimeFromMonoTime(uint64_t *gptp_time_sys, uint64_t time_mono_ns) {
	uint64_t now_local = 0;
	uint64_t update_8021as = 0;
	int64_t delta_8021as = 0;
	int64_t delta_local = 0;
	uint64_t time_mono_qtime_ns  = 0;

	if (!bInitialized) {
		return false;
	}
	if (!gptpScaling(&gPtpTD, gPtpMmap))
		return false;

	if (gPtpTD.port_state == PTP_SLAVE) {
		if (gPtpTD.sync_status == false) {
			return false;
		}
	}

	time_mono_qtime_ns =  time_mono_ns + gPtpTD.qtime_to_mono_offset; //Qtimer is ahead from monotonic

	if (gptpLocalQTime(&gPtpTD, &now_local, &time_mono_qtime_ns)) {
		update_8021as = gPtpTD.local_time - gPtpTD.ml_phoffset;
		delta_local = now_local - gPtpTD.local_time;
		delta_8021as = gPtpTD.ml_freqoffset * delta_local;
		*gptp_time_sys = update_8021as + delta_8021as;
		return true;
	}
	return false;
}

/* public API to query gptp time */
bool gptpGetPtpTimeFromQTimeNs(uint64_t *gptp_time_qt, uint64_t time_qtimer_ns) {
	uint64_t now_local = 0;
	uint64_t update_8021as = 0;
	int64_t delta_8021as = 0;
	int64_t delta_local = 0;
	uint64_t time_ns = time_qtimer_ns;

	if (!bInitialized) {
		return false;
	}
	if (!gptpScaling(&gPtpTD, gPtpMmap))
		return false;

	if (gPtpTD.port_state == PTP_SLAVE) {
		if (gPtpTD.sync_status == false) {
			return false;
		}
	}

	if (gptpLocalQTime(&gPtpTD, &now_local, &time_ns)) {
		update_8021as = gPtpTD.local_time - gPtpTD.ml_phoffset;
		delta_local = now_local - gPtpTD.local_time;
		delta_8021as = gPtpTD.ml_freqoffset * delta_local;
		*gptp_time_qt = update_8021as + delta_8021as;
		return true;
	}
	return false;
}

bool gptpGetPtpTimeFromQTimeTickCount(uint64_t *gptp_time_sys, uint64_t qtime_ticks)  {

	bool ret = false;
	uint64_t qTimerFreq = 0, qtimer_sec = 0, qtimer_nanos_NSec = 0, time_qtimer_ns = 0;
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
	return ret;

}

/* public API to query gptp time */
bool gptpGetTime(uint64_t *gptp_time_sys, uint64_t time_sys_ns) {
	uint64_t now_local = 0;
	uint64_t update_8021as = 0;
	int64_t delta_8021as = 0;
	int64_t delta_local = 0;
	uint64_t time_ns = time_sys_ns;

	if (!bInitialized)
		return false;

	if (!gptpScaling(&gPtpTD, gPtpMmap))
		return false;

	if (gPtpTD.port_state == PTP_SLAVE) {
		if (gPtpTD.sync_status == false) {
			return false;
		}
	}

	if (gptpLocalTime(&gPtpTD, &now_local, &time_ns)) {
		update_8021as = gPtpTD.local_time - gPtpTD.ml_phoffset;
		delta_local = now_local - gPtpTD.local_time;
		delta_8021as = gPtpTD.ml_freqoffset * delta_local;
		*gptp_time_sys = update_8021as + delta_8021as;
		return true;
	}
	return false;
}


/* public API to query current gptp time */
bool gptpGetCurPtpTime(uint64_t *gptp_time_cur) {

    struct timespec ts;

    ts.tv_sec = ts.tv_nsec = 0;
    *gptp_time_cur = 0;

    if (!bInitialized) {
	return false;
    }
    if (clock_gettime(gPtpClockid, &ts)) {
	printf("clock_gettime failed");
	return false;
    }

    *gptp_time_cur = (ts.tv_sec)*1000000000LL + ts.tv_nsec;

    return true;
}


bool gptpGetCurgPtpMonotonicPair(uint64_t *gptp_time_cur, uint64_t *mono_time_cur) {

	*gptp_time_cur = 0;
	*mono_time_cur = 0;

#ifdef AVB_FEATURE_GVM_MODE

	uint64_t *gptp_mem;
    uint64_t *mono_mem;
	struct timespec ts;
	std::atomic<uint32_t> *seq0;
	std::atomic<uint32_t> *seq1;
	uint32_t a,b;
	int count = 0;

    ts.tv_sec = ts.tv_nsec = 0;
    *gptp_time_cur = 0;
	*mono_time_cur =0;

    if (!bInitialized) {
	return false;
    }

	seq0 = (std::atomic<uint32_t> *)gPtpMmap;
	seq1 = (std::atomic<uint32_t> *)(gPtpMmap + sizeof(std::atomic<uint32_t>));

	do {
	a = seq0->load();
	b = seq1->load();
    if (clock_gettime(gPtpClockid, &ts)) {
	printf("clock_gettime failed");
	return false;
    }

	if(ts.tv_sec == 0 && ts.tv_nsec == 0) {
	printf("gptp time read taking longer time\n");
	return false;
	}

	gptp_mem = (uint64_t *) (gPtpMmap + 0x1000 - 3*sizeof(uint64_t));
	mono_mem = (uint64_t *) (gPtpMmap + 0x1000 - 4*sizeof(uint64_t));
	*gptp_time_cur = *gptp_mem;
	*mono_time_cur = *mono_mem;
	count++;

	}while((a!=b || a!=seq0->load() || b != seq1->load())&&count<3);
#endif

	return true;

}



/* public API to init gptp time scaling */
bool gptpInit(void) {
#ifdef GPTP_AUTO_START
	gptpDaemonClientInit();
	return true;
#else
	LOCK();
	if (!bInitialized) {
		if (gptpTimeInit())
			bInitialized = true;
	}
	UNLOCK();
	return bInitialized;
#endif
}

/* public API to deinit gptp time scaling */
bool gptpDeinit(void) {
	gptpMemDeinit(gPtpShmFd, gPtpMmap);
	gptpClkDeInit(gptpPhcFd);
#ifdef GPTP_AUTO_START
	gptpDaemonClientDeInit();
#endif
	bInitialized = false;
	return true;
}

#ifdef  RGPTP_CLNT_ENABLED
/* public API to query current rgptp time */
bool rgptpGetCurPtpTime(uint64_t *rgptp_time) {
    struct timespec ts;
    ts.tv_sec = ts.tv_nsec = 0;
    *rgptp_time = 0;

    if (clock_gettime(rgptp_clkid, &ts)) {
        printf("clock_gettime failed");
        return false;
    }
    *rgptp_time = (ts.tv_sec)*1000000000LL + ts.tv_nsec;
    return true;
}

/* public API to init rgptp time scaling */
bool rgptpInit(void) {
    rptp_fd = open("/dev/ptp1", O_RDWR );

    if( rptp_fd == -1 ||
            (rgptp_clkid = FD_TO_CLOCKID(rptp_fd)) == -1 ) {
        printf("%s, Failed to open PTP clock device\n", __func__);
        return false;
    }
    return true;
}

/* public API to deinit rgptp time scaling */
bool rgptpDeinit(void) {
    if (rptp_fd < 0)
        close(rptp_fd);
    rgptp_clkid = -1;

    return true;
}
#endif
