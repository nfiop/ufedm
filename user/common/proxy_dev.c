/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>

#include "proxy_dev.h"

int open_proxy_device_by_argv_index(const char *index)
{
	unsigned long idx;
	char *end;
	int fd;
	char path[64];

	errno = 0;
	idx = strtoul(index, &end, 10);

	/* Validation */
	if (errno != 0) {
		perror("strtoul");
		return 1;
	}

	if (*end != '\0') {
		fprintf(
		    stderr, "Invalid input: trailing characters '%s'\n", end);
		return 1;
	}

	if (idx > 1000) {
		fprintf(stderr, "Index too large (max 1000)\n");
		return 1;
	}

	snprintf(path, sizeof(path), "/dev/ufedm_proxy%lu", idx);

	fprintf(stderr, "Opening %s\n", path);

	fd = open(path, O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	fprintf(stderr, "Device opened successfully\n");
	return fd;
}

int open_proxy_device_state(const char *path, struct proxy_device_state *state)
{
	int ret;

	state->fd = open(path, O_RDWR);
	if (state->fd < 0) {
		return state->fd;
	}

	ret = ioctl(state->fd, PROXY_IOC_GET_SHM_INFO, &state->shm_info);
	if (ret < 0) {
		ret = -errno;
		return ret;
	}

	ret = ioctl(state->fd, PROXY_IOC_GET_MTD_INFO, &state->mtd_info);
	if (ret < 0) {
		ret = -errno;
		return ret;
	}

	state->shared_mem_buf_mmap_size = state->shm_info.total_buf_size;
	state->shared_mem_buf = mmap(NULL, state->shared_mem_buf_mmap_size,
	    PROT_READ | PROT_WRITE, MAP_SHARED, state->fd, 0);

	if (state->shared_mem_buf == NULL) {
		ret = -EFAULT;
		return ret;
	}

	return 0;
}

void close_proxy_device(struct proxy_device_state *state)
{
	munmap(state->shared_mem_buf, state->shared_mem_buf_mmap_size);

	close(state->fd);
}

static void zero_transform_context_results(
    struct transform_context_results *context_res)
{
	context_res->errno_rc = 0;
	context_res->had_fatal_stop = false;
	context_res->data_retlen = 0;
	context_res->oob_retlen = 0;
}

static void *worker_thread_func(void *arg)
{
	transform_answer_t answer;
	struct transform_context_results context_res;
	u64 counter = 0;
	bool had_fatal_stop = false;
	struct proxy_device_thread_state *state = arg;
	struct shm_slot_hdr *hdr;
	struct proxy_nack nack;
	struct proxy_ack ack;

	int rc = 0;

	hdr = (struct shm_slot_hdr *)&state->slot->header;

	assert(state != NULL);
	assert(state->transform_func != NULL);
	assert(state->efd >= 0);

	while (!state->_stop) {
		// This call should block until there's a new
		// data in the slot to process, or because we
		// wrote to the file descriptor to stop the
		// thread.
		//
		// It is expected that upon destroying of thread workers
		// that the eventfd will be unregistered from the kernel
		// to ensure no interference from stopping.
		rc = read(state->efd, &counter, sizeof(counter));
		if (rc < 0)
			break;
		// We might wake just for exiting right now.
		if (state->_stop)
			break;

		zero_transform_context_results(&context_res);
		answer =
		    state->transform_func(hdr, state->slot->buf, &context_res);
		if (had_fatal_stop)
			break;

		assert(answer == ANSWER_NACK || answer == ANSWER_ACK);
		if (answer == ANSWER_NACK) {
			nack.base.queue_idx = state->queue_idx;
			nack.base.slot_num = state->slot_idx;
			nack.base.seq_num = hdr->seq_num;
			nack.positive_errno = rc;
			rc = ioctl(
			    state->proxy_device_fd, PROXY_IOC_NACK, &nack);
			if (rc < 0) {
				rc = errno;
				had_fatal_stop = true;
				break;
			}
		} else {
			ack.base.queue_idx = state->queue_idx;
			ack.base.slot_num = state->slot_idx;
			ack.base.seq_num = hdr->seq_num;
			ack.retlen = context_res.data_retlen;
			ack.oob_retlen = context_res.oob_retlen;
			rc = ioctl(state->proxy_device_fd, PROXY_IOC_ACK, &ack);
			if (rc < 0) {
				rc = errno;
				had_fatal_stop = true;
				break;
			}
		}
	}

	if (had_fatal_stop && state->notify_fatal_stop_func)
		state->notify_fatal_stop_func(gettid(), rc);

	return NULL;
}

static void destroy_threads(
    struct proxy_device_queue_state *state, size_t max_thread_index)
{
	int ret;
	size_t idx;
	const char *dummy_buf = "0\n";

	for (idx = 0; idx < max_thread_index; idx++) {
		ret = write(state->thread_array[idx].efd, dummy_buf,
		    sizeof(*dummy_buf));
		if (ret < 0) {
			perror("write");
			continue;
		}
		pthread_join(state->thread_array[idx].tid, NULL);
	}
}

static int create_threads(struct proxy_device_queue_state *state,
    u8 *shared_mem_buffer, size_t slot_size,
    transform_data_func_t transform_callback,
    notify_fatal_stop_thread_func_t fatal_stop_callback)
{
	int ret;
	size_t idx;
	struct proxy_device_thread_state *thread_state;

	for (idx = 0; idx < state->info.slots_count; idx++) {
		thread_state = &state->thread_array[idx];

		thread_state->efd = state->eventfds[idx];
		thread_state->_stop = false;
		thread_state->proxy_device_fd = state->proxy_device_fd;
		thread_state->queue_idx = state->info.idx;
		thread_state->slot_idx = idx;
		thread_state->slot =
		    (struct shared_mem_slot *)shared_mem_buffer;
		thread_state->slot_buf_len = slot_size;
		thread_state->transform_func = transform_callback;
		thread_state->notify_fatal_stop_func = fatal_stop_callback;
		ret = pthread_create(&state->thread_array[idx].tid, NULL,
		    worker_thread_func, thread_state);

		shared_mem_buffer += slot_size;
		if (ret < 0)
			goto cleanup;
	}

	return 0;

cleanup:
	destroy_threads(state, idx);
	return ret;
}

static void close_eventfds(
    struct proxy_device_queue_state *state, size_t max_efd_idx)
{
	size_t idx;
	for (idx = 0; idx < max_efd_idx; idx++) {
		close(state->eventfds[idx]);
		state->eventfds[idx] = -1;
	}
}

static int create_eventfds(struct proxy_device_queue_state *state)
{
	int efd;
	size_t idx;

	for (idx = 0; idx < state->info.slots_count; idx++) {
		efd = eventfd(0, 0);
		if (efd < 0) {
			efd = -errno;
			goto cleanup;
		}

		state->eventfds[idx] = efd;
	}

	return 0;

cleanup:
	close_eventfds(state, idx);
	return efd;
}

static void unregister_eventfds(
    struct proxy_device_queue_state *state, size_t max_slot_idx)
{
	for (size_t slot_idx = 0; slot_idx < max_slot_idx; slot_idx++) {
		unregister_proxy_eventfd(state, slot_idx);
	}
}

static int register_eventfds(struct proxy_device_queue_state *state)
{
	int ret;
	size_t idx;
	for (idx = 0; idx < state->info.slots_count; idx++) {
		ret = register_proxy_eventfd(state, idx, state->eventfds[idx]);
		if (ret < 0)
			goto cleanup;
	}

	return 0;

cleanup:
	unregister_eventfds(state, idx);
	return ret;
}

int create_proxy_dev_queue_state(struct proxy_device_state *dev_state,
    struct proxy_device_queue_state *state, size_t queue_idx,
    struct proxy_queue_func_ops *func_ops)
{
	int ret;
	u8 *shared_mem_buf;

	state->proxy_device_fd = dev_state->fd;
	state->info.idx = queue_idx;

	ret = ioctl(
	    state->proxy_device_fd, PROXY_IOC_GET_QUEUE_INFO, &state->info);
	if (ret < 0) {
		ret = errno;
		goto exit;
	}

	state->eventfds = calloc(state->info.slots_count, sizeof(int));
	if (!state->eventfds) {
		ret = -ENOMEM;
		goto exit;
	}

	state->thread_array = calloc(
	    state->info.slots_count, sizeof(struct proxy_device_thread_state));
	if (!state->thread_array) {
		ret = -ENOMEM;
		goto remove_eventfds;
	}

	ret = create_eventfds(state);
	if (ret < 0)
		goto free_eventfds_list;

	assert(state->info.type == PROXY_QUEUE_TYPE_READ ||
	       state->info.type == PROXY_QUEUE_TYPE_WRITE);

	shared_mem_buf =
	    ((u8 *)dev_state->shared_mem_buf) + state->info.mem_offset;
	if (state->info.type == PROXY_QUEUE_TYPE_READ)
		ret = create_threads(state, shared_mem_buf,
		    dev_state->shm_info.slot_size, func_ops->read,
		    func_ops->fatal_stop);
	else
		ret = create_threads(state, shared_mem_buf,
		    dev_state->shm_info.slot_size, func_ops->write,
		    func_ops->fatal_stop);
	if (ret < 0)
		goto remove_eventfds;

	ret = register_eventfds(state);
	if (ret < 0)
		goto remove_threads;

	return 0;

remove_threads:
	destroy_threads(state, state->info.slots_count);
remove_eventfds:
	close_eventfds(state, state->info.slots_count);
free_eventfds_list:
	free(state->eventfds);
exit:
	return ret;
}

void destroy_proxy_dev_queue_state(struct proxy_device_queue_state *state)
{
	unregister_eventfds(state, state->info.slots_count);
	destroy_threads(state, state->info.slots_count);
	close_eventfds(state, state->info.slots_count);
	free(state->eventfds);
}

int register_proxy_eventfd(
    struct proxy_device_queue_state *state, size_t slot_idx, int efd)
{
	int ret;
	struct proxy_register_eventfd register_req;
	assert(state->info.slots_count > slot_idx);
	assert(state->info.type == PROXY_QUEUE_TYPE_READ ||
	       state->info.type == PROXY_QUEUE_TYPE_WRITE);

	register_req.fd = efd;
	register_req.type = state->info.type;

	register_req.slot_idx = slot_idx;
	ret = ioctl(
	    state->proxy_device_fd, PROXY_IOC_REGISTER_EVENTFD, &register_req);
	if (ret < 0) {
		ret = errno;
		perror("ioctl");
		goto exit_ioctl_error;
	}

	return 0;

exit_ioctl_error:
	close(efd);
	return ret;
}

void unregister_proxy_eventfd(
    struct proxy_device_queue_state *state, size_t slot_idx)
{
	int ret;
	struct proxy_unregister_eventfd unregister_req;
	assert(state->info.slots_count > slot_idx);
	assert(state->info.type == PROXY_QUEUE_TYPE_READ ||
	       state->info.type == PROXY_QUEUE_TYPE_WRITE);

	unregister_req.type = state->info.type;
	unregister_req.slot_idx = slot_idx;
	ret = ioctl(state->proxy_device_fd, PROXY_IOC_UNREGISTER_EVENTFD,
	    &unregister_req);

	// We are very unlikey to get error (unless we passed wrong slot idx,
	// etc), so assert on such condition.
	assert(ret == 0);
}
