#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>

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

static void test_proxy_eventfd_register_unregister(int fd)
{
	int ret;

	printf("[TEST] EVENT FD REGISTER IOCTL (expect -EINVAL)\n");

	int read_efd = eventfd(0, 0);
	if (read_efd < 0) {
		printf("  FAIL: failed to allocate read eventfd: %s\n",
		    strerror(errno));
		return;
	}

	int write_efd = eventfd(0, 0);
	if (write_efd < 0) {
		printf("  FAIL: failed to allocate write eventfd: %s\n",
		    strerror(errno));
		return;
	}

	struct proxy_register_eventfd reg_efd;

	reg_efd.type = PROXY_EVENTFD_WRITE_BUFFER;
	reg_efd.fd = write_efd;

	ret = ioctl(fd, PROXY_IOC_REGISTER_EVENTFD, &reg_efd);
	if (ret < 0)
		printf("  FAIL: PROXY_IOC_REGISTER_EVENTFD "
		       "(PROXY_EVENTFD_WRITE_BUFFER): %s\n",
		    strerror(errno));
	else
		printf("  OK: PROXY_IOC_REGISTER_EVENTFD "
		       "(PROXY_EVENTFD_WRITE_BUFFER)\n");

	reg_efd.type = PROXY_EVENTFD_READ_BUFFER;
	reg_efd.fd = read_efd;

	ret = ioctl(fd, PROXY_IOC_REGISTER_EVENTFD, &reg_efd);
	if (ret < 0)
		printf("  FAIL: PROXY_IOC_REGISTER_EVENTFD "
		       "(PROXY_EVENTFD_READ_BUFFER): %s\n",
		    strerror(errno));
	else
		printf("  OK: PROXY_IOC_REGISTER_EVENTFD "
		       "(PROXY_EVENTFD_READ_BUFFER)\n");

	struct proxy_unregister_eventfd unreg_efd;
	unreg_efd.type = PROXY_EVENTFD_WRITE_BUFFER;
	ret = ioctl(fd, PROXY_IOC_UNREGISTER_EVENTFD, &unreg_efd);
	if (ret < 0)
		printf("  FAIL: PROXY_IOC_UNREGISTER_EVENTFD "
		       "(PROXY_EVENTFD_WRITE_BUFFER): %s\n",
		    strerror(errno));
	else
		printf("  OK: PROXY_IOC_UNREGISTER_EVENTFD "
		       "(PROXY_EVENTFD_WRITE_BUFFER)\n");

	unreg_efd.type = PROXY_EVENTFD_READ_BUFFER;
	ret = ioctl(fd, PROXY_IOC_UNREGISTER_EVENTFD, &unreg_efd);
	if (ret < 0)
		printf("  FAIL: PROXY_IOC_UNREGISTER_EVENTFD "
		       "(PROXY_EVENTFD_READ_BUFFER): %s\n",
		    strerror(errno));
	else
		printf("  OK: PROXY_IOC_UNREGISTER_EVENTFD "
		       "(PROXY_EVENTFD_READ_BUFFER)\n");
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
	test_proxy_eventfd_register_unregister(fd);
	test_unknown_ioctl(fd);

	close(fd);
	return 0;
}
