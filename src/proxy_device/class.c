/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/cdev.h>
#include <linux/fs.h>

#include "backing_mtd/device.h"
#include "defs.h"
#include "proxy_device/class.h"

struct prox_dev_class {
	struct class *device_class;
	struct ufedm_proxy_device *dev_arr;
	size_t count;
	dev_t devno;
};

static struct prox_dev_class s_all_devs;

struct ufedm_proxy_device *proxy_device_resolve_by_minor(size_t minor)
{
	// Minor number is simply an index to the device we want
	// so we don't anything sophisticated here.
	// This is all guaranteed by the creation seqeunce in the
	// add_devices function.
	if (minor >= s_all_devs.count)
		return NULL;
	return &s_all_devs.dev_arr[minor];
}

static void remove_devices(struct prox_dev_class *devs, int max_idx)
{
	for (int idx = 0; idx < max_idx; idx++) {
		proxy_device_destroy(&devs->dev_arr[idx]);
	}
}

static int add_devices(struct prox_dev_class *devs, int *max_idx)
{
	int major;
	int ret;
	struct mtd_info *backing_mtd;

	*max_idx = 0;
	major = MAJOR(devs->devno);
	for (; *max_idx < devs->count; ++*max_idx) {
		// Find a backing MTD device to the corresponding proxy
		// device so it can do full I/O work.
		// If we fail here, it's probably a bug - log it and don't
		// continue.
		//
		// We don't manage the refcount of the backing MTD device here!
		// Instead, we get a refcount when getting a struct mtd_info
		// for the backing MTD device beforehand, and during removal
		// of the module we put the refcount.
		backing_mtd = get_backend_mtd_device(*max_idx);
		WARN_ON(backing_mtd == NULL);
		if (backing_mtd == NULL) {
			return -EINVAL;
		}

		struct ufedm_proxy_device *dev = &devs->dev_arr[*max_idx];
		dev->backend_dev = backing_mtd;
		dev->devno = MKDEV(major, *max_idx);
		dev->device_class = devs->device_class;

		ret = proxy_device_create(dev);
		if (ret != 0)
			return ret;
	}
	return 0;
}

static int alloc_array(struct prox_dev_class *devs)
{
	// Protect against invalid numbers right here.
	if (devs->count == 0 || devs->count > PROXY_MAX_DEVICE_COUNT)
		return -EINVAL;

	devs->dev_arr = kvzalloc(
	    devs->count * sizeof(struct ufedm_proxy_device), GFP_KERNEL);

	if (!devs->dev_arr)
		return -ENOMEM;

	return 0;
}

static int proxy_device_class_create_devices(struct prox_dev_class *devs)
{
	int device_idx;
	int ret;
	ret = alloc_chrdev_region(
	    &devs->devno, 0, PROXY_MAX_DEVICE_COUNT, PROXY_DEVICE_NAME);
	if (ret != 0)
		goto failed_chrdev_region_alloc;

	pr_info("ufedm: registered major=%d\n", MAJOR(devs->devno));

	ret = add_devices(devs, &device_idx);
	if (ret != 0)
		goto error_create_devices;

	return 0;

error_create_devices:
	remove_devices(devs, device_idx);
failed_chrdev_region_alloc:
	return ret; // non-zero means failure
}

int proxy_device_class_init(size_t dev_count)
{
	int ret;

	s_all_devs.device_class = class_create("ufedm_proxy");
	if (IS_ERR(s_all_devs.device_class))
		return PTR_ERR(s_all_devs.device_class);

	s_all_devs.count = dev_count;
	ret = alloc_array(&s_all_devs);
	if (ret != 0)
		goto failed_allocating_array;

	ret = proxy_device_class_create_devices(&s_all_devs);
	if (ret != 0)
		goto failed_creating_devices;

	return 0;

failed_creating_devices:
	kvfree(s_all_devs.dev_arr);
failed_allocating_array:
	return ret;
}

void proxy_device_class_exit(void)
{
	// We do these in revese to proxy_device_class_create_devices flow
	remove_devices(&s_all_devs, s_all_devs.count);
	unregister_chrdev_region(s_all_devs.devno, s_all_devs.count);

	kvfree(s_all_devs.dev_arr); // free the allocated array for all devices
	class_destroy(s_all_devs.device_class);
}
