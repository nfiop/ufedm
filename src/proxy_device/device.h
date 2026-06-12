/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __PROXY_DEVICE_
#define __PROXY_DEVICE_

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/types.h>

#include "shared_rings.h"

#define PROXY_DEVICE_NAME "ufedm_proxy"

struct ufedm_proxy_device {
	struct device *device;
	struct class *device_class;

	struct cdev ring_cdev;
	dev_t devno;

	atomic_t already_open;

	struct shared_region *shared;
};

int proxy_device_create(struct ufedm_proxy_device *dev);
void proxy_device_destroy(struct ufedm_proxy_device *dev);

#endif
