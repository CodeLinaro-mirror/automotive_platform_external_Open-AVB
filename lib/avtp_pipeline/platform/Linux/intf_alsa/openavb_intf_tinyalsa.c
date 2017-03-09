/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved
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
/*
* MODULE SUMMARY : Tiny ALSA interface module.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include "openavb_types_pub.h"
#include "openavb_audio_pub.h"
#include "openavb_trace_pub.h"
#include "openavb_mediaq_pub.h"
#include "openavb_map_uncmp_audio_pub.h"
#include "openavb_map_aaf_audio_pub.h"
#include "openavb_intf_pub.h"

#define	AVB_LOG_COMPONENT	"TinyALSA Interface"
#include "openavb_log_pub.h"

// The asoundlib.h header needs to appear after openavb_trace_pub.h otherwise an incompatibtily version of time.h gets pulled in.
//#include <sound/asound.h>
#include <tinyalsa/asoundlib.h>


#define PCM_DEVICE_NAME_DEFAULT	"default"
#define PCM_ACCESS_TYPE	SND_PCM_ACCESS_RW_INTERLEAVED
#define PERIOD_SIZE 256
#define PERIOD_COUNT 4


typedef struct {
	/////////////
	// Config data
	/////////////
	// Ignore timestamp at listener.
	bool ignoreTimestamp;

	// ALSA Device name
	char *pDeviceName;

	// map_nv_audio_rate
	avb_audio_rate_t audioRate;

	// map_nv_audio_type
	avb_audio_type_t audioType;

	// map_nv_audio_bit_depth
	avb_audio_bit_depth_t audioBitDepth;

	// map_nv_audio_endian
	avb_audio_endian_t audioEndian;

	// map_nv_channels
	avb_audio_channels_t audioChannels;

	// map_nv_allow_resampling
	bool allowResampling;

	U32 startThresholdPeriods;

	U32 periodTimeUsec;

	char *pCh2Filename;

	/////////////
	// Variable data
	/////////////
	// Handle for the PCM device
	struct pcm *pcmHandle;
	struct pcm_config config;
	U32 pcm_size;
	// ALSA read/write interval
	U32 intervalCounter;
        int ch2Fd;
} pvt_data_t;


// Each configuration name value pair for this mapping will result in this callback being called.
void openavbIntfTinyalsaCfgCB(media_q_t *pMediaQ, const char *name, const char *value)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);
	if (pMediaQ) {
		char *pEnd;
		long tmp;
		U32 val;

		pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
		if (!pPvtData) {
			AVB_LOG_ERROR("Private interface module data not allocated.");
			return;
		}

		media_q_pub_map_uncmp_audio_info_t *pPubMapUncmpAudioInfo;
		pPubMapUncmpAudioInfo = (media_q_pub_map_uncmp_audio_info_t *)pMediaQ->pPubMapInfo;
		if (!pPubMapUncmpAudioInfo) {
			AVB_LOG_ERROR("Public map data for audio info not allocated.");
			return;
		}


		if (strcmp(name, "intf_nv_ignore_timestamp") == 0) {
			tmp = strtol(value, &pEnd, 10);
			if (*pEnd == '\0' && tmp == 1) {
				pPvtData->ignoreTimestamp = (tmp == 1);
			}
		}

		else if (strcmp(name, "intf_nv_device_name") == 0) {
			if (pPvtData->pDeviceName) {
				free(pPvtData->pDeviceName);
			}
			pPvtData->pDeviceName = strdup(value);
		}

		else if (strcmp(name, "intf_nv_audio_rate") == 0) {
			val = strtol(value, &pEnd, 10);
			// TODO: Should check for specific values
			if (val >= AVB_AUDIO_RATE_8KHZ && val <= AVB_AUDIO_RATE_192KHZ) {
				pPvtData->audioRate = val;
			}
			else {
				AVB_LOG_ERROR("Invalid audio rate configured for intf_nv_audio_rate.");
				pPvtData->audioRate = AVB_AUDIO_RATE_44_1KHZ;
			}

			// Give the audio parameters to the mapping module.
			if (pMediaQ->pMediaQDataFormat) {
				if (strcmp(pMediaQ->pMediaQDataFormat, MapUncmpAudioMediaQDataFormat) == 0
					|| strcmp(pMediaQ->pMediaQDataFormat, MapAVTPAudioMediaQDataFormat) == 0) {
					pPubMapUncmpAudioInfo->audioRate = pPvtData->audioRate;
				}
				//else if (pMediaQ->pMediaQDataFormat == MapSAFMediaQDataFormat) {
				//}
			}
		}

		else if (strcmp(name, "intf_nv_audio_bit_depth") == 0) {
			val = strtol(value, &pEnd, 10);
			// TODO: Should check for specific values
			if (val >= AVB_AUDIO_BIT_DEPTH_1BIT && val <= AVB_AUDIO_BIT_DEPTH_64BIT) {
				pPvtData->audioBitDepth = val;
			}
			else {
				AVB_LOG_ERROR("Invalid audio type configured for intf_nv_audio_bits.");
				pPvtData->audioBitDepth = AVB_AUDIO_BIT_DEPTH_24BIT;
			}

			// Give the audio parameters to the mapping module.
			if (pMediaQ->pMediaQDataFormat) {
				if (strcmp(pMediaQ->pMediaQDataFormat, MapUncmpAudioMediaQDataFormat) == 0
					|| strcmp(pMediaQ->pMediaQDataFormat, MapAVTPAudioMediaQDataFormat) == 0) {
					pPubMapUncmpAudioInfo->audioBitDepth = pPvtData->audioBitDepth;
				}
				//else if (pMediaQ->pMediaQDataFormat == MapSAFMediaQDataFormat) {
				//}
			}
		}

		else if (strcmp(name, "intf_nv_audio_type") == 0) {
			if (strncasecmp(value, "float", 5) == 0) {
				pPvtData->audioType = AVB_AUDIO_TYPE_FLOAT;
			}
			else if (strncasecmp(value, "sign", 4) == 0
					 || strncasecmp(value, "int", 4) == 0) {
				pPvtData->audioType = AVB_AUDIO_TYPE_INT;
			}
			 else if (strncasecmp(value, "unsign", 6) == 0
					 || strncasecmp(value, "uint", 4) == 0) {
				pPvtData->audioType = AVB_AUDIO_TYPE_UINT;
			}
			else {
				AVB_LOG_ERROR("Invalid audio type configured for intf_nv_audio_type.");
				pPvtData->audioType = AVB_AUDIO_TYPE_UNSPEC;
			}

			// Give the audio parameters to the mapping module.
			if (pMediaQ->pMediaQDataFormat) {
				if (strcmp(pMediaQ->pMediaQDataFormat, MapUncmpAudioMediaQDataFormat) == 0
					|| strcmp(pMediaQ->pMediaQDataFormat, MapAVTPAudioMediaQDataFormat) == 0) {
					pPubMapUncmpAudioInfo->audioType = pPvtData->audioType;
				}
				//else if (pMediaQ->pMediaQDataFormat == MapSAFMediaQDataFormat) {
				//}
			}
		}

		else if (strcmp(name, "intf_nv_audio_endian") == 0) {
			if (strncasecmp(value, "big", 3) == 0) {
				pPvtData->audioEndian = AVB_AUDIO_ENDIAN_BIG;
			}
			else if (strncasecmp(value, "little", 6) == 0) {
				pPvtData->audioEndian = AVB_AUDIO_ENDIAN_LITTLE;
			}
			else {
				AVB_LOG_ERROR("Invalid audio type configured for intf_nv_audio_endian.");
				pPvtData->audioEndian = AVB_AUDIO_ENDIAN_UNSPEC;
			}

			// Give the audio parameters to the mapping module.
			if (pMediaQ->pMediaQDataFormat) {
				if (strcmp(pMediaQ->pMediaQDataFormat, MapUncmpAudioMediaQDataFormat) == 0
					|| strcmp(pMediaQ->pMediaQDataFormat, MapAVTPAudioMediaQDataFormat) == 0) {
					pPubMapUncmpAudioInfo->audioEndian = pPvtData->audioEndian;
				}
				//else if (pMediaQ->pMediaQDataFormat == MapSAFMediaQDataFormat) {
				//}
			}
		}

		else if (strcmp(name, "intf_nv_audio_channels") == 0) {
			val = strtol(value, &pEnd, 10);
			// TODO: Should check for specific values
			if (val >= AVB_AUDIO_CHANNELS_1 && val <= AVB_AUDIO_CHANNELS_8) {
				pPvtData->audioChannels = val;
			}
			else {
				AVB_LOG_ERROR("Invalid audio channels configured for intf_nv_audio_channels.");
				pPvtData->audioChannels = AVB_AUDIO_CHANNELS_2;
			}

			// Give the audio parameters to the mapping module.
			if (pMediaQ->pMediaQDataFormat) {
				if (strcmp(pMediaQ->pMediaQDataFormat, MapUncmpAudioMediaQDataFormat) == 0
					|| strcmp(pMediaQ->pMediaQDataFormat, MapAVTPAudioMediaQDataFormat) == 0) {
					pPubMapUncmpAudioInfo->audioChannels = pPvtData->audioChannels;
				}
				//else if (pMediaQ->pMediaQDataFormat == MapSAFMediaQDataFormat) {
				//}
			}

		}

		if (strcmp(name, "intf_nv_allow_resampling") == 0) {
			tmp = strtol(value, &pEnd, 10);
			if (*pEnd == '\0' && tmp == 1) {
				pPvtData->allowResampling = (tmp == 1);
			}
		}

		else if (strcmp(name, "intf_nv_start_threshold_periods") == 0) {
			pPvtData->startThresholdPeriods = strtol(value, &pEnd, 10);
		}

		else if (strcmp(name, "intf_nv_period_time") == 0) {
			pPvtData->periodTimeUsec = strtol(value, &pEnd, 10);
		}

		else if (strcmp(name, "intf_nv_ch2_filename") == 0) {
			if (pPvtData->pCh2Filename) {
				free(pPvtData->pCh2Filename);
			}
			pPvtData->pCh2Filename = strdup(value);
		}

	}

	AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

void openavbIntfTinyalsaGenInitCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);
	AVB_TRACE_EXIT(AVB_TRACE_INTF);
}
// A call to this callback indicates that this interface module will be
// a talker. Any talker initialization can be done in this function.
void openavbIntfTinyalsaTxInitCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);

	if (pMediaQ) {
		pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
		if (!pPvtData) {
			AVB_LOG_ERROR("Private interface module data not allocated.");
			return;
		}

		memset(&pPvtData->config, 0, sizeof(pPvtData->config));
		pPvtData->config.channels = pPvtData->audioChannels;
		pPvtData->config.rate = pPvtData->audioRate;
		pPvtData->config.period_size = PERIOD_SIZE;
		pPvtData->config.period_count = PERIOD_COUNT;
		pPvtData->config.start_threshold = 0;
		pPvtData->config.stop_threshold = 0;
		pPvtData->config.silence_threshold = 0;
		pPvtData->config.format = pPvtData->audioBitDepth;
		switch (pPvtData->audioBitDepth) {
			case AVB_AUDIO_BIT_DEPTH_16BIT:
				pPvtData->config.format = PCM_FORMAT_S16_LE;
				break;
			case AVB_AUDIO_BIT_DEPTH_32BIT:
				pPvtData->config.format = PCM_FORMAT_S32_LE;
				break;
			case AVB_AUDIO_BIT_DEPTH_8BIT:
				pPvtData->config.format = PCM_FORMAT_S8;
				break;
			default:
				pPvtData->config.format = PCM_FORMAT_S16_LE;
				break;
		}
		pPvtData->pcmHandle = pcm_open(0,0,PCM_IN,&pPvtData->config);
		if (!pPvtData->pcmHandle || !pcm_is_ready(pPvtData->pcmHandle)) {
			fprintf(stderr, "Unable to open PCM device (%s)\n",
			pcm_get_error(pPvtData->pcmHandle));
				return ;
		}

		pPvtData->pcm_size = pcm_frames_to_bytes(pPvtData->pcmHandle, pcm_get_buffer_size(pPvtData->pcmHandle));
	}

	AVB_TRACE_EXIT(AVB_TRACE_INTF);

}

// This callback will be called for each AVB transmit interval.
bool openavbIntfTinyalsaTxCB(media_q_t *pMediaQ)
{
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
			S32 rslt = 0;

			if (pMediaQItem->itemSize < pPubMapUncmpAudioInfo->itemSize) {
				AVB_LOG_ERROR("Media queue item not large enough for samples");
			}

			rslt = pcm_read(pPvtData->pcmHandle,
					(U8*)pMediaQItem->pPubData + pMediaQItem->dataLen,
				pPubMapUncmpAudioInfo->itemSize);

			if (rslt != 0) {
				AVB_LOGF_ERROR("pcm_read() error: %d, %s", rslt, strerror(rslt));
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
			return FALSE;	// Media queue full
		}
	}

	AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
	return FALSE;
}

// a listener. Any listener initialization can be done in this function.
void openavbIntfTinyalsaRxInitCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);

	if (pMediaQ) {
		pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
		if (!pPvtData) {
			AVB_LOG_ERROR("Private interface module data not allocated.");
			return;
		}
		memset(&pPvtData->config, 0, sizeof(pPvtData->config));

		pPvtData->config.channels = pPvtData->audioChannels;
		pPvtData->config.rate = pPvtData->audioRate;
		pPvtData->config.period_size = PERIOD_SIZE;
		pPvtData->config.period_count = PERIOD_COUNT;
		pPvtData->config.start_threshold = 0;
		pPvtData->config.stop_threshold = 0;
		pPvtData->config.silence_threshold = 0;
		pPvtData->config.format = pPvtData->audioBitDepth;
		switch(pPvtData->audioBitDepth) {
			case AVB_AUDIO_BIT_DEPTH_16BIT:
				pPvtData->config.format = PCM_FORMAT_S16_LE;
				break;
			case AVB_AUDIO_BIT_DEPTH_32BIT:
				pPvtData->config.format = PCM_FORMAT_S32_LE;
				break;
			case AVB_AUDIO_BIT_DEPTH_8BIT:
				pPvtData->config.format = PCM_FORMAT_S8;
				break;
			default:
				pPvtData->config.format = PCM_FORMAT_S16_LE;
				break;
		}

		//card = 0, device = 0
		pPvtData->pcmHandle = pcm_open(0, 0, PCM_OUT, &pPvtData->config);
		if (!pPvtData->pcmHandle || !pcm_is_ready(pPvtData->pcmHandle)) {
			fprintf(stderr, "Unable to open PCM device %u (%s)\n",
					0, pcm_get_error(pPvtData->pcmHandle));
			return;
		}
		pPvtData->pcm_size = pcm_frames_to_bytes(pPvtData->pcmHandle, pcm_get_buffer_size(pPvtData->pcmHandle));
		AVB_LOGF_INFO("PCM_SIZE: %d\n",pPvtData->pcm_size);
	}

	AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

// This callback is called when acting as a listener.
bool openavbIntfTinyalsaRxCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF_DETAIL);

	if (pMediaQ) {
		pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;

		bool moreItems = TRUE;

		while (moreItems) {
			media_q_item_t *pMediaQItem = openavbMediaQTailLock(pMediaQ, pPvtData->ignoreTimestamp);
			if (pMediaQItem) {
				if (pMediaQItem->dataLen) {
					S32 rslt;

					rslt = pcm_write(pPvtData->pcmHandle, pMediaQItem->pPubData, pMediaQItem->dataLen);
					if (rslt) {
						AVB_LOGF_ERROR("pcm_write: %d %d  %s", rslt, errno, pcm_get_error(pPvtData->pcmHandle));
						pcm_close(pPvtData->pcmHandle);
						pPvtData->pcmHandle = pcm_open(0, 0, PCM_OUT, &pPvtData->config);
						if (!pPvtData->pcmHandle || !pcm_is_ready(pPvtData->pcmHandle)) {
							fprintf(stderr, "Unable to open PCM device %u (%s)\n",
									0, pcm_get_error(pPvtData->pcmHandle));
						}

					}
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
void openavbIntfTinyalsaEndCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);

	if (pMediaQ) {
		pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
		if (!pPvtData) {
			AVB_LOG_ERROR("Private interface module data not allocated.");
			return;
		}

		if (pPvtData->pcmHandle) {
			pcm_close(pPvtData->pcmHandle);
			pPvtData->pcmHandle = NULL;
		}

		if (pPvtData->ch2Fd > 0) {
			close(pPvtData->ch2Fd);
		}
	}

	AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

void openavbIntfTinyalsaGenEndCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);
	AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

// Main initialization entry point into the interface module
extern DLL_EXPORT bool openavbIntfTinyalsaInitialize(media_q_t *pMediaQ, openavb_intf_cb_t *pIntfCB)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);

	if (pMediaQ) {
		// Memory freed by the media queue when the media queue is destroyed.
		pMediaQ->pPvtIntfInfo = calloc(1, sizeof(pvt_data_t));
		if (!pMediaQ->pPvtIntfInfo) {
			AVB_LOG_ERROR("Unable to allocate memory for AVTP interface module.");
			return FALSE;
		}

		pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;

		pIntfCB->intf_cfg_cb = openavbIntfTinyalsaCfgCB;
		pIntfCB->intf_gen_init_cb = openavbIntfTinyalsaGenInitCB;
		pIntfCB->intf_tx_init_cb = openavbIntfTinyalsaTxInitCB;
		pIntfCB->intf_tx_cb = openavbIntfTinyalsaTxCB;
		pIntfCB->intf_rx_init_cb = openavbIntfTinyalsaRxInitCB;
		pIntfCB->intf_rx_cb = openavbIntfTinyalsaRxCB;
		pIntfCB->intf_end_cb = openavbIntfTinyalsaEndCB;
		pIntfCB->intf_gen_end_cb = openavbIntfTinyalsaGenEndCB;

		pPvtData->ignoreTimestamp = FALSE;
		pPvtData->pDeviceName = strdup(PCM_DEVICE_NAME_DEFAULT);
		pPvtData->allowResampling = TRUE;
		pPvtData->intervalCounter = 0;
		pPvtData->startThresholdPeriods = 2;	// Default to 2 periods of frames as the start threshold
		pPvtData->periodTimeUsec = 100000;

		pPvtData->ch2Fd = 0;
	}

	AVB_TRACE_EXIT(AVB_TRACE_INTF);
	return TRUE;
}

