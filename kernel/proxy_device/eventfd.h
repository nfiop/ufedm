/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __PROXY_DEVICE_EVENTFD__
#define __PROXY_DEVICE_EVENTFD__

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/types.h>

/* When initializing the struct, we don't have any eventfd
 * pointer being registered at all. Each pointer is protected
 * by its spinlock.
 *
 * It **SHOULD** be clear that the process does not have to register
 * any eventfd at all - the process volunteers and it can skip it
 * safely. The consequences are repeated polling for incoming events.
 * Whether it's bad or not, is up to a discussion and performance
 * testing & further evaluation.
 *
 * If the process exits, we still retain an eventfd context.
 * This is completely harmless in terms of system stability.
 * When a new process tries to register a new eventfd, it will just
 * replace the old one, regardless of whether anything actually used
 * it or not.
 *
 * Userspace **SHOULD** absolutely not write to eventfd counter if
 * it uses it for tracking the real amount of incoming events.
 * The kernel is not responsible for fixing the value, and will only
 * increment the counter per the incoming events' count.
 * In other words, an eventfd file is used ideally in poll/epoll and
 * the read syscall as well.
 */

#include <linux/eventfd.h>
#include <linux/spinlock.h>

struct protected_eventfd_ctx {
	spinlock_t lock;
	struct eventfd_ctx *efd;
};

int proxy_eventfd_ctx_register(struct protected_eventfd_ctx *ctx, int fd);
void proxy_eventfd_ctx_unregister(struct protected_eventfd_ctx *ctx);
void proxy_eventfd_ctx_notify(struct protected_eventfd_ctx *ctx);

#endif
