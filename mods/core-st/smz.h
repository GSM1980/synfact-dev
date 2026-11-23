/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CORE_ST_SMZ_H
#define CORE_ST_SMZ_H
#include "storage.h"
#define CORE_SMZ_ABI 0x454c0001u
/* Bump for every core change that can alter a bank's resident contract.
 * SAFE feature 16 stores this exact ID in the former global-init word. */
#define CORE_SMZ_CONTRACT 0x53594606u
#define CORE_SMZ_AUDIO 0x4204f750u
/* Core 2.2: 4 MiB, sample store pages 4000..47ff (docs/AUDIO-MEMORY-MAP.md). */
#define CORE_SMZ_AUDIO_MAX 0x400000u
#define CORE_SMZ_PAGE 2048u
#define CORE_SMZ_MACHINES 16u
#define CORE_SMZ_PARAMS 32u
/* The last four controls have reserved IDs/page positions. They require a
 * storage extension before a bank may declare them as usable controls. */
#define CORE_SMZ_STORED_PARAMS 28u
#define CORE_SMZ_EXT_STATE (8u*24u*2u)
#define CORE_SMZ_MAIN_MAX 0x200000u
#define CORE_SMZ_NONE 0xffffffffu
/* Core 2.6.1: up to 32 modules, each with its own parameter-provider slot,
 * native mask slot and native route bit (16 until 2.6.0). Machines stay at
 * 16: their types 64..79 own the parameter IDs 0x1f9..0x3f8. */
#define CORE_SMZ_MODULES 32u
/* Feature 8: one record per mod folder, machine or module (SMZ4.md). */
#define CORE_SMZ_MOD_INFO_MAX (CORE_SMZ_MACHINES+CORE_SMZ_MODULES)
/* Disk words are BE. Only machines are supported, no runtime patch plans,
 * hook subscriptions or global module initializers. */
struct core_smz_view {
    core_u32 image,bytes,relocs,nrel,descriptors,count,audio,audio_bytes,ui,modules,nmods,info,contract;
};
struct core_smz_reader {
    void *context;
    int (*read)(void *,core_u32,void *,core_u32);
    core_u32 bytes;
};
core_u32 core_smz_be(const void *);
core_u32 core_smz_adler(core_u32,const core_u8 *,core_u32);
/* Read-only, complete validation. Does not allocate, execute, relocate or
 * publish anything. Reader errors are failures, including checksum reads. */
int core_smz_validate(const struct core_smz_reader *,struct core_smz_view *);
int core_smz_memory_read(void *,core_u32,void *,core_u32);
/* Call only on the identical, validated file in privately owned RAM. */
void core_smz_relocate(core_u8 *,const struct core_smz_view *);
#endif
