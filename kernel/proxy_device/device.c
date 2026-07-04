/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "proxy_device/chrdev.h"

static void revoke_shmem_mapping(struct ufedm_proxy_device *dev)
{
	mutex_lock(&dev->shmem_lock);

	if (dev->shmem_revoked) {
		mutex_unlock(&dev->shmem_lock);
		return;
	}

	dev->shmem_revoked = true;
	unmap_mapping_range(dev->shmem_file->f_mapping, 0, 0, 1);

	mutex_unlock(&dev->shmem_lock);
}

static int create_shmem_mapping(struct ufedm_proxy_device *dev)
{
	dev->shmem_file = shmem_kernel_file_setup(
	    "ufedm_ringbuffer", PAGE_ALIGN(get_shm_region_size(&dev->info)), 0);

	if (IS_ERR(dev->shmem_file))
		return PTR_ERR(dev->shmem_file);

	return 0;
}

static void destroy_shmem_mapping(struct ufedm_proxy_device *dev)
{
	fput(dev->shmem_file);
	dev->shmem_file = NULL;
}

int proxy_device_create(struct ufedm_proxy_device *dev)
{
	int ret;

	mutex_init(&dev->shmem_lock);

	ret = create_shmem_mapping(dev);
	if (ret)
		goto error_create_shmem_mapping;

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
	destroy_shmem_mapping(dev);

error_create_shmem_mapping:
	return ret;
}

void proxy_device_destroy(struct ufedm_proxy_device *dev)
{
	// We don't drop a refcount to the backend_dev pointer of a backing
	// MTD device here, we allow the module to do this later on during
	// removal.

	device_destroy(dev->device_class, dev->devno);
	proxy_chrdev_destory(dev);

	revoke_shmem_mapping(dev);

	destroy_shmem_mapping(dev);
}
