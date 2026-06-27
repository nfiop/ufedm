obj-m := ufedm.o

ufedm-y := src/init.o \
           src/proxy_device/chrdev.o \
           src/proxy_device/class.o \
           src/proxy_device/device.o \
           src/proxy_device/eventfd.o \
           src/upper_mtd/device.o \
           src/backing_mtd/device.o

ccflags-y += -I$(src)/include -I$(src)/src
