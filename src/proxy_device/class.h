#ifndef __PROXY_DEVICE_CLASS
#define __PROXY_DEVICE_CLASS

#include "proxy_device/device.h"

int proxy_device_class_init(size_t dev_count);
void proxy_device_class_exit(void);

struct ufedm_proxy_device *proxy_device_resolve_by_minor(int minor);

#endif
