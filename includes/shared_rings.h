#ifndef __RING_H
#define __RING_H

#ifdef __KERNEL__

#include <linux/types.h>

#else

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#endif

#define RING_SIZE (64 * 1024)

struct ring {
	u32 head;
	u32 tail;
	u32 size;
	u8 data[RING_SIZE];
};

struct op_rings {
	struct ring tx;
	struct ring rx;
};

struct shared_region {
	struct op_rings read;
	struct op_rings write;
};

#endif
