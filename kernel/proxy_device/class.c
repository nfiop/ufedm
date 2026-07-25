/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/cdev.h>
#include <linux/fs.h>

#include <linux/mtd/rawnand.h>
#include <linux/mtd/spinand.h>

#include "backing_mtd/device.h"
#include "defs.h"
#include "proxy_device/class.h"

struct prox_dev_class {
	struct class *device_class;
	struct ufedm_proxy_device *devs;
	size_t count;
	dev_t devno;
};

static struct prox_dev_class s_prox_dev_class;

struct ufedm_proxy_device *proxy_device_resolve_by_minor(size_t minor)
{
	// Minor number is simply an index to the device we want
	// so we don't anything sophisticated here.
	// This is all guaranteed by the creation seqeunce in the
	// add_devices function.
	if (minor >= s_prox_dev_class.count)
		return NULL;
	return &s_prox_dev_class.devs[minor];
}

static void remove_devices(struct prox_dev_class *dev_class, int max_idx)
{
	for (int idx = 0; idx < max_idx; idx++) {
		proxy_device_destroy(&dev_class->devs[idx]);
	}
}

static int add_devices(struct prox_dev_class *dev_class, int *max_idx)
{
	int major;
	int ret;
	struct nand_device *nanddev;
	struct mtd_info *backing_mtd;

	*max_idx = 0;
	major = MAJOR(dev_class->devno);
	for (; *max_idx < dev_class->count; ++*max_idx) {
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

		struct ufedm_proxy_device *dev = &dev_class->devs[*max_idx];
		dev->backend_dev = backing_mtd;
		dev->devno = MKDEV(major, *max_idx);
		dev->device_class = dev_class->device_class;

		/* This call is guaranteed to be safe. We checked
		 * that we deal with a NAND device before.
		 */
		nanddev = mtd_to_nanddev(backing_mtd);

		/* Data bytes per page, OOB size not included */
		dev->page_data_size = nanddev_page_size(nanddev);

		dev->page_oob_size = nanddev_per_page_oobsize(nanddev);

		ret = proxy_device_create(dev);
		if (ret != 0)
			return ret;
	}
	return 0;
}

static int alloc_array(struct prox_dev_class *dev_class)
{
	// Protect against 0 - which is invalid.
	if (dev_class->count == 0)
		return -EINVAL;

	dev_class->devs = kvzalloc(
	    dev_class->count * sizeof(struct ufedm_proxy_device), GFP_KERNEL);

	if (!dev_class->devs)
		return -ENOMEM;

	return 0;
}

static int proxy_device_class_create_devices(struct prox_dev_class *dev_class)
{
	int device_idx;
	int ret;
	ret = alloc_chrdev_region(
	    &dev_class->devno, 0, dev_class->count, PROXY_DEVICE_NAME);
	if (ret != 0)
		goto failed_chrdev_region_alloc;

	pr_info("ufedm: registered major=%d\n", MAJOR(dev_class->devno));

	ret = add_devices(dev_class, &device_idx);
	if (ret != 0)
		goto error_create_devices;

	return 0;

error_create_devices:
	remove_devices(dev_class, device_idx);
failed_chrdev_region_alloc:
	return ret; // non-zero means failure
}

int proxy_device_class_init(size_t dev_count)
{
	int ret;

	s_prox_dev_class.device_class = class_create("ufedm_proxy");
	if (IS_ERR(s_prox_dev_class.device_class))
		return PTR_ERR(s_prox_dev_class.device_class);

	s_prox_dev_class.count = dev_count;
	ret = alloc_array(&s_prox_dev_class);
	if (ret != 0)
		goto failed_allocating_array;

	ret = proxy_device_class_create_devices(&s_prox_dev_class);
	if (ret != 0)
		goto failed_creating_devices;

	return 0;

failed_creating_devices:
	kvfree(s_prox_dev_class.devs);
failed_allocating_array:
	return ret;
}

void proxy_device_class_exit(void)
{
	// We do these in revese to proxy_device_class_create_devices flow
	remove_devices(&s_prox_dev_class, s_prox_dev_class.count);
	unregister_chrdev_region(s_prox_dev_class.devno, s_prox_dev_class.count);

	kvfree(s_prox_dev_class.devs); // free the allocated array for all devices
	class_destroy(s_prox_dev_class.device_class);
}
