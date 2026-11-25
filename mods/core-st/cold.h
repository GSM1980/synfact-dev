/* SPDX-License-Identifier: GPL-2.0-or-later
 * cold.h - the exception to -O3 in the core (docs/SYNTAKT-BUILD.md, "The -Os
 * exception"). Included last by the files whose code runs at boot, over USB or in
 * a storage transaction, never per tick or per block: sample-pool.c, hzfs.c,
 * files.c, smz.c, storage.c, machine-boot.c, high-storage.c, boot-preimages.c,
 * preferences.c (its public task entry remains CORE_HOT).
 * Their functions are compiled for size, which keeps the 64 KiB resident reserve
 * from filling (core 2.1.1: about 9.7 KB less). A function of these files that a
 * tick, a draw, a key, an encoder or a native path calls is marked CORE_HOT and
 * keeps the build's -O3.
 * Host builds (clang, the tests) ignore both.
 */
#ifndef CORE_COLD_H
#define CORE_COLD_H
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os","no-tree-loop-distribute-patterns")
#define CORE_HOT __attribute__((optimize("O3")))
#else
#define CORE_HOT
#endif
#endif
