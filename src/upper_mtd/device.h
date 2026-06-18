/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __MTD__UPPER_DEVICE
#define __MTD__UPPER_DEVICE

#include <linux/mtd/mtd.h>
#include <linux/types.h>

struct upper_mtd_device {
	struct mtd_info *backend;
	struct mtd_info *upper;

	/* Proxy MTD device pointer which will want
	 * to deref on actual request sending/completion
	 */
	struct ufedm_proxy_device* proxy_dev;
};

int upper_mtd_initialize_devices(uint *mtd_minors_list, size_t count);
void print_upper_to_backend_mtd_mapping(void);
void upper_mtd_destroy_devices(void);

#endif
