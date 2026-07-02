#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "proxy_dev.h"
#include "proxy_ioctl.h"
#include "shared_mem.h"

static void test_invalid_mmap_non_shared(int fd, size_t shm_region_size)
{
	printf("[TEST] INVALID MMAP with MAP_PRIVATE (expect NULL)\n");

	void *map = mmap(
	    NULL, shm_region_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

	if (map == MAP_FAILED) {
		printf("  OK (expected failure)\n");
	} else {
		printf("  FAIL: unexpected success\n");
	}
}

static void test_invalid_mmap_size(int fd, size_t shm_region_size)
{
	printf("[TEST] INVALID MMAP with invalid size (expect NULL)\n");
	printf("Shared memory region size is %zu, testing with %zu\n",
	    shm_region_size, shm_region_size * 16);

	void *map = mmap(NULL, shm_region_size * 16, PROT_READ | PROT_WRITE,
	    MAP_SHARED, fd, 0);

	if (map == MAP_FAILED) {
		printf("  OK (expected failure), errno=%d (%s)\n", errno,
		    strerror(errno));
	} else {
		printf("  FAIL: unexpected success\n");
	}
}

static void test_valid_mmap(int fd, size_t shm_region_size)
{
	printf("[TEST] VALID MMAP\n");

	void *map = mmap(
	    NULL, shm_region_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if (map != MAP_FAILED) {
		printf("  OK (expected success)\n");
		munmap(map, shm_region_size);
	} else {
		printf("  FAIL: unexpected failure\n");
	}
}

int main(int argc, char **argv)
{
	int ret;
	int fd;
	size_t shm_region_size;
	struct proxy_shm_info shm_info;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <index>\n", argv[0]);
		return 1;
	}

	fd = open_proxy_device_by_argv_index(argv[1]);
	if (fd < 0) {
		return 1;
	}

	ret = ioctl(fd, PROXY_IOC_GET_SHM_INFO, &shm_info);
	if (ret < 0) {
		fprintf(stderr, "PROXY_IOC_GET_SHM_INFO ioctl failed: %s\n",
		    strerror(errno));
		return 1;
	}

	shm_region_size = get_shm_region_size(&shm_info);

	test_invalid_mmap_non_shared(fd, shm_region_size);
	test_invalid_mmap_size(fd, shm_region_size);
	test_valid_mmap(fd, shm_region_size);

	close(fd);
	return 0;
}
