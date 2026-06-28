/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __SHARED_MEMORY_BUFFER__PACKET__
#define __SHARED_MEMORY_BUFFER__PACKET__

#include "defs.h"

/* HOW THE SENDING/RECEIVING MECHANISM WORKS -
 *
 * The kernel is requested to serve an I/O request
 * on the backing MTD device. It then generates a packet
 * with a new & unique sequence number by holding an internal
 * counter for that purpose for each ring buffer.
 * It then publishes the data in the buffer first, and the sequence
 * number. A sequence number of 0 is not valid, and marks
 * an entry that should not be processed by userspace.
 * To ease the receiving on userspace, an eventfd is created
 * for each ring buffer to be used for polling.
 *
 * How userspace should handle this -
 * A user program opens the corresponding proxy device
 * and the eventfd's (each for a ring buffer). Instead of scanning
 * it can poll on the matching eventfd and wait for events. Reading
 * from the eventfd will return a counter that is the same as sequence
 * number.
 * Userspace **SHOULD** ignore entries that have a value of 0 in
 * __seq_num field.
 * Userspace **MIGHT** rearrange, modify and ignore bytes within the
 * buffer in the ring packet. The kernel can't assume anything about
 * the layout besides sizes of DATA region and OOB region as well.
 *
 * When userspace handles the WRITE ring buffer, it should read the
 * incoming buffer, and modify & rearrange bytes as it seems fit
 * before written to the actual chip.
 * When userspace handles the READ ring buffer, it should read the
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

struct shm_pkt_hdr {
	/* This is a published kernel sequence number, userspace should
	 * not touch it - it should modify the data as needed, and send
	 * an ACK ioctl based on the provided seq_num when it's done
	 * processing.
	 */
	seq_num_t seq_num;

	/* These values specify the amount of data and OOB being
	 * posted by the kernel to process.
	 * Userspace is allowed to change this according to the
	 * actual processing being done before ACKing.
	 * It should be noted that datalen and ooblen are sanitized
	 * by the kernel so there's no out-of-bound copy.
	 *
	 * Also, the size of the buffer afterwards **DOES NOT** change
	 * due to a datalen or ooblen being less the chip page OOB size
	 * or page data region size.
	 * If 0 is specified for datalen or ooblen, it means that
	 * the kernel didn't submit any bytes in the corresponding
	 * section. After ACKing by userspace, 0 means userspace didn't
	 * proceed to submit its own bytes in such section - this could
	 * lead to a I/O failure or just a warning, depending on the
	 * actual configuration.
	 */
	__u32 datalen;
	__u32 ooblen;
};

struct shm_packet {
	struct shm_pkt_hdr header;

	/* Should contain enough size for page data and OOB data
	 * as well. The offsets and lengths are managed by the
	 * backing MTD device and should be either taken via the
	 * proxy device (with an appropriate ioctl) or the backing
	 * MTD device itself if so desired.
	 */
	__u8 buf[];
};

#endif
