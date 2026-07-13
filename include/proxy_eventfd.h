/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#ifndef __PROXY_EVENTFD__H
#define __PROXY_EVENTFD__H

#include "defs.h"

/* enum for proxy_eventfd_type type */
enum proxy_eventfd_type {
	PROXY_EVENTFD_TYPE_READ = 0,
	PROXY_EVENTFD_TYPE_WRITE,
};

#endif