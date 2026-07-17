/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/mutex.h>

#include "proxy_device/chrdev.h"
#include "proxy_device/io.h"
#include "proxy_device/shm.h"

static void proxy_device_fill_shm_info(
    struct ufedm_proxy_device *dev, struct proxy_shm_info *p)
{
	p->proto_ver = 0;
	p->slot_size = sizeof(struct shared_mem_slot) + dev->page_data_size +
		       dev->page_oob_size;
	p->queues_count = PROXY_MAX_QUEUES_COUNT;

	memset(p->reserved, 0, sizeof(__u32) * 6);
}

static void proxy_device_set_total_shm_buf_size(
    struct ufedm_proxy_device *dev, struct proxy_shm_info *p)
{
	size_t queue_idx;
	p->total_buf_size = 0;

	for (queue_idx = 0; queue_idx < p->queues_count; queue_idx++) {
		p->total_buf_size +=
		    dev->queues[queue_idx].info.slots_count * p->slot_size;
	}
}

int proxy_device_create(struct ufedm_proxy_device *dev)
{
	int ret;

	mutex_init(&dev->shm_mapping.lock);

	proxy_device_fill_shm_info(dev, &dev->shm_info);

	ret = init_io_queues(dev);
	if (ret < 0)
		goto exit;

	// Now that we know the sizes of each queue, we can fill this
	// field safely.
	proxy_device_set_total_shm_buf_size(dev, &dev->shm_info);
	BUG_ON(dev->shm_info.total_buf_size == 0);

	ret = proxy_device_init_shared_memory(dev);
	if (ret != 0)
		goto revert_io_queues;

	ret = start_io_watchdog_threads(dev);
	if (ret != 0)
		goto delete_shared_memory;

	ret = proxy_chrdev_create(dev->devno, dev);
	if (ret != 0)
		goto stop_io_threads;

	dev->device = device_create(dev->device_class, NULL, dev->devno, NULL,
	    "ufedm_proxy%d", MINOR(dev->devno));

	if (IS_ERR(dev->device)) {
		pr_err("ufedm: device_create failed\n");
		ret = PTR_ERR(dev->device);
		goto delete_chrdev;
	}

	atomic_set(&dev->already_open, 0);
	return 0;

delete_chrdev:
	proxy_chrdev_destory(dev);

stop_io_threads:
	stop_io_watchdog_threads(dev);

delete_shared_memory:
	proxy_device_destroy_shared_memory(&dev->shm_mapping);

revert_io_queues:
	destroy_io_queues(dev);

exit:
	return ret;
}

void proxy_device_destroy(struct ufedm_proxy_device *dev)
{
	// We don't drop a refcount to the backend_dev pointer of a backing
	// MTD device here, we allow the module to do this later on during
	// removal.

	device_destroy(dev->device_class, dev->devno);
	proxy_chrdev_destory(dev);
	stop_io_watchdog_threads(dev);

	proxy_device_destroy_shared_memory(&dev->shm_mapping);
	destroy_io_queues(dev);
}
