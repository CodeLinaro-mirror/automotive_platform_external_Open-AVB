/*
* Copyright (c) 2016, The Linux Foundation. All rights reserved.
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
#include <fcntl.h>

#include "openavb_types_pub.h"
#include "openavb_trace_pub.h"
#include "openavb_mediaq_pub.h"
#include "openavb_intf_pub.h"
#include "openavb_map_h264_pub.h"
#include "openavb_filewriter.h"

#define	AVB_LOG_COMPONENT	"H264 File Interface"
#include "openavb_log_pub.h"

#define MAX_READ_SIZE 128
#define MAX_BUFFER_LEN 1024
#define MAX_PAYLOAD_SIZE 1412

typedef struct pvt_data_t
{
	char *file_name;
	bool ignoreTimestamp;
	U8 *fp;
	U32 seq;
        bool asyncTx
	bool asyncRx;
	bool blockingRx;
	U32 read_size;
	U32 loc;
	int fd;
	void* filewriter;
	struct stat statbuf;
	bool get_avtp_timestamp;        /*<! this flag indicates whether
                                        an avtp timestamp should be taken */
	U32 frame_timestamp;            /*<! this is a timestamp of a video frame */
	bool repeatData;
} pvt_data_t;
typedef struct nalu_header {
        U8 type:    5;
        U8 nri:     2;
        U8 f:       1;
} __attribute__((packed)) nalu_header_t;

typedef struct fu_indicator {
        U8 type:    5;
        U8 nri:     2;
        U8 f:       1;
} __attribute__((packed)) fu_indicator_t;

typedef struct fu_header {
        U8 type:    5;
        U8 r:       1;
        U8 e:       1;
        U8 s:       1;
} __attribute__((packed)) fu_header_t;

#define RTP_PAYLOAD_MAX_SIZE         1400
#define SEND_BUF_SIZE                1500
#define NAL_BUF_SIZE                 1500 * 500

U8 nal_buf[NAL_BUF_SIZE];

typedef struct payload_tx_queue_t {
        U8 item[SEND_BUF_SIZE];
        U32 len;
        bool lastPacket;
        bool empty;
} payload_tx_q;

#define MAX_QUEUE_ITEM_NUM 1000
#define TX_SLEEP_MS  10
static payload_tx_q pq[MAX_QUEUE_ITEM_NUM];
static U32 push_index = 0;
static U32 pop_index = 0;
static U32 total_num = 0;
static bool tx_bRunning = false;

static pthread_t asyncTxThread;
static media_q_t *pAsyncTxMediaQ;
static pthread_mutex_t asyncTxMutex = PTHREAD_MUTEX_INITIALIZER;

static int fetch_nal_from_file(FILE *fp, U8 *buf, int *len)
{
       char tmbuf2[1];
       int flag = 0;
       char tmpbuf[4];
       int ret;

        *len = 0;

        do{
               if (feof(fp)) {
                       AVB_LOG_INFO("feof reached, closing the file.");
                       return -1;
                }
                ret = fread(tmpbuf2, 1, 1, fp);
                if (0 == ret){
                        AVB_LOG_INFO("EOF reached, closing the file.");
                       return -1; 
                   }

                   if (!flag && tmpbuf2[0] != 0x0) {
                             buf[*len] = tmpbuf2[0];
                             (*len)++;
                    } else if (!flag && tmpbuf2[0] == 0x0) {
                             flag = 1;
                             tmpbuf[0] = tmpbuf2[0];
                    } else if (flag) {
                             switch (flag) {
                             case 1:
                                     if(tmpbuf2[0] == 0x0) {
                                           flag++;
                                           tmpbuf[1] = tmpbuf2[0];
                                      } else { 
                                             flag = 0;
                                             buf[*len] = tmpbuf[0];
                                             (*len)++;
                                             buf[*len] = tmpbuf2[0];
                                             (*len)++;
                                       }
                                       break;

                              case 2:
                                     if(tmpbuf2[0] == 0x0) {
                                           flag++;
                                           tmpbuf[2] = tmpbuf2[0];
                                      } else if (tmpbuf2[0] == 0x1) {
                                               flag = 0;
                                               return *len;
                                      } else {
                                             flag = 0;
                                             buf[*len] = tmpbuf[0];
                                             (*len)++;
                                             buf[*len] = tmpbuf[1];
                                             (*len)++;
                                             buf[*len] = tmpbuf2[0];
                                             (*len)++;
                                       }
                                       break;
                              case 3:
                                     if(tmpbuf2[0] == 0x1) {
                                           flag = 0;
                                           return *len;
                                      } else {
                                           flag = 0; 
                                           break;
                                      }
                              }
                          }
                   } while (1);
                 
                   return *len;
}

static int h264nal_send(int framerate, U8 *pstStream, int nalu_len)
{

             U8 *nalu_buf;
             nalu_buf = pstStream;
             nalu_header_t *nalu_hdr;
             fu_indicator_t *fu_ind;
             fu_header_t *fu_hdr;

             int fu_pack_num;
             int last_fu_pack_size;
             int fu_seq;

             if (nalu_len < 1) {
                     return -1;
             }
           
             if(nalu_len <= RTP_PAYLOAD_MAX_SIZE) {
                    /*Add pscket into queue*/
                    pthread_mutex_lock(&asyncTxMutex);
                    nalu_hdr = (nalu_header_t *)&(pq[push_index].item[0]);
                    nalu_hdr->f = (nalu_buf[0] & 0x80) >> 7;
                    nalu_hdr->nri = (nalu_buf[0] & 0x60) >> 5;
                    nalu_hdr->type = (nalu_buf[0] & 0x1f);
                    memcpy(pq[push_index].item + 1, nalu_buf + 1, nalu_len - 1);
                    pq[push_index].empty = FALSE;
                    pq[push_index].lastPacket = TRUE;
                    pq[push_index].len = nalu_len;
                    push_index++;
                    if( push_index == MAX_QUEUE_ITEM_NUM)
                             push_index = 0;
                    total_num++;
                    pthread_mutex_lock(&asyncTxMutex);
                } else {
                        fu_pack_num = nalu_len % RTP_PAYLOAD_MAX_SIZE ? (nalu_len / RTP_PAYLOAD_MAX_SIZE + 1) : nalu_len / RTP_PAYLOAD_MAX_SIZE;
                        last_fu_pack_size = nalu_len % RTP_PAYLOAD_MAX_SIZE ? nalu_len % RTP_PAYLOAD_MAX_SIZE :  RTP_PAYLOAD_MAX_SIZE;
                        fu_seq = 0;

                        for(fu_seq = 0; fu_seq < fu_pack_num; fu_seq++) {
                              if(fu_seq == 0) {
                                     memset(pq[push_index].item, 0, SEND_BUF_SIZE);
                                     pthread_mutex_lock(&asyncTxMutex);
                                     /*Add packet into queue */
                                     fu_ind = (fu_indicator_t *)&(pq[push_index].item[0]);
                                     fu_ind->f = (nalu_buf[0] & 0x80) >> 7;
                                     fu_ind->nri = (nalu_buf[0] & 0x60) >> 5;
                                     fu_ind->type = 28;
 
                                     fu_hdr = (nalu_header_t *)&(pq[push_index].item[1]);
                                     fu_hdr->s = 1;
                                     fu_hdr->e = 0;
                                     fu_hdr->r = 0;
                                     fu_hdr->type = nalu_buf[0] & 0x1f;
                                     memcpy(pq[push_index].item + 2, nalu_buf + 1, RTP_PAYLOAD_MAX_SIZE - 1);
                                     pq[push_index].empty = FALSE;
                                     pq[push_index].lastPacket = FALSE;
                                     pq[push_index].len = RTP_PAYLOAD_MAX_SIZE + 1;
                                     push_index++;
                                     if( push_index == MAX_QUEUE_ITEM_NUM)
                                         push_index = 0;
                                    total_num++;
                                    pthread_mutex_lock(&asyncTxMutex);
                     } else if (fu_seq < fu_pack_num -1) {
                              memset(pq[push_index].item, 0, SEND_BUF_SIZE);
                              pthread_mutex_lock(&asyncTxMutex);
                              /*Add packet into queue */
                              fu_ind = (fu_indicator_t *)&(pq[push_index].item[0]);
                              fu_ind->f = (nalu_buf[0] & 0x80) >> 7;
                              fu_ind->nri = (nalu_buf[0] & 0x60) >> 5;
                              fu_ind->type = 28;

                              fu_hdr = (nalu_header_t *)&(pq[push_index].item[1]);
                              fu_hdr->s = 0;
                              fu_hdr->e = 0;
                              fu_hdr->r = 0;
                              fu_hdr->type = nalu_buf[0] & 0x1f;
                               
                              memcpy(pq[push_index].item + 2, nalu_buf + RTP_PAYLOAD_MAX_SIZE * fu_seq, RTP_PAYLOAD_MAX_SIZE);
                              pq[push_index].empty = FALSE;
                              pq[push_index].lastPacket = FALSE;
                              pq[push_index].len = RTP_PAYLOAD_MAX_SIZE + 1;
                              push_index++;
                              if( push_index == MAX_QUEUE_ITEM_NUM)
                                         push_index = 0;
                              total_num++;
                              pthread_mutex_lock(&asyncTxMutex);
                     } else {
                              memset(pq[push_index].item, 0, SEND_BUF_SIZE);
                              pthread_mutex_lock(&asyncTxMutex);
                              /*Add packet into queue */
                              fu_ind = (fu_indicator_t *)&(pq[push_index].item[0]);
                              fu_ind->f = (nalu_buf[0] & 0x80) >> 7;
                              fu_ind->nri = (nalu_buf[0] & 0x60) >> 5;
                              fu_ind->type = 28;

                              fu_hdr = (nalu_header_t *)&(pq[push_index].item[1]);
                              fu_hdr->s = 0;
                              fu_hdr->e = 1;
                              fu_hdr->r = 0;
                              fu_hdr->type = nalu_buf[0] & 0x1f;

                              memcpy(pq[push_index].item + 2, nalu_buf + RTP_PAYLOAD_MAX_SIZE * fu_seq, last_fu_pack_size);
                              pq[push_index].empty = FALSE;
                              pq[push_index].lastPacket = TRUE;
                              pq[push_index].len = last_fu_pack_size + 2;
                              push_index++;
                              if( push_index == MAX_QUEUE_ITEM_NUM)
                                         push_index = 0;
                              total_num++;
                              pthread_mutex_lock(&asyncTxMutex);
                             }
                       }
              }

             return 0;
}

static void* openavbIntfH264FileRtpThread(void* pv)
{

       pvt_data_t *pPvtData;
       FILE *fp = NULL;
       int len = 0;
       int ret = 0;

       if(!pAsyncTxMediaQ) { 
              AVB_LOG_ERROR("No async mediaQ");
              return;
        }

        pPvtData = pAsyncTxMediaQ->pPvtIntfInfo;
        if(!pPvtData) {
              AVB_LOG_ERROR("No async RX private data.");
              return;
        }   

        sleep(1);

        tx_bRunning = true;
        if((fp = fopen(pPvtData->file_name, "r")) == NULL)
                return;

        while(tx_bRunning) {
                if(fetch_nal_from_file(fp, nal_buf, &len) != -1) {
                         ret = h264nal_send(25, nal_buf, len);
                         if(ret != 1)
                                usleep(TX_SLEEP_MS * 1000);
                         } else if (pPvtData->repeatData){
                                  if(fp) {
                                         close(fp);
                                         fp = NULL;
                                   }

                                   if((fp = fopen(pPvtData->file_name, "r")) == NULL)
                                            return;
                          } else {
                                  break;
                          }
                  }

                  return;
}
                              
// Each configuration name value pair for this mapping will result in this callback being called.
void openavbIntfH264RtpFileCfgCB(media_q_t *pMediaQ, const char *name, const char *value)
{
	if (!pMediaQ) {
		AVB_LOG_DEBUG("H264Rtp-file cfgCB: no mediaQ!");
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
        else if (strcmp(name, "intf_nv_blocking_tx") == 0) {
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
	else if (strcmp(name, "intf_nv_repeat_data") == 0) {
		tmp = strtol(value, &pEnd, 10);
		if ((tmp == 0) || (tmp == 1)) {
			pPvtData->repeatData = (tmp == 1);
		}
		else {
			AVB_LOG_ERROR("Invalid intf_nv_repeat_data value : setting to default(no repetition).");
			pPvtData->repeatData = FALSE;
		}
	}
}

void openavbIntfH264RtpFileGenInitCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);
	if (!pMediaQ) {
		AVB_LOG_DEBUG("H264Rtp-file initCB: no mediaQ!");
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return;
	}
	AVB_TRACE_EXIT(AVB_TRACE_INTF);

}

// a talker. Any talker initialization can be done in this function.
void openavbIntfH264RtpFileTxInitCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);

	if (!pMediaQ) {
		AVB_LOG_DEBUG("H264Rtp-gst txinit: no mediaQ!");
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
		AVB_LOG_DEBUG("H264-file txinit: no file not found");
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return;
	}
	fstat(pPvtData->fd, &pPvtData->statbuf);
	pPvtData->fp = (U8*)mmap(NULL, pPvtData->statbuf.st_size, PROT_READ, MAP_FILE|MAP_PRIVATE, pPvtData->fd, (off_t) 0);
	if (pPvtData->fp == (void*)-1) {
		AVB_LOG_DEBUG("h264-file txinit: could not mmap file");
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return;

	}
	pPvtData->loc = 0;
	pPvtData->get_avtp_timestamp = TRUE;
	pPvtData->read_size = MAX_READ_SIZE;
        if(pPvtData->asyncTx) {
                if(pthread_mutex_init(&asyncTxMutex, 0))
                        AVB_LOG_ERROR("Mutex init failed");
                 pAsyncTxMediaQ = pMeiaQ;
                 pthread_attr_t attr;
                 struct sched_param param;
                 pthread_attr_init(&attr);
                 pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
                 param.sched_priority = 0;
                 pthread_attr_setschedparam(&attr, &param);
                 pthread_create(&asyncTxThread, &attr, openavbIntfH264FileRtpThread, NULL);
        }
 
	AVB_TRACE_EXIT(AVB_TRACE_INTF);

	return;
}

// This callback will be called for each AVB transmit interval. Commonly this will be
// 4000 or 8000 times  per second.
bool openavbIntfH264RtpFileTxCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF_DETAIL);

	if (!pMediaQ) {
		AVB_LOG_DEBUG("No MediaQ in H264RtpGstTxCB");
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return FALSE;
	}

	pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
	if (!pPvtData) {
		AVB_LOG_ERROR("Private interface module data not allocated.");
		return FALSE;
	}

	if (pPvtData->fd == -1) {
		return FALSE;
	}

	U32 read_size = 0;
	static U32 buf_size ;
	if(pPvtData->asyncTx) {
               pthread_mutex_lock(&asyncTxMutex);
               if(total_num > 0 && !pq[pop_index].empty) {
                       media_q_item_t *pMediaQItem = openavbMediaQHeadLock(pMediaQ);
                       if(pMediaQItem) {
                         memcpy(pMediaQItem->pPubData, pq[pop_index].item, pq[pop_index].len);
                         pMediaQItem->dataLen = pq[pop_index].len;
                         ((media_q_item_map_h264_pub_data_t *)pMediaQItem->pPubMapData)->lastPacket = pq[pop_index].lastPacket;
                         pq[pop_index].empty = true;
                         pq[pop_index].lastPacket = false;
                         memset(pq[pop_index].item, 0, SEND_BUF_SIZE);
                         pq[pop_index].len = 0;
                         pop_index++;
                         if( pop_index == MAX_QUEUE_ITEM_NUM)
                                         pop_index = 0;
                              total_num--;

                              openavbAvtpTimeSetToWallTime(pMediaQItem->pAvtpTime);
                              openavbMediaQHeadPush(pMediaQ);
                              pthread_mutex_unlock(&asyncTxMutex);
                              AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
                              return TRUE;
                           } else {
                              pthread_mutex_unlock(&asyncTxMutex);
                           } 
		} else {
			pthread_mutex_unlock(&asyncTxMutex);
                        AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
			return FALSE;
		}
        } else {
                media_q_item_t *pMediaQItem = openavbMediaQHeadLock(pMedia);
                if(pMediaQItem) {
                        if(pPvtData->loc + pPvtData->read_size > pPvtData->statbuf.st_size) {
                              read_size = pPvtData->statbuf.st_size - pPvtData->loc;
                        } else {
                               read_size = pPvtData->read_size;
                        }
		        if(read_size > 0) {
                                memcpy(pMediaQItem->pPubMapData, &pPvtData->fp[pPvtData->loc], read_size);
			} else {
			       AVB_LOGF_ERROR("Invalid read size %d. read pos = %d, file size = %d",
                                               read_size, pPvtData->loc, pPvtData->statbuf.st_size);
                               return FALSE;
			}
		                pMediaQItem->dataLen = read_size;
                                pPvtData->loc += read_size;
                                buf_size += read_size;

                                // Check if we've  reached the end of the file
                                if(pPvtData->loc >= pPvtData->statbuf.st_size) {
                                       if (pPvtData->repeatData) {
                                               pPvtData->loc = 0;
                                       } else {
                                              AVB_LOG_INFO("EOF reached, closing the file.");
                                              close(pPvtData->fd);
                                              pPvtData->fd = -1;
                                        }
                          }
                         
                          if (buf_size < MAX_BUFFER_LEN) {
                                 ((media_q_item_map_h264_pub_data_t *)pMediaQItem->pPubMapData)->lastPacket = FALSE;
                                 if (read_size > 0 && read_size < MAX_READ_SIZE) {
                                         ((media_q_item_map_h264_pub_data_t *)pMediaQItem->pPubMapData)->lastPacket = TRUE;
                        }
                }
                else {                               
				((media_q_item_map_h264_pub_data_t *)pMediaQItem->pPubMapData)->lastPacket = TRUE;
                                buf_size = 0;
			}
                                openavbAvtpTimeSetToWallTime(pMediaQItem->pAvtpTime);
                                openavbMediaQHeadPush(pMediaQ);
                                AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
                                return TRUE;
                        }
	}

	AVB_TRACE_EXIT(AVB_TRACE_INTF_DETAIL);
	return TRUE;
}


// A call to this callback indicates that this interface module will be
// a listener. Any listener initialization can be done in this function.
void openavbIntfH264RtpFileRxInitCB(media_q_t *pMediaQ)
{
	AVB_LOG_DEBUG("Rx Init callback.");
	if (!pMediaQ) {
		AVB_LOG_DEBUG("No MediaQ in H264RtpFileRxInitCB");
		return;
	}

	pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
	if (!pPvtData) {
		AVB_LOG_ERROR("Private interface module data not allocated.");
		return;
	}

	if (!pPvtData->file_name) {
		AVB_LOG_ERROR("Output file name not provided in ini");
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return;
	}

	pPvtData->filewriter = filewriter_init(MAX_PAYLOAD_SIZE, pPvtData->file_name);
	if (!pPvtData->filewriter) {
		AVB_LOGF_ERROR("Unable to create filewriter for file: %s", pPvtData->file_name);
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return;
	}

}

// This callback is called when acting as a listener.
bool openavbIntfH264RtpFileRxCB(media_q_t *pMediaQ)
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
		filewriter_write(pPvtData->filewriter, pMediaQItem->pPubData, pMediaQItem->dataLen);
		openavbMediaQTailPull(pMediaQ);
	}
	return TRUE;
}

// This callback will be called when the interface needs to be closed. All shutdown should
// occur in this function.
void openavbIntfH264RtpFileEndCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);
	//bAsyncRXStreaming = FALSE;
	pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;
	if (!pPvtData) {
		AVB_LOG_ERROR("Private interface module data not allocated.");
		return;
	}

	if (pPvtData->fd) {
		close(pPvtData->fd);
		pPvtData->fd = -1;
	}

	if (pPvtData->filewriter) {
		filewriter_close(pPvtData->filewriter);
		pPvtData->filewriter = NULL;
	}
        if (pPvtData->asyncTx) {
                tx_bRunning = false;
                pthread_mutex_destroy(&asyncTxMutex);
                pthread_join(asyncTxThread, NULL);
        }

	AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

void openavbIntfH264RtpFileGenEndCB(media_q_t *pMediaQ)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);
	AVB_TRACE_EXIT(AVB_TRACE_INTF);
}

// Main initialization entry point into the interface module
extern DLL_EXPORT bool openavbIntfH264RtpFileInitialize(media_q_t *pMediaQ, openavb_intf_cb_t *pIntfCB)
{
	AVB_TRACE_ENTRY(AVB_TRACE_INTF);

	if (!pMediaQ) {
		AVB_LOG_DEBUG("H264Rtp-gst GstInitialize: no mediaQ!");
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return TRUE;
	}

	// Memory freed by the media queue when the media queue is destroyed.
	pMediaQ->pPvtIntfInfo = calloc(1, sizeof(pvt_data_t));
	if (!pMediaQ->pPvtIntfInfo) {
		AVB_LOG_DEBUG("H264Rtp-file FileInitialize: Can't allocate pMediaQ->pPvtIntfInfo!");
		AVB_TRACE_EXIT(AVB_TRACE_INTF);
		return FALSE;
	}

	pvt_data_t *pPvtData = pMediaQ->pPvtIntfInfo;

	pIntfCB->intf_cfg_cb = openavbIntfH264RtpFileCfgCB;
	pIntfCB->intf_gen_init_cb = openavbIntfH264RtpFileGenInitCB;
	pIntfCB->intf_tx_init_cb =	openavbIntfH264RtpFileTxInitCB;// NULL;
	pIntfCB->intf_tx_cb =openavbIntfH264RtpFileTxCB;//NULL
	pIntfCB->intf_rx_init_cb = openavbIntfH264RtpFileRxInitCB;
	pIntfCB->intf_rx_cb = openavbIntfH264RtpFileRxCB;
	pIntfCB->intf_end_cb = openavbIntfH264RtpFileEndCB;
	pIntfCB->intf_gen_end_cb = openavbIntfH264RtpFileGenEndCB;

	pPvtData->ignoreTimestamp = FALSE;
	pPvtData->repeatData = FALSE;

	AVB_TRACE_EXIT(AVB_TRACE_INTF);
	return TRUE;
}
