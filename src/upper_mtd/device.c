/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include "upper_mtd/backend.h"
#include "upper_mtd/device.h"

static struct mtd_info **s_backend_mtds;
static struct upper_mtd_device *s_upper_mtds;
static size_t s_mtds_count = 0;

static int upper_read(
    struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	WARN_ON(mtd->priv == NULL);
	struct upper_mtd_device *dev = mtd->priv;
	return mtd_read(dev->backend, from, len, retlen, buf);
}

static int upper_write(struct mtd_info *mtd, loff_t to, size_t len,
    size_t *retlen, const u_char *buf)
{
	WARN_ON(mtd->priv == NULL);
	struct upper_mtd_device *dev = mtd->priv;
	return mtd_write(dev->backend, to, len, retlen, buf);
}

static int upper_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	WARN_ON(mtd->priv == NULL);
	struct upper_mtd_device *dev = mtd->priv;
	return mtd_erase(dev->backend, instr);
}

static int create_device(struct upper_mtd_device *dev, struct mtd_info *backend)
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

	/* Forward operations */
	dev->upper->_read = upper_read;
	dev->upper->_write = upper_write;
	dev->upper->_erase = upper_erase;

	dev->upper->priv = dev->upper;

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

	for (i = 0; i < count; i++) {
		ret = create_device(&devs[i], backends[i]);
		if (ret != 0)
			goto error_create_device;
		devs[i].backend = backends[i];
	}

	return 0;

error_create_device:
	destroy_devices(devs, i);
	return ret;
}

static void put_backend_mtd_devices(
    struct mtd_info **mtd_list, size_t max_index)
{
	for (size_t i = 0; i < max_index; i++)
		put_mtd_device(mtd_list[i]);
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
