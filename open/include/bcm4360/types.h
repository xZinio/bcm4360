/* SPDX-License-Identifier: ISC */
/*
 * Integer types. In the kernel these come from <linux/types.h>; the test
 * build for the emulator is freestanding and defines them here.
 */
#ifndef BCM4360_TYPES_H
#define BCM4360_TYPES_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/kernel.h>
#else

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char s8;
typedef short s16;
typedef int s32;
typedef long long s64;
typedef _Bool bool;
typedef unsigned long size_t;

#define true 1
#define false 0
#define NULL ((void *)0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BIT(n) (1U << (n))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#endif /* __KERNEL__ */

#endif
