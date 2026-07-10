/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __SHARED_MEMORY_REGION_H
#define __SHARED_MEMORY_REGION_H

#include "defs.h"
#include "shm_packet.h"

struct proxy_shm_info {
	/* Protocol version for packet communication, should be	0 for now */
	__u32 proto_ver;

	/* Desired slot size in a queue - including metadata and actual data
	 * buffer */
	__u32 slot_size;

	/* Currently 2 - one for READ and one for WRITE */
	__u8 packet_queues_cnt;

	__u32 reserved[6];	/* reserved for future expansion */
};

/* It could probably be quite nice to define a struct of
 * a shared memory region.
 * However, a packet queue structure size is determined in runtime, due
 * to changing data + OOB sizes of the backing MTD device.
 * The struct would have look like this then:
 * struct packet_queue {
 *		struct shm_packet pkts[PROXY_PACKETS_COUNT_PER_QUEUE];
 * }
 *
 * And then a struct op_queues would look like this:
 *	struct op_queues {
 *		struct packet_queue tx;
 *		struct packet_queue rx;
 *	};
 *
 * And finally an entire shared memory interface could be defined like:
 *	 struct shared_region {
 *		struct op_queues read;
 *		struct op_queues write;
 *	};
 *
 * Still, there's hope. We can define helper functions that will get us
 * right to the actual structures' offsets, as intended.
 * All we need is a pointer to a valid `struct proxy_shm_info` and few
 * more details along it.
 */

enum shm_queue_idx {
	SHM_READ_QUEUE_IDX = 0,
	SHM_WRITE_QUEUE_IDX = 1,
	__SHM_QUEUE_IDX_MAX = 2,
};

#define SHM_QUEUE_OFFSET(info, queue_idx)                                      \
	((queue_idx * (info->slot_size * PROXY_PACKETS_COUNT_PER_QUEUE)))

static inline u8 *get_first_shm_packet_offset_in_queue(
    void *mmap_base, struct proxy_shm_info *info, size_t queue_idx)
{
	return (u8 *)mmap_base + SHM_QUEUE_OFFSET(info, queue_idx);
}

static inline struct shm_packet *get_first_shm_packet_in_queue(
    void *mmap_base, struct proxy_shm_info *info, size_t queue_idx)
{
	return (struct shm_packet *)get_first_shm_packet_offset_in_queue(
	    mmap_base, info, queue_idx);
}

static inline struct shm_packet *get_shm_packet(void *mmap_base,
    struct proxy_shm_info *info, size_t queue_idx, size_t pkt_idx)
{
	return (struct shm_packet *)(get_first_shm_packet_offset_in_queue(
					 mmap_base, info, queue_idx) +
				     (pkt_idx * sizeof(struct shm_packet)));
}

static inline size_t get_shm_region_size(struct proxy_shm_info *info)
{
	return SHM_QUEUE_OFFSET(info, info->packet_queues_cnt + 1);
}

#endif
