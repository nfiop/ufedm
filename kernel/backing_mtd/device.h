/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __MTD__BACKEND_DEVICE
#define __MTD__BACKEND_DEVICE

#include <linux/mtd/mtd.h>
#include <linux/types.h>

int locate_all_backend_mtds(uint *mtd_minors_list, size_t count);

/**
 * @brief Find a backing MTD device with its MTD index number
 *
 * Find a backing MTD device in an internal list of what was registered
 * during init.
 * No actual refcount is modified.
 *
 * @param index an MTD index of a backing MTD device
 * @return NULL if not device is found, or a valid struct mtd_info pointer
 */
struct mtd_info *get_backend_mtd_device(uint index);

void put_backend_mtd_devices(size_t count);

#endif
