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
#include "proxy_device/class.h"
#include "proxy_device/eventfd.h"
#include "proxy_device/io.h"
#include "proxy_ioctl.h"

#include "shm_packet.h"

static int proxy_chrdev_open(struct inode *inode, struct file *filp)
{
	dev_t dev = inode->i_rdev;
	int minor = MINOR(dev);

	struct ufedm_proxy_device *prox_dev =
	    proxy_device_resolve_by_minor(minor);
	if (!prox_dev)
		return -ENODEV;

	mutex_lock(&prox_dev->shm_mapping.lock);

	if (prox_dev->shm_mapping.revoked) {
		mutex_unlock(&prox_dev->shm_mapping.lock);
		return -EIO;
	}

	mutex_unlock(&prox_dev->shm_mapping.lock);

	// Don't allow opening more than once, as we can't really
	// handle multiple clients anyway.
	if (atomic_cmpxchg(&prox_dev->already_open, 0, 1)) {
		return -EBUSY;
	}

	filp->private_data = prox_dev;
	return 0;
}

static int proxy_chrdev_release(struct inode *inode, struct file *filp)
{
	struct ufedm_proxy_device *prox_dev = filp->private_data;
	atomic_set(&prox_dev->already_open, 0);
	return 0;
}

static int proxy_chrdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret;
	struct ufedm_proxy_device *dev = filp->private_data;
	u64 offset = (u64)vma->vm_pgoff << PAGE_SHIFT;
	u64 len = vma->vm_end - vma->vm_start;

	u64 max_size = PAGE_ALIGN(get_shm_region_size(&dev->shm_info));

	if (offset + len > max_size) {
		pr_warn_ratelimited("ufedm_proxy: failed to mmap, (off %llu + "
				    "len %llu) > max_size %llu\n",
		    offset, len, max_size);
		return -EINVAL;
	}

	/* We only support MAP_SHARED semantics. MAP_PRIVATE will require
	 * each process to hold its own pages and mapping, therefore not
	 * allowing the driver to eventually revoke the mappings from any
	 * process that has a memory mapping on this device.
	 */
	if (!(vma->vm_flags & VM_SHARED)) {
		pr_warn_ratelimited(
		    "ufedm_proxy: failed to mmap, only MAP_SHARED allowed\n");
		return -EINVAL;
	}

	mutex_lock(&dev->shm_mapping.lock);

	/*
	 * CRITICAL RACE PROTECTION HERE:
	 * Prevent mmap after revoke (during device removal & module
	 * teardown)
	 */
	if (dev->shm_mapping.revoked) {
		mutex_unlock(&dev->shm_mapping.lock);
		return -EIO;
	}

	/*
	 * Delegate to shmem file mapping f_op function now.
	 * This ensures proper address_space + MM tracking!
	 */
	ret = dev->shm_mapping.filp->f_op->mmap(dev->shm_mapping.filp, vma);

	mutex_unlock(&dev->shm_mapping.lock);
	return ret;
}

static long proxy_chrdev_ioctl(
    struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret;
	struct ufedm_proxy_device *prox_dev = filp->private_data;
	struct mtd_info *backend;

	WARN_ON(prox_dev == NULL);
	if (prox_dev == NULL)
		return -EIO;

	backend = prox_dev->backend_dev;

	switch (cmd) {
	case PROXY_IOC_GET_STATS: {
		if (copy_to_user((void __user *)arg, &prox_dev->stats,
			sizeof(struct proxy_stats))) {
			ret = -EFAULT;
			goto exit;
		}

		ret = 0;
		goto exit;
	}
	case PROXY_IOC_GET_SHM_INFO: {
		if (copy_to_user((void __user *)arg, &prox_dev->shm_info,
			sizeof(struct proxy_shm_info))) {
			ret = -EFAULT;
			goto exit;
		}

		ret = 0;
		goto exit;
	}

	case PROXY_IOC_GET_MTD_INFO: {
		struct proxy_mtd_info tmp;
		tmp.backend_mtd_index = backend->index;
		tmp.flash_page_size = backend->writesize + backend->oobsize;
		tmp.flash_oob_size = backend->oobsize;
		tmp.flash_pages_per_sector_cnt = tmp.flash_erase_sector_size =
		    backend->erasesize;
		memset(tmp.reserved, 0, sizeof(__u32) * 6);
		if (copy_to_user((void __user *)arg, &tmp, sizeof(tmp))) {
			ret = -EINVAL;
			goto exit;
		}

		ret = 0;
		goto exit;
	}

	case PROXY_IOC_ACK: {
		struct proxy_ack tmp;
		if (copy_from_user(
			&tmp, (int __user *)arg, sizeof(struct proxy_ack)))
			return -EFAULT;

		ret = proxy_device_ack_request(prox_dev, &tmp);
		goto exit;
	}

	case PROXY_IOC_NACK: {
		struct proxy_nack tmp;
		if (copy_from_user(
			&tmp, (int __user *)arg, sizeof(struct proxy_nack)))
			return -EFAULT;

		ret = proxy_device_nack_request(prox_dev, &tmp);
		goto exit;
	}

	case PROXY_IOC_REGISTER_EVENTFD: {
		struct proxy_register_eventfd tmp;
		if (copy_from_user(&tmp, (int __user *)arg,
			sizeof(struct proxy_register_eventfd)))
			return -EFAULT;
		struct protected_eventfd_ctx *ctx = NULL;

		if (tmp.type != PROXY_EVENTFD_TYPE_WRITE &&
		    tmp.type != PROXY_EVENTFD_TYPE_READ)
			return -EINVAL;

		ctx = proxy_eventfd_ctx_based_on_type_and_slot(
		    prox_dev, tmp.type, tmp.slot_idx);

		if (ctx == NULL) {
			ret = -EINVAL;
			goto exit;
		}

		ret = proxy_eventfd_ctx_register(ctx, tmp.fd);
		goto exit;
	}

	case PROXY_IOC_UNREGISTER_EVENTFD: {
		struct proxy_unregister_eventfd tmp;
		if (copy_from_user(&tmp, (int __user *)arg,
			sizeof(struct proxy_unregister_eventfd)))
			return -EFAULT;
		struct protected_eventfd_ctx *ctx = NULL;

		if (tmp.type != PROXY_EVENTFD_TYPE_WRITE &&
		    tmp.type != PROXY_EVENTFD_TYPE_READ)
			return -EINVAL;

		ctx = proxy_eventfd_ctx_based_on_type_and_slot(
		    prox_dev, tmp.type, tmp.slot_idx);

		if (ctx == NULL) {
			ret = -EINVAL;
			goto exit;
		}

		proxy_eventfd_ctx_unregister(ctx);
		ret = 0;
		goto exit;
	}

	default:
		ret = -EINVAL;
		goto exit;
	}

exit:
	return ret;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = proxy_chrdev_open,
    .release = proxy_chrdev_release,
    .mmap = proxy_chrdev_mmap,
    .unlocked_ioctl = proxy_chrdev_ioctl,
};

int proxy_chrdev_create(dev_t devno, struct ufedm_proxy_device *dev)
{
	int ret;

	cdev_init(&dev->ring_cdev, &fops);
	dev->ring_cdev.owner = THIS_MODULE;

	ret = cdev_add(&dev->ring_cdev, devno, 1);
	if (ret != 0)
		goto error_cdev_add;

	return 0;

error_cdev_add:
	return ret;
}

void proxy_chrdev_destory(struct ufedm_proxy_device *dev)
{
	cdev_del(&dev->ring_cdev);
}
