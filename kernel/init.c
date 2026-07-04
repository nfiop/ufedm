/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "backing_mtd/device.h"
#include "defs.h"
#include "proxy_device/class.h"
#include "upper_mtd/device.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Liav A");
MODULE_DESCRIPTION("NAND Flash ECC & Data layout in userspace management");
MODULE_VERSION("0.1");

static uint mtds[PROXY_MAX_DEVICE_COUNT];
static int mtds_count;

// We declare a global value that can be used around the module.
// This is not the best practice, but we don't have a better option.
size_t g_mtds_count;

module_param_array(mtds, uint, &mtds_count, 0444);
MODULE_PARM_DESC(mtds, "Array of MTD device minor indices");

static int validate_mtds_list(void)
{
	int i, j;

	if (g_mtds_count == 0 || g_mtds_count < 0) {
		printk(KERN_INFO "ufedm: failed to load, specify indices for "
				 "MTD devices using mtds=1,2,... !\n");
		return -EINVAL;
	}

	for (i = 0; i < g_mtds_count; i++) {
		for (j = i + 1; j < g_mtds_count; j++) {
			if (mtds[i] == mtds[j]) {
				printk(KERN_INFO "ufedm: failed to load due to "
						 "invalid mtds list\n");
				return -EINVAL;
			}
		}
	}

	return 0;
}

static void print_upper_to_backend_mtd_mapping(void)
{
	for (size_t i = 0; i < g_mtds_count; i++) {
		struct mtd_info *mtd = get_backend_mtd_device(i);
		BUG_ON(mtd == NULL);
		pr_info("ufedm: upper mtd%zu -> (backend %d, %s)\n", i,
		    mtd->index, mtd->name);
	}
}

static int __init ufedm_init(void)
{
	int ret;

	if (mtds_count < 0) {
		pr_warn("Invalid mtds count (negative number)\n");
		return -EINVAL;
	}

	g_mtds_count = mtds_count;

	ret = validate_mtds_list();
	if (ret != 0)
		return ret;

	ret = locate_all_backend_mtds(mtds, g_mtds_count);
	if (ret != 0)
		return ret;

	ret = proxy_device_class_init(g_mtds_count);
	if (ret != 0)
		goto error_create_proxy_device_class;

	ret = upper_mtd_initialize_devices(g_mtds_count);
	if (ret != 0)
		goto error_upper_mtd_initialize_devices;

	print_upper_to_backend_mtd_mapping();

	printk(KERN_INFO "ufedm: kernel module loaded!\n");
	return 0;

error_upper_mtd_initialize_devices:
	proxy_device_class_exit();
error_create_proxy_device_class:
	put_backend_mtd_devices(g_mtds_count);
	return ret;
}

static void __exit ufedm_exit(void)
{
	upper_mtd_destroy_devices(g_mtds_count);
	proxy_device_class_exit();
	put_backend_mtd_devices(g_mtds_count);
	printk(KERN_INFO "ufedm: kernel module unloaded!\n");
}

module_init(ufedm_init);
module_exit(ufedm_exit);
