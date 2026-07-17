/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/delay.h>
#include <linux/kthread.h>

#include <linux/mm.h>
#include <linux/vmalloc.h>

#include "device.h"
#include "io.h"
#include "proxy_device/device.h"
#include "proxy_device/eventfd.h"
#include "proxy_device/io.h"
#include "proxy_device/shm.h"

static const char *shm_queue_type_to_queue_name(enum proxy_queue_type type)
{
	BUG_ON(type >= __PROXY_QUEUE_TYPE_MAX);

	if (type == PROXY_QUEUE_TYPE_READ)
		return "read queue";

	return "write queue";
}

void copy_nand_pos_to_to_io_pos_params(
    const struct nand_pos *src, struct nand_io_position_params *pos_params)
{
	pos_params->target = src->target;
	pos_params->lun = src->lun;
	pos_params->plane = src->plane;
	pos_params->eraseblock = src->eraseblock;
	pos_params->page = src->page;
}

int proxy_device_fill_queue_info(
    struct ufedm_proxy_device *dev, struct proxy_shm_queue_info *info)
{
	if (info->idx >= dev->shm_info.queues_count)
		return -EINVAL;

	memcpy(info, &dev->queues[info->idx].info,
	    sizeof(struct proxy_shm_queue_info));
	return 0;
}

static void fill_shm_slot_packet_buffer(struct shared_mem_slot *shm_slot,
    size_t page_data_size, const struct simple_nand_page_io_req *req)
{
	/* If we have a NULL pointer in either databuf or
	 * oobbuf, it means the callee (in the upper MTD layer)
	 * never had such buffers in its initial request in the
	 * first place, so we shouldn't do memset(..., 0, len) or
	 * memset(..., 0xFF, len) here.
	 */

	if (req->databuf != NULL)
		memcpy(shm_slot, req->databuf, req->datalen);

	if (req->oobbuf != NULL)
		memcpy(
		    (u8 *)shm_slot + page_data_size, req->oobbuf, req->ooblen);
}

static void __update_shm_slot_header(struct shm_slot_hdr *header,
    seq_num_t seq_num, u32 len, u32 ooblen,
    const struct nand_io_position_params *pos_params)
{
	header->datalen = len;
	header->ooblen = ooblen;

	/* Generate seq_num in the header at last.
	 * For the internal kernel state it doesn't matter.
	 * However, an unintelligent userspace **MIGHT** wait for
	 * the sequence number to appear as a "flag" to start doing processing
	 * instead of using a proper eventfd registration.
	 * In that case we should defer this to be the last transfer we do.
	 */
	header->seq_num = seq_num;

	if (pos_params) {
		memcpy(&header->pos_params, pos_params,
		    sizeof(struct nand_io_position_params));
	}
}

/*
 * This function is used within a proxy_requests_queue mutex "being locked"
 * scope. We don't verify that the NACK parameters are valid -
 * for this it's expected to call the upper function, which is used
 * within an ioctl handler context and not the watchdog kthread.
 */
static void complete_request_locked(struct proxy_requests_queue *q,
    struct proxy_io_slot *slot, u16 positive_errno)
{
	/* This should be safe under any condition - the actual "free"
	 * of a slot (an actual request on the array) is done in the
	 * upper MTD device and not here.
	 */
	WARN_ON(slot->state != PROXY_IO_SLOT_STATE_PENDING_USER);
	slot->status = positive_errno;
	slot->state = PROXY_IO_SLOT_STATE_ALLOCATED;
	complete(&slot->done);
}

static struct proxy_requests_queue *verify_answer_base(
    struct ufedm_proxy_device *dev, struct proxy_answer_base *base)
{
	struct proxy_requests_queue *q;
	if (base->queue_idx >= dev->shm_info.queues_count)
		return NULL;

	q = &dev->queues[base->queue_idx];

	if (base->slot_num >= q->info.slots_count)
		return NULL;

	return q;
}

static int reject_bogus_answer(
    struct proxy_requests_queue *q, struct proxy_answer_base *base)
{
	/* Just test the bit and **DO NOT** clear it - this is done in the
	 * proxy_device_put_slot function, when we finally return the slot
	 * back to non-allocated state, so it can be allocated again.
	 */

	if (q->req_pkt_slots[base->slot_num].state !=
		PROXY_IO_SLOT_STATE_PENDING_USER ||
	    !test_bit(base->slot_num, q->allocated_bitmap))
		return -EINVAL;

	if (q->req_pkt_slots[base->slot_num].header.seq_num != base->seq_num) {
		return -EINVAL;
	}

	return 0;
}

int proxy_device_ack_request(
    struct ufedm_proxy_device *dev, struct proxy_ack *ack)
{
	int ret = -EINVAL;
	struct proxy_requests_queue *q = verify_answer_base(dev, &ack->base);
	if (!q)
		return ret;

	mutex_lock(&q->lock);

	ret = reject_bogus_answer(q, &ack->base);
	if (ret < 0)
		goto exit;

	/* Using ack->base.seq_num is OK, we checked that it's not bogus.
	 * Pass NULL for pos_params - we don't need to update those upon an
	 * ACK ioctl.
	 */
	__update_shm_slot_header(&q->req_pkt_slots[ack->base.slot_num].header,
	    ack->base.seq_num, ack->retlen, ack->oob_retlen, NULL);

	complete_request_locked(q, &q->req_pkt_slots[ack->base.slot_num], 0);
exit:
	mutex_unlock(&q->lock);
	return ret;
}

int proxy_device_nack_request(
    struct ufedm_proxy_device *dev, struct proxy_nack *nack)
{
	int ret = -EINVAL;
	struct proxy_requests_queue *q = verify_answer_base(dev, &nack->base);
	if (!q)
		return ret;

	mutex_lock(&q->lock);

	ret = reject_bogus_answer(q, &nack->base);
	if (ret < 0)
		goto exit;

	complete_request_locked(
	    q, &q->req_pkt_slots[nack->base.slot_num], nack->positive_errno);

exit:
	mutex_unlock(&q->lock);
	return ret;
}

static int watchdog_thread(void *arg)
{
	struct proxy_requests_queue *q = arg;

	struct proxy_io_slot *slot;

	BUG_ON(q->watchdog_sleep_duration_ms == 0);
	BUG_ON(q->slot_timeout_ms == 0);

	const char *queue_name = shm_queue_type_to_queue_name(q->info.type);

	pr_info("ufedm: watchdog thread (%s) started\n", queue_name);

	while (!kthread_should_stop()) {

		ktime_t now = ktime_get();
		mutex_lock(&q->lock);

		list_for_each_entry(
		    slot, &q->allocated_requests, allocated_node)
		{
			WARN_ON(slot->state == PROXY_IO_SLOT_STATE_UNALLOCATED);

			if (slot->state != PROXY_IO_SLOT_STATE_PENDING_USER)
				continue;

			if (ktime_compare(ktime_sub(now, slot->started_time),
                  ms_to_ktime(q->slot_timeout_ms)) < 0)
				  continue;

			pr_info(
			    "ufedm: request %llu (%s, slot %zu) timed out\n",
			    slot->header.seq_num, queue_name, slot->slot_idx);
			complete_request_locked(q, slot, ETIMEDOUT);
		}

		mutex_unlock(&q->lock);
		msleep(q->watchdog_sleep_duration_ms);
	}

	pr_info("ufedm: watchdog thread (%s) stopped\n", queue_name);

	return 0;
}

static void destroy_queue_slots(
    struct proxy_requests_queue *q, size_t max_slot_idx)
{
	for (size_t idx = 0; idx < max_slot_idx; idx++) {
		kvfree(q->req_pkt_slots[idx].shadow_data_and_oob_buf);
		q->req_pkt_slots[idx].parentq = NULL;
	}
}

static int initalize_queue_slots(
    struct proxy_requests_queue *q, size_t shadow_buf_size)
{
	int ret;
	size_t idx;

	/* Set slot indices so they can be used by the watchdog kthread
	 * later on.
	 * Also, init any completion struct as well and point slots to their
	 * parent queue.
	 * Lastly, allocate a shadow buffer for READ operations.
	 */

	for (idx = 0; idx < q->info.slots_count; idx++) {
		q->req_pkt_slots[idx].slot_idx = idx;
		init_completion(&q->req_pkt_slots[idx].done);
		q->req_pkt_slots[idx].parentq = q;
		q->req_pkt_slots[idx].shadow_data_and_oob_buf =
		    kvzalloc(shadow_buf_size, GFP_KERNEL);
		if (!q->req_pkt_slots[idx].shadow_data_and_oob_buf) {
			ret = -ENOMEM;
			goto error_allocating_shadow_buf;
		}
		q->req_pkt_slots[idx].shadow_data_and_oob_buf_size =
		    shadow_buf_size;
	}

	return 0;

error_allocating_shadow_buf:
	destroy_queue_slots(q, idx);
	return ret;
}

static void __stop_io_watchdog_threads(
    struct ufedm_proxy_device *dev, size_t max_idx)
{
	size_t idx;
	BUG_ON(max_idx > 2);
	for (idx = 0; idx < max_idx; idx++) {
		kthread_stop(dev->queues[idx].watchdog);
	}
}

void stop_io_watchdog_threads(struct ufedm_proxy_device *dev)
{
	__stop_io_watchdog_threads(dev, 2);
}

static int start_io_watchdog_thread(struct proxy_requests_queue *q)
{
	q->watchdog = kthread_run(watchdog_thread, q, "ufedm-proxy-watchdog");
	if (IS_ERR(q->watchdog)) {
		return PTR_ERR(q->watchdog);
	}
	return 0;
}

int start_io_watchdog_threads(struct ufedm_proxy_device *dev)
{
	int ret;
	int idx;

	for (idx = 0; idx < 2; idx++) {
		ret = start_io_watchdog_thread(&dev->queues[idx]);
		if (ret != 0)
			goto stop_threads;
	}

	return 0;

stop_threads:
	__stop_io_watchdog_threads(dev, idx);
	return ret;
}

static int init_proxy_requests_queue(
    struct ufedm_proxy_device *dev, struct proxy_requests_queue *q)
{
	int ret;

	/* ID number 0 is not a valid number, set to 1 instead */
	atomic64_set(&q->next_seq_id, 1);

	/* Put a duration of 300ms for both watchdog sleep and
	 * slot timeout.
	 * We might need to adjust this if benchmarks show better values.
	 */
	q->watchdog_sleep_duration_ms = 300;
	q->slot_timeout_ms = 300;

	q->parent_dev = dev;

	q->allocated_bitmap = bitmap_zalloc(q->info.slots_count, GFP_KERNEL);
	if (!q->allocated_bitmap) {
		ret = -ENOMEM;
		goto exit;
	}

	q->req_pkt_slots = kvzalloc(
	    q->info.slots_count * sizeof(struct proxy_io_slot), GFP_KERNEL);
	if (!q->req_pkt_slots) {
		ret = -ENOMEM;
		goto error_allocating_array;
	}

	ret =
	    initalize_queue_slots(q, dev->page_data_size + dev->page_oob_size);
	if (ret != 0)
		goto error_initializing_queue_slots;

	/* Do this at last - if we managed to create everything, then
	 * we are ready to serve for this queue.
	 */
	INIT_LIST_HEAD(&q->allocated_requests);
	mutex_init(&q->lock);

	return 0;

error_initializing_queue_slots:
	kvfree(q->req_pkt_slots);

error_allocating_array:
	bitmap_free(q->allocated_bitmap);

exit:
	return ret;
}

static void destroy_proxy_requests_queue(struct proxy_requests_queue *q)
{
	destroy_queue_slots(q, q->info.slots_count);
	kvfree(q->req_pkt_slots);
	bitmap_free(q->allocated_bitmap);
}

int init_io_queues(struct ufedm_proxy_device *dev)
{
	int ret;
	dev->writeq = &dev->queues[0];
	dev->writeq->info.idx = 0;
	dev->writeq->info.type = PROXY_QUEUE_TYPE_WRITE;
	dev->writeq->info.slots_count = PROXY_SLOTS_COUNT_PER_QUEUE;

	// FIXME: This is hardcoded. Find a way to not do this.
	dev->writeq->info.mem_offset = 0;
	dev->writeq->info.mem_len =
	    dev->writeq->info.slots_count * dev->shm_info.slot_size;

	dev->readq = &dev->queues[1];
	dev->readq->info.idx = 1;
	dev->readq->info.type = PROXY_QUEUE_TYPE_READ;
	dev->readq->info.slots_count = PROXY_SLOTS_COUNT_PER_QUEUE;

	// FIXME: This is hardcoded. Find a way to not do this.
	dev->readq->info.mem_offset = dev->writeq->info.mem_len;
	dev->readq->info.mem_len =
	    dev->readq->info.slots_count * dev->shm_info.slot_size;

	ret = init_proxy_requests_queue(dev, dev->writeq);
	if (ret != 0)
		goto exit;

	ret = init_proxy_requests_queue(dev, dev->readq);
	if (ret != 0) {
		goto error_init_proxy_requests_read_queue;
	}

	return 0;

error_init_proxy_requests_read_queue:
	destroy_proxy_requests_queue(dev->writeq);

exit:
	return ret;
}

void destroy_io_queues(struct ufedm_proxy_device *dev)
{
	destroy_proxy_requests_queue(dev->readq);
	destroy_proxy_requests_queue(dev->writeq);
}

int proxy_device_get_slot(struct ufedm_proxy_device *dev,
    enum nand_page_io_req_type type, struct proxy_io_slot **slotp)
{
	BUG_ON(type != NAND_PAGE_READ && type != NAND_PAGE_WRITE);

	int slot_num;
	struct proxy_requests_queue *q;

	if (type == NAND_PAGE_READ)
		q = dev->readq;
	else
		q = dev->writeq;

	mutex_lock(&q->lock);

	slot_num = bitmap_find_free_region(
	    q->allocated_bitmap, q->info.slots_count, 0);
	if (slot_num < 0) {
		mutex_unlock(&q->lock);
		return -ENOSPC;
	}

	q->req_pkt_slots[slot_num].state = PROXY_IO_SLOT_STATE_ALLOCATED;
	list_add(&q->req_pkt_slots[slot_num].allocated_node,
		&q->allocated_requests);

	mutex_unlock(&q->lock);

	/* This will be used by the actual upper MTD {_write,_read}_oob callback
	 * to wait for completion, etc, either by the user or the watchdog
	 * kthread.
	 */
	*slotp = &q->req_pkt_slots[slot_num];

	return 0;
}

void proxy_device_io_slot_pub_new_packet(
    struct proxy_io_slot *slot, const struct simple_nand_page_io_req *req)
{
	seq_num_t seq_num;
	struct proxy_requests_queue *q = slot->parentq;
	struct ufedm_proxy_device *dev = q->parent_dev;
	struct shared_mem_slot *shm_slot;

	BUG_ON(req->ooblen > dev->page_oob_size);
	BUG_ON(req->datalen > dev->page_data_size);

	mutex_lock(&q->lock);
	seq_num = (u64)atomic64_inc_return(&q->next_seq_id);

	/* We create a duplicate packet header to keep the state of the slot
	 * away from userspace. In other words, we manage the slots, userspace
	 * just does the processing and either ACK/NACK after it examines (and
	 * done processing if it can).
	 */
	__update_shm_slot_header(&q->req_pkt_slots[slot->slot_idx].header,
	    seq_num, req->datalen, req->ooblen, &req->pos_params);

	reinit_completion(&q->req_pkt_slots[slot->slot_idx].done);
	q->req_pkt_slots[slot->slot_idx].status = 0;

	slot->state = PROXY_IO_SLOT_STATE_PENDING_USER;
	q->req_pkt_slots[slot->slot_idx].started_time = ktime_get();

	/* This is where we copy the data + OOB + packet header
	 * to the shared memory interface. From this point onwards, it
	 * is visible to userspace that there's a new packet to process.
	 */
	shm_slot = proxy_device_queue_and_slot_to_buf(
	    slot->parentq->parent_dev, q->info.idx, slot->slot_idx);

	fill_shm_slot_packet_buffer(shm_slot, dev->page_data_size, req);
	__update_shm_slot_header(&shm_slot->header, seq_num, req->datalen,
	    req->ooblen, &req->pos_params);

	mutex_unlock(&q->lock);

	proxy_eventfd_ctx_notify(&slot->efd);
}

void proxy_device_put_slot(struct proxy_io_slot *slot)
{
	struct proxy_requests_queue *q = slot->parentq;

	mutex_lock(&q->lock);

	WARN_ON(!test_bit(slot->slot_idx, q->allocated_bitmap));

	list_del(&q->req_pkt_slots[slot->slot_idx].allocated_node);
	q->req_pkt_slots[slot->slot_idx].state =
	    PROXY_IO_SLOT_STATE_UNALLOCATED;
	clear_bit(slot->slot_idx, q->allocated_bitmap);

	mutex_unlock(&q->lock);
}
