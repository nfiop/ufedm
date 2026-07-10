/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __PROXY_DEVICE_IO__
#define __PROXY_DEVICE_IO__

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/mtd/nand.h>
#include <linux/types.h>

#include "device.h"
#include "proxy_device/device.h"

/* This is a simplified version of the nand_page_io_req
 * struct. databuf and oobbuf can be NULL, in which such case
 * we simply don't do anything to put data in a the allocated
 * shared memory slot.
 */
struct simple_nand_page_io_req {
	unsigned int datalen;
	unsigned int ooblen;

	void *databuf;
	void *oobbuf;
};

int init_async_io_workers(struct ufedm_proxy_device *dev);
void destroy_async_io_workers(struct ufedm_proxy_device *dev);

int proxy_device_ack_request(
    struct ufedm_proxy_device *dev, struct proxy_ack *ack);

int proxy_device_nack_request(
    struct ufedm_proxy_device *dev, struct proxy_nack *nack);

int proxy_device_get_slot(struct ufedm_proxy_device *dev,
    enum nand_page_io_req_type type, struct proxy_io_slot **slotp);

void proxy_device_io_slot_pub_new_packet(
    struct proxy_io_slot *slot, const struct simple_nand_page_io_req *req);

void proxy_device_put_slot(struct proxy_io_slot *slot);

#endif
