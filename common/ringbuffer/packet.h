/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __COMMON__PACKET__
#define __COMMON__PACKET__

#include "defs.h"

struct ring_packet {
	__u8 type;
	__u32 length;
	__u32 reserved[2]; /* reserved for future expansion */
	__u8 data[];
};

#endif
