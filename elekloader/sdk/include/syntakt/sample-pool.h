/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CORE_ST_SAMPLE_POOL_H
#define CORE_ST_SAMPLE_POOL_H
#include "storage.h"
typedef unsigned char pool_u8;
typedef unsigned short pool_u16;
typedef unsigned int pool_u32;
struct st_machine_wave;struct st_machine_cursor;
const struct st_machine_wave *core_pool_wave(pool_u32);
void core_pool_wave_idle(pool_u32,pool_u32);
int core_pool_sample_name(pool_u32,char *);
const struct st_machine_cursor *core_pool_cursor(pool_u32,pool_u32);
void core_pool_cursor_tick(void);
pool_u32 core_pool_sample_source(pool_u32,const pool_u8 **,pool_u32 *);
pool_u32 core_pool_sample_channels(pool_u32 slot);
pool_u32 core_pool_sample_wavetable(pool_u32 slot); /* core 2.8: waves, or 0 */
pool_u32 core_pool_wavetable_level(pool_u32 slot,pool_u32 level,short *out,pool_u32 max); /* UI API 9 */
pool_u32 core_pool_sample_read(pool_u32 slot,pool_u32 first,pool_u32 count,short *out); /* UI API 9 */
pool_u32 core_pool_code_used(pool_u32 code);
void core_pool_wave_begin(pool_u32 code,pool_u32 frames,pool_u32 channels);
void core_pool_wave_feed(const pool_u8 *,pool_u32);
void core_pool_wave_finish(pool_u32);
int core_pool_wave_storage(pool_u32 slot,pool_u32 out[5]);
void core_pool_stock_changed(pool_u32 address);
#define CORE_POOL_SLOTS 128u
#define CORE_POOL_STOCK_SLOTS 64u
#define CORE_POOL_OFFSET 0xf0u
#define CORE_POOL_RECORD 0x110u
#define CORE_POOL_HZ 0x8000u
/* Core 2.2: sample store pages 8001..ff40, above AUDIO's own page 8000; the 64
 * KiB below 48000000 are the AUDIO stack's (docs/AUDIO-MEMORY-MAP.md). */
#define CORE_POOL_AUDIO 0x4404ff50u
#define CORE_POOL_PCM (CORE_POOL_AUDIO+4096u)
#define CORE_POOL_END 0x47feff50u
#define CORE_POOL_MAGIC 0x45505331u
extern const unsigned char core_pool_image[];
extern unsigned int core_pool_boot_pages,core_pool_busy;
int core_pool_available(void);
int core_pool_ready(void);
int core_pool_valid(const pool_u8 *project);
void core_pool_read(const pool_u8 *,pool_u16 *);
void core_pool_write(pool_u8 *,const pool_u16 *);
void core_pool_empty(pool_u8 *);
void core_pool_created(void);
void core_pool_prepare(void);
extern pool_u32 core_pool_stock_bytes;
const void *core_pool_stock_page(pool_u32 ordinal);
void core_pool_boot_release(void);
const void *core_pool_boot_page(pool_u32 ordinal,pool_u32 *wire);
int core_pool_sync(void);
int core_pool_cached_name(pool_u32 code,char *out);
void core_pool_before_validate(pool_u8 *);
void core_pool_after_validate(pool_u8 *,pool_u32);
void core_pool_after_load(pool_u32,pool_u32);
void core_pool_after_snapshot(pool_u8 *,pool_u8 *);
void core_pool_before_save(void *,pool_u32);
void core_pool_ui_progress(pool_u32,pool_u32);
void core_pool_ui_file(pool_u32,pool_u32,pool_u32);
struct st_pool_ui_callbacks {
    unsigned int version;
    void (*menu)(void *);
    void (*begin)(unsigned int);
    void (*end)(void);
    void (*file)(unsigned int,unsigned int,unsigned int);
    void (*progress)(unsigned int,unsigned int);
};
/* Task/UI snapshot: actual PCM bytes per slot; used counts unique AUDIO page
 * extents referenced by slots 65..128 only. Stock PCM has its own budget. */
struct st_pool_memory {
    pool_u32 used,total,bytes[CORE_POOL_SLOTS];
};
void core_pool_memory(struct st_pool_memory *);
struct st_sample_pool_api {
    unsigned int version;
    int (*valid)(const unsigned char *);
    int (*sync)(void);
    int (*cached_name)(unsigned int,char *);
    int (*preview)(unsigned int);
    void (*refill)(void);
    void (*release)(void);
    int (*bind_ui)(const struct st_pool_ui_callbacks *);
    /* API 2 appends this service, preserving the API 1 prefix. */
    void (*memory)(struct st_pool_memory *);
};
extern const struct st_sample_pool_api core_sample_pool_api;
void core_pool_ui_reset(void);
void st_pool_preview_stop(void);
int st_pool_live_sync(const unsigned short *);
#endif
