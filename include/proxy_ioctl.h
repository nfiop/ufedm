/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

// proxy_ioctl.h - shared UAPI header for kernel and userspace
#ifndef PROXY_IOCTL_H
#define PROXY_IOCTL_H

#include "defs.h"

// Detect whether we are compiling in the kernel or userspace
#ifdef __KERNEL__
#include <linux/ioctl.h> /* _IO, _IOR, _IOW */
#else
#include <sys/ioctl.h>
#endif

/* Magic number for ioctl commands */
#define PROXY_IOC_MAGIC 'P'

struct proxy_mtd_info {
	__u32 backend_mtd_index;
	__u16 flash_page_size; /* Should be the entire page size of a NAND flash
				* chip, including ECC bytes, bad block marker
				* and other misc bytes which are not intended
				* for data storage
				*/
	__u16 flash_oob_size;  // OOB size includes ECC bytes, bad block marker
			       // and other misc bytes
	__u32 flash_pages_per_sector_cnt; // Page count per erase sector (known
					  // also as eraseblock)
	__u32 flash_erase_sector_size;	  // Size of erase sector
	__u32 reserved[6];		  /* reserved for future expansion */
};

struct proxy_shm_info {
	__u32 packet_queue_size; /* size of each ring in bytes */
	__u32 packet_size; /* Desired packet size to send/receive in a ring */
	__u32 proto_ver; /* Protocol version for packet communication, should be
	   0 for now */
	__u32 reserved[6]; /* reserved for future expansion */
};

struct proxy_stats {
	__u64 requests;
	__u64 responses;
	__u64 errors;
	__u64 reserved[5]; /* reserved for future expansion */
};

struct proxy_ack {
	__u64 seq_num; /* A sequence number to ACK */
	/* Data region that is returned. For most cases should be equal
	 * to the actual page data region size on the flash chip.
	 */
	__u32 retlen;
	/* Data region that is returned. For most cases should be equal
	 * to the actual page OOB region size on the flash chip.
	 */
	__u32 oob_retlen;
};

struct proxy_nack {
	__u64 seq_num; /* A sequence number to NACK */
	__u16 positive_errno;
};

/* ioctl commands */
#define PROXY_IOC_GET_SHM_INFO _IOR(PROXY_IOC_MAGIC, 0, struct proxy_shm_info)
#define PROXY_IOC_GET_STATS _IOR(PROXY_IOC_MAGIC, 1, struct proxy_stats)
#define PROXY_IOC_GET_MTD_INFO _IOR(PROXY_IOC_MAGIC, 2, struct proxy_mtd_info)
#define PROXY_IOC_ACK_WRITE _IOW(PROXY_IOC_MAGIC, 3, struct proxy_ack)
#define PROXY_IOC_ACK_READ _IOW(PROXY_IOC_MAGIC, 4, struct proxy_ack)
#define PROXY_IOC_NACK_WRITE _IOW(PROXY_IOC_MAGIC, 5, struct proxy_nack)
#define PROXY_IOC_NACK_READ _IOW(PROXY_IOC_MAGIC, 6, struct proxy_nack)

#endif
