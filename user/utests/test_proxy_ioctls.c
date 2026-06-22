#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#include "proxy_dev.h"
#include "proxy_ioctl.h"

static int __test_proxy_get_mtd_info_cmd(int fd)
{
    int ret;
    struct proxy_mtd_info d;

    ret = ioctl(fd, PROXY_IOC_GET_MTD_INFO, &d);
    if (ret < 0)
        return -errno;
    return 0;
}

static void test_proxy_get_mtd_info_cmd(int fd)
{
    int ret;
    int retries = 0;

    printf("[TEST] PROXY_IOC_GET_MTD_INFO\n");

    while (1) {
        ret = __test_proxy_get_mtd_info_cmd(fd);

        if (ret == 0)
            break;

        if (ret != -EAGAIN) {
            printf("  FAIL: %s\n", strerror(-ret));
            return;
        }

        if (++retries > 1000) {
            printf("  FAIL: too many retries\n");
            return;
        }

        usleep(1000); // 1ms
    }

    printf("  OK\n");
}

static void test_proxy_get_shm_info_cmd(int fd)
{
    int ret;
    struct proxy_shm_info d;
    printf("[TEST] PROXY_IOC_GET_SHM_INFO\n");

    ret = ioctl(fd, PROXY_IOC_GET_SHM_INFO, &d);
    if (ret < 0)
        printf("  FAIL: %s\n", strerror(errno));
    else
        printf("  OK\n");
}

static void test_proxy_get_stats_info_cmd(int fd)
{
    int ret;
    struct proxy_stats d;

    printf("[TEST] PROXY_IOC_GET_STATS\n");
    ret = ioctl(fd, PROXY_IOC_GET_STATS, &d);
    if (ret < 0)
        printf("  FAIL: %s\n", strerror(errno));
    else
        printf("  OK\n");
}

static void test_unknown_ioctl(int fd)
{
    int ret;
    printf("[TEST] INVALID IOCTL (expect -EINVAL)\n");

    struct proxy_stats d;

    ret = ioctl(fd, 0xdeadbeef, &d);
    if (ret < 0)
        printf("  OK (expected failure): %s\n", strerror(errno));
    else
        printf("  FAIL: unexpected success\n");
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

	test_proxy_get_mtd_info_cmd(fd);
	test_proxy_get_shm_info_cmd(fd);
	test_proxy_get_stats_info_cmd(fd);
	test_unknown_ioctl(fd);

	close(fd);
	return 0;
}
