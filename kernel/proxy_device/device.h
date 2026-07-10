/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __PROXY_DEVICE_
#define __PROXY_DEVICE_

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/mtd/mtd.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "proxy_ioctl.h"
#include "shared_mem.h"
#include "shm_packet.h"

#define PROXY_DEVICE_NAME "ufedm_proxy"

/* When initializing the struct, we don't have any eventfd
 * pointer being registered at all. Each pointer is protected
 * by its spinlock.
 *
 * It **SHOULD** be clear that the process does not have to register
 * any eventfd at all - the process volunteers and it can skip it
 * safely. The consequences are repeated polling for incoming events.
 * Whether it's bad or not, is up to a discussion and performance
 * testing & further evaluation.
 *
 * If the process exits, we still retain an eventfd context.
 * This is completely harmless in terms of system stability.
 * When a new process tries to register a new eventfd, it will just
 * replace the old one, regardless of whether anything actually used
 * it or not.
 *
 * Userspace **SHOULD** absolutely not write to eventfd counter if
 * it uses it for tracking the real amount of incoming events.
 * The kernel is not responsible for fixing the value, and will only
 * increment the counter per the incoming events' count.
 * In other words, an eventfd file is used ideally in poll/epoll and
 * the read syscall as well.
 */

struct protected_eventfd_ctx {
	spinlock_t lock;
	struct eventfd_ctx *efd;
};

enum proxy_io_slot_state {
	PROXY_IO_SLOT_STATE_UNALLOCATED = 0,
	PROXY_IO_SLOT_STATE_ALLOCATED,
	PROXY_IO_SLOT_STATE_PENDING_USER,
};

struct proxy_requests_queue;

/* This struct is our "source of truth" for everything related
 * to I/O requests.
 *
 * Each allocated slot represents an on-going I/O transaction (could be
 * multiple NAND pages) on the shared memory interface.
 * For that reason, we keep the flags, counters and any metadata
 * here as well, because the shared memory interface can get corrupted
 * or modified horribly wrong by userspace.
 *
 * In other words, we keep a way to sanitize input upon NACK/ACK for a
 * request. We **DO NOT** keep any data here - we trust userspace to do
 * its best in that regard, and pass it on to the backing MTD device
 * as-is.
 * A `struct shm_pkt_hdr` is a duplicate of what is initially created
 * during the initialization of a new packet on an allocated slot.
 */
struct proxy_io_slot {
	/* This field is an optimization technique for the watchdog thread.
	 * It is supposed to be used by that thread to easily construct a
	 * a proxy_nack struct, as part of O(1) path instead of O(n) search
	 * path for the slot index in the actual array.
	 * We set this index once when creating the array, and never write to
	 * it again - it's supposed to be used in the watchdog kthread for
	 * reading purposes only.
	 */
	size_t slot_idx;
	struct proxy_requests_queue *parentq;

	enum proxy_io_slot_state state;
	struct completion done;
	int status;

	ktime_t started_time_ms;

	/* "our source of truth" metadata structure - a duplicate of what
	 * is also copied into the shared memory interface.
	 */
	struct shm_pkt_hdr header;

	/* This field is similar to the header - it's a shadow buffer that
	 * is allocated during a READ operation for the sake of RAW reading
	 * and sending that buffer directly to the actual shared memory
	 * buffer.
	 */
	size_t shadow_data_and_oob_buf_size;
	void *shadow_data_and_oob_buf;

	/* When we set up this I/O request, we add it to the allocated list
	 * so a watchdog kthread can NACK this if a timeout triggers while
	 * it's pending.
	 */
	struct list_head allocated_node;

	/* This will be used by the kernel to signal incoming packet */
	struct protected_eventfd_ctx efd;
};

typedef size_t shared_mem_ordered_idx_t;

struct ufedm_proxy_device;
struct proxy_requests_queue {
	unsigned long *allocated_bitmap;
	/* Each index in this array represents a slot on thw shared memory
	 * interface for a given packet.
	 */
	struct proxy_io_slot *req_pkt_slots;
	struct list_head allocated_requests;

	struct ufedm_proxy_device *parent_dev;
	shared_mem_ordered_idx_t shm_idx;

	size_t slot_timeout_ms;
	size_t watchdog_sleep_duration_ms;

	struct task_struct *watchdog;

	atomic64_t next_seq_id;

	/* This lock is used for:
	 * 1. Setting up a new proxy_io_slot struct up and adding it to the
	 * pending list.
	 * 2. When the watchdog kthread checks for which pending I/O requests
	 *    have timed out. In such case it behaves like a NACK from
	 * userspace.
	 * 3. When userspace NACK/ACKs an I/O request, so the I/O request is
	 * removed from the pending list.
	 *
	 * As for using a mutex - it's almost 100% guaranteed that we're allowed
	 * to sleep. I looked around the MTD subsystem and spinand_mtd_read
	 * function uses a mutex, so we do so too as an MTD device.
	 */
	struct mutex lock;
};

struct ufedm_shm_mapping {
	/* We allocate a shmem file to get a concise address_space mapping,
	 * which would be used when doing mmap() on this device for the
	 * shared memory buffer operation.
	 * This allows us to immediately revoke the mapping upon unloading
	 * of the module.
	 * A user program in such case would SEGFAULT due to a non-backing page
	 * fault.
	 */
	struct mutex lock;
	struct file *filp;
	bool revoked;

	/* This part of the struct is used as metadata for keeping a memory
	 * window for the kernel to submit the workload via the shm interface.
	 * It has nothing to do with userspace, so it's essentially a
	 * book-keeping for something already _initialized_ with the struct file
	 * pointer.
	 */
	struct page **pages;
	unsigned int nr_pages;
	void *kaddr;
};

struct ufedm_proxy_device {
	struct device *device;
	struct class *device_class;

	struct cdev ring_cdev;
	dev_t devno;

	atomic_t already_open;

	/* This is a pointer to the backing MTD device
	 * and not our own made-up device. This was protected by a mutex
	 * but after some architectural changes, we ensure we have a valid
	 * pointer upon creation of this device so we don't need it anymore.
	 */
	struct mtd_info *backend_dev;
	/* We cache the sizes for I/O operations. This is a small optimization
	 * that might not be necessary.
	 * We also assume no hotplug capabilities whatsoever.
	 * These values DO NOT (and should really not!) change until reloading
	 * of the module again.
	 */
	size_t page_oob_size;
	size_t page_data_size;

	struct proxy_stats stats;

	struct ufedm_shm_mapping shm_mapping;

	/* Used for ioctls and I/O requests - should not change once was
	 * set by the init path.
	 */
	struct proxy_shm_info info;

	struct proxy_requests_queue readq;
	struct proxy_requests_queue writeq;
};

int proxy_device_create(struct ufedm_proxy_device *dev);
void proxy_device_destroy(struct ufedm_proxy_device *dev);

#endif
