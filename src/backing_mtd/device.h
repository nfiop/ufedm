/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __MTD__BACKEND_DEVICE
#define __MTD__BACKEND_DEVICE

#include <linux/mtd/mtd.h>
#include <linux/types.h>

int locate_all_backend_mtds(uint *mtd_minors_list, size_t count);
struct mtd_info *get_backend_mtd_device(uint index);
void put_backend_mtd_devices(size_t count);

#endif
