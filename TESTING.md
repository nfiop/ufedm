# Testing

There are 2 options for testing of this repository and its components:

## Testing on a development board

The kernel module will be in `build/kmod` directory after a cross
compilation is done - see [README](README.md) for instructions on cross
compilation with a buildroot environment.

You can then upload it to the Olimex LIME2 board and load it as usual.

## Testing with `nandsim` on your local machine

With `nandsim`, you can run tests on any platform (x86, ARM64, RISC-V, you name it...) -
it's a powerful utility for those lacking the physical hardware and want to verify
the capabilities of this module.

On x86 machine (probably what you have near your desk), just compile with a normal gcc:
```
./build.sh
```

Load `nandsim` like so (I chose a standard NAND flash chip to simulate):
```sh
modprobe nandsim first_id_byte=0x20 second_id_byte=0xaa third_id_byte=0x00 \
fourth_id_byte=0x15 pagesize=2048 oobpagesize=64 eraseblock_size=131072
```

You should see a kernel log output similar to this:
```
nand: ST Micro NAND 256MiB 1,8V 8-bit
nand: 256 MiB, SLC, erase size: 128 KiB, page size: 2048, OOB size: 64
```

Then attach this driver & specify the corresponding MTD index:
```sh
insmod build/kmod/ufedm.ko mtds=0
```

`nandsim` can technically simulate almost any NAND flash chip you might
think of.

### Running in a VM

Running this kernel module during development stage could be dangerous
to the system stability.

To prevent a kernel crash on your host, you can run a QEMU VM with
`nandsim` being automatically loaded with your Linux image and a custom
built initramfs containing the `build` directory inside.

See `run-nandsim-vm.sh` for more details, but something like this should
get you started:

```sh
KERNEL=/boot/vmlinuz-linux ./run-nandsim-vm.sh
```

Adjust the `KERNEL` to your environment and make sure you have read
permissions on both files.
