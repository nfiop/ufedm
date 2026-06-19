/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __PROXY_DEVICE_
#define __PROXY_DEVICE_

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/mtd/mtd.h>
#include <linux/types.h>

#include "proxy_ioctl.h"
#include "shared_rings.h"

#define PROXY_DEVICE_NAME "ufedm_proxy"

struct ufedm_proxy_device {
	struct device *device;
	struct class *device_class;

	struct cdev ring_cdev;
	dev_t devno;

	atomic_t already_open;

	struct shared_region *shared;

	/* This is a pointer to the backing MTD device
	 * and not our own made-up device. This ensures
	 * that upon removal of the module, we always
	 * have a valid pointer in this struct.
	 * We ensure this by holding a refcount on it as well.
	 * NOTE: This pointer should be protected by backend_lock mutex
	 * for any case of accessing it.
	 */
	struct mtd_info *backend_dev;
	struct mutex backend_lock;

	struct proxy_stats stats;
};

int proxy_device_create(struct ufedm_proxy_device *dev);
void proxy_device_destroy(struct ufedm_proxy_device *dev);

#endif
