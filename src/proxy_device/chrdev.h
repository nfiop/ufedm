#ifndef __PROXY_DEVICE_CHRDEV
#define __PROXY_DEVICE_CHRDEV

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/types.h>

#include "proxy_device/device.h"

int proxy_chrdev_create(dev_t devno, struct ufedm_proxy_device *new_device);
void proxy_chrdev_destory(struct ufedm_proxy_device *dev);

#endif
