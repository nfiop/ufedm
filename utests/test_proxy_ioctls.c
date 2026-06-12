#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

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

static void test_proxy_get_ring_info_cmd(int fd)
{
    int ret;
    struct proxy_ring_info d;
    printf("[TEST] PROXY_IOC_GET_RING_INFO\n");

    ret = ioctl(fd, PROXY_IOC_GET_RING_INFO, &d);
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
    char path[64];
    unsigned long index;
    char *end;

    int fd;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <index>\n", argv[0]);
        return 1;
    }

    errno = 0;
    index = strtoul(argv[1], &end, 10);

    /* Validation */
    if (errno != 0) {
        perror("strtoul");
        return 1;
    }

    if (*end != '\0') {
        fprintf(stderr, "Invalid input: trailing characters '%s'\n", end);
        return 1;
    }

    if (index > 1000) {
        fprintf(stderr, "Index too large (max 1000)\n");
        return 1;
    }

    snprintf(path, sizeof(path), "/dev/ufedm_proxy%lu", index);

    printf("Opening %s\n", path);

    fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    printf("Device opened successfully\n");

    test_proxy_get_mtd_info_cmd(fd);
    test_proxy_get_ring_info_cmd(fd);
    test_proxy_get_stats_info_cmd(fd);
    test_unknown_ioctl(fd);

    close(fd);
    return 0;
}
