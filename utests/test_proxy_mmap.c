#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "proxy_dev.h"
#include "shared_rings.h"

static void test_invalid_mmap_non_shared(int fd)
{
	printf("[TEST] INVALID MMAP with MAP_PRIVATE (expect NULL)\n");

	void *map = mmap(NULL, sizeof(struct shared_region),
	    PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

	if (map == MAP_FAILED) {
		printf("  OK (expected failure)\n");
	} else {
		printf("  FAIL: unexpected success\n");
	}
}

static void test_invalid_mmap_size(int fd)
{
	printf("[TEST] INVALID MMAP with invalid size (expect NULL)\n");

	void *map = mmap(NULL, sizeof(struct shared_region) + 1,
	    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if (map == MAP_FAILED) {
		printf("  OK (expected failure), errno=%d (%s)\n", errno,
		    strerror(errno));
	} else {
		printf("  FAIL: unexpected success\n");
	}
}

static void test_valid_mmap(int fd)
{
	printf("[TEST] VALID MMAP\n");

	void *map = mmap(NULL, sizeof(struct shared_region),
	    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if (map != MAP_FAILED) {
		printf("  OK (expected success)\n");
		munmap(map, sizeof(struct shared_region));
	} else {
		printf("  FAIL: unexpected failure\n");
	}
}

int main(int argc, char **argv)
{
	int fd;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <index>\n", argv[0]);
		return 1;
	}

	fd = open_proxy_device_by_argv_index(argv[1]);
	if (fd < 0) {
		return 1;
	}

	test_invalid_mmap_non_shared(fd);
	test_invalid_mmap_size(fd);
	test_valid_mmap(fd);

	close(fd);
	return 0;
}
