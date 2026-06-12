/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "defs.h"
#include "proxy_device/class.h"
#include "upper_mtd/device.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Liav A");
MODULE_DESCRIPTION("NAND Flash ECC & Data layout in userspace management");
MODULE_VERSION("0.1");

static uint mtds[PROXY_MAX_DEVICE_COUNT];
static int mtds_count;

module_param_array(mtds, uint, &mtds_count, 0444);
MODULE_PARM_DESC(mtds, "Array of MTD device minor indices");

static int validate_mtds_list(void)
{
	int i, j;

	if (mtds_count == 0 || mtds_count < 0) {
		printk(KERN_INFO "ufedm: failed to load, specify indices for "
				 "MTD devices using mtds=1,2,... !\n");
		return -EINVAL;
	}

	for (i = 0; i < mtds_count; i++) {
		for (j = i + 1; j < mtds_count; j++) {
			if (mtds[i] == mtds[j]) {
				printk(KERN_INFO "ufedm: failed to load due to "
						 "invalid mtds list\n");
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int __init ufedm_init(void)
{
	int ret;

	ret = validate_mtds_list();
	if (ret != 0)
		return ret;

	ret = proxy_device_class_init(mtds_count);
	if (ret != 0)
		goto error_create_proxy_device_class;

	ret = upper_mtd_initialize_devices(mtds, mtds_count);
	if (ret != 0)
		goto error_upper_mtd_initialize_devices;

	print_upper_to_backend_mtd_mapping();

	printk(KERN_INFO "ufedm: kernel module loaded!\n");
	return 0;

error_upper_mtd_initialize_devices:
	proxy_device_class_exit();
error_create_proxy_device_class:
	return ret;
}

static void __exit ufedm_exit(void)
{
	upper_mtd_destroy_devices();
	proxy_device_class_exit();
	printk(KERN_INFO "ufedm: kernel module unloaded!\n");
}

module_init(ufedm_init);
module_exit(ufedm_exit);
