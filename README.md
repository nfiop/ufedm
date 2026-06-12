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

Point CMake to the Buildroot directory with the kernel sources and
the prepared toolchain, for example:
```sh
cmake -B build -DBUILDROOT_DIR=../buildroot/ -DKERNEL_ARCH=$(grep '^BR2_ARCH=' ../buildroot/.config | cut -d'=' -f2)
```

OR simply:
```
cmake -B build -DBUILDROOT_DIR=../buildroot/ -DKERNEL_ARCH=arm
```

## Compile

Simply run in the `build` directory:
```sh
make
```

A one-liner to do this:
```sh
pushd build; make clean; make; popd
```

## Run

The kernel module will be in `build/kmod` directory now.

You can then upload it to the Olimex LIME2 board and load it as usual.

