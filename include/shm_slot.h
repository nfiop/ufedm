/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __SHARED_MEMORY_BUFFER__SLOT__
#define __SHARED_MEMORY_BUFFER__SLOT__

#include "defs.h"

typedef __u64 seq_num_t;

/* The inspiration for this struct is the `struct nand_pos` inside a
 * `struct nand_page_io_req`. It was converted from that struct to a
 * well-defined bit-width fields, so we can ensure ABI correctness on
 * the shared memory buffer.
 */
struct nand_io_position_params {
	__u32 target;
	__u32 lun;
	__u32 plane;
	__u32 eraseblock;
	__u32 page;
};

struct shm_slot_hdr {
	/* This is a published kernel sequence number, userspace should
	 * not touch it - it should modify the data as needed, and send
	 * an ACK ioctl based on the provided seq_num when it's done
	 * processing.
	 */
	seq_num_t seq_num;

	/* These parameters should aid userspace with decision on where
	 * to place bad block markers, or skip potential bad block, etc.
	 */
	struct nand_io_position_params pos_params;

	/* These values are set with respect to the I/O request that is
	 * occuring within the slot -
	 *
	 * For write slots, they represent the data and OOB buffer lengths that
	 * are specified by the upper MTD write_oob caller.
	 * When writing back (doing an ACK), userspace should prepare a whole
	 * page buffer in the slot, containing the original buffers within the
	 * slot.
	 *
	 * For read slots, they represent the data and OOB buffer lengths that
	 * are specified by the upper MTD read_oob caller.
	 * When reading back (doing an ACK), userspace should put only the
	 * returned data and OOB buffers in their appropriate sub-buffers, and
	 * the lengths in ACK request should be set according to these
	 * parameters.
	 */
	__u32 datalen;
	__u32 ooblen;
};

struct shared_mem_slot {
	struct shm_slot_hdr header;

	/* Should contain enough size for page data and OOB data
	 * as well. The offsets and lengths are managed by the
	 * backing MTD device and should be either taken via the
	 * proxy device (with an appropriate ioctl) or the backing
	 * MTD device itself if so desired.
	 */
	__u8 buf[];
};

#endif
