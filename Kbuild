obj-m := ufedm.o

ufedm-y := kernel/init.o \
           kernel/proxy_device/chrdev.o \
           kernel/proxy_device/class.o \
           kernel/proxy_device/device.o \
           kernel/proxy_device/eventfd.o \
           kernel/upper_mtd/device.o \
           kernel/backing_mtd/device.o

ccflags-y += -I$(src)/include -I$(src)/kernel
