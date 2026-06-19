/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __RING_BUFFER__PACKET__
#define __RING_BUFFER__PACKET__

#include "defs.h"

/*
 * UFEDM_RING_PACKET_TYPE_WRITE_RAW is a type of a packet that occurs when
 * there's a write operation on the upper MTD device which should propagate to
 * the backing MTD device. The write is raw, so we should determine from the
 * default layout where is the data bytes, and re-encode the ECC bytes and
 * everything else needed. UFEDM_RING_PACKET_TYPE_READ_RAW is a type of a packet
 * that occurs when there's a read operation on the upper MTD device which
 * should propagate to the backing MTD device. The read is raw, so we should
 * determine from the default layout where is the data bytes, and re-encode the
 * ECC bytes and everything else needed.
 */
enum ring_packet_type {
	UFEDM_RING_PACKET_TYPE_WRITE_RAW,
	UFEDM_RING_PACKET_TYPE_READ_RAW,
	UFEDM_RING_PACKET_TYPE_WRITE_BYTES,
	UFEDM_RING_PACKET_TYPE_READ_BYTES,
};

struct ring_packet {
	__u8 type;
	__u32 length;
	__u32 reserved[2]; /* reserved for future expansion */
	__u8 data[];
};

#endif
