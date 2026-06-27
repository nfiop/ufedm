/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include "backing_mtd/device.h"
#include "proxy_device/class.h"
#include "proxy_device/device.h"
#include "upper_mtd/device.h"

static struct upper_mtd_device *s_upper_mtds;

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

	/* RAW mode is dangerous and eliminates
	 * any safe guard of this module. Don't allow it, the
	 * user can just use the backing MTD device in RAW mode.
	 * This should prevent a disaster waiting from happening due
	 * malfunctioning filesystem or userspace program.
	 *
	 * And yes, I know this can be a read function we validate, but
	 * goddammit if someone wants to use this in RAW mode, this is still
	 * invalid.
	 *
	 * PLACE_OOB mode is dangerous as well. It is less dangerous than
	 * RAW mode, because the user still relies on our driver to put
	 * ECC & other important metadata on the NAND flash chip, but it
	 * can do quite a bit of damage if used improperly.
	 *
	 * Besides that, ChatGPT says that PLACE_OOB mode is used by
	 * bootloaders and raw flash writers to sometimes write a specific
	 * bad block markers or put boot metadata at specific OOB offsets.
	 * However, the entire point of this module is to give userspace
	 * the possibility of intervention with ECC & layout to essentially
	 * implement a raw flash writer/reader, based on a known & managed
	 * policy and not allowing random userspace programs to do whatever
	 * they want in that regard.
	 *
	 * If the user needs to do this anyway, they can probably just open
	 * the _original_ backing MTD device and invoke their program on it.
	 */
	if (ops->mode != MTD_OPS_AUTO_OOB) {
		pr_warn_ratelimited("%s: Only MTD_OPS_AUTO_OOB mode access is "
				    "allowed by this device.\n",
		    mtd->name);
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

	dev->backend = backend;
	if (dev->backend == NULL) {
		return -EINVAL;
	}

	/* Basic identity */
	dev->upper->name = "upper-mtd";
	dev->upper->type = backend->type;
	dev->upper->flags = backend->flags;
	dev->upper->size = backend->size;
	dev->upper->erasesize = backend->erasesize;
	dev->upper->writesize = backend->writesize;

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

static int create_upper_devices(struct upper_mtd_device *devs, size_t count)
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

		// We send a `get_backend_mtd_device(i)` and rely on the fact
		// that function will check if it's a NULL pointer.
		ret = create_device(
		    &devs[i], get_backend_mtd_device(i), proxy_dev);
		if (ret != 0)
			goto error_create_device;
	}

	return 0;

error_create_device:
	destroy_devices(devs, i);
	return ret;
}

int upper_mtd_initialize_devices(size_t count)
{
	s_upper_mtds = kvzalloc(sizeof(struct mtd_info) * count, GFP_KERNEL);
	if (!s_upper_mtds) {
		return -ENOMEM;
	};

	return create_upper_devices(s_upper_mtds, count);
}

void upper_mtd_destroy_devices(size_t count)
{
	destroy_devices(s_upper_mtds, count);
}
