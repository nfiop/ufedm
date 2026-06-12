/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __RING_H
#define __RING_H

#include "defs.h"

#define RING_SIZE (64 * 1024)

struct ring {
	u32 head;
	u32 tail;
	u32 size;
	u8 data[RING_SIZE];
};

struct op_rings {
	struct ring tx;
	struct ring rx;
};

struct shared_region {
	struct op_rings read;
	struct op_rings write;
};

#endif
