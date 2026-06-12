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
#include "proxy_device/class.h"

static int proxy_chrdev_open(struct inode *inode, struct file *filp)
{
	int ret;
	dev_t dev = inode->i_rdev;
	int minor = MINOR(dev);

	struct ufedm_proxy_device *prox_dev =
	    proxy_device_resolve_by_minor(minor);
	if (!prox_dev)
		return -ENODEV;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	// Don't allow opening more than once, as we can't really
	// handle multiple clients anyway.
	if (atomic_cmpxchg(&prox_dev->already_open, 0, 1)) {
		ret = -EBUSY;
		goto exit_error;
	}

	filp->private_data = prox_dev;
	return 0;

exit_error:
	module_put(THIS_MODULE);
	return ret;
}

static int proxy_chrdev_release(struct inode *inode, struct file *filp)
{
	struct ufedm_proxy_device *prox_dev = filp->private_data;
	atomic_set(&prox_dev->already_open, 0);
	module_put(THIS_MODULE);
	return 0;
}

static int proxy_chrdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct ufedm_proxy_device *prox_dev = filp->private_data;

	unsigned long size = vma->vm_end - vma->vm_start;

	if (size > sizeof(struct shared_region))
		return -EINVAL;

	return remap_vmalloc_range(vma, prox_dev->shared, 0);
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = proxy_chrdev_open,
    .release = proxy_chrdev_release,
    .mmap = proxy_chrdev_mmap,
};

int proxy_chrdev_create(dev_t devno, struct ufedm_proxy_device *dev)
{
	int ret;

	cdev_init(&dev->ring_cdev, &fops);
	dev->ring_cdev.owner = THIS_MODULE;

	ret = cdev_add(&dev->ring_cdev, devno, 1);
	if (ret)
		goto error_cdev_add;

	return 0;

error_cdev_add:
	return ret;
}

void proxy_chrdev_destory(struct ufedm_proxy_device *dev)
{
	cdev_del(&dev->ring_cdev);
}
