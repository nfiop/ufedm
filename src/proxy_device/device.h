/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __PROXY_DEVICE_
#define __PROXY_DEVICE_

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/file.h>
#include <linux/mtd/mtd.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "proxy_ioctl.h"
#include "shared_mem.h"

#include "proxy_device/eventfd.h"

#define PROXY_DEVICE_NAME "ufedm_proxy"

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

	struct proxy_stats stats;

	/* We allocate a shmem file to get a concise address_space mapping,
	 * which would be used when doing mmap() on this device for the
	 * ring buffer operation.
	 */
	struct mutex shmem_lock;
	struct file *shmem_file;
	bool shmem_revoked;

	/* Used for ioctls and I/O requests - should not change once was
	 * set by the init path.
	 */
	struct proxy_shm_info info;

	struct protected_eventfd_ctx read_efd;
	struct protected_eventfd_ctx write_efd;
};

int proxy_device_create(struct ufedm_proxy_device *dev);
void proxy_device_destroy(struct ufedm_proxy_device *dev);

#endif
