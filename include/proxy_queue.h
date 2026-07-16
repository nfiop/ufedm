/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __PROXY_EVENTFD__H
#define __PROXY_EVENTFD__H

#include "defs.h"

/* enum for proxy_queue_type type */
enum proxy_queue_type {
	PROXY_QUEUE_TYPE_READ = 0,
	PROXY_QUEUE_TYPE_WRITE,
	__PROXY_QUEUE_TYPE_MAX
};

#endif