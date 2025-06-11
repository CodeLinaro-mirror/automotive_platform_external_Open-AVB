/* ============================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
============================================================================ */

#ifndef UTC_HELPER_H
#define UTC_HELPER_H

int utc_helper_init();
void utc_helper_deinit();
int utcGetSyncStatus();
uint64_t utcGetUtcTime();

#endif