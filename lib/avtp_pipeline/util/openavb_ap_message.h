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

#ifndef OPENAVB_AP_MESSAGE_H
#define OPENAVB_AP_MESSAGE_H

#include "openavb_types_base_pub.h"

/** @file **/

#define ETHER_TYPE_AVTP 0x22f0
/// Maximum size of interface name
#define IFNAMESIZE 16
#define ETH_ADDR_LEN   6 /* Size of Ethernet address */

typedef struct {
	uint32_t STREAM_INTERRUPTED;
	uint32_t SEQ_NUM_MISMATCH;
	uint32_t FRAMES_RX;
	uint32_t FRAMES_TX;
	uint32_t MEDIA_LOCKED;
	uint32_t MEDIA_UNLOCKED;
	uint32_t MEDIA_RESET;
	uint32_t UNSUPPORTED_FORMAT;
	uint32_t TIMESTAMP_UNCERTAIN;
	uint32_t TIMESTAMP_VALID;
	uint32_t TIMESTAMP_NOT_VALID;
	uint32_t LATE_TIMESTAMP;
	uint32_t EARLY_TIMESTAMP;
	uint32_t STREAM_RESET;
}stream_stat_t;

typedef struct {
	int socket_d;
}testmodeCfg_t;

#define AP_TEST_STATUS_OFFSET 0											/*!< AP Test Status offset */
#define AP_TEST_STATUS_LENGTH 160										/*!< AP Test Status length in byte */
#define AP_TEST_STATUS_AVTP_SUBTYPE(x) x								/*!< AVTP Subtype */
#define AP_TEST_STATUS_AVTP_VERSION_CONTROL(x) x + 1					/*!< Version and control fields */
#define AP_TEST_STATUS_AVTP_STATUS_LENGTH(x) x + 2						/*!< Status and content length */
#define AP_TEST_STATUS_TARGET_ENTITY_ID(x) x + 4						/*!< Target entity ID */
#define AP_TEST_STATUS_CONTROLLER_ENTITY_ID(x) x + 12					/*!< Controller entity ID */
#define AP_TEST_STATUS_SEQUENCE_ID(x) x + 20							/*!< Sequence ID */
#define AP_TEST_STATUS_COMMAND_TYPE(x) x + 22							/*!< Command type */
#define AP_TEST_STATUS_DESCRIPTOR_TYPE(x) x + 24						/*!< Descriptor Type */
#define AP_TEST_STATUS_DESCRIPTOR_INDEX(x) x + 26						/*!< Descriptor Index */
#define AP_TEST_STATUS_COUNTERS_VALID(x) x + 28							/*!< Counters valid */
#define AP_TEST_STATUS_COUNTER_MEDIA_LOCKED(x) x + 32					/*!< Counter Media locked */
#define AP_TEST_STATUS_COUNTER_MEDIA_UNLOCKED(x) x + 36					/*!< Counter Media unlocked */
#define AP_TEST_STATUS_COUNTER_STREAM_RESET(x) x + 40					/*!< Counter Stream reset */
#define AP_TEST_STATUS_COUNTER_SEQ_NUM_MISMATCH(x) x + 44				/*!< Counter Sequence mismatch */
#define AP_TEST_STATUS_COUNTER_MEDIA_RESET(x) x + 48					/*!< Counter Timestamp uncertain */
#define AP_TEST_STATUS_COUNTER_TIMESTAMP_UNCERTAIN(x) x + 52			/*!< Counter Timestamp valid */
#define AP_TEST_STATUS_COUNTER_TIMESTAMP_VALID(x) x + 56				/*!< Counter Timestamp valid */
#define AP_TEST_STATUS_COUNTER_TIMESTAMP_NOT_VALID(x) x + 60			/*!< Counter Timestamp not valid */
#define AP_TEST_STATUS_COUNTER_UNSUPPORTED_FORMAT(x) x + 64				/*!< Counter Unsupported Format */
#define AP_TEST_STATUS_COUNTER_LATE_TIMESTAMP(x) x + 68					/*!< Counter Late timestamp */
#define AP_TEST_STATUS_COUNTER_EARLY_TIMESTAMP(x) x + 72				/*!< Counter Early Timestamp */
#define AP_TEST_STATUS_COUNTER_AVTP_FRAMES_RX(x) x + 76					/*!< Counter AVTP Frame Rx */
#define AP_TEST_STATUS_COUNTER_AVTP_FRAMES_TX(x) x + 80					/*!< Counter AVTP Frame Tx */
#define AP_TEST_STATUS_MESSAGE_TIMESTAMP(x) x + 128						/*!< Timestamp Value */
#define AP_TEST_STATUS_STATION_STATE(x) x + 136							/*!< Station state */
#define AP_TEST_STATUS_STATION_STATE_SPECIFIC_DATA(x) x + 137			/*!< Station state specific data */
#define AP_TEST_STATUS_COUNTER_27(x) x + 140							/*!< Counter 27 */
#define AP_TEST_STATUS_COUNTER_28(x) x + 144							/*!< Counter 28 */
#define AP_TEST_STATUS_COUNTER_29(x) x + 148							/*!< Counter 29 */
#define AP_TEST_STATUS_COUNTER_30(x) x + 152							/*!< Counter 30 */
#define AP_TEST_STATUS_COUNTER_31(x) x + 156							/*!< Counter 31 */


/**
 * @brief Automotive Profile Test Status Station State
 */
typedef enum {
	TESTMODE_STATE_RESERVED = 0,
	TESTMODE_STATE_ETHERNET_READY,
	TESTMODE_STATE_AVB_SYNC,
	TESTMODE_STATE_AVB_MEDIA_READY,
} TestmodeState_t;

/**
 * @brief Automotive Profile Test Status Descriptor Type
 */
typedef enum {
	TESTMODE_DESCRIPTOR_AVB_INTERFACE = 0x0009,
	TESTMODE_DESCRIPTOR_STREAM_INPUT = 0x0005,
	TESTMODE_DESCRIPTOR_STREAM_OUTPUT = 0x0006
} TestmodeDescriptorType_t;

/**
 * @brief  initializes netlink sockets
 * @params void
 * @return int value 0 on success,-1 on failure
 */
int init_testmode (void);

/**
 * @brief  creates netlink request messages
 * fills packet fields
 * @params avb_role_t,uint16_t, pointer to stream_stat_t
 * @return void
 */
void tx_testmode_message (avb_role_t, U16, stream_stat_t *);

/**
 * @brief  Converts the unsigned short integer hostshort
 * from host byte order to network byte order.
 * @param s short host byte order
 * @return short value in network order
 */
uint16_t AP_htons( uint16_t s );

/**
 * @brief  Converts the unsigned integer hostlong
 * from host byte order to network byte order.
 * @param  l Host long byte order
 * @return value in network byte order
 */
uint32_t AP_htonl( uint32_t l );

/**
 * @brief  Converts the unsigned short integer netshort
 * from network byte order to host byte order.
 * @param s Network order short integer
 * @return host order value
 */
uint16_t AP_ntohs( uint16_t s );

/**
 * @brief  Converts the unsigned integer netlong
 * from network byte order to host byte order.
 * @param l Long value in network order
 * @return Long value on host byte order
 */
uint32_t AP_ntohl( uint32_t l );

/**
 * @brief  Converts a 64-bit word from host to network order
 * @param  x Value to be converted
 * @return Converted value
 */
uint64_t AP_htonll(uint64_t x);

#endif
