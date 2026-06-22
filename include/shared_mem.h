/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __RING_H
#define __RING_H

#include "defs.h"

#define PACKET_QUEUE_SIZE (64 * 1024)

struct packet_queue {
	u8 data[PACKET_QUEUE_SIZE];
};

struct op_queues {
	struct packet_queue tx;
	struct packet_queue rx;
};

struct shared_region {
	struct op_queues read;
	struct op_queues write;
};

#endif
