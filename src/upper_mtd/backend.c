/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/spinand.h>

#include "upper_mtd/backend.h"

int open_backend_mtd_device(struct mtd_info **mtd_ptr_in_list, uint mtd_index)
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

void put_backend_mtd_devices(struct mtd_info **mtd_list, size_t max_index)
{
	for (size_t i = 0; i < max_index; i++)
		put_mtd_device(mtd_list[i]);
}
