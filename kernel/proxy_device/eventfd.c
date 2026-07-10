/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include "proxy_device/eventfd.h"

struct protected_eventfd_ctx *proxy_eventfd_ctx_based_on_type_and_slot(
    struct ufedm_proxy_device *dev, enum proxy_eventfd_type type,
    size_t slot_idx)
{
	BUG_ON(type != PROXY_EVENTFD_TYPE_WRITE &&
	       type != PROXY_EVENTFD_TYPE_READ);

	if (slot_idx >= PROXY_PACKETS_COUNT_PER_QUEUE)
		return NULL;

	if (type == PROXY_EVENTFD_TYPE_WRITE)
		return &dev->writeq.req_pkt_slots[slot_idx].efd;

	return &dev->readq.req_pkt_slots[slot_idx].efd;
}

int proxy_eventfd_ctx_register(struct protected_eventfd_ctx *ctx, int fd)
{
	struct eventfd_ctx *new_efd_ctx;
	struct eventfd_ctx *old_efd_ctx = NULL;

	new_efd_ctx = eventfd_ctx_fdget(fd);
	if (IS_ERR(new_efd_ctx))
		return PTR_ERR(new_efd_ctx);

	spin_lock(&ctx->lock);

	old_efd_ctx = ctx->efd;
	ctx->efd = new_efd_ctx;

	spin_unlock(&ctx->lock);

	if (old_efd_ctx)
		eventfd_ctx_put(old_efd_ctx);
	return 0;
}

void proxy_eventfd_ctx_unregister(struct protected_eventfd_ctx *ctx)
{
	struct eventfd_ctx *old_efd_ctx = NULL;

	spin_lock(&ctx->lock);

	old_efd_ctx = ctx->efd;
	ctx->efd = NULL;

	spin_unlock(&ctx->lock);

	/* This is interesting - we could return an error if we wanted
	 * but it's probably better to forgive userspace and continue.
	 */
	if (old_efd_ctx)
		eventfd_ctx_put(old_efd_ctx);
}

void proxy_eventfd_ctx_notify(struct protected_eventfd_ctx *ctx)
{
	struct eventfd_ctx *efd_ctx = NULL;
	spin_lock(&ctx->lock);

	if (ctx->efd) {
		eventfd_signal(efd_ctx);
	}

	spin_unlock(&ctx->lock);
}
