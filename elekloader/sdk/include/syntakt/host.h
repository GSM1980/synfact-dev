/* SPDX-License-Identifier: GPL-2.0-or-later
 * Borrowed core services. Storage/telemetry are task-only; view_event is UI.
 * native_original requires the original operation's native task/IRQ context.
 * No filesystem operation from an IRQ.
 */
#ifndef ELEKLOADER_ST_HOST_H
#define ELEKLOADER_ST_HOST_H
#include "storage.h"
#include "sample-pool.h"
#include "persist.h"
#define ST_PARAMETER_UI_DEST_FLAGS 36u
/* Init-only MAIN mixer replacement for a stopped-transport preview session.
 * ready() returns 0 or 1. mix has native OS 1.42 mixer C ABI (three words),
 * preserves callee saves and owns bus clearing, preview clock and output. */
struct st_audio_idle_owner {
    int (*ready)(void);
    void (*mix)(unsigned int,unsigned int,void *);
};
struct st_machine_host_api {
    unsigned int version;
    const struct st_storage_io *storage;
    int (*hz_open)(struct st_hzfs *,const struct st_storage_io *);
    void (*hz_close)(struct st_hzfs *);
    unsigned int (*crc_update)(unsigned int,const unsigned char *,unsigned int);
    void (*pool_read)(const unsigned char *,unsigned short *);
    void (*pool_write)(unsigned char *,const unsigned short *);
    int (*pool_ready)(void);
    int (*pool_apply)(const unsigned short *before); /* rollback on AUDIO failure */
    void (*preview_stop)(void);
    int (*view_event)(unsigned int event,void *view,void *data,unsigned int id);
    /* Host API 2: existing telemetry and bounded native operation replays. */
    void (*clock_snapshot)(unsigned int *);
    void (*additem)(void *,const void *);
    const unsigned int *pool_busy;
    unsigned int *query_owner;
    unsigned int (*native_original)(unsigned int,const unsigned int *);
    /* Host API 3: extended-control sender counters, written by the MAIN
     * audio IRQ: {blocks, MAIN timer ticks inside, publications, rows,
     * last publication's changed-track mask, the OR of their XOR
     * differences (nonzero: changed)}. Copy, then recheck blocks. */
    const volatile unsigned int *machine_telemetry;
    /* Host API 4: optional native-operation profile. Pass a retained buffer
     * of 2*ST_NATIVE_OPERATION_COUNT words: MAIN timer ticks per operation,
     * then calls per operation (nested operations counted in both). Null
     * stops measuring. Diagnostic modules only. */
    void (*native_profile)(unsigned int *);
    /* Host API 5: narrow an ev_native subscriber to the operations it uses
     * (bit = ST_NATIVE_* index). Others replay stock without the C
     * dispatcher. Default: every operation. Call from the module's init, its
     * audio IRQ or an IRQ-masked section; keep every operation the module
     * itself issues through native paths (e.g. private IMMEDIATE commands).
     * With the TYPE bit, bits 24..31 limit TYPE to those digital tracks'
     * cursors (0 or 0xff: every cursor). */
    void (*native_interest)(void (*callback)(void),unsigned int mask);
    /* Host API 6: PAGE_SYNC/PAGE_NAV only for views whose vtable (first
     * word) is this value; 0 = every view (default). The filter applies only
     * when every subscriber of those operations declares the same class. */
    void (*native_page_view)(void (*callback)(void),unsigned int vtable);
    /* Host API 7: one provider for engine/UI parameter IDs that no loaded
     * machine claims, called from the core's metadata lookups (any task,
     * bounded, no IO). Query kinds: 0 name,1 short,2 group,3 category,
     * 4 available,5 digital,6 UI->engine,7 local slot,8 owner,9 engine->UI,
     * 10 slot->engine,11 page local,12 page copy,13 range (result = 3 words
     * min,max,initial),14 valid owner,15 layout,16 cached,17 format,
     * 18 short label,19 synth,20 long label,21 flags,22 rank,23 encoder
     * cached. args[0] is the ID (labels: args[1]). Return nonzero with
     * *value; zero keeps the stock path (cached/format may rewrite args[0]
     * to an alias ID and return zero). Core 1.2: modules take their IDs from
     * ST_MODULE_ID / ST_MODULE_UI (<syntakt/machines.h>), after the machines'
     * blocks; LFO3 0x3f9..0x402 / 0x4e0..0x4e9, the chorus the next seven.
     * Core 0.12.15: 24 store — native Sound setter args (proxy, id, value,
     * track, publish, notify) for IDs in the engine block; result/value are
     * null; nonzero means stored (the native setter is skipped).
     * Core 0.12.16 notifications (result/value null, return ignored): 25 after
     * a successful native Sound encode {record, sound, 1}; 26 after a decode
     * {sound, record, 1}; 27 after NEW PROJECT {project container}.
     * Host API 13 (core 1.2): up to four providers, asked in registration
     * order until one returns nonzero; every provider gets the notifications
     * (25..29). 28 after every native FX record encode {record, fx, ok},
     * 29 after every decode {fx, record, ok}; ok is the native result's low
     * byte. ST_NATIVE_PAGE_LAYOUT also asks for page 25 (the dormant chorus
     * page). Queries 16/17/18/20 also come for parameter 72, the dormant
     * Digitone chorus send (no stock labels or formatter). Query 30: a dial
     * no machine claims, args = struct st_machine_dial; nonzero = drawn.
     * Query 17 also gives an unclaimed ID's direct value text.
     * Host API 14 (core 1.4): 24 also comes for the Kit model's setter
     * (40013516, same arguments). Query 31, page randomize: args[0] is any
     * ID on the page (stock or module); nonzero = the native randomize draws
     * it uniformly over its range (query 13 for a module's ID) and stores it
     * through the page's model (24 for a module's ID). Before 13 there is one
     * provider slot: a provider only for 31 registers on 14. Register in
     * the module's init.
     * Host API 16 (core 2.4), no new fields: queries 32/33 implement the
     * global Kit model's has/get, with original args {model,id,...} (24 is
     * set). For 32 only, args[-1] is the native caller PC; the inverted
     * lock highlight caller is 4003a3c4. 34 broadcasts task-side Kit lifecycle
     * {kind,a,b,n,phase}: COPY=1, NEW=2, ENCODE=5, DECODE=6 (sound-meta.h).
     * Codec phase 0 precedes native execution, phase 1 follows it with n
     * equal to the native result. COPY/NEW have only phase 1, n=Kit bytes.
     * 35 may return a synchronous task memcpy entry, same native C ABI,
     * to preserve owning-object metadata; zero replays the stock copy.
     * The core owns these sites on 16; modules must not install them.
     * Host API 19 (core 2.8.1): query 34 appends args[5], the encoded Kit
     * slot capacity (1600h or zero). After a successful codec the core
     * recognizes the fixed project/save buffers and KitActive's currently
     * registered mirror arrays, even without LFO3. Zero gives no permission
     * to access padding beyond the native 14d2h record. The five-field
     * prefix and all native results remain unchanged.
     * Query 37 lets a project-codec adapter lend a known slot's capacity:
     * args[0]=encoded slot, *value=1600h, nonzero return, or unhandled.
     * Task-only, no IO/allocation; never infer capacity from a record header.
     * Host API 17 (core 2.6): query ST_PARAMETER_UI_DEST_FLAGS (36), only
     * while the native destination builder enumerates a registered machine.
     * args[0] is the engine ID; result is the borrowed model/proxy whose
     * destinations are being listed (NOT a result buffer). Return nonzero
     * with *value=0 to hide it, or a capability mask such as 0xe00 to keep it.
     * Read that model's TYPE through its native getter, not global UI state.
     * No writes, heap or IO. This never changes runtime flags (query 21),
     * parameter IDs, saved routing or Sound values. Unclaimed/module IDs
     * retain their ordinary query-21 provider lookup. (On the MD/SY MnM
     * branch's cores 1.8.2..2.3.2 this was query 32 under API 16/17; main's
     * core 2.4 gave 32 to Kit has, so the merged core 2.6 moved it to 36.) */
    void (*parameter_provider)(int (*query)(unsigned int,unsigned int *,void *,unsigned int *));
    /* API 8 native_query and the ARP branch's native_dispatch are aliases
     * for the same existing bus at offset 84. Both prefixes stay compatible.
     * Use the operation's native context; OWNERS is MAIN IRQ-only. Results
     * are borrowed and expire at the next native transaction.
     * API 10 adds NOTE_ACCEPT and SOUND_META without enlarging the record.
     * API 4 profiling retains its original 18-operation/36-word layout. */
    union {
        unsigned int (*native_query)(unsigned int,const unsigned int *);
        unsigned int (*native_dispatch)(unsigned int,const unsigned int *);
    };
    /* API 11 appends pool infrastructure; APIs 8/10 keep their prefix. */
    const struct st_sample_pool_api *pool;
    /* Host API 12: the core's mod list and the loaded bank's, zero-terminated 40-byte
     * records (id in 24 bytes, version in 16, NUL-padded). bank_info points to the
     * bank's list pointer; it is null when no bank with a mod-info directory is loaded. */
    const unsigned char *core_info;
    const unsigned char *const *bank_info;
    /* Host API 13 (core 1.2) adds no field: chained parameter providers, FX
     * codec notifications and queries 28..30 (parameter_provider above).
     * Host API 14 (core 1.4) adds no field: queries 24 (Kit) and 31. */
    /* Host API 15: one init-only owner slot. Refuse a nonempty slot, then
     * publish a retained immutable descriptor with both callbacks nonnull.
     * Failed bank init clears the slot. ready() is MAIN render-in context;
     * mix() replaces the native mixer call only while stopped and ready.
     * Both are bounded, no heap/IO; mix may use the native EMAC/output ABI. */
    const struct st_audio_idle_owner *volatile *audio_idle;
    /* API 20: independent Kit mirrors and global preferences. */
    const struct st_persistence_api *persistence;
    /* Host API 16 adds no field: the shared Kit queries 32..35. API 15's
     * stopped-transport mixer owner and its ABI offset remain unchanged. */
    /* Host API 17 adds no field: UI-only destination query 36, and extended
     * configuration controls with alias ST_MACHINE_CONFIG_PARAMETER, which
     * are unmodulated and not destinations. */
    /* Host API 18 (core 2.7) adds no field: SMZ events 16/17 (modules.h),
     * the PATTERN and RELOAD FROM PROJ menus' build events. */
    /* Core 2.8 adds no field either: ST_MACHINE_TEMPO_PARAMETER controls
     * receive the tempo on AUDIO (machines.h). */
    /* API 21: an optional fast filter for the relation subscriber. The low
     * 16 bits list physical destination tracks, the high 16 list sources
     * that take their relation row from an owner. The second word packs the
     * track-minus-owner offset in two bits per track. The core uses the filter
     * while this is the only relation subscriber. Publish under exclusion. */
    void (*native_relation_scope)(void (*)(void),unsigned int,unsigned int);
    /* API 22. Group membership is independent of modulation ownership.
     * The mask uses physical audio tracks 0..11, or ~0u with no publisher.
     * A group allocator publishes under native exclusion. The core copies
     * twelve signed Q16 semitone offsets. Null clears the pitch offsets. */
    const volatile unsigned int *voice_groups;
    void (*voice_groups_set)(unsigned int,const int *);
};
#endif
