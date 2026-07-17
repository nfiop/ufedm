/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __SHARED_MEMORY_REGION_H
#define __SHARED_MEMORY_REGION_H

#include "defs.h"
#include "shm_slot.h"

struct proxy_shm_info {
	/* Protocol version for packet communication, should be	0 for now */
	__u32 proto_ver;

	/* Desired slot size in a queue - including metadata and actual data
	 * buffer */
	__u32 slot_size;

	/* Currently 2 - one for READ and one for WRITE */
	__u32 queues_count;

	/* Should indicate the size for passing to mmap(2)
	 * when for getting access to the shared memory.
	 */
	__u32 total_buf_size;

	__u32 reserved[6];	/* reserved for future expansion */
};

/* This struct is used by userspace for determining the
 * the slots count (currently hardcoded) and the type of
 * the queue (READ or WRITE).
 */
struct proxy_shm_queue_info {
	__u32 idx;

	__u8 type;
	__u16 slots_count;

	__u32 mem_offset;
	__u32 mem_len;
};

/* It could probably be quite nice to define a struct of
 * a shared memory region.
 * However, a packet queue structure size is determined in runtime, due
 * to changing data + OOB sizes of the backing MTD device.
 * The struct would have look like this then:
 * struct packet_queue {
 *		struct shared_mem_slot slots[SLOTS_COUNT_PER_QUEUE];
 * }
 *
 * And finally an entire shared memory interface could be defined like:
 *	 struct shared_region {
 *		struct packet_queue read;
 *		struct packet_queue write;
 *	};
 *
 * Unfortunately, evaluating sizes is too hard to do in a function helpers.
 * Userspace should evaluate this via specific ioctls, and the kernel has
 * the needed metadata in its data structures already.
 */

#endif
