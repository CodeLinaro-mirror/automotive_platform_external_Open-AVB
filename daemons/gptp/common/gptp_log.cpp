/*************************************************************************************************************
Copyright (c) 2012-2016, Harman International Industries, Incorporated
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
*************************************************************************************************************/

#include <gptp_log.hpp>

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <platform.hpp>

// MS VC++ 2013 has C++11 but not C11 support, use this to get millisecond resolution
#include <chrono>

#ifdef GENIVI_DLT
DLT_DECLARE_CONTEXT(dlt_con_gptp);
#endif

void gptplogRegister(void)
{
#ifdef GENIVI_DLT
	DLT_REGISTER_APP("GPTP","OpenAVB gPTP");
	DLT_REGISTER_CONTEXT(dlt_con_gptp, "GNRL", "General Context");
#endif
}

void gptplogUnregister(void)
{
#ifdef GENIVI_DLT
	DLT_UNREGISTER_CONTEXT(dlt_con_gptp);
	DLT_UNREGISTER_APP();
#endif
}

static int requestedLoggingType = GPTP_LOG_LOGCAT;
static int defaultLoggingType = GPTP_LOG_LOGCAT;

static FILE* gptp_diagnostic_counter_fp = NULL;
static FILE* gptp_exception_fp = NULL;


bool gptpLogModeConfigure(uint8_t reqlogmode )
{
	requestedLoggingType = (int) reqlogmode;
	return true;
}

bool gptpOpenCountersFile(char *diagnostic_counter_file)
{
	bool status = false;

	gptp_diagnostic_counter_fp = fopen(diagnostic_counter_file, "w+");
	if (gptp_diagnostic_counter_fp != NULL) {
		status = true;
	}

	return status;
}

bool gptpCloseCountersFile()
{
	if (gptp_diagnostic_counter_fp) {
		fflush(gptp_diagnostic_counter_fp);
		fclose(gptp_diagnostic_counter_fp);
		gptp_diagnostic_counter_fp = NULL;
	}

	if (errno == 0) {
		return true;
	}

	return false;
}

bool gptpOpenExceptionsFile(char *exception_fp)
{
	bool status = false;

	gptp_exception_fp = fopen(exception_fp, "w+");
	if (gptp_exception_fp != NULL) {
		status = true;
	}

	return status;
}

bool gptpCloseExceptionsFile()
{
	errno = 0;

	if (gptp_exception_fp) {
		fflush(gptp_exception_fp);
		fclose(gptp_exception_fp);
		gptp_exception_fp = NULL;
	}

	if (errno == 0) {
		return true;
	}

	return false;
}

void gptpLog(GPTP_LOG_LEVEL level, const char *tag, const char *path, int line, const char *fmt, ...)
{
	char msg[1024];
	char custom_file_msg[GPTP_LOG_FULL_MSG_LEN] ="";

	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);

#ifndef GENIVI_DLT
	std::chrono::system_clock::time_point cNow = std::chrono::system_clock::now();
	time_t tNow = std::chrono::system_clock::to_time_t(cNow);
	struct tm tmNow;
	PLAT_localtime(&tNow, &tmNow);
	std::chrono::system_clock::duration roundNow = cNow - std::chrono::system_clock::from_time_t(tNow);
	long int millis = (long int) std::chrono::duration_cast<std::chrono::milliseconds>(roundNow).count();

	if (requestedLoggingType != GPTP_LOG_DISABLED) {
		if (requestedLoggingType == GPTP_LOG_FILE) {
			if (strncmp(tag, "DIAGNOSTIC",10) == 0) {
				snprintf(custom_file_msg, GPTP_LOG_FULL_MSG_LEN, "COUNTER : GPTP [%d:%09lu] %s\n", tmNow.tm_sec % 10, (millis *1000000), msg);
				fputs(custom_file_msg, gptp_diagnostic_counter_fp);
			}
			if (strncmp(tag, "EXCEPTION",9) == 0) {
				snprintf(custom_file_msg, GPTP_LOG_FULL_MSG_LEN, "%s : GPTP [%d:%09lu] %s\n", tag, tmNow.tm_sec % 10, (millis *1000000), msg);
				fputs(custom_file_msg, gptp_exception_fp);
			}
		}

		if (path) {
			ALOGE("%s: GPTP [%2.2d:%2.2d:%2.2d:%3.3ld] [%s:%u] %s\n",
				   tag, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, millis, path, line, msg);
		}
		else {
			ALOGE("%s: GPTP [%2.2d:%2.2d:%2.2d:%3.3ld] %s\n",
				   tag, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, millis, msg);
		}
	}
#else
	DltLogLevelType dlt_level; 

	switch (level) {
	case GPTP_LOG_LVL_CRITICAL:
		dlt_level = DLT_LOG_FATAL;
		break;
	case GPTP_LOG_LVL_ERROR:
		dlt_level = DLT_LOG_ERROR;
		break;
	case GPTP_LOG_LVL_EXCEPTION:
	case GPTP_LOG_LVL_WARNING:
		dlt_level = DLT_LOG_WARN;
		break;
	case GPTP_LOG_LVL_INFO:
	case GPTP_LOG_LVL_STATUS:
		dlt_level = DLT_LOG_INFO;
		break;
	case GPTP_LOG_LVL_DEBUG:
		dlt_level = DLT_LOG_DEBUG;
		break;
	case GPTP_LOG_LVL_VERBOSE:
		dlt_level = DLT_LOG_VERBOSE;
		break;
	default:
		dlt_level = DLT_LOG_INFO;
		break;
	}

	DLT_LOG(dlt_con_gptp, dlt_level, DLT_STRING(msg));
#endif

}

