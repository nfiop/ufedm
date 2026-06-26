/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __SHARED_MEMORY_REGION_H
#define __SHARED_MEMORY_REGION_H

#include "defs.h"
#include "shm_packet.h"

/* This bitmap is a shared between userspace and kernel. Behind the scenes
 * it is working like this -
 *
 * 1. A new incoming I/O request arrives and dispatched by the kernel from
 * 	  the upper MTD device.
 * 2. The kernel holds an internal map of sequence numbers to used slots
 *    and tries to find a free packet slot. If it finds a packet slot,
 *    It allocates it in the map, generate a new sequence number, puts
 *	  the data + OOB buffers and any related metadata in the shared buffer
 * 3. The kernel sets the corresponding bit in `struct user_pkt_owneship_bitmap`
 *	  bits[] array, to ease scanning for userspace.
 * 4. Userspace can scan periodically and find a new packet by observing
 *	  the corresponding bit being set to 1. If it uses epoll()/poll() on
 *	  an eventfd, it will make it more efficient to get updates.
 * 5. Userspace can either:
 * 5a. NACK the I/O request if it cannot reasonably handle it. It should
 *     set the corresponding bit to 0 before doing so.
 * 5b. Write the content back to the shared buffer as intended by
 *    its policy and known algorithms.
 *	  Userspace then set the corresponding bit to 0, and to ACK the I/O
 *    request afterwards, as soon as possible.
 *
 * It **SHOULD** be noted that this map is not taken into consideration
 * on the allocation decision of packet slots in a queue. The kernel holds
 * an internal map of which slot is allocated regardless, and will publish
 * updates to bitmap as a best-effort mechanism to ease processing of incoming
 * packets by a userspace implementation.
 */
struct user_pkt_owneship_bitmap {
	uint32_t nbits;
	uint32_t reserved;
	unsigned long bits[];
};

struct proxy_shm_info {
	__u32 proto_ver; /* Protocol version for packet communication, should be
	   0 for now */
	__u32 bitmap_bits_cnt; /* Corresponds to the size that would be at
	       unsigned long bits[] in `struct user_pkt_owneship_bitmap`  */
	__u32 packet_size; /* Desired packet size to send/receive in a queue - 
			including metadata and actual data buffer  */
	__u8 packet_queues_cnt; /* Currently 2 - one for READ and one for WRITE
				 */
	__u32 reserved[6];	/* reserved for future expansion */
};

/* It could probably be quite nice to define a struct of
 * a shared memory region.
 * However, a packet queue structure size is determined in runtime, due
 * to changing data + OOB sizes of the backing MTD device.
 * The struct would have look like this then:
 * struct packet_queue {
 *		struct user_pkt_owneship_bitmap bitmap;
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

#define SHM_BITMAP_OFFSET(info, queue_idx)                                     \
	((queue_idx *                                                          \
	    (sizeof(struct user_pkt_owneship_bitmap) +                         \
		(info->bitmap_bits_cnt * sizeof(unsigned long)) +              \
		(info->packet_size  * PROXY_PACKETS_COUNT_PER_QUEUE))))

#define SHM_FIRST_PACKET_OFFSET(info, queue_idx)                               \
	SHM_BITMAP_OFFSET(info, queue_idx)                                     \
	+sizeof(struct user_pkt_owneship_bitmap) +                             \
	    (info->bitmap_bits_cnt * sizeof(unsigned long))

static inline u8 *get_first_shm_packet_offset_in_queue(
    void *mmap_base, struct proxy_shm_info *info, size_t queue_idx)
{
	return (u8 *)mmap_base + SHM_FIRST_PACKET_OFFSET(info, queue_idx);
}

static inline struct user_pkt_owneship_bitmap *get_shm_bitmap(
    void *mmap_base, struct proxy_shm_info *info, size_t queue_idx)
{
	return (struct user_pkt_owneship_bitmap *)((u8 *)mmap_base +
						   SHM_BITMAP_OFFSET(
						       info, queue_idx));
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
	return SHM_BITMAP_OFFSET(info, info->packet_queues_cnt + 1);
}

#endif
