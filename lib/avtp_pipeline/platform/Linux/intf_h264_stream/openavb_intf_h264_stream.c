/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
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
#include <sys/mman.h>

#include "openavb_types_pub.h"
#include "openavb_trace_pub.h"
#include "openavb_mediaq_pub.h"
#include "openavb_intf_pub.h"
#include "openavb_map_h264_pub.h"

#define	AVB_LOG_COMPONENT	"H264 stream Interface"
#include "openavb_log_pub.h"
#include "Avbh264Stream.h"


#define NBUFS 256
#define MAX_READ_SIZE 192
int DataSendSock;
typedef struct pvt_data_t
{
	char *file_name;
	bool ignoreTimestamp;
	U8 *fp;
	char *pPipelineStr;
	U32 bufwr;
	U32 bufrd;
	U32 seq;
	bool asyncRx;
	bool blockingRx;
	U32 read_size;
	U32 loc;
	int fd;
        struct stat statbuf;
	bool get_avtp_timestamp;        /*<! this flag indicates whether
                                        an avtp timestamp should be taken */
	U32 frame_timestamp;            /*<! this is a timestamp of a video frame */
} pvt_data_t;

// Each configuration name value pair for this mapping will result in this callback being called.
void openavbIntfH264StreamCfgCB(media_q_t *pMediaQ, const char *name, const char *value)
{
	if (!pMediaQ) {
		AVB_LOG_DEBUG("H264-Stream cfgCB: no mediaQ!");
		return;
	}

	char *pEnd = NULL ;
	long tmp;

	pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
	if (!pPvtData) {
		AVB_LOG_ERROR("Private interface module data not allocated.");
		return;
	}

	pPvtData->asyncRx = FALSE;

	if (strcmp(name, "intf_nv_file_name") == 0) {
		if (pPvtData->file_name) {
			free(pPvtData->file_name);
		}
		pPvtData->file_name = strdup(value);
	}
	else if (strcmp(name, "intf_nv_async_rx") == 0)	{
		tmp = strtol(value, &pEnd, 10);
		if (*pEnd == '\0' && tmp == 1) {
			pPvtData->asyncRx = (tmp == 1);
		}
	}
	else if (strcmp(name, "intf_nv_blocking_rx") == 0) {
		tmp = strtol(value, &pEnd, 10);
		if (*pEnd == '\0' && tmp == 1) {
			pPvtData->blockingRx = (tmp == 1);
		}
	}
	else if (strcmp(name, "intf_nv_ignore_timestamp") == 0) {
		tmp = strtol(value, &pEnd, 10);
		if (*pEnd == '\0' && tmp == 1) {
			pPvtData->ignoreTimestamp = (tmp == 1);
		}
	}
}

void openavbIntfH264StreamGenInitCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);
	if (!pMediaQ) {
		AVB_LOG_DEBUG("H264-Stream initCB: no mediaQ!");
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return;
	}
	AVB_TRACE_EXIT(AVB_TRACE_INTF);

}

static int openavbMediaQGetItemSize(media_q_t *pMediaQ)
{
	int itemSize = 0;
	media_q_item_t *pMediaQItem = openavbMediaQHeadLock(pMediaQ);
	if (pMediaQItem) {
		itemSize = pMediaQItem->itemSize;
		openavbMediaQHeadUnlock(pMediaQ);
	}
	else {
		AVB_LOG_ERROR("pMediaQ item NULL in getMediaQItemSize");
	}

	return itemSize;
}

// a talker. Any talker initialization can be done in this function.
void openavbIntfH264StreamTxInitCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);

	if (!pMediaQ) {
		AVB_LOG_DEBUG("H264-stream txinit: no mediaQ!");
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return;
	}

	pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
	if (!pPvtData) {
		AVB_LOG_ERROR("Private interface module data not allocated.");
		return;
	}
	//mmap file to read and setup private data
	pPvtData->fd = open(pPvtData->file_name, O_RDONLY);
	if (pPvtData->fd == -1) {
		AVB_LOG_DEBUG("H264-stream txinit: no file not found");
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return;
	}
	fstat(pPvtData->fd, &pPvtData->statbuf);
	pPvtData->fp = (U8*)mmap(NULL, pPvtData->statbuf.st_size, PROT_READ, MAP_FILE|MAP_PRIVATE, pPvtData->fd, (off_t) 0);
	if (pPvtData->fp == (void*)-1) {
		AVB_LOG_DEBUG("h264-stream txinit: could not mmap file");
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return;

	}
	pPvtData->loc = 0;
	pPvtData->get_avtp_timestamp = TRUE;
	pPvtData->read_size = MAX_READ_SIZE;

	AVB_TRACE_EXIT(AVB_TRACE_INTF);

	return;
}

// This callback will be called for each AVB transmit interval. Commonly this will be
// 4000 or 8000 times  per second.
bool openavbIntfH264StreamTxCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF_DETAIL);

	if (!pMediaQ) {
		AVB_LOG_DEBUG("No MediaQ in H264StreamTxCB");
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return FALSE;
	}

	pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
	if (!pPvtData) {
		AVB_LOG_ERROR("Private interface module data not allocated.");
		return FALSE;
	}
	U32 read_size = 0;
	media_q_item_t *pMediaQItem = openavbMediaQHeadLock(pMediaQ);
	if (pMediaQItem) {
		if (pPvtData->loc + pPvtData->read_size >= pPvtData->statbuf.st_size) {
			read_size = pPvtData->statbuf.st_size - pPvtData->loc - 1;
		}
		else {
			read_size = pPvtData->read_size;
		}
	        if (read_size > 0) {
			memcpy(pMediaQItem->pPubData, &pPvtData->fp[pPvtData->loc], read_size);
		}
		else {
			return FALSE;
		}
		pMediaQItem->dataLen = read_size;
		pPvtData->loc += read_size ;
		openavbAvtpTimeSetToWallTime(pMediaQItem->pAvtpTime);
		openavbMediaQHeadPush(pMediaQ);
		AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
		return TRUE;

	}
	else {
		AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
		return FALSE;
	}
	AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
	return TRUE;
}


// A call to this callback indicates that this interface module will be
// a listener. Any listener initialization can be done in this function.
void openavbIntfH264StreamRxInitCB(media_q_t *pMediaQ)
{
	AVB_LOG_DEBUG("Rx Init callback.");
	if (!pMediaQ) {
		AVB_LOG_DEBUG("No MediaQ in H264StreamRxInitCB");
		return;
	}

	pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
	if (!pPvtData) {
		AVB_LOG_ERROR("Private interface module data not allocated.");
		return;
	}
	int status = Avbh264streamInitialize();
        if (status < 0) {
		AVB_LOG_ERROR("unable to initialize the h264sink Thread");
        }
        DataSendSock = Avbh264ConnectToStreamSource();
	if (DataSendSock <= 0 ) {
	       AVB_LOG_ERROR("failed to connect the streaming application");
        }

}

// This callback is called when acting as a listener.
bool openavbIntfH264StreamRxCB(media_q_t *pMediaQ)
{
	if (!pMediaQ) {
		AVB_LOG_DEBUG("RxCB: no mediaQ!");
		return TRUE;
	}
	pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
	if (!pPvtData) {
		AVB_LOG_ERROR("Private interface module data not allocated.");
		return FALSE;
	}

	bool moreSourcePackets = TRUE;
        int size = 0;

	while (moreSourcePackets) {
		media_q_item_t *pMediaQItem = openavbMediaQTailLock(pMediaQ, pPvtData->ignoreTimestamp);
		// there are no packets available or they are from the future
		if (!pMediaQItem) {
			moreSourcePackets = FALSE;
			continue;
		}
		if (!pMediaQItem->dataLen) {
			AVB_LOG_DEBUG("No dataLen");
			openavbMediaQTailPull(pMediaQ);
			continue;
		}
		if (pPvtData->asyncRx) {
			AVB_LOG_INFO("Rx async called...");
			U32 bufwr = pPvtData->bufwr;
			U32 bufrd = pPvtData->bufrd;
			U32 mdif = bufwr - bufrd;
			if (mdif >= NBUFS) {
				openavbMediaQTailPull(pMediaQ);
				AVB_LOGF_INFO("Rx async queue full, dropping (%" PRIu32 " - %" PRIu32 " = %" PRIu32 ")", bufwr, bufrd, mdif);
				moreSourcePackets = FALSE;
				continue;
			}
		}
		int rc = write(DataSendSock, pMediaQItem->pPubData,pMediaQItem->dataLen);
		if (rc != pMediaQItem->dataLen) {
		    AVB_LOG_ERROR("Failed to send the data from the stack");
		}

		openavbMediaQTailPull(pMediaQ);
	}
	return TRUE;
}

// This callback will be called when the interface needs to be closed. All shutdown should
// occur in this function.
void openavbIntfH264StreamEndCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);
	pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
	if (!pPvtData) {
		AVB_LOG_ERROR("Private interface module data not allocated.");
		return;
	}
        close(DataSendSock);
	AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

void openavbIntfH264StreamGenEndCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);
	AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

// Main initialization entry point into the interface module
extern DLL_EXPORT bool openavbIntfH264StreamInitialize(media_q_t *pMediaQ, openavb_intf_cb_t *pIntfCB)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);

	if (!pMediaQ) {
		AVB_LOG_DEBUG("H264-gst GstInitialize: no mediaQ!");
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return TRUE;
	}

	// Memory freed by the media queue when the media queue is destroyed.
	pMediaQ->pPvtIntfInfo = calloc(1, sizeof(pvt_data_t));

	pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;

	pIntfCB->intf_cfg_cb = openavbIntfH264StreamCfgCB;
	pIntfCB->intf_gen_init_cb = openavbIntfH264StreamGenInitCB;
	pIntfCB->intf_tx_init_cb = openavbIntfH264StreamTxInitCB;
	pIntfCB->intf_tx_cb =openavbIntfH264StreamTxCB;
	pIntfCB->intf_rx_init_cb = openavbIntfH264StreamRxInitCB;
	pIntfCB->intf_rx_cb = openavbIntfH264StreamRxCB;
	pIntfCB->intf_end_cb = openavbIntfH264StreamEndCB;
	pIntfCB->intf_gen_end_cb = openavbIntfH264StreamGenEndCB;

	pPvtData->ignoreTimestamp = FALSE;

	AVB_TRACE_EXIT(AVB_TRACE_INTF);
	return TRUE;
}
