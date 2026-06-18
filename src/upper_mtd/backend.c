/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mm.h>

#include "upper_mtd/backend.h"

int open_backend_mtd_device(struct mtd_info **mtd_ptr_in_list, uint mtd_index)
{
	*mtd_ptr_in_list = get_mtd_device(NULL, mtd_index);
	if (IS_ERR(*mtd_ptr_in_list)) {
		pr_err("ufedm: failed to open mtd%d\n", mtd_index);
		return PTR_ERR(*mtd_ptr_in_list);
	}

	struct mtd_info *mtd = *mtd_ptr_in_list;
	if (mtd->type != MTD_NANDFLASH) {
		pr_err(
		    "ufedm: failed to open mtd%d, is not a NAND flash chip!\n",
		    mtd_index);
		put_mtd_device(mtd);
		return -EINVAL;
	}

	pr_info("ufedm: opened mtd%d (%s)\n", mtd->index, mtd->name);
	return 0;
}

void put_backend_mtd_devices(struct mtd_info **mtd_list, size_t max_index)
{
	for (size_t i = 0; i < max_index; i++)
		put_mtd_device(mtd_list[i]);
}
