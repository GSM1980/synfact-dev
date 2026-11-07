/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CORE_ST_SMZ_H
#define CORE_ST_SMZ_H
#include "storage.h"
#define CORE_SMZ_ABI 0x454c0001u
/* Bump for every core change that can alter a bank's resident contract.
 * SAFE feature 16 stores this exact ID in the former global-init word. */
#define CORE_SMZ_CONTRACT 0x53594601u
#define CORE_SMZ_AUDIO 0x4204f750u
#define CORE_SMZ_AUDIO_MAX (0x42100000u-CORE_SMZ_AUDIO)
#define CORE_SMZ_PAGE 2048u
#define CORE_SMZ_MACHINES 16u
#define CORE_SMZ_PARAMS 32u
/* The last four controls have reserved IDs/page positions. They require a
 * storage extension before a bank may declare them as usable controls. */
#define CORE_SMZ_STORED_PARAMS 28u
#define CORE_SMZ_EXT_STATE (8u*24u*2u)
#define CORE_SMZ_MAIN_MAX 0x200000u
#define CORE_SMZ_NONE 0xffffffffu
/* Feature 8: at most 16 mods in the bank's mod-info directory (SMZ4.md). */
#define CORE_SMZ_MOD_INFO_MAX 16u
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
