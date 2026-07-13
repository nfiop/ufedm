/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
