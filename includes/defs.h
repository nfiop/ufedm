/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __DEFS_H
#define __DEFS_H

// 32 is a reasonable number for MTD partitions' count on one
// system. It can be easily changed if so desired.
#define PROXY_MAX_DEVICE_COUNT 32

// Detect whether we are compiling in the kernel or userspace
#ifdef __KERNEL__
#include <linux/types.h> /* __u32, __u64 */
#else
#include <stdint.h> /* uint32_t, uint64_t */
typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

#endif
