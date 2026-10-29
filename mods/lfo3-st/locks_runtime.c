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
typedef struct {u16 v[8];} rec_t;
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
static u32 owned_storage[394],pattern_storage[282],events_storage[386],budget_storage[5];
u32 lfo3_active_locks;
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
/* -> parameters for this block: the base bank, or the overlay with locks.
 * The overlay is kept incrementally: a full copy only when locks appear;
 * then only rows that are locked, changed lock state or were republished. */
const u16 *lfo3_events_bank(void)
{
    events_t *c=lfo3_events_context;u32 bits=lfo3_active_locks,rows,t,i,mask,slot,id,flags,type;
    u32 *d,*s;u16 *o;const u16 *v;
    if(!bits || !c){overlay_locked=0;return &lfo3_bank->params[0][0];}
    rows=overlay_locked?bits|overlay_locked|lfo3_params_dirty:0x1fffu;
    lfo3_params_dirty=0;overlay_locked=bits;
    for(t=0;rows;t++,rows>>=1){
        if(!(rows&1u))continue;
        d=(u32 *)lfo3_bank->overlay[t];s=(u32 *)lfo3_bank->params[t];d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=s[3];
        if(!(bits&(1u<<t)))continue;
        mask=c->active[t].mask;v=c->active[t].record.v;o=lfo3_bank->overlay[t];
        for(i=0;i<8;i++)if(mask&(1u<<i))o[i]=v[i];
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
