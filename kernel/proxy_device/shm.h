/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __PROXY_DEVICE__SHM__
#define __PROXY_DEVICE__SHM__

#include <linux/types.h>

#include "proxy_device/device.h"

/**
 * @brief Create a shared memory mapping of a ufedm proxy device
 *
 * Initialize and allocate a shared memory mapping window for userspace
 * and kernel, using a shmem file.
 *
 * @param mapping mapping object inside `struct ufedm_proxy_device`
 * @return 0 if successful, otherwise (negative) if had error.
 */
int proxy_device_init_shared_memory(struct ufedm_shm_mapping *mapping);

/**
 * @brief Create a shared memory mapping of a ufedm proxy device
 *
 * Tear down a shared memory mapping - revoke user mappings that were
 * created with mmap(2) syscall and the kernel memory window as well.
 *
 * @param mapping mapping object inside `struct ufedm_proxy_device`
 * @return void
 */
void proxy_device_destroy_shared_memory(struct ufedm_shm_mapping *mapping);

#endif
