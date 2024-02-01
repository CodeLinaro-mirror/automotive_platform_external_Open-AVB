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

#ifndef __GPTP_HELPER_H__
#define __GPTP_HELPER_H__

#ifdef __cplusplus
extern "C" {
#endif
/* Get PTP time in nanoseconds for system time in nanoseconds */
bool gptpGetTime(uint64_t *gptp_time_ns, uint64_t time_sys_ns);

/* Get PTP time in nanoseconds for Qtimer time in nanoseconds */
bool gptpGetPtpTimeFromQTimeNs(uint64_t *gptp_time_ns, uint64_t time_qtimer_ns);

/* Get PTP time in nanoseconds for Qtimer time in ticks */
bool gptpGetPtpTimeFromQTimeTickCount(uint64_t *gptp_time_ns,
                                      uint64_t qtime_ticks);

/* Get PTP time in nanoseconds for Monotonic in nanoseconds */
bool gptpGetPtpTimeFromMonoTime(uint64_t *gptp_time_ns, uint64_t time_mono_ns);

/* Get current PTP time in nanoseconds */
bool gptpGetCurPtpTime(uint64_t *gptp_time_ns);

/* Get current PTP time and monolithic time in nanoseconds */
bool gptpGetCurgPtpMonotonicPair(uint64_t *gptp_time_cur,
                                 uint64_t *mono_time_cur);

/* Get Boot time in nanoseconds for Ptp time in nanoseconds */
bool gptpGetBootTimeFromPtpTime(uint64_t *boot_time_ns, uint64_t ptp_time_ns);

struct gptp_update {
    uint64_t curr_gptp_time;
    int64_t clock_adjust;
};


typedef void(*GPTP_UPDATE_NOTIFY_CALLBACK)(struct gptp_update update);

bool gptpRegisterCallback(GPTP_UPDATE_NOTIFY_CALLBACK fn_ptr);


bool gptpInit(void);

bool gptpDeinit(void);

#ifdef  RGPTP_CLNT_ENABLED
/* Get current rptp time in nanoseconds */
bool rgptpGetCurPtpTime(uint64_t *rgptp_time_ns);

bool rgptpInit(void);

bool rgptpDeinit(void);
#endif

typedef struct {

  GPTP_UPDATE_NOTIFY_CALLBACK gPTP_Update_Event;

} gPTPLibInterfaceEvent;

typedef struct {

   bool (*gptpGetTimeIf)(uint64_t *gptp_time_ns, uint64_t time_sys_ns);
   bool (*gptpGetPtpTimeFromQTimeNsIf)(uint64_t *gptp_time_ns,
                                      uint64_t time_qtimer_ns);
   bool (*gptpGetPtpTimeFromQTimeTickCountIf) (uint64_t *gptp_time_ns,
                                      uint64_t qtime_ticks);
   bool (*gptpGetPtpTimeFromMonoTimeIf)(uint64_t *gptp_time_ns,
                                      uint64_t time_mono_ns);
   bool (*gptpGetCurPtpTimeIf)(uint64_t *gptp_time_ns);
   bool (*gptpGetCurgPtpMonotonicPairIf)(uint64_t *gptp_time_cur,
                                      uint64_t *mono_time_cur);
   bool (*gptpGetBootTimeFromPtpTimeIf)(uint64_t *boot_time_ns,
                                      uint64_t ptp_time_ns);
   bool (*gptpInitIf)(void);
   bool (*gptpDeinitIf)(void);
   bool (*gptpRegisterEvent)(void);
   bool (*gptpUnregisterEvent)(void);

} gPTPLibInterfaceReq;


typedef const gPTPLibInterfaceReq* (*get_gPTPLib_if_t)(const gPTPLibInterfaceEvent* eventCallback);
const gPTPLibInterfaceReq* get_gPTPLib_if(const gPTPLibInterfaceEvent* eventCallback);

#ifdef __cplusplus
}
#endif

#endif      /* __GPTP_HELPER_H__ */
