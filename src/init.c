/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Liav A");
MODULE_DESCRIPTION("NAND Flash ECC & Data layout in userspace management");
MODULE_VERSION("0.1");

/* Called when module is loaded */
static int __init ufedm_init(void)
{
    printk(KERN_INFO "ufedm: kernel module loaded!\n");
    return 0;  // non-zero means failure
}

/* Called when module is removed */
static void __exit ufedm_exit(void)
{
    printk(KERN_INFO "ufedm: kernel module unloaded!\n");
}

module_init(ufedm_init);
module_exit(ufedm_exit);
