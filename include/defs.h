/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __DEFS_H
#define __DEFS_H

#ifndef __KERNEL__
#include <stdint.h> /* uint32_t, uint64_t */
#include <sys/types.h>
#endif

#include <linux/types.h>

#ifndef __KERNEL__
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

// 32 is a reasonable number for MTD partitions' count on one
// system. It can be easily changed if so desired.
#define PROXY_MAX_DEVICE_COUNT 32

// A value that represents the max count of queues per proxy device.
// DON'T CHANGE THIS UNLESS YOU UNDERSTAND THE IMPLICATIONS.
#define PROXY_MAX_QUEUES_COUNT 2

// We might allow dynamically setting this value in the future...
// For now, this value is hardcoded, so don't use it except the
// very few places where it should be (like in the kernel!).
#define PROXY_SLOTS_COUNT_PER_QUEUE 20

#endif
