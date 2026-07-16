/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __PROXY_DEVICE_EVENTFD__
#define __PROXY_DEVICE_EVENTFD__

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/eventfd.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "device.h"
#include "proxy_device/device.h"
#include "proxy_ioctl.h"
#include "proxy_queue.h"

struct protected_eventfd_ctx *proxy_eventfd_ctx_based_on_type_and_slot(
    struct ufedm_proxy_device *dev, enum proxy_queue_type type,
    size_t slot_idx);

int proxy_eventfd_ctx_register(struct protected_eventfd_ctx *ctx, int fd);
void proxy_eventfd_ctx_unregister(struct protected_eventfd_ctx *ctx);
void proxy_eventfd_ctx_notify(struct protected_eventfd_ctx *ctx);

#endif
