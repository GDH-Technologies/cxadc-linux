// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 GDH Technologies

#ifndef _CONSOLE_H
#define _CONSOLE_H

// Line-based command console on the CDC ACM interface.
// Call console_task() from the core0 main loop after tud_task().
void console_task();

#endif
