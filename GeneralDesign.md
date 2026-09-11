# General design of the kernel module

The kernel module has simple lifetime model, and uses 3 sets of char
devices.

## Lifetime model

The kernel module owns the overall setup, but individual proxy devices 
are created once during module load and destroyed once during module unload. 

A nice load-module graph looks like this:
```
load module
    │
    ├── allocate array of upper_mtd_device
    │
    ├── find/hold the backing MTD devices
    │
    ├── create the proxy-device class
    │
    └── create the upper MTD devices
            │
            ├── upper MTD #0 → backing MTD #1
            └── upper MTD #1 → backing MTD #2
```

The opposite pattern is therefore applied - 
```
              UNLOAD
                 │
                 ▼
        destroy upper MTDs
                 │
                 ▼
       destroy proxy class
                 │
                 ▼
       release backing MTDs
                 │
                 ▼
          free ufedm memory
```

When the MTD layer asks an upper device to perform an operation, a state
associated with that particular operation is created -
```
MTD write()
     │
     ▼
ufedm request
     │
     ├── describe operation
     ├── expose/pass data to userspace
     ├── userspace performs ECC/etc.
     ├── userspace signals completion
     │
     ▼
backing MTD write()
     │
     ▼
request finished
     │
     ▼
request state can die
```

## Character devices

A good way to observe this -
```
                   Linux MTD users
                          │
                          │ read/write/ioctl
                          ▼
                 ┌─────────────────┐
                 │   Upper MTD     │
                 │                 │
                 │ /dev/mtdX       │
                 └────────┬────────┘
                          │
                    ufedm intercepts
                          │
                          ▼
                 ┌─────────────────┐
                 │   Proxy dev     │
                 │                 │
                 │  ufedm internal │
                 │  request path   │
                 └────────┬────────┘
                          │
                 userspace handles
                 ECC/data layout
                          │
                          ▼
                 ┌─────────────────┐
                 │  Backing MTD    │
                 │                 │
                 │ /dev/mtdY       │
                 └─────────────────┘
                          │
                          ▼
                       NAND flash
```

### Upper MTDs

ufedm creates a new MTD device that looks like a normal MTD device to
the rest of the kernel.

The upper MTD is what we want the rest of the Linux system to use.

### Backing MTDs

These are normal Linux MTD devices provided by some NAND controller/flash driver.

ufedm takes one of these and calls it the backend/backing MTD - this is
the device that ultimately performs the actual flash operation.

### Proxy devices

The proxy is essentially the implementation between the upper MTD and
backing MTD.

The proxy's job is essentially to take a normal MTD operation and turn it
into a request that can be serviced with userspace involvement.


## I/O handling

### General flow

The flow roughly looks like this:
 - The upper MTD operation turns into a slot in a kernel-managed request queue.
 - That slot is exposed through shared memory to userspace.
 - Then, userspace processes it and ACKs/NACKs it through an ioctl.
 - The kernel wakes the original MTD operation, which then continues.

In a call graph -

```
        Kernel
          │
          │ MTD read/write
          ▼
   ┌──────────────┐
   │   Upper MTD  │
   └──────┬───────┘
          │
          │ create/get slot
          ▼
   ┌──────────────┐
   │ Proxy device │
   │   I/O queue  │
   └──────┬───────┘
          │
          │ publish request
          ▼
   ┌──────────────┐
   │ Shared memory│
   │    slot      │
   └──────┬───────┘
          │
          │ eventfd notification
          ▼
   ┌──────────────┐
   │   Userspace  │
   │    worker    │
   └──────┬───────┘
          │
          │ ACK / NACK ioctl
          ▼
   ┌──────────────┐
   │ Proxy device │
   └──────┬───────┘
          │
          │ wake waiting request
          ▼
   ┌──────────────┐
   │   Upper MTD  │
   └──────┬───────┘
          │
          ▼
     Backing MTD
```

The proxy code has separate queues for read and write operations.

### Coordination

There are the datalen & ooblen fields in the slot header, which are used
together with userspace coordination -

#### Write requests

For a write request, datalen and ooblen describe the amount of data and OOB 
data supplied by the MTD caller for the current nand_io_iter iteration. 

The proxy places these requested bytes into the shared-memory slot and 
exposes the request to userspace. Userspace may then process or transform 
the data, including constructing the complete raw NAND page and OOB area 
required by the backing device. When userspace acknowledges the request, 
the shared-memory buffer is expected to contain the complete raw NAND page 
that should be written to the backing MTD, while datalen and ooblen continue
to describe only the logical amount requested by the MTD operation.

#### Read requests

For a read request, datalen and ooblen describe the amount of data and OOB 
data requested by the MTD caller for the current nand_io_iter iteration. 

The proxy may nevertheless populate the shared-memory slot with a complete 
raw NAND page and OOB area, since the underlying NAND operation is page-oriented. 

Userspace processes this raw page, performs any required ECC or data transformation, 
and returns the requested data through the shared-memory slot. Thus, datalen and 
ooblen represent the logical request size, not necessarily the size of the raw NAND
data present in the shared-memory buffer.
