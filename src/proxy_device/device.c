/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "proxy_device/chrdev.h"

static void unmap_shared_region(struct ufedm_proxy_device *dev)
{
	kvfree(dev->shared);
}

static int map_shared_region(struct ufedm_proxy_device *dev)
{
	dev->shared = kvzalloc(sizeof(struct shared_region), GFP_KERNEL);

	if (!dev->shared)
		return -ENOMEM;

	return 0;
}

int proxy_device_create(struct ufedm_proxy_device *dev)
{
	int ret;
	ret = map_shared_region(dev);
	if (ret)
		goto error_map_shared_region;

	ret = proxy_chrdev_create(dev->devno, dev);
	if (ret != 0)
		goto error_proxy_chrdev_create;

	dev->device = device_create(dev->device_class, NULL, dev->devno, NULL,
	    "ufedm_proxy%d", MINOR(dev->devno));

	if (IS_ERR(dev->device)) {
		pr_err("ufedm: device_create failed\n");
		ret = PTR_ERR(dev->device);
		goto error_create_device;
	}

	atomic_set(&dev->already_open, 0);
	return 0;

error_create_device:
	proxy_chrdev_destory(dev);
error_proxy_chrdev_create:
	unmap_shared_region(dev);
error_map_shared_region:
	return ret;
}

void proxy_device_destroy(struct ufedm_proxy_device *dev)
{
	struct mtd_info* backend_dev;

	mutex_lock(&dev->backend_lock);
	backend_dev = dev->backend_dev;

	dev->backend_dev = NULL;

	if (backend_dev != NULL)
		put_mtd_device(backend_dev);

	mutex_unlock(&dev->backend_lock);

	device_destroy(dev->device_class, dev->devno);
	proxy_chrdev_destory(dev);
	unmap_shared_region(dev);
}
