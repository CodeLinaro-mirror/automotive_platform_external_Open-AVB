/******************************************************************************

Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear

******************************************************************************/

#ifndef QGPTP_RMGR_H
#define QGPTP_RMGR_H

#include <stdint.h>
#include <avbts_clock.hpp>
//#include <avbts_port.hpp>
#include <common_port.hpp>

#define DRIVER_NAME "qgptp_rmgr"
#define DEVICE_NAME "/dev/qgptp"
#define DEVICE_NAME_GM "/dev/qgptp_gm"
#define GUEST_INTERFACE_NAME "GUEST"
#define IF_NAMESIZE 16
// socket failure handling
#define RES_MAX_RETRY_COUNT 100
#define RES_RETRY_SLEEP 2000 //in us (2ms sleep)
#define SCT_SHM_SIZE 0x2000

#ifdef ANDROID
#define DEFAULT_GROUPNAME "vendor_ptp"     /*!< Default groupname for the shared memory interface*/
#else
#define DEFAULT_GROUPNAME "vnw"     /*!< Default groupname for the shared memory interface*/
#endif

#ifdef AVB_FEATURE_GVM_MODE
#define SCT_SHM_NAME  "/dev/sct_gptp_shm"     /*!< Shared memory name*/
#else
#ifdef ANDROID
#define SCT_SHM_NAME  "/dev/sctptpshm"     /*!< Shared memory name*/
#else
#define SCT_SHM_NAME  "/sct_ptp"        /*!< Shared memory name*/
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
extern int qgptp_rmgr_init(const char *name, CommonPort *port);
extern int qgptp_rmgr_deinit();
extern uint64_t qgptp_readtime(char *ifname);
extern uintptr_t get_emac_base_addr(char *ifname);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif /* QGPTP_RMGR_H */
