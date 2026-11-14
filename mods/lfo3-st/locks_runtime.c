/* SPDX-License-Identifier: GPL-2.0-or-later
 * LFO3 p-lock runtime: the module's version of Synfact locks_runtime.c.
 * Task writers use a native recursive mutex (40001980/40001aa2, as malloc);
 * interrupt readers never take it, allocate, or keep pattern pointers.
 *
 * Engine overlay (MAIN IRQ, per block): no active lock costs one test. With
 * locks, the prepared base bank is copied to the SRAM overlay and only the
 * locked words of the locked tracks are patched. A locked DEST is resolved
 * through the machine lookups only when its raw value or the track's machine
 * type changes (Synfact resolved it every block; destination_reuse cached it).
 */
typedef unsigned int u32;typedef unsigned short u16;typedef unsigned char u8;
typedef struct {u16 v[16];} rec_t;
typedef struct {u32 mask;rec_t record;} event_t;
typedef struct {u32 stride;event_t queued[64],active[13];} events_t;
struct lfo3_bank {u8 state[13][40];u32 rng[2];u16 params[13][8];u16 overlay[13][8];};
extern struct lfo3_bank *lfo3_bank;
extern events_t *lfo3_events_context;
extern void *lfo3_pattern_context,*lfo3_budget_context;
extern u32 lfo3_owned_context_bytes(void),lfo3_pattern_context_bytes(void);
extern u32 lfo3_budget_context_bytes(void),lfo3_events_context_bytes(void);
extern u32 lfo3_owned_init(void *,u32),lfo3_budget_init(void *,u32);
extern void lfo3_pattern_init(void *,u32);
extern u32 shadow_query_callback,shadow_relocate_callback,shadow_tracked_callback;
extern u32 shadow_enter_callback,shadow_leave_callback;
extern rec_t *lfo3_shadow_owned_record_body(u32,u32,u32 *,u32);
extern void lfo3_shadow_owner_relocate_body(u32,u32,u32);
extern u32 lfo3_shadow_owned_tracked_body(u32);
extern u32 lfo3_track_type(u32),lfo3_track_valid(u32);
static u32 writer_mutex[4];
static u32 owned_storage[394],pattern_storage[282],events_storage[694],budget_storage[5];
u32 lfo3_active_locks;
/* Potentially audible effective DEP/DEST rows. Updated only by parameter
 * publication or DEP/DEST lock changes; never scanned in the render path. */
u32 lfo3_output_rows;
extern u32 lfo3_lock(void);
extern void lfo3_unlock(u32),lfo3_output_interest(u32);
static u16 dest_raw[13]={0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff};
static u16 dest_done[13];static u8 dest_type[13];

/* A new publication may change validity: resolve that track's DEST again. */
void lfo3_dest_forget(u32 t){if(t<13)dest_raw[t]=0xffff;}
void lfo3_writer_enter(void){((void (*)(void *))0x40001980u)(writer_mutex);}
void lfo3_writer_leave(void){((void (*)(void *))0x40001aa2u)(writer_mutex);}

/* Task context, before any lock hook is installed. 8 MiB budget as Synfact;
 * nothing is reserved until a pattern gets its first LFO3 lock. */
u32 lfo3_locks_setup(void)
{
    if(lfo3_owned_context_bytes()!=sizeof(owned_storage) ||
       lfo3_pattern_context_bytes()!=sizeof(pattern_storage) ||
       lfo3_events_context_bytes()!=sizeof(events_storage) ||
       lfo3_budget_context_bytes()!=sizeof(budget_storage))return 0;
    lfo3_budget_context=budget_storage;
    if(!lfo3_budget_init(budget_storage,0x800000u))return 0;
    lfo3_pattern_context=pattern_storage;
    lfo3_pattern_init(pattern_storage,0x40a38b34u);          /* 128 live patterns */
    if(!lfo3_owned_init(owned_storage,0x40a38b34u))return 0;
    lfo3_events_context=(events_t *)events_storage;
    shadow_query_callback=(u32)lfo3_shadow_owned_record_body;
    shadow_relocate_callback=(u32)lfo3_shadow_owner_relocate_body;
    shadow_tracked_callback=(u32)lfo3_shadow_owned_tracked_body;
    shadow_enter_callback=(u32)lfo3_writer_enter;shadow_leave_callback=(u32)lfo3_writer_leave;
    lfo3_active_locks=0;return 1;
}

/* Rows of the base bank changed by a publication or edit (ui.c prepare_track). */
volatile u32 lfo3_params_dirty;
static u32 overlay_locked;

static __inline__ u32 output_enabled(u32 t)
{
    const u16 *p=lfo3_bank->params[t];events_t *c=lfo3_events_context;
    u32 depth=p[7],dest=p[3],mask=c?c->active[t].mask:0;
    if(mask&0x80u)depth=c->active[t].record.v[7];
    if(mask&0x08u)dest=c->active[t].record.v[3];
    /* A locked DEST may later fail the native metadata resolver. Keeping
     * its route enabled is conservative: the existing overlay still validates
     * it. An absent route must never hide a lock that can activate output. */
    dest>>=8;
    return depth!=0x4000u && dest>=1 && dest<=70;
}
void lfo3_output_track(u32 t)
{
    u32 sr,before,after,bit;
    if(!lfo3_bank || t>=13)return;
    sr=lfo3_lock();before=lfo3_output_rows;bit=1u<<t;
    after=output_enabled(t)?before|bit:before&~bit;
    lfo3_output_rows=after;
    if((!before)!=(!after))lfo3_output_interest(after!=0);
    lfo3_unlock(sr);
}
/* Initialization, pool reset or CRC fallback only. Invalidate an old overlay
 * when the prepared bank resets, and commit the route exactly once. */
void lfo3_output_reset(void)
{
    u32 sr,t,bits=0;
    if(!lfo3_bank)return;
    sr=lfo3_lock();
    for(t=0;t<13;t++)if(output_enabled(t))bits|=1u<<t;
    lfo3_output_rows=bits;lfo3_params_dirty=0x1fffu;overlay_locked=0;
    lfo3_output_interest(bits!=0);
    lfo3_unlock(sr);
}
/* -> parameters for this block: the base bank, or the overlay with locks.
 * The overlay is kept incrementally: a full copy only when locks appear;
 * then only rows that are locked, changed lock state or were republished. */
const u16 *lfo3_events_bank(void)
{
    events_t *c=lfo3_events_context;u32 bits=lfo3_active_locks,rows,t,mask,slot,id,flags,type;
    u32 *d,*s;u16 *o;const u16 *v;
    if(!bits || !c){overlay_locked=0;return &lfo3_bank->params[0][0];}
    rows=overlay_locked?bits|overlay_locked|lfo3_params_dirty:0x1fffu;
    lfo3_params_dirty=0;overlay_locked=bits;
    for(t=0;rows;t++,rows>>=1){
        if(!(rows&1u))continue;
        d=(u32 *)lfo3_bank->overlay[t];s=(u32 *)lfo3_bank->params[t];d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=s[3];
        if(!(bits&(1u<<t)))continue;
        mask=c->active[t].mask;v=c->active[t].record.v;o=lfo3_bank->overlay[t];
        /* Eight fixed lanes: expose constant bit tests/offsets to ColdFire
         * without a loop counter, per-iteration shift or extra cache/check. */
        if(mask&0x01u)o[0]=v[0];
        if(mask&0x02u)o[1]=v[1];
        if(mask&0x04u)o[2]=v[2];
        if(mask&0x08u)o[3]=v[3];
        if(mask&0x10u)o[4]=v[4];
        if(mask&0x20u)o[5]=v[5];
        if(mask&0x40u)o[6]=v[6];
        if(mask&0x80u)o[7]=v[7];
        if(!(mask&8u))continue;
        type=lfo3_track_type(t);
        if(dest_raw[t]!=o[3] || dest_type[t]!=type){
            dest_raw[t]=o[3];dest_type[t]=(u8)type;slot=o[3]>>8;id=flags=0;
            if(lfo3_track_valid(t) && slot<=70 && !(o[3]&255)){
                id=t==12?((u32 (*)(u32))0x400a6300u)(slot):((u32 (*)(u32,u32))0x400a63ecu)(slot,type);
                if(id)flags=((u32 (*)(u32))0x400a666au)(id);
            }
            dest_done[t]=flags&0x200?o[3]:0x7f00;
        }
        o[3]=dest_done[t];
    }
    return &lfo3_bank->overlay[0][0];
}
