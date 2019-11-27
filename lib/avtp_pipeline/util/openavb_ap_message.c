/*************************************************************************************************************
Copyright (c) 2019, The Linux Foundation. All rights reserved.

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

#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <netpacket/packet.h>
#include <linux/ethtool.h>
#include <net/if.h>
#include <linux/sockios.h>
#include <unistd.h>

#include "openavb_ap_message.h"

#define  AVB_LOG_COMPONENT	"AVNU Testmode"
#include "openavb_pub.h"
#include "openavb_log.h"

// Octet based data 2 buffer macros
#define OCT_D2BMEMCP(d, s) memcpy(d, s, sizeof(s)); d += sizeof(s);
#define OCT_D2BBUFCP(d, s, len) memcpy(d, s, len); d += len;
#define OCT_D2BHTONB(d, s) *(U8 *)(d) = s; d += sizeof(s);
#define OCT_D2BHTONS(d, s) *(U16 *)(d) = AP_htons(s); d += sizeof(s);
#define OCT_D2BHTONL(d, s) *(U32 *)(d) = AP_htonl(s); d += sizeof(s);

// Bit based data 2 buffer macros
#define BIT_D2BHTONB(d, s, shf) *(uint8_t *)(d) |= s << shf;
#define BIT_D2BHTONS(d, s, shf) *(uint16_t *)(d) |= AP_htons((uint16_t)(s << shf));
#define BIT_D2BHTONL(d, s, shf) *(uint32_t *)(d) |= AP_htonl((uint32_t)(s << shf));

struct sockaddr_ll localsockaddr;
testmodeCfg_t testmode_cfg;
extern openavb_endpoint_cfg_t x_cfg;

/*!< AVnu Automotive profile test status msg Multicast value */
unsigned char AVNU_TEST_STATUS_MULTICAST[] = {0x01, 0x1B, 0xC5, 0x0A, 0xC0, 0x00};

/**
 * @brief  Converts a 64 bit word from network to host order
 * @param  x Value to be converted
 * @return Converted value
 */

uint16_t AP_htons( uint16_t s ) {
	return htons( s );
}
uint32_t AP_htonl( uint32_t l ) {
	return htonl( l );
}
uint16_t AP_ntohs( uint16_t s ) {
	return ntohs( s );
}
uint32_t AP_ntohl( uint32_t l ) {
	return ntohl( l );
}
uint64_t AP_htonll(uint64_t x) {
	return ( (htonl(1) == 1) ? x : ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32) );
}
uint64_t AP_ntohll(uint64_t x) {
	return( (ntohl(1) == 1) ? x : ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32) );
}

int init_testmode(void) {
	struct ifreq reqdev;
	int ifindex, err;

	memset(&testmode_cfg,0,sizeof(testmodeCfg_t));
	memset(&reqdev, 0, sizeof(reqdev));
	memcpy(reqdev.ifr_name, x_cfg.ifname, IFNAMESIZE);

	testmode_cfg.socket_d = socket(PF_PACKET, SOCK_DGRAM, 0);
	if (testmode_cfg.socket_d == -1) {
		AVB_LOGF_ERROR("failed to open event socket: %s", strerror(errno));
		return -1;
	}

	err = ioctl(testmode_cfg.socket_d, SIOCGIFINDEX, &reqdev);
	if (err == -1) {
		AVB_LOGF_ERROR("Failed to get interface index: %s", strerror(errno));
		return -1;
	}

	ifindex = reqdev.ifr_ifindex;
	memset(&localsockaddr,0,sizeof(localsockaddr));
	localsockaddr.sll_family = AF_PACKET;
	localsockaddr.sll_protocol = AP_htons(ETHER_TYPE_AVTP);
	localsockaddr.sll_ifindex = ifindex;
	localsockaddr.sll_halen = ETH_ADDR_LEN;
	memcpy((void*)(localsockaddr.sll_addr),(void*)AVNU_TEST_STATUS_MULTICAST,ETH_ADDR_LEN);

	return 0;
}

void tx_testmode_message(avb_role_t eprole, U16 descriptorIndex, stream_stat_t *tlStreamStats) {
	static uint16_t sequenceId = 0;

	uint8_t buf_t[256];
	uint8_t *buf_ptr = buf_t;
	uint16_t tmp16;
	uint32_t tmp32;
	uint64_t tmp64;

	uint16_t messageLength;

	memset(buf_t, 0, 256);

	messageLength = AP_TEST_STATUS_LENGTH;

	BIT_D2BHTONB(buf_ptr + AP_TEST_STATUS_AVTP_SUBTYPE(AP_TEST_STATUS_OFFSET), 0xfb, 0);
	BIT_D2BHTONB(buf_ptr + AP_TEST_STATUS_AVTP_VERSION_CONTROL(AP_TEST_STATUS_OFFSET), 0x1, 0);
	BIT_D2BHTONS(buf_ptr + AP_TEST_STATUS_AVTP_STATUS_LENGTH(AP_TEST_STATUS_OFFSET), 148, 0);

	memcpy(buf_ptr + AP_TEST_STATUS_TARGET_ENTITY_ID(AP_TEST_STATUS_OFFSET),&x_cfg.ifmac,ETH_ADDR_LEN);

	tmp16 = AP_htons(sequenceId++);
	memcpy(buf_ptr + AP_TEST_STATUS_SEQUENCE_ID(AP_TEST_STATUS_OFFSET), &tmp16, sizeof(tmp16));

	BIT_D2BHTONS(buf_ptr + AP_TEST_STATUS_COMMAND_TYPE(AP_TEST_STATUS_OFFSET), 1, 15);
	BIT_D2BHTONS(buf_ptr + AP_TEST_STATUS_COMMAND_TYPE(AP_TEST_STATUS_OFFSET), 0x29, 0);

	if (eprole == AVB_ROLE_TALKER) {
		tmp16 = AP_htons(TESTMODE_DESCRIPTOR_STREAM_OUTPUT);
		memcpy(buf_ptr + AP_TEST_STATUS_DESCRIPTOR_TYPE(AP_TEST_STATUS_OFFSET), &tmp16, sizeof(tmp16));
	}
	else if (eprole == AVB_ROLE_LISTENER) {
		tmp16 = AP_htons(TESTMODE_DESCRIPTOR_STREAM_INPUT);
		memcpy(buf_ptr + AP_TEST_STATUS_DESCRIPTOR_TYPE(AP_TEST_STATUS_OFFSET), &tmp16, sizeof(tmp16));
	}

	tmp16 = AP_htons(descriptorIndex);
	memcpy(buf_ptr + AP_TEST_STATUS_DESCRIPTOR_INDEX(AP_TEST_STATUS_OFFSET), &tmp16, sizeof(tmp16));

	//To Do: make flags for counters
	tmp32 = AP_htonl(0x00001FFF);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTERS_VALID(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	tmp32 = AP_htonl(tlStreamStats->MEDIA_LOCKED);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_MEDIA_LOCKED(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	tmp32 = AP_htonl(tlStreamStats->MEDIA_UNLOCKED);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_MEDIA_UNLOCKED(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	tmp32 = AP_htonl(tlStreamStats->STREAM_RESET);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_STREAM_RESET(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	tmp32 = AP_htonl(tlStreamStats->SEQ_NUM_MISMATCH);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_SEQ_NUM_MISMATCH(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	tmp32 = AP_htonl(tlStreamStats->MEDIA_RESET);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_MEDIA_RESET(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	tmp32 = AP_htonl(tlStreamStats->TIMESTAMP_UNCERTAIN);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_TIMESTAMP_UNCERTAIN(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	tmp32 = AP_htonl(tlStreamStats->TIMESTAMP_VALID);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_TIMESTAMP_VALID(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	tmp32 = AP_htonl(tlStreamStats->TIMESTAMP_NOT_VALID);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_TIMESTAMP_NOT_VALID(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	tmp32 = AP_htonl(tlStreamStats->UNSUPPORTED_FORMAT);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_UNSUPPORTED_FORMAT(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	tmp32 = AP_htonl(tlStreamStats->LATE_TIMESTAMP);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_LATE_TIMESTAMP(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	tmp32 = AP_htonl(tlStreamStats->EARLY_TIMESTAMP);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_EARLY_TIMESTAMP(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	tmp32 = AP_htonl(tlStreamStats->FRAMES_RX);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_AVTP_FRAMES_RX(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	tmp32 = AP_htonl(tlStreamStats->FRAMES_TX);
	memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_AVTP_FRAMES_TX(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	struct timespec nowTS;
	CLOCK_GETTIME(OPENAVB_CLOCK_WALLTIME, &nowTS);
	tmp64 = AP_htonll((nowTS.tv_sec*NANOSECONDS_PER_SECOND) + nowTS.tv_nsec);

	memcpy(buf_ptr + AP_TEST_STATUS_MESSAGE_TIMESTAMP(AP_TEST_STATUS_OFFSET), &tmp64, sizeof(tmp64));

	BIT_D2BHTONB(buf_ptr + AP_TEST_STATUS_STATION_STATE(AP_TEST_STATUS_OFFSET), TESTMODE_STATE_AVB_MEDIA_READY, 0);

	if (sendto(testmode_cfg.socket_d, buf_t,messageLength, 0, (struct sockaddr *)&localsockaddr, sizeof(localsockaddr)) < 0) {
		AVB_LOGF_ERROR("Failed to send avnu test message, error %s",strerror(errno));
	}

	AVB_LOG_INFO("AP message_sent with TESTMODE_STATE_AVB_MEDIA_READY");

	return;
}

