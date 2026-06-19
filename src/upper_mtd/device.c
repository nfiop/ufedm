/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include "proxy_device/class.h"
#include "proxy_device/device.h"
#include "upper_mtd/backend.h"
#include "upper_mtd/device.h"

static struct mtd_info **s_backend_mtds;
static struct upper_mtd_device *s_upper_mtds;
static size_t s_mtds_count = 0;

static int upper_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	WARN_ON(mtd->priv == NULL);
	struct upper_mtd_device *dev = mtd->priv;
	return mtd_erase(dev->backend, instr);
}

static int ensure_safe_environment(struct mtd_info *mtd, struct mtd_oob_ops *ops,
	struct ufedm_proxy_device **proxy_dev_ptr,
	struct upper_mtd_device **dev_ptr)
{
	WARN_ON_ONCE(mtd->priv == NULL);
	if (mtd->priv == NULL)
		return -EIO;

	if (ops->mode == MTD_OPS_RAW) {
		/* RAW mode is dangerous and eliminates 
		 * any safe guard of this module. Don't allow it, the
		 * user can just use the backing MTD device in RAW mode.
		 * This should prevent a disaster waiting from happening due
		 * malfunctioning filesystem or userspace program.
		 *
		 * And yes, I know this can be a read function we validate, but goddammit
		 * if someone wants to use this in RAW mode, this is still invalid.
		*/
		pr_warn_ratelimited("%s: RAW mode access is denied by this device.\n", mtd->name);
		return -EOPNOTSUPP;
    }

	*dev_ptr = mtd->priv;

	*proxy_dev_ptr = (*dev_ptr)->proxy_dev;
	if (!*proxy_dev_ptr)
		return -EAGAIN;

	return 0;
}

static int upper_read_oob(struct mtd_info *mtd, loff_t from,
                              struct mtd_oob_ops *ops)
{
	int ret;
	struct upper_mtd_device *dev;
	struct ufedm_proxy_device *proxy_dev;

	ret = ensure_safe_environment(mtd, ops, &proxy_dev, &dev);
	if (ret != 0)
		return ret;

    // TODO: Change this when we actually implement the mechanism.
    return -ENOTSUPP;
}

static int upper_write_oob(struct mtd_info *mtd, loff_t to,
                               struct mtd_oob_ops *ops)
{
	int ret;
	struct upper_mtd_device *dev;
	struct ufedm_proxy_device *proxy_dev;

	ret = ensure_safe_environment(mtd, ops, &proxy_dev, &dev);
	if (ret != 0)
		return ret;

	// TODO: Change this when we actually implement the mechanism.
    return -ENOTSUPP;
}

static int create_device(struct upper_mtd_device *dev, struct mtd_info *backend, struct ufedm_proxy_device *proxy_dev)
{
	int ret;

	dev->upper = kvzalloc(sizeof(struct mtd_info), GFP_KERNEL);
	if (!dev->upper) {
		return -ENOMEM;
	}

	/* Basic identity */
	dev->upper->name = "upper-mtd";
	dev->upper->type = backend->type;
	dev->upper->flags = backend->flags;
	dev->upper->size = backend->size;
	dev->upper->erasesize = backend->erasesize;
	dev->upper->writesize = backend->writesize;

	/* Use default mtd_{read,write} functions here */
	dev->upper->_read = mtd_read;
	dev->upper->_write = mtd_write;

	dev->upper->_erase = upper_erase;

	dev->upper->_write_oob = upper_write_oob;
    dev->upper->_read_oob  = upper_read_oob;

	dev->upper->priv = dev;

	// Connect a proxy_dev into our upper device
	// so it can deref it later on when doing I/O.
	dev->proxy_dev = proxy_dev;

	ret = mtd_device_register(dev->upper, NULL, 0);
	if (ret) {
		pr_err("ufedm: failed to register upper MTD\n");
		return ret;
	}

	return 0;
}

static void destroy_device(struct upper_mtd_device *dev)
{
	mtd_device_unregister(dev->upper);
	dev->proxy_dev = NULL;
	kvfree(dev->upper);
}

static void destroy_devices(struct upper_mtd_device *devs, size_t max_index)
{
	for (size_t idx = 0; idx < max_index; idx++)
		destroy_device(&devs[idx]);
}

static int create_upper_devices(
    struct upper_mtd_device *devs, struct mtd_info **backends, size_t count)
{
	size_t i;
	int ret;
	struct ufedm_proxy_device *proxy_dev;

	for (i = 0; i < count; i++) {
		proxy_dev = proxy_device_resolve_by_minor(i);
		WARN_ON(proxy_dev == NULL);
		/* There's nothing sane we can do besides just
		 * exiting with a failure.
		 */
		if (!proxy_dev)
			goto error_create_device;

		ret = create_device(&devs[i], backends[i], proxy_dev);
		if (ret != 0)
			goto error_create_device;
		devs[i].backend = backends[i];

		mutex_lock(&proxy_dev->backend_lock);
		// Connect a backing MTD device to the corresponding proxy
		// device so it can do full I/O work.
		proxy_dev->backend_dev = backends[i];
		mutex_unlock(&proxy_dev->backend_lock);
	}

	return 0;

error_create_device:
	destroy_devices(devs, i);
	return ret;
}

static int attach_backend_mtd_devices(
    struct mtd_info **mtd_list, uint *mtd_indices_list, size_t count)
{
	int ret;
	size_t i;

	WARN_ON(count == 0);

	for (i = 0; i < count; i++) {
		ret =
		    open_backend_mtd_device(&mtd_list[i], mtd_indices_list[i]);
		if (ret != 0)
			goto error_open_mtd_device;
	}

	return 0;

error_open_mtd_device:
	put_backend_mtd_devices(mtd_list, i);
	return ret;
}

int upper_mtd_initialize_devices(uint *mtd_indices_list, size_t count)
{
	int ret;

	s_backend_mtds =
	    kvzalloc(sizeof(struct mtd_info *) * count, GFP_KERNEL);
	if (!s_backend_mtds) {
		return -ENOMEM;
	};

	s_upper_mtds = kvzalloc(sizeof(struct mtd_info) * count, GFP_KERNEL);
	if (!s_upper_mtds) {
		return -ENOMEM;
	};

	s_mtds_count = count;
	ret = attach_backend_mtd_devices(
	    s_backend_mtds, mtd_indices_list, s_mtds_count);
	if (ret != 0)
		goto error_attach_backend_mtd_devices;

	ret = create_upper_devices(s_upper_mtds, s_backend_mtds, s_mtds_count);
	if (ret != 0)
		goto error_create_upper_devices;

	return 0;

error_create_upper_devices:
	put_backend_mtd_devices(s_backend_mtds, s_mtds_count);
error_attach_backend_mtd_devices:
	return ret;
}

void print_upper_to_backend_mtd_mapping(void)
{
	for (size_t i = 0; i < s_mtds_count; i++) {
		struct mtd_info *mtd = s_backend_mtds[i];
		pr_info("ufedm: upper mtd%d -> (backend %d, %s)\n", i,
		    mtd->index, mtd->name);
	}
}

void upper_mtd_destroy_devices(void)
{
	destroy_devices(s_upper_mtds, s_mtds_count);
	put_backend_mtd_devices(s_backend_mtds, s_mtds_count);
}
