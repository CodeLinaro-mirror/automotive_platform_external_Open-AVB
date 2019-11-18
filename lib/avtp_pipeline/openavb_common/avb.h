 /*
  * Copyright (c) <2013>, Intel Corporation.
  *
  * This program is free software; you can redistribute it and/or modify it
  * under the terms and conditions of the GNU Lesser General Public License,
  * version 2.1, as published by the Free Software Foundation.
  *
  * This program is distributed in the hope it will be useful, but WITHOUT
  * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
  * more details.
  *
  * You should have received a copy of the GNU Lesser General Public License along with
  * this program; if not, write to the Free Software Foundation, Inc.,
  * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
  *
 */

#ifndef __AVBTP_H__
#define __AVBTP_H__

#include <inttypes.h>
#include <sys/types.h>

#define VALID		1
#define INVALID		0

#define MAC_ADDR_LEN	6

#define IGB_BIND_NAMESZ		24

#define SHM_SIZE (sizeof(gPtpTimeData) + sizeof(pthread_mutex_t))

#ifdef ANDROID
#define SHM_NAME  "/dev/ptpshm"
#else
#define SHM_NAME  "/ptp"
#endif

#define PTP_CLOCK_IDENTITY_LENGTH 8	/*!< Size of a clock identifier stored in the ClockIndentity class, described at IEEE 802.1AS-2011 Clause 8.5.2.4*/

/*Type for process id*/
#define PID_TYPE    pid_t

/**
 * @brief PortState enumeration
 */
typedef enum {
	PTP_MASTER = 7,		//!< Port is PTP Master
	PTP_PRE_MASTER,		//!< Port is not PTP Master yet.
	PTP_SLAVE,			//!< Port is PTP Slave
	PTP_UNCALIBRATED,	//!< Port is uncalibrated.
	PTP_DISABLED,		//!< Port is not PTP enabled. All messages are ignored when in this state.
	PTP_FAULTY,			//!< Port is in a faulty state. Recovery is implementation specific.
	PTP_INITIALIZING,	//!< Port's initial state.
	PTP_LISTENING		//!< Port is in a PTP listening state. Currently not in use.
} PortState;

#define MAX_SAMPLE_VALUE ((1U << ((sizeof(int32_t)*8)-1))-1)

#define IEEE_61883_IIDC_SUBTYPE 0x0

#define MRPD_PORT_DEFAULT 7500

#define STREAM_ID_SIZE		8

#define ETHER_TYPE_AVTP		0x22f0

typedef struct __attribute__ ((packed)) {
	uint64_t subtype:7;
	uint64_t cd_indicator:1;
	uint64_t timestamp_valid:1;
	uint64_t gateway_valid:1;
	uint64_t reserved0:1;
	uint64_t reset:1;
	uint64_t version:3;
	uint64_t sid_valid:1;
	uint64_t seq_number:8;
	uint64_t timestamp_uncertain:1;
	uint64_t reserved1:7;
	uint64_t stream_id;
	uint64_t timestamp:32;
	uint64_t gateway_info:32;
	uint64_t length:16;

} seventeen22_header;

/* 61883 CIP with SYT Field */
typedef struct {
	uint16_t packet_channel:6;
	uint16_t format_tag:2;
	uint16_t app_control:4;
	uint16_t packet_tcode:4;
	uint16_t source_id:6;
	uint16_t reserved0:2;
	uint16_t data_block_size:8;
	uint16_t reserved1:2;
	uint16_t source_packet_header:1;
	uint16_t quadlet_padding_count:3;
	uint16_t fraction_number:2;
	uint16_t data_block_continuity:8;
	uint16_t format_id:6;
	uint16_t eoh:2;
	uint16_t format_dependent_field:8;
	uint16_t syt;
} six1883_header;

typedef struct {
	uint8_t label;
	uint8_t value[3];
} six1883_sample;

#define ETH_ALEN   6 /* Size of Ethernet address */

typedef struct __attribute__ ((packed)) {
	/* Destination MAC address. */
	uint8_t h_dest [ETH_ALEN];
	/* Destination MAC address. */
	uint8_t h_source [ETH_ALEN];
	/* Protocol ID. */
	uint8_t h_protocol[2];
} eth_header;

typedef long double FrequencyRatio;

#ifndef false
typedef enum { false = 0, true = 1 } bool;
#endif

typedef struct {
	bool sync_status;				//!< PTP Sync status
	int64_t ml_phoffset;			//!< Master to local phase offset
	int64_t ls_phoffset;			//!< Local to system phase offset
	FrequencyRatio ml_freqoffset;	//!< Master to local frequency offset
	FrequencyRatio ls_freqoffset;	//!< Local to system frequency offset
	uint64_t local_time;			//!< Local time of last update

	/* Current grandmaster information */
	/* Referenced by the IEEE Std 1722.1-2013 AVDECC Discovery Protocol Data Unit (ADPDU) */
	uint8_t gptp_grandmaster_id[PTP_CLOCK_IDENTITY_LENGTH]; //!< Current grandmaster id (all 0's if no grandmaster selected)
	uint8_t gptp_domain_number; 	//!< gPTP domain number

	/* Grandmaster support for the network interface */
	/* Referenced by the IEEE Std 1722.1-2013 AVDECC AVB_INTERFACE descriptor */
	uint8_t  clock_identity[PTP_CLOCK_IDENTITY_LENGTH]; //!< The clock identity of the interface
	uint8_t  priority1; 			//!< The priority1 field of the grandmaster functionality of the interface, or 0xFF if not supported
	uint8_t  clock_class;			//!< The clockClass field of the grandmaster functionality of the interface, or 0xFF if not supported
	int16_t  offset_scaled_log_variance;	//!< The offsetScaledLogVariance field of the grandmaster functionality of the interface, or 0x0000 if not supported
	uint8_t  clock_accuracy;		//!< The clockAccuracy field of the grandmaster functionality of the interface, or 0xFF if not supported
	uint8_t  priority2; 			//!< The priority2 field of the grandmaster functionality of the interface, or 0xFF if not supported
	uint8_t  domain_number; 		//!< The domainNumber field of the grandmaster functionality of the interface, or 0 if not supported
	int8_t	 log_sync_interval; 	//!< The currentLogSyncInterval field of the grandmaster functionality of the interface, or 0 if not supported
	int8_t	 log_announce_interval; //!< The currentLogAnnounceInterval field of the grandmaster functionality of the interface, or 0 if not supported
	int8_t	 log_pdelay_interval;	//!< The currentLogPDelayReqInterval field of the grandmaster functionality of the interface, or 0 if not supported
	uint16_t port_number;			//!< The portNumber field of the interface, or 0x0000 if not supported

	/* Linux-specific */
	uint32_t sync_count;			//!< Sync messages count
	uint32_t pdelay_count;			//!< pdelay messages count
	bool asCapable; 				//!< asCapable flag: true = device is AS Capable; false otherwise
	PortState port_state;			//!< gPTP port state. It can assume values defined at ::PortState
	PID_TYPE process_id;			//!< Process id number
	uint8_t gmIdentifier[PTP_CLOCK_IDENTITY_LENGTH];
	uint16_t portNumber;

}gPtpTimeData;

int gptpscaling(gPtpTimeData * td, char *memory_offset_buffer);

bool gptplocaltime(const gPtpTimeData * td, uint64_t* now_local);

void gptpdeinit(int shm_fd, char *memory_offset_buffer);

int gptpinit(int *shm_fd, char **memory_offset_buffer);

void avb_set_1722_cd_indicator(seventeen22_header *h1722, uint64_t cd_indicator);
uint64_t avb_get_1722_cd_indicator(seventeen22_header *h1722);
void avb_set_1722_subtype(seventeen22_header *h1722, uint64_t subtype);
uint64_t avb_get_1722_subtype(seventeen22_header *h1722);
void avb_set_1722_sid_valid(seventeen22_header *h1722, uint64_t sid_valid);
uint64_t avb_get_1722_sid_valid(seventeen22_header *h1722);
void avb_set_1722_version(seventeen22_header *h1722, uint64_t version);
uint64_t avb_get_1722_version(seventeen22_header *h1722);
void avb_set_1722_reset(seventeen22_header *h1722, uint64_t reset);
uint64_t avb_get_1722_reset(seventeen22_header *h1722);
void avb_set_1722_reserved0(seventeen22_header *h1722, uint64_t reserved0);
uint64_t avb_get_1722_reserved0(seventeen22_header *h1722);
void avb_set_1722_reserved1(seventeen22_header *h1722, uint64_t reserved1);
uint64_t avb_get_1722_reserved1(seventeen22_header *h1722);
void avb_set_1722_timestamp_uncertain(seventeen22_header *h1722, uint64_t timestamp_uncertain);
uint64_t avb_get_1722_timestamp_uncertain(seventeen22_header *h1722);
void avb_set_1722_timestamp(seventeen22_header *h1722, uint64_t timestamp);
uint64_t avb_get_1722_reset(seventeen22_header *h1722);
void avb_set_1722_reserved0(seventeen22_header *h1722, uint64_t reserved0);
uint64_t avb_get_1722_reserved0(seventeen22_header *h1722);
void avb_set_1722_gateway_valid(seventeen22_header *h1722, uint64_t gateway_valid);
uint64_t avb_get_1722_gateway_valid(seventeen22_header *h1722);
void avb_set_1722_timestamp_valid(seventeen22_header *h1722, uint64_t timestamp_valid);
uint64_t avb_get_1722_timestamp_valid(seventeen22_header *h1722);
void avb_set_1722_reserved1(seventeen22_header *h1722, uint64_t reserved1);
uint64_t avb_get_1722_reserved1(seventeen22_header *h1722);
void avb_set_1722_timestamp_uncertain(seventeen22_header *h1722, uint64_t timestamp_uncertain);
uint64_t avb_get_1722_timestamp_uncertain(seventeen22_header *h1722);
void avb_set_1722_timestamp(seventeen22_header *h1722, uint64_t timestamp);
uint64_t avb_get_1722_timestamp(seventeen22_header *h1722);
void avb_set_1722_gateway_info(seventeen22_header *h1722, uint64_t gateway_info);
uint64_t avb_get_1722_gateway_info(seventeen22_header *h1722);
void avb_set_1722_length(seventeen22_header *h1722, uint64_t length);
uint64_t avb_get_1722_length(seventeen22_header *h1722);
void avb_set_1722_stream_id(seventeen22_header *h1722, uint64_t stream_id);
uint64_t avb_get_1722_stream_id(seventeen22_header *h1722);
void avb_set_1722_seq_number(seventeen22_header *h1722, uint64_t seq_number);
uint64_t avb_get_1722_seq_number(seventeen22_header *h1722);

void avb_set_61883_packet_channel(six1883_header *h61883, uint16_t packet_channel);
uint16_t avb_get_61883_length(six1883_header *h61883);
void avb_set_61883_format_tag(six1883_header *h61883, uint16_t format_tag);
uint16_t avb_get_61883_format_tag(six1883_header *h61883);
void avb_set_61883_app_control(six1883_header *h61883, uint16_t app_control);
uint16_t avb_get_61883_app_control(six1883_header *h61883);
void avb_set_61883_packet_tcode(six1883_header *h61883, uint16_t packet_tcode);
uint16_t avb_get_61883_packet_tcode(six1883_header *h61883);
void avb_set_61883_source_id(six1883_header *h61883, uint16_t source_id);
uint16_t avb_get_61883_source_id(six1883_header *h61883);
void avb_set_61883_reserved0(six1883_header *h61883, uint16_t reserved0);
uint16_t avb_get_61883_reserved0(six1883_header *h61883);
void avb_set_61883_data_block_size(six1883_header *h61883, uint16_t data_block_size);
uint16_t avb_get_61883_data_block_size(six1883_header *h61883);
void avb_set_61883_reserved1(six1883_header *h61883, uint16_t reserved1);
uint16_t avb_get_61883_reserved1(six1883_header *h61883);
void avb_set_61883_source_packet_header(six1883_header *h61883, uint16_t source_packet_header);
uint16_t avb_get_61883_source_packet_header(six1883_header *h61883);
void avb_set_61883_quadlet_padding_count(six1883_header *h61883, uint16_t quadlet_padding_count);
uint16_t avb_get_61883_quadlet_padding_count(six1883_header *h61883);
void avb_set_61883_fraction_number(six1883_header *h61883, uint16_t fraction_number);
uint16_t avb_get_61883_fraction_number(six1883_header *h61883);
void avb_set_61883_data_block_continuity(six1883_header *h61883, uint16_t data_block_continuity);
uint16_t avb_get_61883_data_block_continuity(six1883_header *h61883);
void avb_set_61883_format_id(six1883_header *h61883, uint16_t format_id);
uint16_t avb_get_61883_format_id(six1883_header *h61883);
void avb_set_61883_eoh(six1883_header *h61883, uint16_t eoh);
uint16_t avb_get_61883_eoh(six1883_header *h61883);
void avb_set_61883_format_dependent_field(six1883_header *h61883, uint16_t format_dependent_field);
uint16_t avb_get_61883_format_dependent_field(six1883_header *h61883);
void avb_set_61883_syt(six1883_header *h61883, uint16_t syt);
uint16_t avb_get_61883_syt(six1883_header *h61883);

void * avb_create_packet(uint32_t payload_len);

void avb_initialize_h1722_to_defaults(seventeen22_header *h1722);

void avb_initialize_61883_to_defaults(six1883_header *h61883);

int32_t avb_get_iface_mac_address(int8_t *iface, uint8_t *addr);

int32_t
avb_eth_header_set_mac(eth_header *ethernet_header, uint8_t *addr, int8_t *iface);

void avb_1722_set_eth_type(eth_header *eth_header);

#endif		/*  __AVBTP_H__ */
