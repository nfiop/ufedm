/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include "proxy_device/eventfd.h"

#include "proxy_queue.h"

struct protected_eventfd_ctx *proxy_eventfd_ctx_based_on_type_and_slot(
    struct ufedm_proxy_device *dev, enum proxy_queue_type type, size_t slot_idx)
{
	struct proxy_requests_queue *q;
	BUG_ON(type != PROXY_QUEUE_TYPE_READ && type != PROXY_QUEUE_TYPE_WRITE);

	if (type == PROXY_QUEUE_TYPE_WRITE)
		q = dev->writeq;
	else
		q = dev->readq;

	if (slot_idx >= q->info.slots_count)
		return NULL;
	return &q->req_pkt_slots[slot_idx].efd;
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
	spin_lock(&ctx->lock);

	if (ctx->efd) {
		eventfd_signal(ctx->efd);
	}

	spin_unlock(&ctx->lock);
}
