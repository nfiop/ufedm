obj-m := ufedm.o

ufedm-y := src/init.o \
           src/proxy_device/chrdev.o \
           src/proxy_device/class.o \
           src/proxy_device/device.o

ccflags-y += -I$(src)/includes -I$(src)/src
ccflags-y += -DPROXY_DEVICE_COUNT=1
