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
 * @param dev a `struct ufedm_proxy_device` to initialize its shm_mapping struct
 * @return 0 if successful, otherwise (negative) if had error.
 */
int proxy_device_init_shared_memory(struct ufedm_proxy_device *dev);

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

/**
 * @brief Convert a tuple of proxy device pointer, queue index and slot index
 *        to a memory window
 *
 * @param dev a proxy device instance
 * @param queue_idx a queue index
 * @param slot_idx a slot index
 * @return pointer to a memory address in the kernel shared memory mapping
 */
struct shared_mem_slot *proxy_device_queue_and_slot_to_buf(
    struct ufedm_proxy_device *dev, size_t queue_idx, size_t slot_idx);

#endif
