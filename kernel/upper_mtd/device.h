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

/**
 * @brief Initialize a counted set of upper MTD devices on an array.
 *
 * Iterates over all backing MTD devices and initialize a correspoinding
 * upper MTD device
 *
 * @param dev_array array of `struct upper_mtd_device` objects.
 * @param count size of array
 * @return 0 for success or negative number for error.
 */
int upper_mtd_initialize_devices(
    struct upper_mtd_device *dev_array, size_t count);

/**
 * @brief Tear down a counted set of upper MTD devices on an array.
 *
 * Iterates over all upper MTD devices in an array and tear down them
 *
 * @param dev_array array of `struct upper_mtd_device` objects.
 * @param count size of array
 * @return void
 */
void upper_mtd_destroy_devices(
    struct upper_mtd_device *dev_array, size_t count);

#endif
