/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __MTD__BACKEND_DEVICE
#define __MTD__BACKEND_DEVICE

#include <linux/mtd/mtd.h>
#include <linux/types.h>

int open_backend_mtd_device(struct mtd_info **mtd_ptr_in_list, uint mtd_index);
void put_backend_mtd_devices(struct mtd_info **mtd_list, size_t max_index);

#endif
