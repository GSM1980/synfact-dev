/* SPDX-License-Identifier: GPL-2.0-or-later
 * md-types.h - the fixed-width types the mdkit voices use.
 *
 * The Syntakt build is freestanding and its toolchain ships no <stdint.h> or
 * <stddef.h>; a host build (the generator and the tests) uses the host's own.
 * On the Syntakt, MD_ST_TARGET leaves out the float renders and MD_ST_TABLES
 * reads the double-precision functions from tables (md-tables.h).
 */
#ifndef MD_TYPES_H
#define MD_TYPES_H
#if defined(__mcoldfire__) || defined(__mcfv4__)
#define MD_ST_TARGET 1
#ifndef MD_ST_TABLES
#define MD_ST_TABLES 1
#endif
typedef signed char int8_t;typedef unsigned char uint8_t;
typedef short int16_t;typedef unsigned short uint16_t;
typedef int int32_t;typedef unsigned int uint32_t;
typedef long long int64_t;typedef unsigned long long uint64_t;
typedef unsigned int size_t;typedef unsigned int uintptr_t;
#define INT64_C(x) x##LL
#define UINT64_C(x) x##ULL
#define INT32_C(x) x
#define UINT32_C(x) x##U
#ifndef NULL
#define NULL ((void *)0)
#endif
#else
#include <stdint.h>
#include <stddef.h>
#endif
/* A voice descriptor's float render: the host's, absent on the Syntakt. */
#ifdef MD_ST_TARGET
#define MD_ST_RENDER(f) 0
#else
#define MD_ST_RENDER(f) f
#endif
#endif
