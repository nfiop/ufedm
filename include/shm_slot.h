/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __SHARED_MEMORY_BUFFER__SLOT__
#define __SHARED_MEMORY_BUFFER__SLOT__

#include "defs.h"

/* HOW THE SENDING/RECEIVING MECHANISM WORKS -
 *
 * The kernel is requested to serve an I/O request
 * on the backing MTD device. It then generates a packet
 * with a new & unique sequence number by holding an internal
 * counter for that purpose for each shared buffer.
 * It then publishes the data in the buffer first, and the sequence
 * number. A sequence number of 0 is not valid, and marks
 * an entry that should not be processed by userspace.
 * To ease the receiving on userspace, an eventfd is created
 * for each shared buffer to be used for polling.
 *
 * How userspace should handle this -
 * A user program opens the corresponding proxy device
 * and the eventfd's (each for a shared buffer). Instead of scanning
 * it can poll on the matching eventfd and wait for events. Reading
 * from the eventfd will return a counter that is the same as sequence
 * number.
 * Userspace **SHOULD** ignore entries that have a value of 0 in
 * __seq_num field.
 * Userspace **MIGHT** rearrange, modify and ignore bytes within the
 * buffer in the ring packet. The kernel can't assume anything about
 * the layout besides sizes of DATA region and OOB region as well.
 *
 * When userspace handles the WRITE shared buffer, it should read the
 * incoming buffer, and modify & rearrange bytes as it seems fit
 * before written to the actual chip.
 * When userspace handles the READ shared buffer, it should read the
 * incoming buffer and apply the reverse operation before ACKing
 * being done.
 *
 * A userspace implementation should try to handle given OOB buffer in a
 * graceful way, as much as possible. If there's no such option, it **SHOULD**
 * send a NACK immediately to reduce pressure on the subsystem.
 *
 * In general, userspace **SHOULD** the least amount of processing being
 * possible to ensure fast I/O transcations. It **SHOULD** respond to each
 * submission by the the kernel, to prevent a severe meltdown in terms of
 * non-responsive MTD device.
 *
 * For example, let's say we handle a NAND flash chip with page size
 * of 2048+64 (data + OOB) bytes. For a READ operation, the upper layers
 * can send an entire page containing some OOB bytes and a whole chunk of
 * data bytes. Userspace can either ACK or NACK the submission.
 * In case of ACK, the userspace daemon should ensure a prepared buffer
 * is appearing in the same shared memory location.
 * The userspace implementation can also immediately NACK the submission
 * with an errno to be sent back to the upper layer.
 */

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

	/* These values have are set with respect to the I/O request that
	 * is occuring within the slot -
	 *
	 * For write slots, they represent the data and OOB buffer lengths that
	 * are specified by the upper MTD write_oob callee.
	 * When writing back (doing an ACK), userspace should prepare a whole
	 * page buffer in the slot, containing the original buffers within the
	 * slot.
	 *
	 * For read slots, they represent the data and OOB buffer lengths that
	 * are specified by the upper MTD read_oob callee.
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
