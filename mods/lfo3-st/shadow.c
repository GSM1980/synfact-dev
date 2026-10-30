#include <syntakt/sound-meta.h>
extern void lfo3_meta_notify(unsigned int,unsigned int,unsigned int,unsigned int);
/* SPDX-License-Identifier: GPL-2.0-or-later
 * Imported from Synfact src/features/lfo3/shadow_owned.c (sha256 5a7ceca4...).
 * Module changes are marked "elekloader:". Configuration is set by main.c. */
/* Integrated shadow codecs. Derived from preserved shadow_proto; heap-owned
 * records supplement the bounded hash associations for stack decode buffers. */
/* LFO3 option B: shadow records that follow native Sound/kit copies and kit codecs
 * (research prototype, 2026-10-21). Not part of any build; compiled only by
 * build_proto.py for the Unicorn tests of this folder.
 *
 * memcpy hook contract (entry hook on 40115f78, see shadow_entry.s):
 *   n < 0x8e             -> native copy only (3 instructions of filter).
 *   n >= 0x8e, IPL == 0  -> native copy, then lfo3_after_copy(dst, src, n).
 *   IPL != 0            -> native copy only (also excludes masked tasks).
 *   n == 0x3a5604        -> project/snapshot: LP travels in reserved kit padding;
 *                          only full known destinations receive the LP section.
 * Objects (whole and aligned only):
 *   kit   : n == 0x1618, live kit = kits_base + k*0x1618 (k < 128)      -> 13 records
 *   sound : n == 0x123,  Sound inside a live kit (+0x2a + t*0x123, t < 12)
 *           or pool Sound (pool_base + p*0x123, p < 128)                -> 1 record
 *   FX    : n == 0x8e, kit + 0xdce                                   -> 1 record
 * Untracked memory (clipboard, undo, decode buffers) uses a side table keyed by
 * (address, size); an entry is used only while a hash of the native bytes matches.
 *
 * Kit codec hooks (entries 400a787c encode(storage, kit, flag), 400a7828 decode(kit,
 * storage)): the LFO3 trailer lives in the free tail of an encoded kit slot, at
 * +0x14e0 (the codec uses [0, 0x14d2) of each 0x1600 slot). It is written or read
 * ONLY for 0x1600 slots known by construction: the state image container, a
 * container registered by the serializer/deserializer hooks, and the save/reload kit
 * buffers. Other destinations (SysEx export buffers of exactly 0x14d2 B) are never
 * written beyond the native record.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define KIT_SIZE    0x1618u
#define SOUND_SIZE  0x123u
#define FX_SIZE     0x8eu
#define FX_OFF      0xdceu
#define SOUND_OFF   0x2au
#define TRACKS      13u
#define SOUNDS      12u               /* track 12 is FX, not a 0x123-byte Sound */
#define KITS        128u
#define POOL        128u
#define WORDS       8u                 /* SPD MULT FADE DEST WAVE SPH MODE DEP */
#define SIDE        8u
#define KIT_SLOTS   0x2e0200u          /* container offset of 128 kit slots */
#define KIT_SLOT    0x1600u
#define KIT_STORED  0x14d2u            /* bytes the native codec uses */
#define TRAILER     0x14e0u
#define MAGIC       0x4c33u            /* 'L3' */
#define CONTAINERS  2u

typedef struct { u16 v[WORDS]; } rec_t;

/* Shared with the native destination fixture. Working shadows always keep Q8
 * slots; only persisted records use the canonical namespace. Read old raw-Q8
 * format 1, write canonical format 2. No native Sound/FX codec sees these codes. */
#define RECORD_FORMAT 2u
extern void lfo3_record_encode(u16 *dst, const u16 *src);
extern void lfo3_record_decode(u16 *dst, const u16 *src, u32 format);
typedef struct { u32 addr, size, tag, seq, count; rec_t r[TRACKS]; } side_t;
typedef struct { u16 magic; u8 version, count; rec_t r[TRACKS]; u32 hash; } trailer_t;

/* Machine-file blobs get no BSS clear: every object lives in initialized .data. */
#define DATA __attribute__((section(".data")))
/* configuration, written by the init of the real module (here by the test) */
u32 kits_base DATA = 0, pool_base DATA = 0;
u32 image_container DATA = 0, save_buffer DATA = 0, reload_buffer DATA = 0;
u32 containers[CONTAINERS] DATA = {0};   /* registered during (de)serializer calls */
u32 container_scope_callback DATA=0;
static u32 container_at(u32 i)
{
    if(container_scope_callback)return ((u32 (*)(u32))container_scope_callback)(i);
    if(i<CONTAINERS)return containers[i];
    if(i==CONTAINERS)return image_container;
    return i==CONTAINERS+1 ? 0x43c0cfc0u : 0x43ff35d0u;
}
/* live shadows */
rec_t kit_shadow[KITS][TRACKS] DATA = {{{{0}}}};
rec_t pool_shadow[POOL] DATA = {{{0}}};
side_t side[SIDE] DATA = {{0}};
u32 seq_now DATA = 1;
/* memcpy counters: calls, acted, side_store, side_hit, side_stale, unknown_source, ignored */
u32 stats[7] DATA = {0};
/* codec counters: trailer_written, encode_other_dst, decode_valid, decode_invalid,
 * decode_other_src */
u32 codec_stats[5] DATA = {0};

static const rec_t DEFAULTS = {{0x7000, 0x0300, 0x4000, 0x0000, 0x0000, 0x0000, 0x0000, 0x4000}};

static u32 hash(const u8 *p, u32 n)
{
    u32 h = 0x811c9dc5u, i = 0;
    const u32 *w = (const u32 *)p;
    if (((u32)p & 3u) == 0)
        for (; i + 4 <= n; i += 4)
            h = (h ^ *w++) * 0x01000193u;
    for (; i < n; i++)
        h = (h ^ p[i]) * 0x01000193u;
    return h;
}

/* -> live records of a whole object at a (and their count), or 0 */
u32 shadow_query_callback DATA=0,shadow_relocate_callback DATA=0,shadow_tracked_callback DATA=0;
u32 shadow_enter_callback DATA=0,shadow_leave_callback DATA=0;
extern u32 lfo3_irq_mask(void);
void lfo3_task_enter(void),lfo3_task_leave(void);
rec_t *lfo3_shadow_owned_record(u32 a,u32 n,u32 *count,u32 create)
{
    rec_t *r;u32 task;
    if(!shadow_query_callback)return 0;
    task=!lfo3_irq_mask();if(task)lfo3_task_enter();
    r=((rec_t *(*)(u32,u32,u32 *,u32))shadow_query_callback)(a,n,count,create);
    if(task)lfo3_task_leave();
    return r;
}
/* elekloader: a republish pass already holds the writer mutex. Its lookup
 * memo is invalidated by every metadata mutator generated in locks/owners.c. */
rec_t *lfo3_shadow_owned_record_locked(u32 a,u32 n,u32 *count,u32 create)
{return shadow_query_callback ? ((rec_t *(*)(u32,u32,u32 *,u32))shadow_query_callback)(a,n,count,create):0;}
void lfo3_shadow_owner_relocate(u32 d,u32 s,u32 n)
{if(shadow_relocate_callback){lfo3_task_enter();
 ((void (*)(u32,u32,u32))shadow_relocate_callback)(d,s,n);lfo3_task_leave();}}
u32 lfo3_shadow_owned_tracked(u32 a)
{return shadow_tracked_callback ? ((u32 (*)(u32))shadow_tracked_callback)(a):0;}
void lfo3_task_enter(void) {if(shadow_enter_callback)((void (*)(void))shadow_enter_callback)();}
void lfo3_task_leave(void) {if(shadow_leave_callback)((void (*)(void))shadow_leave_callback)();}
static rec_t *live(u32 a, u32 n, u32 *count)
{
    if (kits_base && a >= kits_base && a < kits_base + KITS * KIT_SIZE) {
        u32 off = a - kits_base, k = off / KIT_SIZE, in = off - k * KIT_SIZE;
        if (n == KIT_SIZE && in == 0) {
            *count = TRACKS;
            return kit_shadow[k];
        }
        if (n == FX_SIZE && in == FX_OFF) {
            *count = 1;
            return &kit_shadow[k][12];
        }
        if (n == SOUND_SIZE && in >= SOUND_OFF && in < SOUND_OFF + SOUNDS * SOUND_SIZE &&
            (in - SOUND_OFF) % SOUND_SIZE == 0) {
            *count = 1;
            return &kit_shadow[k][(in - SOUND_OFF) / SOUND_SIZE];
        }
        return 0;
    }
    if (pool_base && n == SOUND_SIZE && a >= pool_base && a < pool_base + POOL * SOUND_SIZE &&
        (a - pool_base) % SOUND_SIZE == 0) {
        *count = 1;
        return &pool_shadow[(a - pool_base) / SOUND_SIZE];
    }
    return lfo3_shadow_owned_record(a,n,count,0);
}

static side_t *side_find(u32 a, u32 n)
{
    u32 i;
    for (i = 0; i < SIDE; i++)
        if (side[i].addr == a && side[i].size == n)
            return &side[i];
    return 0;
}

static side_t *side_slot(u32 a, u32 n)
{
    u32 i;
    side_t *s = side_find(a, n), *old = &side[0];
    if (s)
        return s;
    for (i = 0; i < SIDE; i++) {
        if (side[i].addr == 0)
            return &side[i];
        if (side[i].seq < old->seq)
            old = &side[i];
    }
    return old;                              /* least recently stored */
}

static void copy_recs(rec_t *d, const rec_t *s, u32 count)
{
    u32 i, j;
    for (i = 0; i < count; i++)
        for (j = 0; j < WORDS; j++)
            d[i].v[j] = s ? s[i].v[j] : DEFAULTS.v[j];
}

static u32 record_format(u32 format)
{
    return format == 1 || format == RECORD_FORMAT;
}

static void encode_recs(rec_t *d, const rec_t *s, u32 count)
{
    u32 i;
    for (i = 0; i < count; i++)
        lfo3_record_encode(d[i].v, s ? s[i].v : DEFAULTS.v);
}

static void decode_recs(rec_t *d, const rec_t *s, u32 count, u32 format)
{
    u32 i;
    for (i = 0; i < count; i++)
        if (s)
            lfo3_record_decode(d[i].v, s[i].v, format);
        else
            copy_recs(&d[i], 0, 1);
}

/* Persisted -> persisted, including validation and legacy conversion. One local
 * working record avoids mixing namespaces or adding mutable global state. */
static void transcode_rec(rec_t *d, const rec_t *s, u32 format)
{
    rec_t work;
    decode_recs(&work, s, 1, format);
    encode_recs(d, &work, 1);
}

/* Records of the object at a: live shadow, or a side entry whose hash still matches.
 * -> pointer, or 0 when unknown (the caller then uses defaults). */
static const rec_t *records_of(u32 a, u32 n)
{
    u32 count, i, off;
    side_t *s;
    rec_t *r = live(a, n, &count);
    if (r)
        return r;
    if ((s = side_find(a, n)) != 0) {
        if (s->tag == hash((const u8 *)a, n)) {
            stats[3]++;
            return s->r;
        }
        stats[4]++;
    }
    /* A whole clipboard kit also owns its twelve physical Sounds. */
    if (n == SOUND_SIZE || n == FX_SIZE)
        for (i = 0; i < SIDE; i++) {
            s = &side[i];
            if (!s->addr || s->size != KIT_SIZE || a < s->addr + SOUND_OFF)
                continue;
            off = a - (s->addr + SOUND_OFF);
            if (n == FX_SIZE) {
                if (a == s->addr + FX_OFF && s->tag == hash((const u8 *)s->addr, KIT_SIZE))
                    return &s->r[12];
                continue;
            }
            if (off < SOUNDS * SOUND_SIZE && off % SOUND_SIZE == 0 &&
                s->tag == hash((const u8 *)s->addr, KIT_SIZE))
                return &s->r[off / SOUND_SIZE];
        }
    return 0;
}

/* Full native constructors reset LFO1/LFO2 as well. Machine selection does not
 * use these constructors; it changes SYN values through 40024db2. */
static u32 set_records(u32 a, u32 n, const rec_t *r, u32 known);

void lfo3_sound_new(u32 sound)
{
    lfo3_meta_notify(ST_SOUND_NEW,sound,0,SOUND_SIZE);
    u32 i, off;
    side_t *s;
    if (!sound)
        return;
    for (i = 0; i < SIDE; i++) {
        s = &side[i];
        if (!s->addr || s->size != KIT_SIZE || sound < s->addr + SOUND_OFF)
            continue;
        off = sound - (s->addr + SOUND_OFF);
        if (off < SOUNDS * SOUND_SIZE && off % SOUND_SIZE == 0) {
            if (s->tag == hash((const u8 *)s->addr, KIT_SIZE))
                copy_recs(&s->r[off / SOUND_SIZE], 0, 1);
            else
                s->addr = 0;
        }
    }
    set_records(sound, SOUND_SIZE, 0, 1);
}

void lfo3_kit_new(u32 kit)
{
    lfo3_meta_notify(ST_SOUND_NEW,kit,0,KIT_SIZE);
    u32 i;
    if (!kit)
        return;
    for (i = 0; i < SIDE; i++)
        if ((side[i].size == SOUND_SIZE && side[i].addr >= kit + SOUND_OFF &&
             side[i].addr < kit + SOUND_OFF + SOUNDS * SOUND_SIZE) ||
            (side[i].size == FX_SIZE && side[i].addr == kit + FX_OFF))
            side[i].addr = 0;
    set_records(kit, KIT_SIZE, 0, 1);
}

void lfo3_fx_new(u32 fx)
{
    u32 i;
    side_t *s;
    if (!fx)
        return;
    for (i = 0; i < SIDE; i++) {
        s = &side[i];
        if (s->addr && s->size == KIT_SIZE && fx == s->addr + FX_OFF) {
            if (s->tag == hash((const u8 *)s->addr, KIT_SIZE))
                copy_recs(&s->r[12], 0, 1);
            else
                s->addr = 0;
        }
    }
    set_records(fx, FX_SIZE, 0, 1);
}

/* Give the object at a the records r (0 = defaults); known = r came from a known source.
 * -> 1 if a live object was written. */
static u32 set_records(u32 a, u32 n, const rec_t *r, u32 known)
{
    u32 count = n == KIT_SIZE ? TRACKS : 1, c;
    side_t *s;
    rec_t *to = live(a, n, &c);
    if(!to)to=lfo3_shadow_owned_record(a,n,&c,1);
    if (to) {
        copy_recs(to, r, count);             /* unknown source -> defaults, never stale */
        return 1;
    }
    if (known) {
        s = side_slot(a, n);
        s->addr = a;
        s->size = n;
        s->count = count;
        s->tag = hash((const u8 *)a, n);
        s->seq = ++seq_now;
        copy_recs(s->r, r, count);
        stats[2]++;
    } else if ((s = side_find(a, n)) != 0) {
        s->addr = 0;                         /* overwritten with unrelated content */
    }
    return 0;
}

/* Two native vector reallocation callers free the old allocation immediately
 * afterwards. Move keys of valid side entries, not their records or native bytes.
 * The wrapper scopes this to 40177852 / 40177918, with disjoint allocations.
 * A bitmask on its stack carries the validated entries across native memmove. */
u32 lfo3_prepare_relocate(u32 dst, u32 src, u32 n)
{
    u32 i, mask = 0;
    side_t *s;
    if (!n || dst == src || n % KIT_SIZE ||
        (dst > src ? dst - src < n : src - dst < n))
        return 0;
    for (i = 0; i < SIDE; i++) {
        s = &side[i];
        if (!s->addr)
            continue;
        if (s->addr >= dst && s->addr - dst < n) {
            s->addr = 0;                 /* destination allocation was reused */
            continue;
        }
        if (s->addr >= src && s->addr - src < n && s->size <= n - (s->addr - src)) {
            if (s->tag == hash((const u8 *)s->addr, s->size))
                mask |= 1u << i;
            else
                s->addr = 0;
        }
    }
    return mask;
}

void lfo3_finish_relocate(u32 dst, u32 src, u32 n, u32 mask)
{
    u32 i, to;
    side_t *s, *existing;
    for (i = 0; i < SIDE; i++) {
        s = &side[i];
        if (!(mask & (1u << i)) || !s->addr || s->addr < src ||
            s->addr - src >= n || s->size > n - (s->addr - src))
            continue;                   /* a nested one-kit memcpy may evict it */
        to = dst + (s->addr - src);
        if (s->tag != hash((const u8 *)to, s->size)) {
            s->addr = 0;
            continue;
        }
        existing = side_find(to, s->size);
        if (existing && existing != s && existing->tag == s->tag)
            s->addr = 0;                 /* nested memcpy already copied the entry */
        else {
            s->addr = to;
            s->seq = ++seq_now;
        }
    }
    lfo3_shadow_owner_relocate(dst,src,n);
    lfo3_meta_notify(ST_SOUND_RELOCATE,dst,src,n);
}

static u32 project_copy(u32 dst, u32 src, u32 n);
static u32 meta_container(u32 record);

void lfo3_after_copy(u32 dst, u32 src, u32 n)
{
    lfo3_meta_notify(ST_SOUND_COPY,dst,src,n);
    if(n==KIT_SLOT)lfo3_meta_notify(ST_SOUND_KIT_RECORD_COPY,dst,src,meta_container(src));
    if(n==0x107u)lfo3_meta_notify(ST_SOUND_RECORD_COPY,dst,src,meta_container(src));
    if(n==KIT_SLOT || n==0x107u)lfo3_meta_notify(ST_SOUND_RECORD_BIND,dst,n,meta_container(dst));
    const rec_t *source;
    u32 count;

    stats[0]++;
    if (project_copy(dst, src, n))
        return;
    if (n != KIT_SIZE && n != SOUND_SIZE && n != FX_SIZE) {
        stats[6]++;
        return;
    }
    source = records_of(src, n);
    if (live(dst, n, &count)) {
        if (!source)
            stats[5]++;
        set_records(dst, n, source, source != 0);
        stats[1]++;
        return;
    }
    set_records(dst, n, source, source != 0);
    if (!source)
        stats[6]++;
}

/* Is a the start of a 0x1600 kit slot known by construction? */
static u32 full_container(u32 a)
{
    u32 i;
    /* Two native cache entries: 12-byte header + 0x3e6604-byte container. */
    if (a && (a == image_container || a == 0x43c0cfc0u || a == 0x43ff35d0u))
        return 1;
    for (i = 0; i < (container_scope_callback ? 0xffffffffu:CONTAINERS); i++) {
        u32 base=container_at(i);if(container_scope_callback && !base)break;
        if (a && a == base)
            return 1;
    }
    return 0;
}

static u32 kit_slot(u32 a)
{
    u32 i, base, off;
    if (a && (a == save_buffer || a == reload_buffer))
        return 1;
    for (i = 0; i < 2; i++) {
        base = i ? 0x43ff35d0u : 0x43c0cfc0u;
        off = a - (base + KIT_SLOTS);
        if (a >= base + KIT_SLOTS && off < KITS * KIT_SLOT && off % KIT_SLOT == 0)
            return 1;
    }
    for (i = 0; i < (container_scope_callback ? 0xffffffffu:CONTAINERS+3); i++) {
        base=container_at(i);if(container_scope_callback && !base)break;
        if (!base)
            continue;
        off = a - (base + KIT_SLOTS);
        if (a >= base + KIT_SLOTS && off < KITS * KIT_SLOT && off % KIT_SLOT == 0)
            return 1;
    }
    return 0;
}

/* Incremental native model refreshes encode/decode a Sound or FX directly
 * inside its kit slot. They do not call the enclosing Kit codec. */
extern u32 kit_codec_depth;
static u32 subtrack(u32 off,u32 fx,u32 *track)
{
    if(fx){if(off!=0xc82)return 0;*track=12;return 1;}
    if(off<0x2e || off>=0x2eu+12*0x107u || (off-0x2e)%0x107u)return 0;
    *track=(off-0x2e)/0x107u;return 1;
}
static u32 subslot(u32 record,u32 fx,u32 *track)
{
    u32 i,base,slot,off,k;
    for(i=0;i<2;i++) {
        base=i ? reload_buffer:save_buffer;
        if(base && record>=base && record-base<KIT_SLOT) {
            if(subtrack(record-base,fx,track))return base;
            return 0;
        }
    }
    for(i=0;i<(container_scope_callback ? 0xffffffffu:CONTAINERS+3);i++) {
        base=container_at(i);if(container_scope_callback && !base)break;
        if(!base || record<base+KIT_SLOTS)continue;
        off=record-base-KIT_SLOTS;k=off/KIT_SLOT;
        if(k>=128)continue;
        slot=base+KIT_SLOTS+k*KIT_SLOT;off-=k*KIT_SLOT;
        if(subtrack(off,fx,track))return slot;
    }
    return 0;
}
static u32 meta_container(u32 record)
{
    u32 i,base;
    for(i=0;i<(container_scope_callback?0xffffffffu:CONTAINERS+3);i++) {
        base=container_at(i);if(container_scope_callback && !base)break;
        if(base && record>=base && record-base<0x3a5604u)return base;
    }
    return 0;
}
static u32 valid_trailer(const trailer_t *t)
{return t->magic==MAGIC && record_format(t->version) && t->count==TRACKS &&
    t->hash==hash((const u8 *)t,sizeof(trailer_t)-4);}
static u32 sub_encoded(u32 record,u32 source,u32 fx)
{
    u32 track,slot=subslot(record,fx,&track),i;trailer_t *t;const rec_t *r;
    if(!slot)return 0;
    t=(trailer_t *)(slot+TRAILER);r=records_of(source,fx ? FX_SIZE:SOUND_SIZE);
    if(!valid_trailer(t))encode_recs(t->r,0,TRACKS);
    else if(t->version!=RECORD_FORMAT)for(i=0;i<TRACKS;i++)transcode_rec(&t->r[i],&t->r[i],t->version);
    encode_recs(&t->r[track],r,1);
    t->magic=MAGIC;t->version=RECORD_FORMAT;t->count=TRACKS;
    t->hash=hash((const u8 *)t,sizeof(trailer_t)-4);return 1;
}
static u32 sub_decoded(u32 object,u32 record,u32 fx)
{
    u32 track,slot=subslot(record,fx,&track);const trailer_t *t;rec_t work;
    if(!slot)return 0;
    t=(const trailer_t *)(slot+TRAILER);
    if(valid_trailer(t)){decode_recs(&work,&t->r[track],1,t->version);set_records(object,fx ? FX_SIZE:SOUND_SIZE,&work,1);}
    else set_records(object,fx ? FX_SIZE:SOUND_SIZE,0,0);
    return 1;
}
void lfo3_fx_encoded(u32 record,u32 fx,u32 ok)
{if((ok&255u) && !kit_codec_depth)sub_encoded(record,fx,1);}
void lfo3_fx_decoded(u32 fx,u32 record,u32 ok)
{
    if(!(ok&255u) || kit_codec_depth)return;
    if(!sub_decoded(fx,record,1))set_records(fx,FX_SIZE,0,0);
}

/* after the native kit encode(storage, kit, flag) -> ok */
void lfo3_kit_encoded(u32 storage, u32 kit, u32 ok)
{
    if(ok&255u)lfo3_meta_notify(ST_SOUND_KIT_ENCODE,storage,kit,meta_container(storage));
    trailer_t *t = (trailer_t *)(storage + TRAILER);
    const rec_t *r;
    if (!(ok & 0xffu))
        return;
    if (!kit_slot(storage)) {
        codec_stats[1]++;
        return;
    }
    r = records_of(kit, KIT_SIZE);
    t->magic = MAGIC;
    t->version = RECORD_FORMAT;
    t->count = TRACKS;
    encode_recs(t->r, r, TRACKS);
    t->hash = hash((const u8 *)t, sizeof(trailer_t) - 4);
    codec_stats[0]++;
}

/* ------------------------------------------------------------------ Sound codecs
 * Sound pool of a container: 128 native records at +0x390204 + p*0x107 (no padding).
 * Their LFO3 records live in a section of the container tail at +0x3a6000 (2308 B):
 *   u16 'LP', u8 format, u8 count 128, rec_t r[128], u16 check[128].
 * check = folded hash of the record and its index, so a record copied to another
 * index never validates. Records are written and read one by one inside the Sound
 * codec hooks, only for containers known by construction (image, registered).
 *
 * +Drive sounds (S1): the save 40111b08(mmcfs, record, index) writes from s1_buf =
 * native record + 24-byte LFO3 trailer {u16 'LS', u8 ver, u8 present, rec_t, u32 hash}
 * at the odd offset 0x107 (built aligned, copied bytewise); the size operand at 40111b80
 * becomes 0x13f (one sector either way; shared optional metadata). The load 40110ac4(mmcfs, buffer, index) reads
 * 0x13f B (size operand at 40110b1c) into s1_buf,
 * copies the native 0x107 B to the caller's buffer and remembers the trailer for the
 * decode of that buffer (association validated by a hash of the native bytes).
 */
#define SOUND_STORED 0x107u
#define POOL_RECORDS 0x390204u
#define POOL_SECTION 0x3a6000u
#define POOL_MAGIC   0x4c50u            /* 'LP' */
#define S1_MAGIC     0x4c53u            /* 'LS' */

typedef struct { u16 magic; u8 version, count; rec_t r[POOL]; u16 check[POOL]; } pool_section_t;
typedef struct { u16 magic; u8 version, present; rec_t r; u32 hash; } s1_trailer_t;   /* 24 B */
/* Snapshot carrier, inside each kit slot's unused [0x15b8,0x15d0) padding.
 * The native snapshot copies the slot, but has no capacity for the LP section. */
#define CARRIER 0x15b8u
#define CARRIER_MAGIC 0x4c70u       /* 'Lp', distinct from the canonical 'LP' */
typedef struct { u16 magic; u8 version, index; rec_t r; u32 hash; } carrier_t;
typedef struct { u32 addr, tag, valid; rec_t r; } assoc_t;

u8 s1_buf[0x200] DATA __attribute__((aligned(16))) = {0};
u32 kit_codec_depth DATA = 0;        /* > 0 while a kit codec runs (its per-track Sounds) */
assoc_t encoded DATA = {0};          /* last record encoded from a Sound with known LFO3 */
assoc_t loaded DATA = {0};           /* last +Drive record loaded with a valid trailer */
/* sound counters: pool_written, pool_read_valid, pool_read_invalid, s1_saved_with,
 * s1_saved_without, s1_loaded_valid, s1_loaded_without, decode_from_s1 */
u32 sound_stats[8] DATA = {0};

static u16 fold(const rec_t *r, u32 index)
{
    u32 h = hash((const u8 *)r, sizeof(rec_t)) ^ (index * 0x9e3779b1u);
    return (u16)(h ^ (h >> 16));
}

void lfo3_project_open(u32 container);

/* A single LP header covers all records. Upgrade every validated old entry
 * before changing it, or a partial write would reinterpret the untouched pool.
 * Unknown/corrupt headers never authorize records from reused memory. */
static void pool_current(pool_section_t *s)
{
    u32 p, old = s->version;
    u32 valid = s->magic == POOL_MAGIC && record_format(old) && s->count == POOL;
    if (valid && old == RECORD_FORMAT)
        return;
    for (p = 0; p < POOL; p++) {
        transcode_rec(&s->r[p], valid && s->check[p] == fold(&s->r[p], p) ? &s->r[p] : 0, old);
        s->check[p] = fold(&s->r[p], p);
    }
    s->magic = POOL_MAGIC;
    s->count = POOL;
    s->version = RECORD_FORMAT;
}

/* -> pool index of a native record in a known container, its section via *section */
static int pool_index(u32 record, pool_section_t **section)
{
    u32 i, base, off;
    for (i = 0; i < (container_scope_callback ? 0xffffffffu:CONTAINERS+3); i++) {
        base=container_at(i);if(container_scope_callback && !base)break;
        if (!base || record < base + POOL_RECORDS)
            continue;
        off = record - (base + POOL_RECORDS);
        if (off < POOL * SOUND_STORED && off % SOUND_STORED == 0) {
            *section = (pool_section_t *)(base + POOL_SECTION);
            return (int)(off / SOUND_STORED);
        }
    }
    return -1;
}

/* after the native Sound encode(record, sound, flag) -> ok */
void lfo3_sound_encoded(u32 record, u32 sound, u32 ok)
{
    if((ok&255u) && !kit_codec_depth)lfo3_meta_notify(ST_SOUND_ENCODE,record,sound,meta_container(record));
    const rec_t *r;
    pool_section_t *s;
    int p;
    if (!(ok & 0xffu) || kit_codec_depth)   /* track Sounds: the kit trailer covers them */
        return;
    if(sub_encoded(record,sound,0))return;
    r = records_of(sound, SOUND_SIZE);
    if ((p = pool_index(record, &s)) >= 0) {
        lfo3_project_open((u32)s - POOL_SECTION);
        pool_current(s);
        encode_recs(&s->r[p], r, 1);
        s->check[p] = fold(&s->r[p], (u32)p);
        *(u16 *)((u32)s - POOL_SECTION + KIT_SLOTS + (u32)p * KIT_SLOT + CARRIER) = 0;
        sound_stats[0]++;
        return;
    }
    {u32 count;rec_t *owned=lfo3_shadow_owned_record(record,SOUND_STORED,&count,r?1:2);
     if(owned)copy_recs(owned,r,1);}
    encoded.valid = r != 0;              /* a record for the +Drive or another buffer */
    encoded.addr = record;
    encoded.tag = hash((const u8 *)record, SOUND_STORED);
    copy_recs(&encoded.r, r, 1);
}

/* after native standalone Sound decode: known metadata, otherwise defaults. */
void lfo3_sound_decoded(u32 sound, u32 record, u32 ok)
{
    if((ok&255u) && !kit_codec_depth)lfo3_meta_notify(ST_SOUND_DECODE,sound,record,meta_container(record));
    pool_section_t *s;
    rec_t work;
    int p;
    if (!(ok & 0xffu) || kit_codec_depth)
        return;
    if(sub_decoded(sound,record,0))return;
    if ((p = pool_index(record, &s)) >= 0) {
        if (s->magic == POOL_MAGIC && record_format(s->version) && s->count == POOL &&
            s->check[p] == fold(&s->r[p], (u32)p)) {
            sound_stats[1]++;
            decode_recs(&work, &s->r[p], 1, s->version);
            set_records(sound, SOUND_SIZE, &work, 1);
        } else {
            sound_stats[2]++;
            set_records(sound, SOUND_SIZE, 0, 0);
        }
        return;
    }
    {u32 count;rec_t *owned=lfo3_shadow_owned_record(record,SOUND_STORED,&count,0);
     if(owned){sound_stats[7]++;set_records(sound,SOUND_SIZE,owned,1);return;}}
    if (loaded.valid && loaded.addr == record && loaded.tag == hash((const u8 *)record, SOUND_STORED)) {
        sound_stats[7]++;
        set_records(sound, SOUND_SIZE, &loaded.r, 1);
        loaded.valid = 0;
        return;
    }
    /* Kit Sounds were excluded by kit_codec_depth. An unassociated standalone
     * record (including a stock/SysEx import) has no LFO3: never keep old values. */
    set_records(sound, SOUND_SIZE, 0, 0);
}

/* +Drive save: -> buffer for the native save (record + trailer) */
static void bytes_copy(u8 *d, const u8 *s, u32 n)
{
    while (n--)
        *d++ = *s++;
}

/* Native project-sized copies do not include LP. Only full, known containers
 * have capacity at +POOL_SECTION; a native snapshot has just 0x3a5604 bytes.
 * Carrier helpers never enlarge that native copy or read beyond a short source. */
static carrier_t *carrier(u32 container, u32 p)
{
    return (carrier_t *)(container + KIT_SLOTS + p * KIT_SLOT + CARRIER);
}

static void carriers_restore(u32 container)
{
    u32 p;
    pool_section_t *s = (pool_section_t *)(container + POOL_SECTION);
    carrier_t *c;
    s->magic = POOL_MAGIC;
    s->version = RECORD_FORMAT;
    s->count = POOL;
    for (p = 0; p < POOL; p++) {
        c = carrier(container, p);
        transcode_rec(&s->r[p], c->magic == CARRIER_MAGIC && record_format(c->version) && c->index == p &&
                  c->hash == hash((const u8 *)c, sizeof(*c) - 4) ? &c->r : 0, c->version);
        s->check[p] = fold(&s->r[p], p);
        c->magic = 0;
    }
}

/* Called only with a full container, before decoding. Any carrier mark makes
 * carriers authoritative; a corrupt/missing individual carrier gives defaults.
 * Once consumed, canonical LP owns the records again. */
void lfo3_project_open(u32 container)
{
    lfo3_meta_notify(ST_SOUND_PROJECT_READY,container,0,0);
    u32 p;
    if (!container)               /* native decode rejects a missing cache */
        return;
    for (p = 0; p < POOL; p++)
        if (carrier(container, p)->magic == CARRIER_MAGIC) {
            carriers_restore(container);
            return;
        }
}

static u32 project_copy(u32 dst, u32 src, u32 n)
{
    u32 p;
    carrier_t c;
    pool_section_t *s;
    if (n != 0x3a5604u)
        return 0;
    /* Size alone is not an identity: leave unrelated large buffers untouched. */
    if ((dst & 3u) || *(const u32 *)dst != 0xbeefbaceu ||
        *(const u32 *)(dst + 4) != 13 || *(const u32 *)(dst + 0x3a5600u) != 0xbacef00cu)
        return 1;
    if (full_container(src)) {
        lfo3_project_open(src);
        s = (pool_section_t *)(src + POOL_SECTION);
        for (p = 0; p < POOL; p++) {
            c.magic = CARRIER_MAGIC;
            c.version = RECORD_FORMAT;
            c.index = (u8)p;
            transcode_rec(&c.r, s->magic == POOL_MAGIC && record_format(s->version) && s->count == POOL &&
                      s->check[p] == fold(&s->r[p], p) ? &s->r[p] : 0, s->version);
            c.hash = hash((const u8 *)&c, sizeof(c) - 4);
            bytes_copy((u8 *)carrier(dst, p), (const u8 *)&c, sizeof(c));
        }
    } else {
        /* A short/unknown source may contain carriers or legacy padding. Seal
         * defaults for invalid entries so an unknown destination cannot retain
         * a stale but valid LP when it is later opened as a full container. */
        for (p = 0; p < POOL; p++) {
            carrier_t *to = carrier(dst, p);
            c.magic = CARRIER_MAGIC;
            c.version = RECORD_FORMAT;
            c.index = (u8)p;
            transcode_rec(&c.r, to->magic == CARRIER_MAGIC && record_format(to->version) && to->index == p &&
                          to->hash == hash((const u8 *)to, sizeof(*to) - 4) ? &to->r : 0, to->version);
            c.hash = hash((const u8 *)&c, sizeof(c) - 4);
            bytes_copy((u8 *)to, (const u8 *)&c, sizeof(c));
        }
    }
    if (full_container(dst)) {
        /* Consume carriers from the bytes just copied, never read LP past a
         * short source. Unknown legacy snapshots therefore give defaults. */
        carriers_restore(dst);
    }
    return 1;
}

/* Native NEW PROJECT leaves all padding and LP untouched on a reused buffer.
 * Clear only extension marks after it returns, including every kit trailer. */
void lfo3_project_new(u32 container)
{
    lfo3_meta_notify(ST_SOUND_PROJECT_NEW,container,0,0);
    u32 k;
    carrier_t c;
    u32 full = full_container(container);
    for (k = 0; k < KITS; k++) {
        *(u16 *)(container + KIT_SLOTS + k * KIT_SLOT + TRAILER) = 0;
        if (full)
            carrier(container, k)->magic = 0;
        else {
            c.magic = CARRIER_MAGIC;
            c.version = RECORD_FORMAT;
            c.index = (u8)k;
            encode_recs(&c.r, 0, 1);
            c.hash = hash((const u8 *)&c, sizeof(c) - 4);
            bytes_copy((u8 *)carrier(container, k), (const u8 *)&c, sizeof(c));
        }
    }
    if (full)
        ((pool_section_t *)(container + POOL_SECTION))->magic = 0;
}

u32 lfo3_s1_prepare(u32 record,u32 output)
{
    s1_trailer_t t;
    u8 *buffer=(u8 *)output;u32 count;rec_t *owned;
    bytes_copy(buffer, (const u8 *)record, SOUND_STORED);
    t.magic = S1_MAGIC;
    t.version = RECORD_FORMAT;
    t.present = encoded.valid && encoded.addr == record &&
                encoded.tag == hash((const u8 *)record, SOUND_STORED);
    owned=lfo3_shadow_owned_record(record,SOUND_STORED,&count,0);
    if(owned)t.present=1;
    encode_recs(&t.r, owned ? owned : t.present ? &encoded.r : 0, 1);
    t.hash = hash((const u8 *)&t, sizeof(t) - 4);
    bytes_copy(buffer + SOUND_STORED, (const u8 *)&t, sizeof(t));
    sound_stats[t.present ? 3 : 4]++;
    /* The stack scratch is 512 bytes; zero the optional namespace even when
     * no subscriber exists, then let it supply its versioned record. */
    {u32 i;for(i=0;i<ST_SOUND_S1_EXTENSION_BYTES;i++)buffer[ST_SOUND_S1_EXTENSION+i]=0;}
    lfo3_meta_notify(ST_SOUND_S1_SAVE,record,output,meta_container(record));
    return output;
}

/* +Drive load, after the native read into s1_buf: give the caller its 0x107 B */
void lfo3_s1_loaded(u32 buffer, u32 ok,u32 input)
{
    lfo3_meta_notify(ST_SOUND_S1_LOAD,buffer,input,ok);
    s1_trailer_t trailer, *t = &trailer;
    bytes_copy((u8 *)buffer, (const u8 *)input, SOUND_STORED);
    bytes_copy((u8 *)&trailer, (const u8 *)input + SOUND_STORED, sizeof(trailer));
    loaded.valid = 0;
    if (!(ok & 0xffu))
        return;
    /* The association exists even without a valid trailer: loading a sound without
     * LFO3 (saved by 1.41, imported by SysEx) must give defaults, like every other
     * parameter of the loaded sound, never keep the previous sound's LFO3. */
    loaded.valid = 1;
    loaded.addr = buffer;
    loaded.tag = hash((const u8 *)buffer, SOUND_STORED);
    if (t->magic == S1_MAGIC && record_format(t->version) && t->present &&
        t->hash == hash((const u8 *)t, sizeof(s1_trailer_t) - 4)) {
        decode_recs(&loaded.r, &t->r, 1, t->version);
        sound_stats[5]++;
    } else {
        copy_recs(&loaded.r, 0, 1);
        sound_stats[6]++;
    }
    {u32 count;rec_t *owned=lfo3_shadow_owned_record(buffer,SOUND_STORED,&count,1);
     if(owned)copy_recs(owned,&loaded.r,1);}
}

/* after the native kit decode(kit, storage) -> ok */
void lfo3_kit_decoded(u32 kit, u32 storage, u32 ok)
{
    if(ok&255u)lfo3_meta_notify(ST_SOUND_KIT_DECODE,kit,storage,meta_container(storage));
    const trailer_t *t = (const trailer_t *)(storage + TRAILER);
    rec_t work[TRACKS];
    rec_t *to;
    u32 count;
    if (!(ok & 0xffu))
        return;
    if (!kit_slot(storage)) {                /* SysEx import or unknown buffer */
        codec_stats[4]++;
        set_records(kit, KIT_SIZE, 0, 0);
        return;
    }
    if (t->magic == MAGIC && record_format(t->version) && t->count == TRACKS &&
        t->hash == hash((const u8 *)t, sizeof(trailer_t) - 4)) {
        codec_stats[2]++;
        /* Most decodes target the live array. Decode directly into its shadow;
         * an intermediate 208-byte record array would just be copied again. */
        to = live(kit, KIT_SIZE, &count);
        if (to)
            decode_recs(to, t->r, TRACKS, t->version);
        else {
            decode_recs(work, t->r, TRACKS, t->version);
            set_records(kit, KIT_SIZE, work, 1);
        }
    } else {
        codec_stats[3]++;                    /* project without LFO3 or damaged trailer */
        set_records(kit, KIT_SIZE, 0, 0);
    }
}

void lfo3_shadow_forget_extent(u32 base,u32 bytes)
{
    lfo3_meta_notify(ST_SOUND_FREE,base,0,bytes);
    u32 i;
    for(i=0;i<SIDE;i++)if(side[i].addr-base<bytes)side[i].addr=0;
    if(encoded.addr-base<bytes)encoded.valid=0;
    if(loaded.addr-base<bytes)loaded.valid=0;
}

extern u32 lfo3_memcpy_entry(u32,u32,u32);
u32 lfo3_project_copy_task(u32 d,u32 s,u32 n)
{u32 result;lfo3_task_enter();result=lfo3_memcpy_entry(d,s,n);lfo3_task_leave();return result;}
