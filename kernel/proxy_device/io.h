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

	struct nand_io_position_params pos_params;
};

void copy_nand_pos_to_to_io_pos_params(
    const struct nand_pos *src, struct nand_io_position_params *pos_params);

/**
 * @brief Initialize I/O mechanism and required facility for a proxy device
 *
 * Initialize the read & write I/O queues and slots as a preparation,
 * before
 *
 * @param dev the proxy device corresponding to the upper MTD device
 * @return 0 if allocated succesfully, otherwise negative number as error
 */
int init_io_queues(struct ufedm_proxy_device *dev);

/**
 * @brief Tear-down I/O mechanism for a proxy device
 *
 * Destroy the read & write I/O queues and their resources.
 *
 * @param dev the proxy device corresponding to the upper MTD device
 * @return void
 */
void destroy_io_queues(struct ufedm_proxy_device *dev);

/**
 * @brief Start watchdog threads for I/O queues
 *
 * Start watchdog threads on each I/O queue for a proxy device
 *
 * @param dev the proxy device corresponding to the upper MTD device
 * @return 0 if done succesfully, otherwise negative number as error
 */
int start_io_watchdog_threads(struct ufedm_proxy_device *dev);

/**
 * @brief Stop watchdog threads for I/O queues
 *
 * Stop watchdog thread for each I/O queue for a proxy device
 *
 * @param dev the proxy device corresponding to the upper MTD device
 * @return void
 */
void stop_io_watchdog_threads(struct ufedm_proxy_device *dev);

/**
 * @brief Do a ACK on a I/O request in a slot
 *
 * Do a ACK ("Acknowledge") on a given packet/request in a slot
 * with returned DATA and OOB being actually processed.
 *
 * @param dev the proxy device corresponding to the upper MTD device
 * @param ack a struct containing details on the ACK'ed slot, etc
 * @return 0 if allocated succesfully, otherwise negative number as error
 */
int proxy_device_ack_request(
    struct ufedm_proxy_device *dev, struct proxy_ack *ack);

/**
 * @brief Do a NACK on a I/O request in a slot
 *
 * Do a NACK ("Not Acknowledge") on a given packet/request in a slot
 * for a given reason (set by userspace)
 *
 * @param dev the proxy device corresponding to the upper MTD device
 * @param nack a struct containing details on the NACK'ed slot, etc
 * @return 0 if allocated succesfully, otherwise negative number as error
 */
int proxy_device_nack_request(
    struct ufedm_proxy_device *dev, struct proxy_nack *nack);

/**
 * @brief Fill a queue information struct with details
 *
 * Fetch a queue based on info->packet_queue_idx (should have a valid ID)
 * and fill the rest of the details in the buffer.
 *
 * @param dev the proxy device corresponding to the upper MTD device
 * @param info a struct containing an ID of a queue and should be filled
 *             by the function
 * @return 0 if successful, otherwise negative number as error
 */
int proxy_device_fill_queue_info(
    struct ufedm_proxy_device *dev, struct proxy_shm_queue_info *info);

/**
 * @brief Allocate a shared memory I/O slot
 *
 * Find & possibly allocate a shared memory I/O slot, depending on the
 * I/O type (read or write) and if free slots' list is not exhausted at
 * the moment.
 *
 * @param dev the proxy device corresponding to the upper MTD device
 * @param type a I/O slot type
 * @param slotp a pointer to a pointer of `struct proxy_io_slot` to be
 *              set if allocated succesfully.
 * @return 0 if allocated succesfully, otherwise negative number as error
 */
int proxy_device_get_slot(struct ufedm_proxy_device *dev,
    enum nand_page_io_req_type type, struct proxy_io_slot **slotp);

/**
 * @brief Publish new packet to an allocated shared memory I/O slot
 *
 * Publish a new packet with data and metadata to the shared memory slot
 * and invoke a triggered notification via an appropriate eventfd to
 * userspace.
 *
 * @param slot the allocated slot
 * @param req a NAND page I/O request (containing details & buffers)
 * @return void
 */
void proxy_device_io_slot_pub_new_packet(
    struct proxy_io_slot *slot, const struct simple_nand_page_io_req *req);

/**
 * @brief Free an allocated shared memory I/O slot
 *
 * Returns an allocated I/O slot back into a "free" state after an I/O
 * transaction within the upper MTD device callback is done and there's
 * no actual need for the allocated slot on the shared memory buffer
 * anymore
 *
 * @param slot the allocated slot
 * @return void
 */
void proxy_device_put_slot(struct proxy_io_slot *slot);

#endif
