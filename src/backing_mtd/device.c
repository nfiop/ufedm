/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/spinand.h>

#include "backing_mtd/device.h"

static struct mtd_info **s_backend_mtds;
extern size_t g_mtds_count;

struct mtd_info *get_backend_mtd_device(uint index)
{
	if (index >= g_mtds_count)
		return NULL;

	return s_backend_mtds[index];
}

static int open_backend_mtd_device(
    struct mtd_info **mtd_ptr_in_list, uint mtd_index)
{
	struct nand_device *nanddev;
	*mtd_ptr_in_list = get_mtd_device(NULL, mtd_index);
	if (IS_ERR(*mtd_ptr_in_list)) {
		pr_err("ufedm: failed to open mtd%d\n", mtd_index);
		return PTR_ERR(*mtd_ptr_in_list);
	}

	struct mtd_info *mtd = *mtd_ptr_in_list;
	if (!(mtd->type == MTD_NANDFLASH || mtd->type == MTD_MLCNANDFLASH)) {
		pr_err(
		    "ufedm: failed to open mtd%d, is not a NAND flash chip!\n",
		    mtd_index);
		put_mtd_device(mtd);
		return -EINVAL;
	}

	nanddev = mtd_to_nanddev(mtd);

	/*
	 * In kernel 6.16.8 we don't have a type variable for SPI NAND or
	 * raw flash chips.
	 * However, by looking at drivers/mtd/nand/raw/nand_base.c and
	 * drivers/mtd/nand/spi/core.c files we can see:
	 * ```c
	 * nand->ecc.defaults.engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;
	 * ```
	 * against -
	 * ```c
	 * nand->ecc.defaults.engine_type = NAND_ECC_ENGINE_TYPE_ON_DIE;
	 * ```
	 * respectively. And that's it. Nothing else sets it.
	 * So we can infer based the default ECC engine if we handle a SPI
	 * NAND flash chip or not.
	 */
	if (nanddev->ecc.defaults.engine_type == NAND_ECC_ENGINE_TYPE_ON_HOST) {
		struct nand_chip *chip = mtd_to_nand(mtd);
		/* We don't support NAND flash chips that need scrambling right
		 * now. In case you still need to do I/O on such chip through
		 * this module, consider re-compilation of the kernel for
		 * actually disabling this flag temporarily and then re-try.
		 */
		if ((chip->options & NAND_NEED_SCRAMBLING)) {
			pr_err("ufedm: failed to open mtd%d, requires "
			       "scrambling from the controller driver!\n",
			    mtd_index);
			put_mtd_device(mtd);
			return -EINVAL;
		}
	}

	pr_info("ufedm: opened mtd%d (%s)\n", mtd->index, mtd->name);
	return 0;
}

static void __put_backend_mtd_devices(
    struct mtd_info **mtd_list, size_t max_index)
{
	for (size_t i = 0; i < max_index; i++)
		put_mtd_device(mtd_list[i]);
}

static int attach_backend_mtd_devices(
    struct mtd_info **mtd_list, uint *mtd_minors_list, size_t count)
{
	int ret;
	size_t i;

	WARN_ON(count == 0);

	for (i = 0; i < count; i++) {
		ret = open_backend_mtd_device(&mtd_list[i], mtd_minors_list[i]);
		if (ret != 0)
			goto error_open_mtd_device;
	}

	return 0;

error_open_mtd_device:
	__put_backend_mtd_devices(mtd_list, i);
	return ret;
}

int locate_all_backend_mtds(uint *mtd_minors_list, size_t count)
{
	s_backend_mtds =
	    kvzalloc(sizeof(struct mtd_info *) * count, GFP_KERNEL);
	if (!s_backend_mtds) {
		return -ENOMEM;
	};

	return attach_backend_mtd_devices(
	    s_backend_mtds, mtd_minors_list, count);
}

void put_backend_mtd_devices(size_t count)
{
	__put_backend_mtd_devices(s_backend_mtds, count);
}
