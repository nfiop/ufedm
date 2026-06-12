# ufedm - Userspace flash ECC & data management

This kernel module registers a new `mtd` device on top of a "raw"
mtd device, to act as a proxy for I/O requests.

It also registers a new character device which userspace should open
for handling of I/O requests - especially for managmenent of error-correction
and things alike.

## Prepare for compiling

If not clean, clean the build directory
```sh
rm -rf build/
mkdir -p build
```

## Compile

Simply run (with adjustments to your buildroot path):
```sh
KERNEL_ARCH=arm BUILDROOT_DIR=../buildroot/ ./build.sh
```

## Run

The kernel module will be in `build/kmod` directory now.

You can then upload it to the Olimex LIME2 board and load it as usual.

