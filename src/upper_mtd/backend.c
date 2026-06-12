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

	pr_info("ufedm: opened mtd%d (%s)\n", mtd->index, mtd->name);
	return 0;
}
