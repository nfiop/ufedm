/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "proxy_device/class.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Liav A");
MODULE_DESCRIPTION("NAND Flash ECC & Data layout in userspace management");
MODULE_VERSION("0.1");

static int __init ufedm_init(void)
{
	int ret;

	ret = proxy_device_class_init(PROXY_DEVICE_COUNT);
	if (ret != 0)
		goto error_create_proxy_device_class;

	printk(KERN_INFO "ufedm: kernel module loaded!\n");
	return 0;

error_create_proxy_device_class:
	return ret;
}

static void __exit ufedm_exit(void)
{
	proxy_device_class_exit();
	printk(KERN_INFO "ufedm: kernel module unloaded!\n");
}

module_init(ufedm_init);
module_exit(ufedm_exit);
