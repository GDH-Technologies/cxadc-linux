// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2023 Rene Wolf

#ifndef _MAIN1_H
#define _MAIN1_H

#include <stdint.h>

void main1();

// increments once per core1 main-loop iteration, watched by wdt_trace on core0
extern volatile uint32_t main1_heartbeat;

#endif

