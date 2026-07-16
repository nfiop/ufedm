/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

// proxy_ioctl.h - shared UAPI header for kernel and userspace
#ifndef PROXY_IOCTL_H
#define PROXY_IOCTL_H

#include "defs.h"
#include "proxy_queue.h"
#include "shared_mem.h"

// Detect whether we are compiling in the kernel or userspace
#ifdef __KERNEL__
#include <linux/ioctl.h> /* _IO, _IOR, _IOW */
#else
#include <sys/ioctl.h>
#endif

/* Magic number for ioctl commands */
#define PROXY_IOC_MAGIC 'P'

/* Struct definitions for ioctl ops */
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

struct proxy_stats {
	__u64 requests;
	__u64 responses;
	__u64 errors;
	__u64 reserved[5]; /* reserved for future expansion */
};

/* enum for proxy_answer_base type */
enum proxy_io_answer_type {
	PROXY_IO_ANSWER_WRITE = 0,
	PROXY_IO_ANSWER_READ,
};

struct proxy_answer_base {
	__u8 type;

	/* A packet slot to ACK or NACK, currently used with a slot number
	 * to verify that userspace didn't do this by mistake.
	 * It **MIGHT** be removed if deemed unnecessary later on...
	 */
	__u64 seq_num;

	__u64 slot_num;
};

struct proxy_ack {
	struct proxy_answer_base base;

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
	struct proxy_answer_base base;

	__u16 positive_errno;
};

struct proxy_register_eventfd {
	__u8 type;
	__u32 slot_idx;
	int fd;
};

struct proxy_unregister_eventfd {
	__u8 type;
	__u32 slot_idx;
};

/* ioctl commands */
#define PROXY_IOC_GET_SHM_INFO _IOR(PROXY_IOC_MAGIC, 0, struct proxy_shm_info)
#define PROXY_IOC_GET_STATS _IOR(PROXY_IOC_MAGIC, 1, struct proxy_stats)
#define PROXY_IOC_GET_MTD_INFO _IOR(PROXY_IOC_MAGIC, 2, struct proxy_mtd_info)
#define PROXY_IOC_GET_QUEUE_INFO                                               \
	_IOR(PROXY_IOC_MAGIC, 3, struct proxy_shm_queue_info)
#define PROXY_IOC_ACK _IOW(PROXY_IOC_MAGIC, 4, struct proxy_ack)
#define PROXY_IOC_NACK _IOW(PROXY_IOC_MAGIC, 5, struct proxy_nack)
#define PROXY_IOC_REGISTER_EVENTFD                                             \
	_IOW(PROXY_IOC_MAGIC, 7, struct proxy_register_eventfd)
#define PROXY_IOC_UNREGISTER_EVENTFD                                           \
	_IOW(PROXY_IOC_MAGIC, 8, struct proxy_unregister_eventfd)

#endif
