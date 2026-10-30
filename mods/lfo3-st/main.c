/* SPDX-License-Identifier: GPL-2.0-or-later
 * Third LFO, engine stage. Thirteen engines (12 tracks + FX) run the Synfact
 * LFO3 kernel (render.s, native OS 1.41 algorithm, own state and RNG) before
 * the two native LFOs, from the core's LFO operation, so LFO3 may modulate
 * LFO1/LFO2 parameters in the same frame. Non-consuming: POLY or the native
 * engine continue unchanged. No native code is patched.
 *
 * MAIN SRAM: the kernel runs from a copy in the first tail and its state,
 * RNG and parameter bank live in the second tail (SRAM data never misses).
 * Both tails must still be all zero as the stock boot leaves them; otherwise,
 * or if the copy's CRC ever changes, the same kernel and data run from DDR.
 */
#include <syntakt/native.h>
#include <syntakt/host.h>
#include <syntakt/sound-meta.h>
typedef unsigned int u32;typedef unsigned short u16;typedef unsigned char u8;
#ifndef LFO3_MEM
#define LFO3_MEM(a) ((volatile u8 *)(a))
#define LFO3_FILL ((void (*)(void *,int,int,int,int,u32))0x400f7df2u)
#define LFO3_TEXT ((void (*)(void *,void *,int,int,u32,const char *,const char *))0x400f8a0cu)
#endif
#define TRACKS 13u
typedef void (*render_t)(u8 *,u32,u32,const u16 *,u8 *,u32 *);
extern void lfo3_render(u8 *,u32,u32,const u16 *,u8 *,u32 *);
extern const u8 lfo3_render_end[];
extern void lfo3_sync(void);
/* Parameter record per engine: SPD, MULT, FADE, DEST (slot<<8, 7f00 = none),
 * WAVE, SPH/SLEW, MODE, DEP, in native LFO units. */
struct lfo3_bank {u8 state[TRACKS][40];u32 rng[2];u16 params[TRACKS][8];u16 overlay[TRACKS][8];};
static const u16 defaults[8]={0x7000,0x300,0x4000,0x7f00,0,0,0,0x4000};
static const u32 tails[2][2]={{0x80007330u,0x80008000u},{0x8000f860u,0x80010000u}};
#define SRAM_CODE 0x80007340u
#define SRAM_BANK 0x8000f870u
static const struct st_machine_host_api *host;
static struct lfo3_bank ddr_bank;
struct lfo3_bank *lfo3_bank;
static render_t render;
static u32 code_bytes,code_crc,ticks,in_sram,refused;

static u32 crc(u32 at,u32 n){return ~host->crc_update(~0u,(const u8 *)LFO3_MEM(at),n);}
static void bank_reset(struct lfo3_bank *b)
{
    u32 t,i;
    for(t=0;t<TRACKS;t++){
        for(i=0;i<40;i++)b->state[t][i]=0;
        *(u32 *)&b->state[t][16]=0x3fffffffu;
        for(i=0;i<8;i++)b->params[t][i]=defaults[i];
    }
    b->rng[0]=0x12345678u;b->rng[1]=0x9abcdef0u;     /* Synfact seeds */
}
static void use_ddr(void)
{
    bank_reset(&ddr_bank);lfo3_bank=&ddr_bank;render=lfo3_render;in_sram=0;
}
/* Returns 0 with the kernel and bank in SRAM, else the refusal reason. */
static u32 install(void)
{
    u32 i,at,n=(u32)lfo3_render_end-(u32)lfo3_render;
    if(SRAM_CODE+n>tails[0][1] || SRAM_BANK+sizeof(struct lfo3_bank)>tails[1][1])return 1;
    for(i=0;i<2;i++)for(at=tails[i][0];at<tails[i][1];at+=4)
        if(*(volatile u32 *)at)return 2;              /* not as the stock boot left it */
    for(i=0;i<n;i++)LFO3_MEM(SRAM_CODE)[i]=((const u8 *)lfo3_render)[i];
    code_bytes=n;code_crc=crc((u32)lfo3_render,n);
    if(crc(SRAM_CODE,n)!=code_crc){for(i=0;i<n;i++)LFO3_MEM(SRAM_CODE)[i]=0;return 3;}
    lfo3_sync();
    lfo3_bank=(struct lfo3_bank *)SRAM_BANK;bank_reset(lfo3_bank);
    render=(render_t)SRAM_CODE;in_sram=1;
    return 0;
}
/* ---- UI hooks installed on stock code (preimages checked, all or none) ---- */
extern void lfo3_constructor(void),lfo3_wave_site(void),lfo3_ui_get_entry(void),lfo3_editor_entry(void),
    lfo3_widget_valid_entry(void),lfo3_widget_header_entry(void),lfo3_widget_view_get_entry(void),
    lfo3_widget_wave_flags(void),lfo3_sound_publication(void),lfo3_sound_success(void),lfo3_fx_publication(void),
    lfo3_resolver_entry(void),lfo3_encoder_metadata(void),lfo3_encoder_finish_metadata(void),
    lfo3_popup_metadata(void),lfo3_group_destination(void),lfo3_sound_get(void),lfo3_fx_get(void),
    lfo3_fx_store(void),lfo3_relative_entry(void),lfo3_editor_proxy_alt(void),lfo3_editor_pattern_alt(void),
    lfo3_relative_destination(void),lfo3_relative_fx_destination(void),lfo3_validate_entry(void),
    lfo3_validate_alt_entry(void),lfo3_validate_third_entry(void),lfo3_memcpy_entry(void),
    lfo3_sound_new_entry(void),lfo3_kit_new_entry(void),lfo3_fx_new_entry(void),lfo3_relocate_entry(void),
    lfo3_kit_encode_entry(void),lfo3_kit_decode_entry(void),lfo3_serialize_entry(void),
    lfo3_deserialize_entry(void),lfo3_decode_project_entry(void),lfo3_s1_save_entry(void),lfo3_s1_load_entry(void),
    lfo3_fx_encode_entry(void),lfo3_fx_decode_entry(void),lfo3_native_reset_entry(void),
    lfo3_actions_page_entry(void),lfo3_actions_capture_entry(void),lfo3_actions_paste_compat(void),
    lfo3_actions_paste_translate(void),lfo3_actions_random_metadata(void),lfo3_actions_random_dest(void),
    lfo3_actions_random_bound(void),lfo3_actions_reload_bound(void),lfo3_actions_reload_scope(void),
    lfo3_actions_reload_read(void),lfo3_actions_reload_parameter(void),
    lfo3_owned_copy_entry(void),lfo3_lock_ui_fx_store(void),lfo3_lock_ui_view_get(void);
/* p-lock entries (locks/*.s, locks/*.c) */
extern void lfo3_events_allocate_entry(void),
    lfo3_events_apply(void),
    lfo3_events_capture_entry(void),
    lfo3_events_clear(void),
    lfo3_events_convert(void),
    lfo3_events_fx_restore(void),
    lfo3_events_pool_init(void),
    lfo3_events_restore(void),
    lfo3_lock_ui_alternate(void),
    lfo3_lock_ui_slot(void),
    lfo3_owned_allocate_entry(void),
    lfo3_owned_clear_page(void),
    lfo3_owned_clear_track(void),
    lfo3_owned_clear_track_action(void),
    lfo3_owned_copy_page(void),
    lfo3_owned_copy_pattern(void),
    lfo3_owned_copy_track(void),
    lfo3_owned_move(void),
    lfo3_owned_paste_page(void),
    lfo3_owned_paste_patterns(void),
    lfo3_owned_paste_track(void),
    lfo3_owned_pattern_decode(void),
    lfo3_owned_release_entry(void),
    lfo3_pattern_clear(void),
    lfo3_pattern_clear_row(void),
    lfo3_pattern_compress(void),
    lfo3_pattern_delete_entry(void),
    lfo3_pattern_encode(void),
    lfo3_pattern_get_entry(void),
    lfo3_pattern_present_entry(void),
    lfo3_pattern_row_count(void),
    lfo3_pattern_set(void),
    lfo3_pattern_total(void),
    lfo3_pattern_update(void);
/* Shadow layer configuration (Synfact integration.c values, OS 1.41). */
extern u32 kits_base,pool_base,image_container,save_buffer,reload_buffer;
extern u32 lfo3_lock(void),lfo3_engine_busy(void);extern const u16 *lfo3_events_bank(void);extern void lfo3_unlock(u32),lfo3_code16(u32,u32);
extern void lfo3_records_init(void);extern u32 lfo3_locks_setup(void);
extern int lfo3_page_native(u32,const u32 *,u32 *),lfo3_parameter_query(u32,u32 *,void *,u32 *);
/* call = 1: the site is a `jsr abs.l` whose target is replaced (lock slot calls). */
static const struct site {u32 at;u8 bytes;u8 stock[10];void (*target)(void);u8 call;} sites[]={
    {0x400f38f2u,8,{0x4f,0xef,0xff,0xd8,0x48,0xd7,0x0c,0x0c},lfo3_constructor},
    {0x400f37f8u,6,{0x20,0x2a,0x00,0x90,0x66,0x4a},lfo3_wave_site},
    {0x40039160u,6,{0x4f,0xef,0xff,0xd8,0x70,0xff},lfo3_ui_get_entry},
    {0x40037f9au,8,{0x4f,0xef,0xff,0xcc,0x48,0xd7,0x1c,0xfc},lfo3_editor_entry},
    {0x400123e6u,8,{0x4f,0xef,0xff,0xe8,0x48,0xd7,0x04,0x0c},lfo3_widget_valid_entry},
    {0x40037d80u,8,{0x20,0x6f,0x00,0x04,0x20,0x28,0x00,0x90},lfo3_widget_header_entry},
    {0x40037e50u,8,{0x4f,0xef,0xff,0xe8,0x48,0xd7,0x0c,0x3c},lfo3_lock_ui_view_get},  /* -> widget view get */
    {0x400f3708u,10,{0x2f,0x2a,0x00,0x74,0x4e,0xb9,0x40,0x01,0xff,0x74},lfo3_widget_wave_flags},
    {0x400a31a2u,8,{0x4f,0xef,0xff,0xf4,0x48,0xd7,0x04,0x0c},lfo3_sound_publication},
    {0x400a3226u,6,{0x4e,0xf9,0x40,0x09,0x76,0xb4},lfo3_sound_success},
    {0x400a1d18u,8,{0x20,0x39,0x43,0xbd,0xe4,0x44,0x22,0x00},lfo3_fx_publication},
    {0x4001c422u,10,{0x4f,0xef,0xff,0xf4,0x70,0x0d,0x48,0xd7,0x00,0x1c},lfo3_resolver_entry},
    {0x4003b8d4u,10,{0x0c,0x80,0x00,0x00,0x01,0xf8,0x63,0x02,0x42,0x80},lfo3_encoder_metadata},
    {0x4003baaeu,10,{0x0c,0x82,0x00,0x00,0x01,0xf8,0x62,0x00,0x00,0xc0},lfo3_encoder_finish_metadata},
    {0x4003b6beu,10,{0x0c,0x82,0x00,0x00,0x01,0xf8,0x62,0x04,0x20,0x02},lfo3_popup_metadata},
    {0x4003b11au,6,{0x70,0x59,0xb0,0x83,0x67,0x08},lfo3_group_destination},
    {0x40012986u,8,{0x2f,0x0a,0x2f,0x02,0x24,0x6f,0x00,0x0c},lfo3_sound_get},
    {0x400129ecu,8,{0x2f,0x0a,0x2f,0x02,0x24,0x6f,0x00,0x0c},lfo3_fx_get},
    {0x40013516u,8,{0x4e,0x56,0xff,0xdc,0x48,0xd7,0x04,0x7c},lfo3_lock_ui_fx_store},  /* -> lfo3_fx_store */
    {0x40012316u,8,{0x4f,0xef,0xff,0xd4,0x48,0xd7,0x0c,0xfc},lfo3_relative_entry},
    {0x4001306eu,6,{0x20,0x68,0x00,0x50,0x4e,0x90},lfo3_editor_proxy_alt},
    {0x40038124u,6,{0x20,0x68,0x00,0x50,0x4e,0x90},lfo3_editor_pattern_alt},
    {0x40014c48u,10,{0x2f,0x0a,0x2f,0x02,0x74,0x59,0x22,0x2f,0x00,0x0c},lfo3_relative_destination},
    {0x40014e32u,10,{0x2f,0x0a,0x2f,0x02,0x74,0x59,0x22,0x2f,0x00,0x0c},lfo3_relative_fx_destination},
    {0x40014ad8u,8,{0x4e,0x56,0xff,0xe4,0x48,0xd7,0x0c,0x0c},lfo3_validate_entry},
    {0x40014cc2u,8,{0x4e,0x56,0xff,0xe4,0x48,0xd7,0x0c,0x0c},lfo3_validate_alt_entry},
    {0x40014eacu,8,{0x4e,0x56,0xff,0xe4,0x48,0xd7,0x04,0x0c},lfo3_validate_third_entry},
    /* storage (phase 2a): copies, constructors, kit codec, project (de)serializer */
    {0x40115f78u,8,{0x22,0x6f,0x00,0x04,0x20,0x6f,0x00,0x08},lfo3_owned_copy_entry},   /* -> lfo3_memcpy_entry */
    {0x400b3c38u,8,{0x4f,0xef,0xff,0xd8,0x48,0xd7,0x7c,0x0c},lfo3_sound_new_entry},
    {0x400b330cu,10,{0x4f,0xef,0xff,0xdc,0x42,0x80,0x48,0xd7,0x7c,0x3c},lfo3_kit_new_entry},
    {0x400b32b0u,8,{0x4f,0xef,0xff,0xe4,0x48,0xd7,0x1c,0x04},lfo3_fx_new_entry},
    {0x40177794u,8,{0x2f,0x03,0x26,0x3c,0xa4,0x07,0x3d,0xeb},lfo3_relocate_entry},
    {0x400a787cu,8,{0x4f,0xef,0xff,0xe4,0x48,0xd7,0x3c,0x1c},lfo3_kit_encode_entry},
    {0x400a7828u,8,{0x20,0x2f,0x00,0x04,0x20,0x6f,0x00,0x08},lfo3_kit_decode_entry},
    {0x400ae338u,8,{0x4f,0xef,0xff,0xd4,0x48,0xd7,0x7c,0xfc},lfo3_serialize_entry},
    {0x400b0368u,8,{0x4f,0xef,0xff,0xd8,0x48,0xd7,0x04,0x3c},lfo3_deserialize_entry},
    {0x400adff8u,8,{0x4f,0xef,0xff,0xdc,0x48,0xd7,0x7c,0x3c},lfo3_decode_project_entry},
    /* storage (phase 2b): +Drive sounds, FX record codec, native LFO reset */
    {0x40111b08u,8,{0x4f,0xef,0xff,0xd4,0x48,0xd7,0x04,0x3c},lfo3_s1_save_entry},
    {0x40110ac4u,8,{0x4f,0xef,0xff,0xd4,0x48,0xd7,0x00,0x3c},lfo3_s1_load_entry},
    {0x400a6d96u,8,{0x2f,0x0b,0x2f,0x0a,0x24,0x6f,0x00,0x0c},lfo3_fx_encode_entry},
    {0x400a6d2eu,8,{0x4f,0xef,0xff,0xf4,0x48,0xd7,0x1c,0x00},lfo3_fx_decode_entry},
    {0x40118e24u,6,{0x41,0xf9,0x44,0x4d,0xa7,0x51},lfo3_native_reset_entry},
    /* page actions: copy/paste/clear/randomize/reload of the LFO3 page */
    {0x4003792cu,8,{0x20,0x6f,0x00,0x04,0x20,0x28,0x00,0x90},lfo3_actions_page_entry},
    {0x4001f16cu,8,{0x4f,0xef,0xff,0xb0,0x48,0xd7,0x7c,0x0c},lfo3_actions_capture_entry},
    {0x4001f656u,8,{0x72,0x04,0x20,0x28,0x00,0x04,0xb2,0x80},lfo3_actions_paste_compat},
    {0x4001f6d4u,6,{0x50,0x8f,0x4a,0x07,0x67,0x1e},lfo3_actions_paste_translate},
    {0x400149b0u,6,{0x0c,0x82,0x00,0x00,0x01,0xf8},lfo3_actions_random_metadata},
    {0x400149e2u,6,{0x70,0x59,0xb0,0x82,0x67,0x08},lfo3_actions_random_dest},
    {0x400385fcu,6,{0x70,0x20,0xb0,0x82,0x6d,0x34},lfo3_actions_random_bound},
    {0x40038666u,6,{0x70,0x20,0xb0,0x82,0x6d,0x50},lfo3_actions_reload_bound},
    {0x4001e412u,8,{0x4e,0x56,0xff,0xc4,0x48,0xd7,0x3c,0xfc},lfo3_actions_reload_scope},
    {0x4001e4aeu,6,{0x4e,0xb9,0x40,0x11,0x06,0xce},lfo3_actions_reload_read},
    {0x4001e574u,6,{0x0c,0x87,0x00,0x00,0x01,0xf8},lfo3_actions_reload_parameter},
    /* p-locks (Synfact locks_link): pattern codec/matrix, events, owned banks, lock UI */
    {0x400a8048u,8,{0x4f,0xef,0xff,0xe0,0x48,0xd7,0x7c,0x1c},lfo3_owned_pattern_decode},
    {0x400a81ecu,8,{0x4f,0xef,0xff,0xe8,0x48,0xd7,0x1c,0x1c},lfo3_pattern_encode},
    {0x400a811au,8,{0x4f,0xef,0xff,0xd4,0x48,0xd7,0x7c,0xfc},lfo3_pattern_compress},
    {0x400a827cu,8,{0x4f,0xef,0xff,0xdc,0x48,0xd7,0x1c,0xfc},lfo3_pattern_update},
    {0x400b373cu,8,{0x4f,0xef,0xff,0xf4,0x48,0xd7,0x0c,0x04},lfo3_pattern_clear},
    {0x4001722au,8,{0x4f,0xef,0xff,0xf0,0x48,0xd7,0x04,0x1c},lfo3_pattern_get_entry},
    {0x40017602u,8,{0x4e,0x56,0xff,0xdc,0x48,0xd7,0x04,0x7c},lfo3_pattern_set},
    {0x4001705cu,8,{0x4f,0xef,0xff,0xf0,0x48,0xd7,0x04,0x1c},lfo3_pattern_present_entry},
    {0x4001741eu,8,{0x4e,0x56,0xff,0xe0,0x48,0xd7,0x04,0x3c},lfo3_pattern_delete_entry},
    {0x400170eeu,8,{0x4f,0xef,0xff,0xf4,0x48,0xd7,0x04,0x0c},lfo3_pattern_row_count},
    {0x400171a0u,8,{0x4f,0xef,0xff,0xe8,0x48,0xd7,0x04,0x7c},lfo3_pattern_total},
    {0x400174f0u,8,{0x4f,0xef,0xff,0xe0,0x48,0xd7,0x0c,0xfc},lfo3_pattern_clear_row},
    {0x4008bbf6u,6,{0x20,0x3c,0x00,0x00,0x05,0xdc},lfo3_events_capture_entry},
    {0x4011a48au,6,{0x42,0xa8,0x00,0x10,0x52,0x90},lfo3_events_allocate_entry},
    {0x4011a3a0u,10,{0x2f,0x03,0x2f,0x02,0x41,0xf9,0x44,0x4d,0xbc,0x80},lfo3_events_pool_init},
    {0x4008b830u,8,{0x4f,0xef,0xff,0xf4,0x20,0x2f,0x00,0x1c},lfo3_events_convert},
    {0x400978b4u,8,{0x4f,0xef,0xff,0xd4,0x48,0xd7,0x7c,0xfc},lfo3_events_apply},
    {0x40097760u,8,{0x4f,0xef,0xff,0xdc,0x22,0x2f,0x00,0x28},lfo3_events_restore},
    {0x40097874u,6,{0x20,0x2f,0x00,0x04,0x22,0x00},lfo3_events_clear},
    {0x40097802u,6,{0x4f,0xef,0xff,0xe4,0x42,0x80},lfo3_events_fx_restore},
    {0x4010350cu,8,{0x4f,0xef,0xff,0xd4,0x48,0xd7,0x7c,0xfc},lfo3_owned_allocate_entry},
    {0x40103712u,8,{0x4f,0xef,0xff,0xf4,0x48,0xd7,0x00,0x1c},lfo3_owned_release_entry},
    {0x40177588u,8,{0x2f,0x03,0x26,0x3c,0x78,0x70,0xe0,0xcb},lfo3_owned_move},
    {0x400b3616u,8,{0x4f,0xef,0xff,0xf4,0x48,0xd7,0x1c,0x00},lfo3_owned_clear_track},
    {0x4001e2a8u,8,{0x4e,0x56,0xff,0xd8,0x48,0xd7,0x1c,0x1c},lfo3_owned_copy_pattern},
    {0x4001fa0eu,8,{0x4e,0x56,0xff,0xc0,0x41,0xee,0xff,0xe8},lfo3_owned_paste_patterns},
    {0x4002a816u,8,{0x4e,0x56,0xff,0xd8,0x48,0xd7,0x3c,0x0c},lfo3_owned_copy_page},
    {0x4002a966u,8,{0x4e,0x56,0xff,0xd8,0x48,0xd7,0x3c,0x0c},lfo3_owned_copy_track},
    {0x4002bb26u,8,{0x4e,0x56,0xff,0xbc,0x48,0xd7,0x3c,0xfc},lfo3_owned_paste_page},
    {0x4002bfb6u,8,{0x4e,0x56,0xff,0xc4,0x48,0xd7,0x3c,0x7c},lfo3_owned_paste_track},
    {0x4002bddau,8,{0x4e,0x56,0xff,0xc4,0x12,0x2e,0x00,0x0f},lfo3_owned_clear_page},
    {0x4002c1aau,8,{0x4e,0x56,0xff,0xd4,0x48,0xd7,0x3c,0x1c},lfo3_owned_clear_track_action},
    {0x40011e4au,6,{0x20,0x68,0x00,0x50,0x4e,0x90},lfo3_lock_ui_alternate},
    {0x4001340eu,6,{0x4e,0xb9,0x40,0x0a,0x5c,0x4a},lfo3_lock_ui_slot,1},
    {0x40013476u,6,{0x4e,0xb9,0x40,0x0a,0x5c,0x4a},lfo3_lock_ui_slot,1},
    {0x400134deu,6,{0x4e,0xb9,0x40,0x0a,0x5c,0x4a},lfo3_lock_ui_slot,1},
    {0x40013628u,6,{0x4e,0xb9,0x40,0x0a,0x5c,0x4a},lfo3_lock_ui_slot,1},
    {0x40013690u,6,{0x4e,0xb9,0x40,0x0a,0x5c,0x4a},lfo3_lock_ui_slot,1},
    {0x400136f8u,6,{0x4e,0xb9,0x40,0x0a,0x5c,0x4a},lfo3_lock_ui_slot,1},
    {0x4001924cu,6,{0x4e,0xb9,0x40,0x0a,0x5c,0x4a},lfo3_lock_ui_slot,1}};
/* +Drive record size operands (pea 0x107 -> 0x13f) of the save/load above. */
static const struct {u32 at;u16 stock,value;} operands[]={{0x40111b82u,0x0107,ST_SOUND_S1_BYTES},{0x40110b1eu,0x0107,ST_SOUND_S1_BYTES}};
#define SITES (sizeof sites/sizeof sites[0])
static u32 hooks_installed;
static u32 install_hooks(void)
{
    u32 i,j,sr,target;
    for(i=0;i<SITES;i++)for(j=0;j<sites[i].bytes;j++)
        if(LFO3_MEM(sites[i].at)[j]!=sites[i].stock[j])return 1;
    for(i=0;i<2;i++)if((LFO3_MEM(operands[i].at-2)[0]<<8|LFO3_MEM(operands[i].at-2)[1])!=0x4878 ||
                       (LFO3_MEM(operands[i].at)[0]<<8|LFO3_MEM(operands[i].at)[1])!=operands[i].stock)return 1;
    sr=lfo3_lock();
    for(i=0;i<2;i++)lfo3_code16(operands[i].at,operands[i].value);
    for(i=0;i<SITES;i++){
        target=(u32)sites[i].target;
        lfo3_code16(sites[i].at+2,target>>16);lfo3_code16(sites[i].at+4,target&0xffffu);
        for(j=6;j<sites[i].bytes;j+=2)lfo3_code16(sites[i].at+j,0x4e71);
        lfo3_code16(sites[i].at,sites[i].call?0x4eb9:0x4ef9);   /* entry word last */
    }
    lfo3_sync();lfo3_unlock(sr);
    return 0;
}
/* Shared Sound metadata uses the core's existing bus, no private registry. */
void lfo3_meta_notify(u32 kind,u32 a,u32 b,u32 n)
{
    u32 args[4]={kind,a,b,n};
    if(host && host->version>=10 && host->native_dispatch)
        host->native_dispatch(ST_NATIVE_SOUND_META,args);
}
static int lfo3_native(u32 op,const u32 *a,u32 *result)
{
    if(op==ST_NATIVE_LFO){
        if(a && render && !lfo3_engine_busy())
            render((u8 *)a[0],a[1],a[2],lfo3_events_bank(),&lfo3_bank->state[0][0],lfo3_bank->rng);
        return 0;                                 /* POLY or native LFO1/LFO2 continue */
    }
    return hooks_installed?lfo3_page_native(op,a,result):0;
}
static void lfo3_tick(void *controller)
{
    ticks++;
    if(in_sram && ticks%60u==0 && crc(SRAM_CODE,code_bytes)!=code_crc){render=0;use_ddr();refused=4;}
    if(ticks<=300u && ticks%30u==0)((u8 *)controller)[0x20]=1;
}
static void lfo3_draw(void *bitmap,void *controller)
{
    /* Only the DDR fallback is worth a notice; running from SRAM is normal. */
    static char line[]="LFO3 SRAM ON";
    (void)controller;if(ticks>300u || in_sram)return;
    line[5]='D';line[6]='D';line[7]='R';line[8]=' ';line[9]=(char)('0'+refused);line[10]=line[11]=' ';
    LFO3_FILL(bitmap,18,54,127,63,0);
    LFO3_TEXT(bitmap,(void *)0x443da230u,20,55,0,"%s",line);
}
/* Service API 3 (NATIVE subscription), host API 7 (core 0.12.14: LFO
 * arguments and the parameter provider). An older core keeps stock. */
static int initialize(const struct st_smz_api *api)
{
    if(!api || api->version<3 || !api->ui || api->ui->version<7 || !api->ui->host)return 1;
    host=api->ui->host;
    if(host->version<7){refused=5;return 0;}          /* core older than 0.12.14 */
    refused=install();if(refused)use_ddr();
    kits_base=0x41ad5134u;pool_base=0x41b93ec9u;image_container=0x40652514u;
    save_buffer=0x405ff554u;reload_buffer=0x405fd354u;
    lfo3_records_init();
    hooks_installed=lfo3_locks_setup() && !install_hooks();     /* task context, before any hook */
    if(hooks_installed)host->parameter_provider(lfo3_parameter_query);
    host->native_interest((void (*)(void))lfo3_native,
        (1u<<ST_NATIVE_LFO)|(hooks_installed?(1u<<ST_NATIVE_PAGE_SYNC)|(1u<<ST_NATIVE_PAGE_NAV)|(1u<<ST_NATIVE_PAGE_LAYOUT):0));
    return 0;
}
static const struct st_smz_subscription subscriptions[]={
    {ST_EV_NATIVE,(void (*)(void))lfo3_native},
    {ST_EV_TICK,(void (*)(void))lfo3_tick},
    {ST_EV_DRAW,(void (*)(void))lfo3_draw}};
const struct st_smz_module lfo3_module={"LFO3",initialize,subscriptions,3};
