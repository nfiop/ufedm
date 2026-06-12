obj-m := ufedm.o

ufedm-y := src/init.o \
           src/proxy_device/chrdev.o \
           src/proxy_device/class.o \
           src/proxy_device/device.o \
           src/upper_mtd/device.o \
           src/upper_mtd/backend.o

ccflags-y += -I$(src)/include -I$(src)/src
