# ufedm - Userspace flash ECC & data management

This kernel module registers a new `mtd` device on top of a "raw"
mtd device, to act as a proxy for I/O requests.

It also registers a new character device which userspace should open
for handling of I/O requests - especially for managmenent of error-correction
and things alike.

## Prepare for compiling

If not clean, clean the build directory
```sh
./build.sh clean
```

## Cross compile (with buildroot)

Simply run (with adjustments to your buildroot path):
```sh
./build.sh --buildroot ../buildroot/
```

## Run on a development board

The kernel module will be in `build/kmod` directory now.

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

### Data restoration as an example use case

Data restoration people might find using `nandsim` extremely valuable -
it could be useful in case of a damaged chip and having a backup, even
one that's incomplete, from a chip, without needing the actual physical
chip or risking ruining the actual data on the chip further.

In such case you can identify bad block markers yourself and instantiate
an appropriate `nandsim` instance with the data to examine valuable data
and keeping a backup of such data before trying to apply it on a new
chip.

### Running in a VM

Running this kernel module during development stage could be dangerous
to the system stability.

To prevent a kernel crash on your host, you can run a QEMU VM with
`nandsim` being automatically loaded with your Linux image and a custom
built initramfs containing the `build` directory inside.

See `run-nandsim-vm.sh` for more details, but something like this should
get you started:

```sh
KERNEL=/boot/vmlinuz-linux INITRD=/boot/initramfs-linux.img \
  ./run-nandsim-vm.sh
```

Adjust the `KERNEL` and `INITRD` to your environment and make sure you
have read permissions on both files.

## Limitations

There are several limitations in the design, some are easily solvable and
some not without putting some effort on your side:

- Raw access on some SPI NAND flash chips are disallowed. This is probably
  due to a constraint on the hardware side. 
  
  See `drivers/mtd/nand/spi/skyhigh.c` for more details.

  For these chips, we probably can't do much anyway without writing with
  ECC & data layout in the way they **want** anyway.

- We rely on a raw NAND flash controller. As ChatGPT said - 

  "The driver will attempt to minimize transformation of data/OOB as 
  much as it supports"
  If you use a NAND flash controller that does ECC in its die or transform
  the input before sending it to the NAND flash chip, this driver will
  fail to work.

  I asked ChatGPT -
  "if my NAND controller doesn't transform data bytes or change their
  location and allow bypassing ECC, am I good then?"

  And the answer was - 
  "Yes — if (and only if) your NAND controller truly behaves that way,
  then your design is internally consistent."
  For a NAND flash controller like the one on the Olimex A20 board,
  we're probably fine.

- Some NAND flash chips have a flag of scrambling, due to high possibility
  of data corruption if data is not "randomized" upon write.

  We don't handle those chips - see `src/upper_mtd/backend.c` for more
  details on how to fix this on your setup.

- Although we solve this by hacking a check of a SPI NAND flash chip
  vs raw flash chips, there's no official check for the actual type,
  so we are "fine" for version 6.16.8 of the Linux kernel.

  See `src/upper_mtd/backend.c` for more details on the check we do.

- After a write to a page, new data must written after an eraseblock
  erasure. We cannot allow skipping this, and when I tested on a `nandsim`
  simulation in the QEMU VM, it still required me to run `flash_eraseall`
  on the backing MTD device.

  IMPORTANT: Please take a note of the bad block markers in devices,
  because many vendors mark them as the chip is manufactured, so the
  bad block marks can be gone if not erasing carefully.
