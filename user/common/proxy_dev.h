/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __USER_PROXY_DEV_H_
#define __USER_PROXY_DEV_H_

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>

#include "defs.h"
#include "proxy_ioctl.h"
#include "proxy_queue.h"

typedef enum {
	ANSWER_ACK,
	ANSWER_NACK,
} transform_answer_t;

struct transform_context_results {
	int errno_rc;
	bool had_fatal_stop;
	size_t data_retlen;
	size_t oob_retlen;
};
typedef transform_answer_t (*transform_data_func_t)(
    const struct shm_slot_hdr *hdr, const u8 *entire_page_buf,
    struct transform_context_results *context_res);
typedef void (*notify_fatal_stop_thread_func_t)(pid_t tid, int rc);

struct proxy_device_state {
	struct proxy_mtd_info mtd_info;
	struct proxy_shm_info shm_info;

	void *shared_mem_buf;
	size_t shared_mem_buf_mmap_size;

	int fd;
};

struct proxy_queue_func_ops {
	transform_data_func_t write;
	transform_data_func_t read;
	notify_fatal_stop_thread_func_t fatal_stop;
};
struct proxy_device_thread_state {
	int efd;
	pthread_t tid;
	atomic_bool _stop;

	int proxy_device_fd;
	size_t queue_idx;
	size_t slot_idx;
	struct shared_mem_slot *slot;
	size_t slot_buf_len;

	transform_data_func_t transform_func;
	notify_fatal_stop_thread_func_t notify_fatal_stop_func;
};

struct proxy_device_queue_state {
	int proxy_device_fd;

	struct proxy_shm_queue_info info;

	int *eventfds;
	struct proxy_device_thread_state *thread_array;
};

/**
 * @brief Open a proxy device file descriptor based on a full path
 *
 * This function opens a ufedm proxy device and fills a given
 * `struct proxy_device_state` - it issues several ioctls to fetch crucial
 * data on the device and its backing MTD device, create a memory mapping
 * to shared memory buffer [with mmap(2) call] and registers eventfd's for
 * each write slots and read slots on the shared memory buffer.
 *
 * Upon success, the given `struct proxy_device_state *state` should be filled
 * with all the needed details about current state of the proxy device.
 *
 * @param index a string containing an MTD index (e.g. "0" for /dev/mtd0)
 * @param state to-be-filled pointer of a struct containting the new state
 * @return 0 if successful, other negative number for error
 */
int open_proxy_device_state(const char *path, struct proxy_device_state *state);

/**
 * @brief Create an I/O queue state
 *
 * @param dev_state a complete and operable proxy device state
 * @param state an empty `struct proxy_device_queue_state`
 * @param queue_idx a queue index (in order of appearance in the shm buffer)
 * @param process_callback a I/O processing callback
 * @param fatal_stop_callback a fatal stop callback
 * @return 0 if successful, other negative number for error
 */
int create_proxy_dev_queue_state(struct proxy_device_state *dev_state,
    struct proxy_device_queue_state *state, size_t queue_idx,
    struct proxy_queue_func_ops *func_ops);

/**
 * @brief Destroy an I/O queue state
 *
 * Destroy an I/O queue - free resources, destroy threads and close
 * eventfds.
 *
 * @param state a complete and operable proxy device queue state
 * @return 0 if successful, other negative number for error
 */
void destroy_proxy_dev_queue_state(struct proxy_device_queue_state *state);

/**
 * @brief Close a proxy device
 *
 * This function closes a ufedm proxy device that was opened with the
 * `open_proxy_device_state` function.
 * It unregisters eventfd's, unmap the shared memory buffer and finally closes
 * the file descriptor.
 *
 * IMPORTANT: We don't free the `struct proxy_device_state` - it's up to the
 * user application to do this afterwards.
 *
 * @param state to-be-filled pointer of a struct containting the new state
 * @return 0 if successful, other negative number for error
 */
void close_proxy_device(struct proxy_device_state *state);

/**
 * @brief Open a proxy device file descriptor based on a MTD index string
 *
 * This function is mainly used by utests - it's lean, and doesn't do much
 * and also quite noisy about what it does which is perfect for verbose apps.
 *
 * @param index a string containing an MTD index (e.g. "0" for /dev/mtd0)
 * @return non-negative file descriptor number if successful, other negative
 *         number for error
 */
int open_proxy_device_by_argv_index(const char *index);

/**
 * @brief Register an eventfd on a I/O read/write slot
 *
 * This function tries to allocate an eventfd for a read/write slot
 * and then attempts to register it on the request slot index.
 *
 * @param state A pointer to open proxy device state struct
 * @param slot_idx The I/O write/read slot index
 * @return 0 if successful, other negative number for error
 */
int register_proxy_eventfd(
    struct proxy_device_queue_state *state, size_t slot_idx, int efd);

/**
 * @brief Unregister an eventfd on a I/O read/write slot
 *
 * This function unregisters an eventfd for an I/O write/read slot.
 * It also closes the eventfd file descriptor.
 * NOTE: This function has many assert statements to ensure a callee
 * doesn't use this before actually calling `register_proxy_eventfd`
 * successfully.
 *
 * @param state A pointer to open proxy device state struct
 * @param type The type of the I/O request to listen to with eventfd
 * @param slot_idx The I/O write/read slot index
 * @return void
 */
void unregister_proxy_eventfd(
    struct proxy_device_queue_state *state, size_t slot_idx);

#endif
