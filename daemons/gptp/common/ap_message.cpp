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

#include <ieee1588.hpp>
#include <avbts_clock.hpp>
#include <avbap_message.hpp>
#include <ether_port.hpp>
#include <avbts_ostimer.hpp>
#include <gptp_log.hpp>

#include <pthread.h>
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
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <linux/sockios.h>
#include <unistd.h>

// Octet based data 2 buffer macros
#define OCT_D2BMEMCP(d, s) memcpy(d, s, sizeof(s)); d += sizeof(s);
#define OCT_D2BBUFCP(d, s, len) memcpy(d, s, len); d += len;
#define OCT_D2BHTONB(d, s) *(U8 *)(d) = s; d += sizeof(s);
#define OCT_D2BHTONS(d, s) *(U16 *)(d) = PLAT_htons(s); d += sizeof(s);
#define OCT_D2BHTONL(d, s) *(U32 *)(d) = PLAT_htonl(s); d += sizeof(s);

// Bit based data 2 buffer macros
#define BIT_D2BHTONB(d, s, shf) *(uint8_t *)(d) |= s << shf;
#define BIT_D2BHTONS(d, s, shf) *(uint16_t *)(d) |= PLAT_htons((uint16_t)(s << shf));
#define BIT_D2BHTONL(d, s, shf) *(uint32_t *)(d) |= PLAT_htonl((uint32_t)(s << shf));

struct rtnl_link_stats rtnlstats;
extern char *interfaceName;
extern int rtnetlink_sock;

void getRtnetlinkstats (struct nlmsghdr *rtnlmsg)
{
	struct ifinfomsg *infomsg;
	char ifname[IFNAMELEN];
	int rtlen;
	struct rtattr *rtnetlink_attr;

	infomsg = (struct ifinfomsg *)NLMSG_DATA(rtnlmsg);
	rtlen = rtnlmsg->nlmsg_len - NLMSG_LENGTH(sizeof(*infomsg));

	for (rtnetlink_attr = IFLA_RTA(infomsg); RTA_OK(rtnetlink_attr, rtlen); rtnetlink_attr = RTA_NEXT(rtnetlink_attr, rtlen))
	{
		switch(rtnetlink_attr->rta_type)
		{
			case IFLA_IFNAME:
				memcpy(&ifname, (char *) RTA_DATA(rtnetlink_attr), IFNAMELEN);
				break;
			case IFLA_STATS:
				if (interfaceName != NULL) {
					if (memcmp(interfaceName, ifname, sizeof(IFNAMELEN)) == 0) {
						memcpy(&rtnlstats, (char *) RTA_DATA(rtnetlink_attr), sizeof(struct rtnl_link_stats));
					}
				}
				break;
			default:
				break;
		}
	}
}

int getIfaceStats(void) {
	static uint32_t nlmsg_seq = 0;
	int keeprunning = 0;
	char rplybuff[RTNETLINK_RPLY_BUFF];

	if (rtnetlink_sock < 0) {
		GPTP_LOG_ERROR("invalid netlink socket");
		return -1;
	}

	typedef struct netlink_s netlinkreq_t;

	struct netlink_s {
		struct nlmsghdr rtnlhdr;
		struct rtgenmsg rtnlgen;
	};

	struct iovec inout_vec;
	netlinkreq_t rtnetlinkreq;
	struct msghdr rtnetl_msg;
	struct sockaddr_nl sockaddrkernel;

	memset(&rtnetl_msg, 0, sizeof(rtnetl_msg));
	memset(&sockaddrkernel, 0, sizeof(sockaddrkernel));
	memset(&rtnetlinkreq, 0, sizeof(rtnetlinkreq));

	sockaddrkernel.nl_family = AF_NETLINK; /* fill-in kernel address (destination) */

	rtnetlinkreq.rtnlgen.rtgen_family = AF_PACKET; /*  no preferred AF, we will get *all* interfaces */
	rtnetlinkreq.rtnlhdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
	rtnetlinkreq.rtnlhdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	rtnetlinkreq.rtnlhdr.nlmsg_pid = getpid();
	rtnetlinkreq.rtnlhdr.nlmsg_type = RTM_GETLINK;
	rtnetlinkreq.rtnlhdr.nlmsg_seq = nlmsg_seq++;

	inout_vec.iov_base = &rtnetlinkreq;
	inout_vec.iov_len = rtnetlinkreq.rtnlhdr.nlmsg_len;

	rtnetl_msg.msg_iov = &inout_vec;
	rtnetl_msg.msg_name = &sockaddrkernel;
	rtnetl_msg.msg_namelen = sizeof(sockaddrkernel);
	rtnetl_msg.msg_iovlen = 1;

	sendmsg(rtnetlink_sock, (struct msghdr *) &rtnetl_msg, 0);

	while (!keeprunning)
	{
		int readlen;
		struct nlmsghdr *nlmsgptr;
		struct iovec iovrply;
		struct msghdr rtrply;

		memset(&iovrply, 0, sizeof(iovrply));
		memset(&rtrply, 0, sizeof(rtrply));

		iovrply.iov_base = rplybuff;
		iovrply.iov_len = RTNETLINK_RPLY_BUFF;
		rtrply.msg_iov = &iovrply;
		rtrply.msg_iovlen = 1;
		rtrply.msg_name = &sockaddrkernel;
		rtrply.msg_namelen = sizeof(sockaddrkernel);

		readlen = recvmsg(rtnetlink_sock, &rtrply, 0);
		if (readlen)
		{
			for (nlmsgptr = (struct nlmsghdr *) rplybuff; NLMSG_OK(nlmsgptr, (unsigned int)readlen); nlmsgptr = NLMSG_NEXT(nlmsgptr, readlen))
			{
				switch(nlmsgptr->nlmsg_type)
				{
					case RT_NLMSG_DONE:
						keeprunning++;
						break;
					case RT_RTM_NEWLINK:
						getRtnetlinkstats(nlmsgptr);
						break;
					default:
						GPTP_LOG_INFO("rtnetlink: message type %d, length %d\n", nlmsgptr->nlmsg_type, nlmsgptr->nlmsg_len);
						break;
				}
			}
		}
	}
	return 0;
}

APMessageTestStatus::APMessageTestStatus()
{
}

APMessageTestStatus::~APMessageTestStatus()
{
}

APMessageTestStatus::APMessageTestStatus( EtherPort *port )
{
}

void APMessageTestStatus::sendPort( EtherPort * port )
{
	static uint16_t sequenceId = 0;

	uint8_t buf_t[256];
	uint8_t *buf_ptr = buf_t + port->getPayloadOffset();
	uint16_t tmp16;
	uint32_t tmp32;
	uint64_t tmp64;

	memset(buf_t, 0, 256);

	// Create packet in buf
	messageLength = AP_TEST_STATUS_LENGTH;

	BIT_D2BHTONB(buf_ptr + AP_TEST_STATUS_AVTP_SUBTYPE(AP_TEST_STATUS_OFFSET), 0xfb, 0);
	BIT_D2BHTONB(buf_ptr + AP_TEST_STATUS_AVTP_VERSION_CONTROL(AP_TEST_STATUS_OFFSET), 0x1, 0);
	BIT_D2BHTONS(buf_ptr + AP_TEST_STATUS_AVTP_STATUS_LENGTH(AP_TEST_STATUS_OFFSET), 148, 0);

	port->getLocalAddr()->toOctetArray(buf_ptr + AP_TEST_STATUS_TARGET_ENTITY_ID(AP_TEST_STATUS_OFFSET));

	tmp16 = PLAT_htons(sequenceId++);
	memcpy(buf_ptr + AP_TEST_STATUS_SEQUENCE_ID(AP_TEST_STATUS_OFFSET), &tmp16, sizeof(tmp16));

	BIT_D2BHTONS(buf_ptr + AP_TEST_STATUS_COMMAND_TYPE(AP_TEST_STATUS_OFFSET), 1, 15);
	BIT_D2BHTONS(buf_ptr + AP_TEST_STATUS_COMMAND_TYPE(AP_TEST_STATUS_OFFSET), 0x29, 0);

	tmp16 = PLAT_htons(0x09);
	memcpy(buf_ptr + AP_TEST_STATUS_DESCRIPTOR_TYPE(AP_TEST_STATUS_OFFSET), &tmp16, sizeof(tmp16));
	tmp16 = PLAT_htons(0x00);
	memcpy(buf_ptr + AP_TEST_STATUS_DESCRIPTOR_INDEX(AP_TEST_STATUS_OFFSET), &tmp16, sizeof(tmp16));

	if (getIfaceStats() != 0) {
		// To Do: flags for counters
		// clear countervalid bit for FRAME TX, FRAME RX and FRAME CRC error counters
		tmp32 = PLAT_htonl(0x07000023);
		memcpy(buf_ptr + AP_TEST_STATUS_COUNTERS_VALID(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

		tmp32 = PLAT_htonl(port->getLinkUpCount());
		memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_LINKUP(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

		tmp32 = PLAT_htonl(port->getLinkDownCount());
		memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_LINKDOWN(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

	}
	else {
		// To Do: flags for counters
		// set countervalid bit for FRAME TX, FRAME RX and FRAME CRC error counters
		tmp32 = PLAT_htonl(0x0700003F);
		memcpy(buf_ptr + AP_TEST_STATUS_COUNTERS_VALID(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

		tmp32 = PLAT_htonl(port->getLinkUpCount());
		memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_LINKUP(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

		tmp32 = PLAT_htonl(port->getLinkDownCount());
		memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_LINKDOWN(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

		tmp32 = PLAT_htonl(rtnlstats.tx_packets);
		memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_FRAMES_TX(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

		tmp32 = PLAT_htonl(rtnlstats.rx_packets);
		memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_FRAMES_RX(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));

		tmp32 = PLAT_htonl(rtnlstats.rx_crc_errors);
		memcpy(buf_ptr + AP_TEST_STATUS_COUNTER_FRAMES_RX_CRC_ERROR(AP_TEST_STATUS_OFFSET), &tmp32, sizeof(tmp32));
	}

	Timestamp system_time;
	Timestamp device_time;
	uint32_t local_clock, nominal_clock_rate;
	port->getDeviceTime(system_time, device_time, local_clock, nominal_clock_rate);
	tmp64 = PLAT_htonll(TIMESTAMP_TO_NS(system_time));
	memcpy(buf_ptr + AP_TEST_STATUS_MESSAGE_TIMESTAMP(AP_TEST_STATUS_OFFSET), &tmp64, sizeof(tmp64));

	BIT_D2BHTONB(buf_ptr + AP_TEST_STATUS_STATION_STATE(AP_TEST_STATUS_OFFSET), (uint8_t)port->getStationState(), 0);

	port->sendGeneralPort(AVTP_ETHERTYPE, buf_t, messageLength, MCAST_TEST_STATUS, NULL);

	return;
}

