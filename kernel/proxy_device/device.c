/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/mutex.h>

#include "proxy_device/chrdev.h"
#include "proxy_device/io.h"
#include "proxy_device/shm.h"

int proxy_device_create(struct ufedm_proxy_device *dev)
{
	int ret;

	mutex_init(&dev->shm_mapping.lock);

	ret = proxy_device_init_shared_memory(&dev->shm_mapping);
	if (ret != 0)
		return ret;

	ret = init_async_io_workers(dev);
	if (ret < 0)
		goto error_init_async_io_workers;

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
	destroy_async_io_workers(dev);

error_init_async_io_workers:
	proxy_device_destroy_shared_memory(&dev->shm_mapping);

	return ret;
}

void proxy_device_destroy(struct ufedm_proxy_device *dev)
{
	// We don't drop a refcount to the backend_dev pointer of a backing
	// MTD device here, we allow the module to do this later on during
	// removal.

	device_destroy(dev->device_class, dev->devno);
	proxy_chrdev_destory(dev);

	destroy_async_io_workers(dev);

	proxy_device_destroy_shared_memory(&dev->shm_mapping);
}
