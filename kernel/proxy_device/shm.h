/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __PROXY_DEVICE__SHM__
#define __PROXY_DEVICE__SHM__

#include <linux/types.h>

#include "proxy_device/device.h"

int proxy_device_init_shared_memory(struct ufedm_shm_mapping *mapping);
void proxy_device_destroy_shared_memory(struct ufedm_shm_mapping *mapping);

#endif
