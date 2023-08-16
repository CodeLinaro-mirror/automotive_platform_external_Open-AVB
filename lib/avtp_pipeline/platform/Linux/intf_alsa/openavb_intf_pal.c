/*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

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
* MODULE SUMMARY : PAL interface module.
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
#include "openavb_reference_clock_pub.h"

#define AVB_LOG_COMPONENT   "PAL Interface"
#include "openavb_log_pub.h"

// The asoundlib.h header needs to appear after openavb_trace_pub.h otherwise an incompatibtily version of time.h gets pulled in.
//#include <sound/asound.h>
#include "PalApi.h"




#define PCM_DEVICE_NAME_DEFAULT "default"
#define PCM_ACCESS_TYPE SND_PCM_ACCESS_RW_INTERLEAVED
#define PERIOD_SIZE 256
#define PERIOD_COUNT 4

static int pal_init_status = 0;

#if 0 // set to 1 to enable MCR debug logs
#define MCR_DEBUG_LOG AVB_LOG_ERROR
#define MCR_DEBUG_LOGF AVB_LOGF_ERROR
#else
#define MCR_DEBUG_LOG(MSG)
#define MCR_DEBUG_LOGF(FMT, ...)
#endif

#define MUTEX_LOCK_ERR(mutex_handle) { \
    MUTEX_CREATE_ERR(); \
    MUTEX_LOCK(mutex_handle); \
    MUTEX_LOG_ERR("Mutex Lock failure"); \
}

#define MUTEX_UNLOCK_ERR(mutex_handle) { \
    MUTEX_CREATE_ERR(); \
    MUTEX_UNLOCK(mutex_handle); \
    MUTEX_LOG_ERR("Mutex Unlock failure"); \
}

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

    U32 frameSizeBytes;

    /////////////
    // Variable data
    /////////////
    // Handle for the PCM device
    uint64_t *pcmHandle;
    struct pal_device devices;
    struct pal_stream_attributes stream_attr;
    uint32_t inBufSize;
    uint32_t outBufSize;
    uint32_t inBufCount;
    uint32_t outBufCount;
    U32 pcm_size;
    // ALSA read/write interval
    U32 intervalCounter;
    int ch2Fd;

    // This is used by the listener.
    // 1 if the clock tick is to be generated when the audio is consumed.
    // 0 otherwise.
    U16 genClockTickOnConsumeAudio;

    // intf_nv_clock_source_timestamp_interval
    U32 clockSourceTimestampInterval;

    // intf_nv_clock_source_timestamp_throwaway
    // This is used by the listener when acting as a clock source.
    // This specifies how many clock reference timestamps to throwaway
    // when starting the stream to allow the ALSA buffer to fill and
    // the consume rate to stabilize.
    U32 clockSourceTimestampThrowaway;

    // intf_nv_sync_to_clock_tick_on_tx_audio
    // This is used by the talker.
    // 1 if the talker should synchronize to the reference clock when
    // transmitting audio.
    // 0 otherwise.
    U16 syncToClockTickOnTxAudio;

    // intf_nv_clock_recovery_adjustment_range
    // This useused by the talker.
    // Clock recovery adjustment range. If the actual number of frames
    // minus the reference number of frames is off by this amount,
    // then we'll add one frame at the next clock tick.
    U16 clockRecoveryAdjustmentRange;

    // VARIABLES FOR TALKER REFERENCE CLOCK

    // The number of frames that have been transmitted minus
    // the number of frames that should have been transmitted according to
    // the reference clock. If this number if negative, then we haven't
    // transmitted enough frames and we need to repeat frames to catch up.
    // If this number is positive, then we transmitted too many frames and
    // we need to remove frames to catch up.
    S32 actualMinusRefFrames;

    // The number of frames that are necessary to maintain the clock
    // synchronization. If this is negative, then we need to repeat this
    // many frames before the next clock tick. If this is positive,
    // then we need to remove this many frames before the
    // next clock tick. This is computed by actualMinusRefFrames
    // divided by the clockRecoveryAdjustmentRange
    S32 framesToRecoverClock;

    // Mutex for actualMinusRefFrames
    MUTEX_HANDLE(mtxActualMinusRefFrames);

    // True if the talker has begun transmitting
    bool txStarted;

    // True if the reference clock has started
    bool refClkStarted;

    // The scratch audio buffer.
    // When frames need to be added or removed for clock synchronization,
    // extra frames are stored in this buffer. This is necessary because
    // audio reads must be in multiples of the period size.
    U8 *audioBuffer;
    U32 audioBufferSize;
    U32 audioBufferStartIdx;

    // VARIABLES FOR LISTENER REFERENCE CLOCK

    // The number of frames that have been passed to ALSA by the listener.
    U32 framesConsumed;

    U32 alsaPeriodSize;
    U32 alsaPeriodCount;

    U32 deviceId;
    U32 cardId;

    U32 alsaStartThreshold;
    U32 alsaStopThreshold;

    // True, if it's a modem
    // Otherwise mic/headset
    bool isVoiceCall;
    float volume_level;
} pvt_data_t;

typedef enum {
    //OUTPUT DEVICES
    AVB_PAL_DEVICE_OUT_MIN = 0,
    AVB_PAL_DEVICE_OUT_NONE = 1, /**< for transcode usecases*/
    AVB_PAL_DEVICE_OUT_HANDSET = 2,
    AVB_PAL_DEVICE_OUT_SPEAKER = 3,
    AVB_PAL_DEVICE_OUT_WIRED_HEADSET = 4,
    AVB_PAL_DEVICE_OUT_WIRED_HEADPHONE = 5, /**< Wired headphones without mic*/
    AVB_PAL_DEVICE_OUT_LINE = 6,
    AVB_PAL_DEVICE_OUT_BLUETOOTH_SCO = 7,
    AVB_PAL_DEVICE_OUT_BLUETOOTH_A2DP = 8,
    AVB_PAL_DEVICE_OUT_AUX_DIGITAL = 9,
    AVB_PAL_DEVICE_OUT_HDMI = 10,
    AVB_PAL_DEVICE_OUT_USB_DEVICE = 11,
    AVB_PAL_DEVICE_OUT_USB_HEADSET = 12,
    AVB_PAL_DEVICE_OUT_SPDIF = 13,
    AVB_PAL_DEVICE_OUT_FM = 14,
    AVB_PAL_DEVICE_OUT_AUX_LINE = 15,
    AVB_PAL_DEVICE_OUT_PROXY = 16,
    AVB_PAL_DEVICE_OUT_AUX_DIGITAL_1 = 17,
    // Add new OUT devices here, increment MAX and MIN below when you do so
    AVB_PAL_DEVICE_OUT_MAX = 18,
} avb_pal_out_device_id_t;

typedef enum {
    //INPUT DEVICES
    AVB_PAL_DEVICE_IN_MIN = 0,
    AVB_PAL_DEVICE_IN_NONE = 1, /**< for transcode usecases*/
    AVB_PAL_DEVICE_IN_HANDSET_MIC = 2,
    AVB_PAL_DEVICE_IN_SPEAKER_MIC = 3,
    AVB_PAL_DEVICE_IN_WIRED_HEADSET = 4,
    AVB_PAL_DEVICE_IN_HANDSET_VA_MIC = 5,
    AVB_PAL_DEVICE_IN_LINE = 6,
    AVB_PAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET = 7,
    AVB_PAL_DEVICE_IN_BLUETOOTH_A2DP = 8,
    AVB_PAL_DEVICE_IN_AUX_DIGITAL = 9,
    AVB_PAL_DEVICE_IN_HDMI = 10,
    AVB_PAL_DEVICE_IN_USB_DEVICE = 11,
    AVB_PAL_DEVICE_IN_USB_HEADSET = 12,
    AVB_PAL_DEVICE_IN_SPDIF = 13,
    AVB_PAL_DEVICE_IN_FM_TUNER = 14,
    AVB_PAL_DEVICE_IN_HEADSET_VA_MIC = 15,
    AVB_PAL_DEVICE_IN_PROXY = 16,
    AVB_PAL_DEVICE_IN_USB_ACCESSORY = 17,
    AVB_PAL_DEVICE_IN_VI_FEEDBACK = 18,
    // Add new IN devices here, increment MAX and MIN below when you do so
    AVB_PAL_DEVICE_IN_MAX = 19,
} avb_pal_in_device_id_t;

static void parsePcmDeviceName(char *pDeviceName, U32 *cardID, U32 *deviceID)
{
    U32 card = 0;
    U32 device = 0;

    if ((!pDeviceName) || (!cardID) || (!deviceID)) {
        AVB_LOGF_ERROR("%s: Invalid format ", __func__);
        return;
    }

    /*Default values*/
    *cardID = 0;
    *deviceID = 0;

    if ((pDeviceName[0] != 'h')
            || (pDeviceName[1] != 'w')
            || (pDeviceName[2] != ':')) {
        AVB_LOGF_ERROR("%s: Invalid format ", __func__);
        return;
    } else if (sscanf(&pDeviceName[3], "%u,%u", &card, &device) != 2) {
        AVB_LOGF_ERROR("%s: Invalid format ", __func__);
        return;
    }

    *cardID = card;
    return;
}


void channel_map(uint8_t* ch_map, int channels)
{
    switch (channels) {
        case 1:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            break;

        case 2:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_FR;
            break;

        case 3:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_FR;
            ch_map[2] = PAL_CHMAP_CHANNEL_C;
            break;

        case 4:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_FR;
            ch_map[2] = PAL_CHMAP_CHANNEL_LB;
            ch_map[3] = PAL_CHMAP_CHANNEL_RB;
            break;

        case 5:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_FR;
            ch_map[2] = PAL_CHMAP_CHANNEL_LB;
            ch_map[3] = PAL_CHMAP_CHANNEL_RB;
            ch_map[4] = PAL_CHMAP_CHANNEL_C;
            break;

        case 6:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_FR;
            ch_map[2] = PAL_CHMAP_CHANNEL_C;
            ch_map[3] = PAL_CHMAP_CHANNEL_LFE;
            ch_map[4] = PAL_CHMAP_CHANNEL_LB;
            ch_map[5] = PAL_CHMAP_CHANNEL_RB;
            break;

        case 7:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_FR;
            ch_map[2] = PAL_CHMAP_CHANNEL_C;
            ch_map[3] = PAL_CHMAP_CHANNEL_LFE;
            ch_map[4] = PAL_CHMAP_CHANNEL_LB;
            ch_map[5] = PAL_CHMAP_CHANNEL_RB;
            ch_map[6] = PAL_CHMAP_CHANNEL_RC;
            break;

        case 8:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_C;
            ch_map[2] = PAL_CHMAP_CHANNEL_FR;
            ch_map[3] = PAL_CHMAP_CHANNEL_SL;
            ch_map[4] = PAL_CHMAP_CHANNEL_SR;
            ch_map[5] = PAL_CHMAP_CHANNEL_LB;
            ch_map[6] = PAL_CHMAP_CHANNEL_RB;
            ch_map[7] = PAL_CHMAP_CHANNEL_LFE;
            break;
    }
}

void get_in_device_id (avb_pal_in_device_id_t inDeviceId,
                       struct pal_device *devices)
{
    switch (inDeviceId) {
        case AVB_PAL_DEVICE_IN_MIN:
            devices->id = PAL_DEVICE_IN_MIN;
            break;

        case AVB_PAL_DEVICE_IN_NONE:
            devices->id = PAL_DEVICE_NONE;
            break;

        case AVB_PAL_DEVICE_IN_HANDSET_MIC:
            devices->id = PAL_DEVICE_IN_HANDSET_MIC;
            break;

        case AVB_PAL_DEVICE_IN_SPEAKER_MIC:
            devices->id = PAL_DEVICE_IN_SPEAKER_MIC;
            break;

        case AVB_PAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET:
            devices->id = PAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
            break;

        case AVB_PAL_DEVICE_IN_WIRED_HEADSET:
            devices->id = PAL_DEVICE_IN_WIRED_HEADSET;
            break;

        case AVB_PAL_DEVICE_IN_AUX_DIGITAL:
            devices->id = PAL_DEVICE_IN_AUX_DIGITAL;
            break;

        case AVB_PAL_DEVICE_IN_HDMI:
            devices->id = PAL_DEVICE_IN_HDMI;
            break;

        case AVB_PAL_DEVICE_IN_USB_ACCESSORY:
            devices->id = PAL_DEVICE_IN_USB_ACCESSORY;
            break;

        case AVB_PAL_DEVICE_IN_USB_DEVICE:
            devices->id = PAL_DEVICE_IN_USB_DEVICE;
            break;

        case AVB_PAL_DEVICE_IN_USB_HEADSET:
            devices->id = PAL_DEVICE_IN_USB_HEADSET;
            break;

        case AVB_PAL_DEVICE_IN_FM_TUNER:
            devices->id = PAL_DEVICE_IN_FM_TUNER;
            break;

        case AVB_PAL_DEVICE_IN_LINE:
            devices->id = PAL_DEVICE_IN_LINE;
            break;

        case AVB_PAL_DEVICE_IN_SPDIF:
            devices->id = PAL_DEVICE_IN_SPDIF;
            break;

        case AVB_PAL_DEVICE_IN_PROXY:
            devices->id = PAL_DEVICE_IN_PROXY;
            break;

        case AVB_PAL_DEVICE_IN_HANDSET_VA_MIC:
            devices->id = PAL_DEVICE_IN_HANDSET_VA_MIC;
            break;

        case AVB_PAL_DEVICE_IN_BLUETOOTH_A2DP:
            devices->id = PAL_DEVICE_IN_BLUETOOTH_A2DP;
            break;

        case AVB_PAL_DEVICE_IN_HEADSET_VA_MIC:
            devices->id = PAL_DEVICE_IN_HEADSET_VA_MIC;
            break;

        case AVB_PAL_DEVICE_IN_VI_FEEDBACK:
            devices->id = PAL_DEVICE_IN_VI_FEEDBACK;
            break;

        default:
            AVB_LOG_ERROR("Invalid input device id");
            break;
    }
}

void get_out_device_id (avb_pal_out_device_id_t outDeviceId,
                        struct pal_device *devices)
{
    switch (outDeviceId) {
        case AVB_PAL_DEVICE_OUT_MIN:
            devices->id = PAL_DEVICE_OUT_MIN;
            break;

        case AVB_PAL_DEVICE_OUT_NONE:
            devices->id = PAL_DEVICE_NONE;
            break;

        case AVB_PAL_DEVICE_OUT_HANDSET:
            devices->id = PAL_DEVICE_OUT_HANDSET;
            break;

        case AVB_PAL_DEVICE_OUT_SPEAKER:
            devices->id = PAL_DEVICE_OUT_SPEAKER;
            break;

        case AVB_PAL_DEVICE_OUT_WIRED_HEADSET:
            devices->id = PAL_DEVICE_OUT_WIRED_HEADSET;
            break;

        case AVB_PAL_DEVICE_OUT_WIRED_HEADPHONE:
            devices->id = PAL_DEVICE_OUT_WIRED_HEADPHONE;
            break;

        case AVB_PAL_DEVICE_OUT_LINE:
            devices->id = PAL_DEVICE_OUT_LINE;
            break;

        case AVB_PAL_DEVICE_OUT_BLUETOOTH_SCO:
            devices->id = PAL_DEVICE_OUT_BLUETOOTH_SCO;
            break;

        case AVB_PAL_DEVICE_OUT_BLUETOOTH_A2DP:
            devices->id = PAL_DEVICE_OUT_BLUETOOTH_A2DP;
            break;

        case AVB_PAL_DEVICE_OUT_AUX_DIGITAL:
            devices->id = PAL_DEVICE_OUT_AUX_DIGITAL;
            break;

        case AVB_PAL_DEVICE_OUT_HDMI:
            devices->id = PAL_DEVICE_OUT_HDMI;
            break;

        case AVB_PAL_DEVICE_OUT_USB_DEVICE:
            devices->id = PAL_DEVICE_OUT_USB_DEVICE;
            break;

        case AVB_PAL_DEVICE_OUT_USB_HEADSET:
            devices->id = PAL_DEVICE_OUT_USB_HEADSET;
            break;

        case AVB_PAL_DEVICE_OUT_SPDIF:
            devices->id = PAL_DEVICE_OUT_SPDIF;
            break;

        case AVB_PAL_DEVICE_OUT_FM:
            devices->id = PAL_DEVICE_OUT_FM;
            break;

        case AVB_PAL_DEVICE_OUT_AUX_LINE:
            devices->id = PAL_DEVICE_OUT_AUX_LINE;
            break;

        case AVB_PAL_DEVICE_OUT_PROXY:
            devices->id = PAL_DEVICE_OUT_PROXY;
            break;

        case AVB_PAL_DEVICE_OUT_AUX_DIGITAL_1:
            devices->id = PAL_DEVICE_OUT_AUX_DIGITAL_1;
            break;

        default:
            AVB_LOG_ERROR("Invalid output device id");
            break;
    }
}


// Each configuration name value pair for this mapping will result in this callback being called.
void openavbIntfPalCfgCB(media_q_t *pMediaQ, const char *name,
                         const char *value)
{
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);

    if (pMediaQ) {
        char *pEnd;
        long tmp;
        U32 val;
        U32 VoiceCall = 0;
        avb_pal_in_device_id_t      inDeviceId = 0;
        avb_pal_out_device_id_t     outDeviceId = 0;
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;

        if (!pPvtData) {
            AVB_LOG_ERROR("Private interface module data not allocated.");
            return;
        }

        media_q_pub_map_uncmp_audio_info_t *pPubMapUncmpAudioInfo;
        pPubMapUncmpAudioInfo = (media_q_pub_map_uncmp_audio_info_t *)
                                pMediaQ->pPubMapInfo;

        if (!pPubMapUncmpAudioInfo) {
            AVB_LOG_ERROR("Public map data for audio info not allocated.");
            return;
        }

        if (strcmp(name, "intf_nv_ignore_timestamp") == 0) {
            tmp = strtol(value, &pEnd, 10);

            if (*pEnd == '\0' && tmp == 1) {
                pPvtData->ignoreTimestamp = (tmp == 1);
            }
        } else if (strcmp(name, "intf_nv_device_name") == 0) {
            if (pPvtData->pDeviceName) {
                free(pPvtData->pDeviceName);
            }

            pPvtData->pDeviceName = strdup(value);
            parsePcmDeviceName(pPvtData->pDeviceName, &pPvtData->cardId,
                               &pPvtData->deviceId);
            AVB_LOGF_INFO("PCM: card=%u device=%u", pPvtData->cardId, pPvtData->deviceId);
        } else if (strcmp(name, "intf_nv_audio_rate") == 0) {
            val = strtol(value, &pEnd, 10);

            // TODO: Should check for specific values
            if (val >= AVB_AUDIO_RATE_8KHZ && val <= AVB_AUDIO_RATE_192KHZ) {
                pPvtData->audioRate = val;
            } else {
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
        } else if (strcmp(name, "intf_nv_audio_bit_depth") == 0) {
            val = strtol(value, &pEnd, 10);

            // TODO: Should check for specific values
            if (val >= AVB_AUDIO_BIT_DEPTH_1BIT && val <= AVB_AUDIO_BIT_DEPTH_64BIT) {
                pPvtData->audioBitDepth = val;
            } else {
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
        } else if (strcmp(name, "intf_nv_audio_type") == 0) {
            if (strncasecmp(value, "float", 5) == 0) {
                pPvtData->audioType = AVB_AUDIO_TYPE_FLOAT;
            } else if (strncasecmp(value, "sign", 4) == 0
                       || strncasecmp(value, "int", 4) == 0) {
                pPvtData->audioType = AVB_AUDIO_TYPE_INT;
            } else if (strncasecmp(value, "unsign", 6) == 0
                       || strncasecmp(value, "uint", 4) == 0) {
                pPvtData->audioType = AVB_AUDIO_TYPE_UINT;
            } else {
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
        } else if (strcmp(name, "intf_nv_audio_endian") == 0) {
            if (strncasecmp(value, "big", 3) == 0) {
                pPvtData->audioEndian = AVB_AUDIO_ENDIAN_BIG;
            } else if (strncasecmp(value, "little", 6) == 0) {
                pPvtData->audioEndian = AVB_AUDIO_ENDIAN_LITTLE;
            } else {
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
        } else if (strcmp(name, "intf_nv_audio_channels") == 0) {
            val = strtol(value, &pEnd, 10);

            // TODO: Should check for specific values
            if (val >= AVB_AUDIO_CHANNELS_1 && val <= AVB_AUDIO_CHANNELS_32) {
                pPvtData->audioChannels = val;
            } else {
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
        } else if (strcmp(name, "intf_nv_start_threshold_periods") == 0) {
            pPvtData->startThresholdPeriods = strtol(value, &pEnd, 10);
        } else if (strcmp(name, "intf_nv_period_time") == 0) {
            pPvtData->periodTimeUsec = strtol(value, &pEnd, 10);
        } else if (strcmp(name, "intf_nv_ch2_filename") == 0) {
            if (pPvtData->pCh2Filename) {
                free(pPvtData->pCh2Filename);
            }

            pPvtData->pCh2Filename = strdup(value);
        } else if (strcmp(name, "intf_nv_clock_source_timestamp_interval") ==
                   0) {
            pPvtData->clockSourceTimestampInterval = strtol(value,
                    &pEnd, 10);
            pPubMapUncmpAudioInfo->timestampInterval =
                pPvtData->clockSourceTimestampInterval;
            AVB_LOGF_INFO("clockSourceTimestampInterval = %d",
                          pPvtData->clockSourceTimestampInterval);
        } else if (strcmp(name, "intf_nv_gen_clock_tick_on_consume_audio") ==
                   0) {
            pPvtData->genClockTickOnConsumeAudio = strtol(value,
                                                   &pEnd, 10);
            AVB_LOGF_INFO("genClockTickOnConsumeAudio = %d",
                          pPvtData->genClockTickOnConsumeAudio);
        } else if (strcmp(name, "intf_nv_sync_to_clock_tick_on_tx_audio") ==
                   0) {
            pPvtData->syncToClockTickOnTxAudio = strtol(value,
                                                 &pEnd, 10);
            AVB_LOGF_INFO("intf_nv_sync_to_clock_tick_on_tx_audio = %d",
                          pPvtData->syncToClockTickOnTxAudio);
        } else if (strcmp(name, "intf_nv_clock_recovery_adjustment_range") ==
                   0) {
            pPvtData->clockRecoveryAdjustmentRange = strtol(value,
                    &pEnd, 10);
            AVB_LOGF_INFO("intf_nv_clock_recovery_adjustment_range = %d",
                          pPvtData->clockRecoveryAdjustmentRange);
        } else if (strcmp(name, "intf_nv_alsa_period_size") ==
                   0) {
            pPvtData->alsaPeriodSize = strtol(value,
                                              &pEnd, 10);
            AVB_LOGF_INFO("intf_nv_alsaPeriodSize = %d", pPvtData->alsaPeriodSize);
        } else if (strcmp(name, "intf_nv_alsa_period_count") ==
                   0) {
            pPvtData->alsaPeriodCount = strtol(value,
                                               &pEnd, 10);
            AVB_LOGF_INFO("intf_nv_alsa_period_count = %d", pPvtData->alsaPeriodCount);
        } else if (strcmp(name, "intf_nv_alsa_start_threshold") ==
                   0) {
            pPvtData->alsaStartThreshold = strtol(value,
                                                  &pEnd, 10);
            AVB_LOGF_INFO("intf_nv_alsa_start_threshold = %d",
                          pPvtData->alsaStartThreshold);
        } else if (strcmp(name, "intf_nv_alsa_stop_threshold") ==
                   0) {
            pPvtData->alsaStopThreshold = strtol(value,
                                                 &pEnd, 10);
            AVB_LOGF_INFO("intf_nv_alsa_stop_threshold = %d", pPvtData->alsaStopThreshold);
        } else if (strcmp(name, "intf_nv_is_voice_call") ==
                   0) {
            VoiceCall = strtol(value, &pEnd, 10);

            if (VoiceCall != 0 && VoiceCall != 1) {
                AVB_LOG_ERROR("intf_nv_is_voice_call has to be 0 or 1. setting the default value as 0");
                pPvtData->isVoiceCall = 0;
            } else {
                pPvtData->isVoiceCall = VoiceCall;
                AVB_LOGF_INFO("intf_nv_is_voice_call = %d", pPvtData->isVoiceCall);
            }
        } else if (strcmp(name, "intf_nv_pal_in_device_id") ==
                   0) {
            inDeviceId = strtol(value, &pEnd, 10);

            if (inDeviceId < 0 || inDeviceId > 18) {
                AVB_LOG_ERROR("intf_nv_pal_in_device_id has to be in between 0 to 18. setting the default value as 3");
                inDeviceId = 3;
            }

            AVB_LOGF_INFO("intf_nv_pal_in_device_id = %d", inDeviceId);
            get_in_device_id(inDeviceId, &(pPvtData->devices));
            pPvtData->deviceId = pPvtData->devices.id;
            AVB_LOGF_INFO("in Device Id = %d", pPvtData->deviceId);
        } else if (strcmp(name, "intf_nv_pal_out_device_id") ==
                   0) {
            outDeviceId = strtol(value, &pEnd, 10);

            if (outDeviceId < 0 || outDeviceId > 17) {
                AVB_LOG_ERROR("intf_nv_pal_out_device_id has to be in between 0 to 17. setting the default value as 3\n");
                outDeviceId = 3;
            }

            AVB_LOGF_INFO("intf_nv_pal_out_device_id = %d", outDeviceId);
            get_out_device_id(outDeviceId, &(pPvtData->devices));
            pPvtData->deviceId = pPvtData->devices.id;
            AVB_LOGF_INFO("out device id= %d", pPvtData->deviceId);
        } else if (strcmp(name, "intf_nv_pal_volume_level") == 0) {
            pPvtData->volume_level = strtof(value, &pEnd);
            AVB_LOGF_INFO("intf_nv_pal_volume_level: %f \n", pPvtData->volume_level);

            if (pPvtData->volume_level < 0 || pPvtData->volume_level > 1) {
                AVB_LOG_INFO("volume level has to be between 0.0 to 1.0. setting the default value of 0.4");
                pPvtData->volume_level = 0.4f;
            }
        }
    }

    AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

void openavbIntfPalGenInitCB(media_q_t *pMediaQ)
{
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);
    media_q_pub_map_uncmp_audio_info_t *pPubMapUncmpAudioInfo;
    pPubMapUncmpAudioInfo = (media_q_pub_map_uncmp_audio_info_t *)
                            pMediaQ->pPubMapInfo;

    if (!pPubMapUncmpAudioInfo) {
        AVB_LOG_ERROR("Public map data for audio info not allocated.");
        return;
    }

    pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;

    if (!pPvtData) {
        AVB_LOG_ERROR("Private interface module data not allocated.");
        return;
    }

    pPvtData->frameSizeBytes = pPubMapUncmpAudioInfo->itemFrameSizeBytes;
    AVB_TRACE_EXIT(AVB_TRACE_INTF);
}


// This is called every time the clock ticks for the talker.
// The interval is the number of frames have should have been transmitted
// since the last clock tick.
static void handleClockTick(void *context, U64 timestamp,
                            U16 ticks, U32 interval, bool restartClock)
{
    pvt_data_t *pPvtData = context;
    (void) timestamp;
    MUTEX_LOCK_ERR(pPvtData->mtxActualMinusRefFrames);

    // If restartClock is true, then set the counters to 0
    if (restartClock) {
        MCR_DEBUG_LOG("Restarting the clock");
        pPvtData->actualMinusRefFrames = 0;
        pPvtData->framesToRecoverClock = 0;
    }

    if (ticks) {
        pPvtData->refClkStarted = TRUE;
    }

    if (ticks && pPvtData->txStarted) {
        // We received a clock tick so adjust the count by the timing
        // interval.
        pPvtData->actualMinusRefFrames -= ticks * interval;
        // If we haven't transmitted enough frames or we have
        // transmitted too few frames, compute how many frames we
        // need to add or remove to maintain synchronization.
        // This is computed here so that if if the clock stops
        // ticking, the talker will freerun.
        pPvtData->framesToRecoverClock +=
            pPvtData->actualMinusRefFrames /
            pPvtData->clockRecoveryAdjustmentRange;

        if (pPvtData->framesToRecoverClock != 0) {
            MCR_DEBUG_LOGF("actualMinusRefFrames: %d, framesToRecoverClock: %d",
                           pPvtData->actualMinusRefFrames,
                           pPvtData->framesToRecoverClock);
        }
    }

    MUTEX_UNLOCK_ERR(pPvtData->mtxActualMinusRefFrames);
}


// A call to this callback indicates that this interface module will be
// a talker. Any talker initialization can be done in this function.
void openavbIntfPalTxInitCB(media_q_t *pMediaQ)
{
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);
    int errval = 0;

    if (pMediaQ) {
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
        media_q_pub_map_uncmp_audio_info_t *pPubMapUncmpAudioInfo =
            pMediaQ->pPubMapInfo;

        if (!pPvtData) {
            AVB_LOG_ERROR("Private interface module data not allocated.");
            return;
        }

        // Register the handler for the reference clock ticks
        if (pPvtData->syncToClockTickOnTxAudio) {
            refClkRegisterObserver(handleClockTick, pPvtData);
        }

        if (pPvtData->isVoiceCall == true) {
            memset(&pPvtData->stream_attr, 0, sizeof(pPvtData->stream_attr));
            pPvtData->stream_attr.type = PAL_STREAM_VOICE_CALL_RECORD;
            pPvtData->stream_attr.info.voice_rec_info.record_direction =
                INCALL_RECORD_VOICE_DOWNLINK;
            pPvtData->stream_attr.flags = PAL_STREAM_FLAG_TIMESTAMP;
            pPvtData->stream_attr.direction = PAL_AUDIO_INPUT;
            pPvtData->stream_attr.in_media_config.ch_info.channels =
                pPvtData->audioChannels;
            pPvtData->stream_attr.in_media_config.sample_rate = pPvtData->audioRate;
            channel_map(pPvtData->stream_attr.in_media_config.ch_info.ch_map,
                        pPvtData->audioChannels);
            pPvtData->stream_attr.in_media_config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM;
            AVB_LOG_INFO("Voice Call setup.");
        } else {
            memset(&pPvtData->stream_attr, 0, sizeof(pPvtData->stream_attr));
            pPvtData->stream_attr.type = PAL_STREAM_DEEP_BUFFER;
            pPvtData->stream_attr.info.opt_stream_info.version = 1;
            pPvtData->stream_attr.info.opt_stream_info.duration_us = -1;
            pPvtData->stream_attr.info.opt_stream_info.has_video = false;
            pPvtData->stream_attr.info.opt_stream_info.is_streaming = true;
            pPvtData->stream_attr.flags = (pal_stream_flags_t)(PAL_STREAM_FLAG_TIMESTAMP |
                                          PAL_STREAM_FLAG_NON_BLOCKING);;
            pPvtData->stream_attr.direction = PAL_AUDIO_INPUT;
            pPvtData->stream_attr.in_media_config.ch_info.channels =
                pPvtData->audioChannels;
            pPvtData->stream_attr.in_media_config.sample_rate = pPvtData->audioRate;
            channel_map(pPvtData->stream_attr.in_media_config.ch_info.ch_map,
                        pPvtData->audioChannels);
            pPvtData->stream_attr.in_media_config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM;
            AVB_LOG_INFO("Direct audio setup.");
        }

        memset(&pPvtData->devices, 0, sizeof(pPvtData->devices));
        pPvtData->devices.id = pPvtData->deviceId;
        pPvtData->devices.config.ch_info.channels = pPvtData->audioChannels;
        pPvtData->devices.config.sample_rate = pPvtData->audioRate;
        pPvtData->devices.config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM;
        channel_map(pPvtData->devices.config.ch_info.ch_map, pPvtData->audioChannels);

        switch (pPvtData->audioBitDepth) {
            case AVB_AUDIO_BIT_DEPTH_16BIT:
                pPvtData->stream_attr.in_media_config.bit_width = 16;
                break;

            case AVB_AUDIO_BIT_DEPTH_32BIT:
                pPvtData->stream_attr.in_media_config.bit_width = 32;
                break;

            case AVB_AUDIO_BIT_DEPTH_8BIT:
                pPvtData->stream_attr.in_media_config.bit_width = 8;
                break;

            default:
                pPvtData->stream_attr.in_media_config.bit_width = 16;
                break;
        }

        AVB_LOGF_INFO("%s: pcm_open cardId=%u deviceId=%u channels %d", __func__,
                      pPvtData->cardId,
                      pPvtData->deviceId, pPvtData->audioChannels);
        errval = pal_stream_open(&pPvtData->stream_attr, 1, &pPvtData->devices, 0, NULL,
                                 NULL, NULL,
                                 &pPvtData->pcmHandle);

        if (errval != 0) {
            AVB_LOGF_ERROR("stream_open failed, returned %d ", errval);
            return;
        }

        if (!pPvtData->pcmHandle) {
            AVB_LOGF_ERROR("Unable to open errval %d\n",
                           errval);
            return;
        } else {
            AVB_LOG_INFO("pcm open success");
        }

        pPvtData->inBufSize = 1920;
        pPvtData->inBufCount = 4;
        errval = pal_stream_set_buffer_size(pPvtData->pcmHandle,
                                            (size_t*)&pPvtData->inBufSize, pPvtData->inBufCount,
                                            (size_t*)&pPvtData->outBufSize, pPvtData->outBufCount);

        if (errval != 0) {
            AVB_LOGF_ERROR("pal_stream_set_buffer_size failed, returned %d ",
                           errval);
            return;
        }

        errval = pal_stream_start(pPvtData->pcmHandle);

        if (errval != 0) {
            AVB_LOGF_ERROR("stream_start failed, returned %d ", errval);
            return -1;
        }

        struct pal_volume_data* volume_data = (struct pal_volume_data *) calloc(1,
                                              sizeof(uint32_t) + (pPvtData->audioChannels * sizeof(struct
                                                      pal_channel_vol_kv)));

        if (volume_data == NULL) {
            AVB_LOG_ERROR("Unable to set volume");
        } else {
            if (pPvtData->audioChannels == 1) {
                volume_data->no_of_volpair = 1;
                volume_data->volume_pair[0].channel_mask = PAL_CHMAP_CHANNEL_FL;
                volume_data->volume_pair[0].vol = pPvtData->volume_level;
            } else {
                volume_data->no_of_volpair = 2;
                volume_data->volume_pair[0].channel_mask = PAL_CHMAP_CHANNEL_FL;
                volume_data->volume_pair[0].vol = pPvtData->volume_level;
                volume_data->volume_pair[1].channel_mask = PAL_CHMAP_CHANNEL_FR;
                volume_data->volume_pair[1].vol = pPvtData->volume_level;
            }

            errval = pal_stream_set_volume(pPvtData->pcmHandle, volume_data);

            if (errval) {
                AVB_LOGF_ERROR("Pal Stream volume Error (%x)", errval);
            }

            AVB_LOG_INFO("Pal Stream volume is set");
        }
    }

    AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

// read correct amount
static bool readAudio(pvt_data_t *pPvtData, uint8_t *buffer, U32 buflen)
{
    // Set the tx_start flag to TRUE so that we can start counting clock
    // ticks.
    pPvtData->txStarted = TRUE;
    struct pal_buffer in_buffer;
    memset(&in_buffer, 0, sizeof(struct pal_buffer));
    in_buffer.buffer = (uint8_t*)(buffer);
    in_buffer.size = (size_t)(buflen);

    // If the reference clock hasn't started, then simply read from
    // ALSA and copy it to the buffer. No nead to do clock sync.
    if (!pPvtData->refClkStarted) {
        S32 rslt = pal_stream_read(pPvtData->pcmHandle, &in_buffer);

        if (rslt < 0) {
            AVB_LOGF_ERROR("readAudio - pal_stream_read() error: %d, %s",
                           rslt, strerror(rslt));
            return FALSE;
        }

        return TRUE;
    }

    // The clock sync magic starts here.
    uint32_t bufIdx = 0;

    // If the scratch audio buffer hasn't been allocated yet, allocate the audio
    // buffer. Initialize the start index to the end to indicate that there
    // is no data in the buffer.
    if (pPvtData->audioBuffer == NULL) {
        pPvtData->audioBuffer = malloc(buflen);

        if (pPvtData->audioBuffer == NULL) {
            AVB_LOG_ERROR("Failed to allocated memory for audio buffer");
            return FALSE;
        }

        pPvtData->audioBufferSize = buflen;
        // The data is always at the end of the buffer so
        // an empty buffer has audioBufferStartIdx as the same
        // as the audioBufferSize.
        pPvtData->audioBufferStartIdx = pPvtData->audioBufferSize;
    }

    // Copy the bytes left in the scratch audio buffer to the output buffer.
    if (pPvtData->audioBufferSize - pPvtData->audioBufferStartIdx > 0) {
        memcpy(buffer,
               pPvtData->audioBuffer + pPvtData->audioBufferStartIdx,
               pPvtData->audioBufferSize - pPvtData->audioBufferStartIdx);
        bufIdx += pPvtData->audioBufferSize - pPvtData->audioBufferStartIdx;
        pPvtData->audioBufferStartIdx = pPvtData->audioBufferSize;
    }

    // If we've already filled up the output buffer, return it.
    if (bufIdx == buflen) {
        MCR_DEBUG_LOG("extra packet for clock sync.");
        return TRUE;
    }

    in_buffer.buffer = (uint8_t*)(pPvtData->audioBuffer);
    in_buffer.size = (size_t)(pPvtData->audioBufferSize);
    // Now the scratch buffer is empty. Read new data into the scratch buffer.
    S32 rslt = pal_stream_read(pPvtData->pcmHandle,
                               &in_buffer);
    pPvtData->audioBufferStartIdx = 0;

    if (rslt < 0) {
        AVB_LOGF_ERROR("pal_stream_read() error: %d, %s", rslt, strerror(rslt));
        return FALSE;
    }

    // Fill up the rest of the output buffer with data from the scratch buffer.
    memcpy(buffer + bufIdx, pPvtData->audioBuffer, buflen - bufIdx);
    pPvtData->audioBufferStartIdx += buflen - bufIdx;
    bufIdx = buflen;
    // At this point, the scratch buffer can be empty, or partially full,
    // but it can't be completely full.
    MUTEX_LOCK_ERR(pPvtData->mtxActualMinusRefFrames);
    // Subtract the number of frames read from ALSA. (We're counting the
    // frames as they come out of ALSA and then adjusting by the number
    // of added or removed frames rather than when counting when the frames
    // are queued for transmit because frames come out of ALSA at
    // more regular intervals.)
    pPvtData->actualMinusRefFrames += pPvtData->audioBufferSize /
                                      pPvtData->frameSizeBytes;
    // Determine how many frames to remove or repeat.
    // Do this inside the critical section but do the actual remove or
    // repeating of frames outside the critical section.
    int frames_repeated = 0;
    int frames_removed = 0;

    // If the number of frames to recover clock is negative, then we need to
    // repeat this many frames to catch up. But we can only repeat as
    // many frames as we have space in the scratch buffer. We'll make up
    // the remainder the next time this function is called.
    if (pPvtData->framesToRecoverClock < 0) {
        frames_repeated = -1 * pPvtData->framesToRecoverClock;

        if ((int)(pPvtData->audioBufferStartIdx / pPvtData->frameSizeBytes) <
                frames_repeated) {
            frames_repeated = pPvtData->audioBufferStartIdx /
                              pPvtData->frameSizeBytes;
        }
    }
    // If we need to remove frames, we can remove as many frames as we want.
    // We'll just have to read from ALSA again if we need to remove more
    // frames than we already have.
    else if (pPvtData->framesToRecoverClock > 0) {
        frames_removed = pPvtData->framesToRecoverClock;
    }

    // Adjust the count actualMinusRefFrames by the number of frames
    // removed or repeated.
    pPvtData->actualMinusRefFrames += frames_repeated - frames_removed;
    pPvtData->framesToRecoverClock += frames_repeated - frames_removed;
    MUTEX_UNLOCK_ERR(pPvtData->mtxActualMinusRefFrames);

    // Repeat the frames as necessary by copying the last frames in the
    // output buffer to the begining of the scratch buffer
    for (int i = 0; i < frames_repeated; i++) {
        pPvtData->audioBufferStartIdx -= pPvtData->frameSizeBytes;
        memcpy(pPvtData->audioBuffer + pPvtData->audioBufferStartIdx,
               buffer + buflen - pPvtData->frameSizeBytes,
               pPvtData->frameSizeBytes);
    }

    // Remove the frames as necessary
    for (int i = 0; i < frames_removed; i++) {
        // If there are no frames to remove, read some more frames
        if (pPvtData->audioBufferStartIdx >= pPvtData->audioBufferSize) {
            MCR_DEBUG_LOG("extra read for clock sync.");
            in_buffer.buffer = (uint8_t*)(pPvtData->audioBuffer);
            in_buffer.size = (size_t)(pPvtData->audioBufferSize);
            rslt = pal_stream_read(pPvtData->pcmHandle,
                                   &in_buffer);
            pPvtData->audioBufferStartIdx = 0;

            if (rslt < 0) {
                AVB_LOGF_ERROR("pal_stream_read() error: %d, %s", rslt, strerror(rslt));
                // Return TRUE because we have already filled
                // the output buffer.
                return TRUE;
            }

            // Adjust the count because we just read some more frames
            // from ALSA
            MUTEX_LOCK_ERR(pPvtData->mtxActualMinusRefFrames);
            pPvtData->actualMinusRefFrames += pPvtData->audioBufferSize /
                                              pPvtData->frameSizeBytes;
            MUTEX_UNLOCK_ERR(pPvtData->mtxActualMinusRefFrames);
        }

        pPvtData->audioBufferStartIdx += pPvtData->frameSizeBytes;
    }

    return TRUE;
}

// This callback will be called for each AVB transmit interval.
bool openavbIntfPalTxCB(media_q_t *pMediaQ)
{
    AVB_TRACE_ENTRY(AVB_TRACE_INTF_DETAIL);

    if (pMediaQ) {
        media_q_pub_map_uncmp_audio_info_t *pPubMapUncmpAudioInfo =
            pMediaQ->pPubMapInfo;
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
        media_q_item_t *pMediaQItem = NULL;

        if (!pPvtData) {
            AVB_LOG_ERROR("Private interface module data not allocated.");
            return FALSE;
        }

        //put current wall time into tail item used by AAF mapping module
        if ((pPubMapUncmpAudioInfo->sparseMode != TS_SPARSE_MODE_UNSPEC)) {
            pMediaQItem = openavbMediaQTailLock(pMediaQ, TRUE);

            if ((pMediaQItem)
                    && (pPvtData->intervalCounter % pPubMapUncmpAudioInfo->sparseMode == 0)) {
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

            bool rslt = readAudio(pPvtData, (uint8_t*)pMediaQItem->pPubData,
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
            } else {
                openavbAvtpTimeSetToWallTime(pMediaQItem->pAvtpTime);
                openavbMediaQHeadPush(pMediaQ);
                AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
                return TRUE;
            }
        } else {
            AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
            return FALSE;   // Media queue full
        }
    }

    AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
    return FALSE;
}

// a listener. Any listener initialization can be done in this function.
void openavbIntfPalRxInitCB(media_q_t *pMediaQ)
{
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);
    uint8_t ch_map[8] = {PAL_CHMAP_CHANNEL_FL, PAL_CHMAP_CHANNEL_FR, PAL_CHMAP_CHANNEL_C, PAL_CHMAP_CHANNEL_LS,
                         PAL_CHMAP_CHANNEL_RS, PAL_CHMAP_CHANNEL_LFE, PAL_CHMAP_CHANNEL_LB, PAL_CHMAP_CHANNEL_RB
                        };
    int errval = 0;

    if (pMediaQ) {
        media_q_pub_map_uncmp_audio_info_t *pPubMapUncmpAudioInfo =
            pMediaQ->pPubMapInfo;
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;

        if (!pPvtData) {
            AVB_LOG_ERROR("Private interface module data not allocated.");
            return;
        }

        memset(&pPvtData->stream_attr, 0, sizeof(pPvtData->stream_attr));
        pPvtData->stream_attr.type = PAL_STREAM_VOICE_CALL_MUSIC;
        pPvtData->stream_attr.info.voice_rec_info.record_direction =
            INCALL_RECORD_VOICE_UPLINK;
        pPvtData->stream_attr.flags = PAL_STREAM_FLAG_TIMESTAMP;
        pPvtData->stream_attr.direction = PAL_AUDIO_OUTPUT;
        pPvtData->stream_attr.out_media_config.ch_info.channels =
            pPvtData->audioChannels;
        pPvtData->stream_attr.out_media_config.sample_rate = pPvtData->audioRate;
        memcpy(pPvtData->stream_attr.out_media_config.ch_info.ch_map, ch_map,
               8 * sizeof(uint8_t));

        if (pPvtData->isVoiceCall == true) {
            memset(&pPvtData->stream_attr, 0, sizeof(pPvtData->stream_attr));
            pPvtData->stream_attr.type = PAL_STREAM_VOICE_CALL_MUSIC;
            pPvtData->stream_attr.info.voice_rec_info.record_direction =
                INCALL_RECORD_VOICE_UPLINK;
            pPvtData->stream_attr.flags = PAL_STREAM_FLAG_TIMESTAMP;
            pPvtData->stream_attr.direction = PAL_AUDIO_OUTPUT;
            pPvtData->stream_attr.out_media_config.ch_info.channels =
                pPvtData->audioChannels;
            pPvtData->stream_attr.out_media_config.sample_rate = pPvtData->audioRate;
            channel_map(pPvtData->stream_attr.out_media_config.ch_info.ch_map,
                        pPvtData->audioChannels);
            pPvtData->stream_attr.out_media_config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM;
            AVB_LOG_INFO("Voice Call setup.");
        } else {
            memset(&pPvtData->stream_attr, 0, sizeof(pPvtData->stream_attr));
            pPvtData->stream_attr.type = PAL_STREAM_LOW_LATENCY;
            pPvtData->stream_attr.info.opt_stream_info.version = 1;
            pPvtData->stream_attr.info.opt_stream_info.duration_us = -1;
            pPvtData->stream_attr.info.opt_stream_info.has_video = false;
            pPvtData->stream_attr.info.opt_stream_info.is_streaming = true;
            pPvtData->stream_attr.flags = (pal_stream_flags_t)(PAL_STREAM_FLAG_TIMESTAMP |
                                          PAL_STREAM_FLAG_NON_BLOCKING);;
            pPvtData->stream_attr.direction = PAL_AUDIO_OUTPUT;
            pPvtData->stream_attr.out_media_config.ch_info.channels =
                pPvtData->audioChannels;
            pPvtData->stream_attr.out_media_config.sample_rate = pPvtData->audioRate;
            channel_map(pPvtData->stream_attr.out_media_config.ch_info.ch_map,
                        pPvtData->audioChannels);
            pPvtData->stream_attr.out_media_config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM;
            AVB_LOG_INFO("Direct audio setup.");
        }

        memset(&pPvtData->devices, 0, sizeof(pPvtData->devices));
        pPvtData->devices.id = pPvtData->deviceId;
        pPvtData->devices.config.ch_info.channels = pPvtData->audioChannels;
        pPvtData->devices.config.sample_rate = pPvtData->audioRate;
        pPvtData->devices.config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM;
        channel_map(pPvtData->devices.config.ch_info.ch_map, pPvtData->audioChannels);

        switch (pPvtData->audioBitDepth) {
            case AVB_AUDIO_BIT_DEPTH_16BIT:
                pPvtData->stream_attr.out_media_config.bit_width = 16;
                break;

            case AVB_AUDIO_BIT_DEPTH_32BIT:
                pPvtData->stream_attr.out_media_config.bit_width = 32;
                break;

            case AVB_AUDIO_BIT_DEPTH_8BIT:
                pPvtData->stream_attr.out_media_config.bit_width = 8;
                break;

            default:
                pPvtData->stream_attr.out_media_config.bit_width = 16;
                break;
        }

        AVB_LOGF_INFO("%s: pcm_open cardId=%u deviceId=%u ", __func__, pPvtData->cardId,
                      pPvtData->deviceId);
        errval = pal_stream_open(&pPvtData->stream_attr, 1, &pPvtData->devices, 0, NULL,
                                 NULL, NULL,
                                 &pPvtData->pcmHandle);

        if (errval != 0) {
            AVB_LOGF_ERROR("stream_open failed, returned %d ", errval);
            return -1;
        }

        if (!pPvtData->pcmHandle) {
            AVB_LOGF_ERROR("Unable to open errval %d\n",
                           errval);
            return -1;
        } else {
            AVB_LOG_INFO("pcm open success");
        }

        pPvtData->outBufSize = pPubMapUncmpAudioInfo->itemSize; //previously set to 1024
        pPvtData->outBufCount = 4;
        errval = pal_stream_set_buffer_size(pPvtData->pcmHandle,
                                            (size_t*)&pPvtData->inBufSize, pPvtData->inBufCount,
                                            (size_t*)&pPvtData->outBufSize, pPvtData->outBufCount);

        if (errval != 0) {
            AVB_LOGF_ERROR("pal_stream_set_buffer_size failed, returned %d ",
                           errval);
            return -1;
        }

        errval = pal_stream_start(pPvtData->pcmHandle);

        if (errval != 0) {
            AVB_LOGF_ERROR("stream_start failed, returned %d ", errval);
            return -1;
        }

        struct pal_volume_data* volume_data = (struct pal_volume_data *) calloc(1,
                                              sizeof(uint32_t) + (pPvtData->audioChannels * sizeof(struct
                                                      pal_channel_vol_kv)));

        if (volume_data == NULL) {
            AVB_LOG_ERROR("Unable to set volume");
        } else {
            if (pPvtData->audioChannels == 1) {
                volume_data->no_of_volpair = 1;
                volume_data->volume_pair[0].channel_mask = PAL_CHMAP_CHANNEL_FL;
                volume_data->volume_pair[0].vol = pPvtData->volume_level;
            } else {
                volume_data->no_of_volpair = 2;
                volume_data->volume_pair[0].channel_mask = PAL_CHMAP_CHANNEL_FL;
                volume_data->volume_pair[0].vol = pPvtData->volume_level;
                volume_data->volume_pair[1].channel_mask = PAL_CHMAP_CHANNEL_FR;
                volume_data->volume_pair[1].vol = pPvtData->volume_level;
            }

            errval = pal_stream_set_volume(pPvtData->pcmHandle, volume_data);

            if (errval) {
                AVB_LOGF_ERROR("Pal Stream volume Error (%x)", errval);
            }

            AVB_LOG_INFO("Pal Stream volume is set");
        }
    }

    AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

static void consumeAudio(pvt_data_t *pPvtData, void *data, U32 dataLen)
{
    S32 rslt;
    int errval;
    struct pal_buffer out_buffer;
    memset(&out_buffer, 0, sizeof(struct pal_buffer));
    out_buffer.buffer = (uint8_t*)(data);
    out_buffer.size = (size_t)(dataLen);
    rslt = pal_stream_write(pPvtData->pcmHandle, &out_buffer);

    if (rslt) {
        if (errno == EPIPE) {
            pal_stream_stop(pPvtData->pcmHandle);
            pal_stream_close(pPvtData->pcmHandle);
            AVB_LOGF_INFO("%s: pcm_open cardId=%u deviceId=%u", __func__, pPvtData->cardId,
                          pPvtData->deviceId);
            AVB_LOGF_INFO("Period size %d Period count %d", pPvtData->alsaPeriodSize,
                          pPvtData->alsaPeriodCount);
            AVB_LOGF_INFO("Start threshold %u Stop threshold %u",
                          pPvtData->alsaStartThreshold, pPvtData->alsaStopThreshold);
            errval = pal_stream_open(&pPvtData->stream_attr, 1, &pPvtData->devices, 0, NULL,
                                     NULL, NULL,
                                     &pPvtData->pcmHandle);

            if (errval != 0) {
                AVB_LOGF_ERROR("stream_open failed, returned %d ", errval);
                return;
            }

            if (!pPvtData->pcmHandle) {
                AVB_LOGF_ERROR("Unable to open errval %d\n",
                               errval);
                return;
            } else {
                AVB_LOG_INFO("pcm open success");
            }

            pPvtData->outBufSize = pPvtData->audioChannels * (pPvtData->audioBitDepth / 8);
            pPvtData->outBufCount = 4;
            errval = pal_stream_set_buffer_size(pPvtData->pcmHandle,
                                                (size_t*)&pPvtData->inBufSize, pPvtData->inBufCount,
                                                (size_t*)&pPvtData->outBufSize, pPvtData->outBufCount);

            if (errval != 0) {
                AVB_LOGF_ERROR("pal_stream_set_buffer_size failed, returned %d ",
                               errval);
                return;
            }
        }
    }

    // Trigger a clock tick if we've consumed enough frames.
    if (pPvtData->genClockTickOnConsumeAudio &&
            pPvtData->clockSourceTimestampInterval) {
        pPvtData->framesConsumed += dataLen / pPvtData->frameSizeBytes;

        while (pPvtData->framesConsumed >= pPvtData->clockSourceTimestampInterval) {
            if (pPvtData->clockSourceTimestampThrowaway <= 0) {
                U64 walltime;

                if (CLOCK_GETTIME64(OPENAVB_CLOCK_WALLTIME, &walltime)) {
                    refClkSignalTick(walltime, 1,
                                     pPvtData->clockSourceTimestampInterval, FALSE);
                }
            } else {
                pPvtData->clockSourceTimestampThrowaway--;
            }

            pPvtData->framesConsumed -= pPvtData->clockSourceTimestampInterval;
        }
    }
}

// This callback is called when acting as a listener.
bool openavbIntfPalRxCB(media_q_t *pMediaQ)
{
    AVB_TRACE_ENTRY(AVB_TRACE_INTF_DETAIL);

    if (pMediaQ) {
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
        bool moreItems = TRUE;

        while (moreItems) {
            media_q_item_t *pMediaQItem = openavbMediaQTailLock(pMediaQ,
                                          pPvtData->ignoreTimestamp);

            if (pMediaQItem) {
                if (pMediaQItem->dataLen) {
                    consumeAudio(pPvtData,
                                 pMediaQItem->pPubData,
                                 pMediaQItem->dataLen);
                }

                openavbMediaQTailPull(pMediaQ);
            } else {
                moreItems = FALSE;
            }
        }
    }

    AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
    return TRUE;
}

// This callback will be called when the interface needs to be closed. All shutdown should
// occur in this function.
void openavbIntfPalEndCB(media_q_t *pMediaQ)
{
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);

    if (pMediaQ) {
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;

        if (!pPvtData) {
            AVB_LOG_ERROR("Private interface module data not allocated.");
            return;
        }

        if (pPvtData->pcmHandle) {
            pal_stream_stop(pPvtData->pcmHandle);
            pal_stream_close(pPvtData->pcmHandle);

            if (pal_init_status == 1) {
                pal_deinit();
            }

            pal_init_status--;
            pPvtData->pcmHandle = NULL;
        }

        if (pPvtData->ch2Fd > 0) {
            close(pPvtData->ch2Fd);
        }
    }

    AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

void openavbIntfPalGenEndCB(media_q_t *pMediaQ)
{
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);
    (void) pMediaQ;
    AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

// Main initialization entry point into the interface module
extern DLL_EXPORT bool openavbIntfPalInitialize(media_q_t *pMediaQ,
        openavb_intf_cb_t *pIntfCB)
{
    AVB_TRACE_ENTRY(AVB_TRACE_INTF);

    if (pMediaQ) {
        // Memory freed by the media queue when the media queue is destroyed.
        pMediaQ->pPvtIntfInfo = calloc(1, sizeof(pvt_data_t));

        if (!pMediaQ->pPvtIntfInfo) {
            AVB_LOG_ERROR("Unable to allocate memory for AVTP interface module.");
            return FALSE;
        }

        int32_t err;

        if (pal_init_status == 0) {
            err = pal_init();

            if (err) {
                AVB_LOG_ERROR("pal_init failed");
            }
        }

        pal_init_status++;
        pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
        pIntfCB->intf_cfg_cb = openavbIntfPalCfgCB;
        pIntfCB->intf_gen_init_cb = openavbIntfPalGenInitCB;
        pIntfCB->intf_tx_init_cb = openavbIntfPalTxInitCB;
        pIntfCB->intf_tx_cb = openavbIntfPalTxCB;
        pIntfCB->intf_rx_init_cb = openavbIntfPalRxInitCB;
        pIntfCB->intf_rx_cb = openavbIntfPalRxCB;
        pIntfCB->intf_end_cb = openavbIntfPalEndCB;
        pIntfCB->intf_gen_end_cb = openavbIntfPalGenEndCB;
        pPvtData->ignoreTimestamp = FALSE;
        pPvtData->pDeviceName = strdup(PCM_DEVICE_NAME_DEFAULT);
        pPvtData->allowResampling = TRUE;
        pPvtData->intervalCounter = 0;
        pPvtData->startThresholdPeriods =
            2;    // Default to 2 periods of frames as the start threshold
        pPvtData->periodTimeUsec = 100000;
        pPvtData->syncToClockTickOnTxAudio = 0;
        pPvtData->clockSourceTimestampThrowaway = 10;
        pPvtData->clockRecoveryAdjustmentRange = 500;
        pPvtData->alsaPeriodCount = PERIOD_COUNT;
        pPvtData->alsaPeriodSize = PERIOD_SIZE;
        pPvtData->alsaStartThreshold = 0;
        pPvtData->alsaStopThreshold = 0;
        MUTEX_ATTR_HANDLE(mta);
        MUTEX_ATTR_INIT(mta);
        MUTEX_ATTR_SET_TYPE(mta, MUTEX_ATTR_TYPE_DEFAULT);
        MUTEX_ATTR_SET_NAME(mta, "mtxActualMinusRefFrames");
        MUTEX_CREATE_ERR();
        MUTEX_CREATE(pPvtData->mtxActualMinusRefFrames, mta);
        MUTEX_LOG_ERR("Error creating mutex");
        pPvtData->ch2Fd = 0;
        pPvtData->volume_level = 1.0f;
    }

    AVB_TRACE_EXIT(AVB_TRACE_INTF);
    return TRUE;
}

