/* SPDX-License-Identifier: GPL-2.0-or-later
 * modulation-skip-st: caches the stock modulation 40097afc per track and
 * runs only tracks whose inputs changed. The full stock body costs about
 * 5.7 CPU points in the MAIN audio IRQ.
 *
 * The stock body writes, per track t, working[t] + 2*index for 4 entries of
 * 12 velocity groups and 13x5 source groups; entry = {amount, index} longs of
 * the track's unsmoothed machine data. Its output depends on that track's
 * source tables, machine data and destination words. Zero-amount entries still
 * saturate out-of-range words; normal filter values need no recalculation.
 * A filtered copy of the stock kernel runs dirty tracks; clean tracks receive
 * cached active outputs and zero-depth saturation. Out-of-track destinations
 * and dense matrices use stock.
 *
 * Site: 40097b04, the stock body after its prologue, where core-st's
 * modulation adapter jumps when no module replaces it (analog POLY does,
 * and that path is untouched). Its six bytes (two instructions) are replaced
 * by `jmp abs.l`; the stock path re-executes them. Install once at init,
 * nothing if the bytes differ (header `MSKIP NO1`).
 * Modes: VERIFY always runs stock and compares (errors counted, never skips);
 * ON skips or runs selected tracks, with periodic stock comparisons; the first
 * difference turns it OFF (stock forever). Header page 3 of 3 (System info,
 * irq-timer-st, this): `MSKIP ON  H99 S98 E000` = mode, % of blocks with no
 * dirty tracks, % avoiding the full stock body, differences found.
 */
#include <syntakt/modules.h>
typedef unsigned int u32;typedef unsigned short u16;typedef unsigned char u8;typedef short s16;typedef int s32;
#ifndef SKIP_R32
#define SKIP_R32(a) (*(const volatile u32 *)(a))
#define SKIP_R16(a) (*(const volatile u16 *)(a))
#define SKIP_W16(a,v) (*(volatile u16 *)(a)=(u16)(v))
#define SKIP_FILL ((void (*)(void *,int,int,int,int,u32))0x400f7df2u)
#define SKIP_TEXT ((void (*)(void *,void *,int,int,u32,const char *,const char *))0x400f8a0cu)
#define SKIP_MEM(a) ((volatile u8 *)(a))
#endif
#ifndef MODULATION_SKIP_VERIFY
#define MODULATION_SKIP_VERIFY 0
#endif
#define WORKING 0x80006348u
#define TRACK(t) (WORKING+0x1cu+0x8eu*(t))
#define RAW(t) (WORKING+0x754u+0x7cu*(t))
#define SITE 0x40097b04u
#define WRITER 0x400a30c4u
#define CHECK_EVERY 64u
enum {OFF,VERIFY,ON,REFUSED};
enum {RUN_STOCK,SKIPPED,RUN_AND_CHECK,RUN_FILTERED,RUN_SPARSE};
extern void modulation_skip_entry(void),modulation_skip_machine_entry(void),modulation_skip_filtered(void);
extern u32 modulation_skip_lock(void);
extern void modulation_skip_unlock(u32),modulation_skip_code16(u32,u32),modulation_skip_sync(void);
u32 modulation_skip_mode;                       /* OFF until installed */
/* The stock source reads, in the same group order as RAW(t). Group 5 is a
 * longword shifted by eight; all other groups read signed halfwords. */
static const u32 source_base[6]={0x80004670u,0x80004746u,0x8000472cu,
                                  0x80004712u,0x800046f8u,0x800046b8u};
#define TRACKS 13u
#define ALL_TRACKS ((1u<<TRACKS)-1u)
#define ENTRIES 24u
#define GROUPS 6u
#define TRACK_WORDS (0x8eu/2u)
#define PARTIAL_LIMIT 12u
#define DEST_LIMIT 100u
static u32 raw_shadow[TRACKS][ENTRIES],source_shadow[TRACKS][GROUPS];
static u32 dest[TRACKS][ENTRIES];static u16 pre[TRACKS][ENTRIES],post[TRACKS][ENTRIES];
static u32 zero_dest[TRACKS][ENTRIES];static u16 zero_pre[TRACKS][ENTRIES];
static u8 used[TRACKS],zero_used[TRACKS],active_groups[TRACKS],sparse_safe[TRACKS];
static u8 active_seen[TRACK_WORDS],zero_seen[TRACK_WORDS],epoch;
static u8 active_ids[TRACKS],zero_ids[TRACKS],active_count,zero_count;
/* Derived only when matrix membership changes, in the same row/entry order.
 * The frequent all-track projection needs no per-row indexing or mask test. */
static u32 zero_all[TRACKS*ENTRIES] __attribute__((aligned(16)));
static u32 zero_all_count,sparse_track_mask;
static u16 zero_offset[TRACKS] __attribute__((aligned(4)));
static u32 valid_mask,unsafe_mask,active_track_mask,zero_track_mask,total_used,raw_ready,count,check_mask,capture_mask;
#ifndef SKIP_HOST
u32 modulation_skip_decide(u32);
extern u32 modulation_skip_zero_decide(u32),modulation_skip_stock_decide(u32);
/* Publication chooses the entry. Active matrices pay no zero-path guards. */
static u32 (*volatile modulation_skip_decider)(u32) __attribute__((used,aligned(4)))=modulation_skip_decide;
#endif
volatile u32 modulation_skip_run_mask;
volatile u32 modulation_skip_blocks,modulation_skip_hits,modulation_skip_skips;
volatile u32 modulation_skip_partial_runs,modulation_skip_errors;
static u32 source(u32 t,u32 g)
{return g==5u?SKIP_R32(source_base[g]+4u*t):SKIP_R16(source_base[g]+2u*t);}
/* Keep the stock RAM address independent of the relocatable cache pointer.
 * GCC otherwise folds their difference into an invalid bank relocation. */
static __inline__ u32 raw_address(u32 t)
{u32 at=RAW(t);__asm__ volatile("":"+r"(at));return at;}
static void build_track(u32 t)
{
    u32 g,k,e,i,at,bit=1u<<t;
    used[t]=zero_used[t]=0;active_groups[t]=0;sparse_safe[t]=0;unsafe_mask&=~bit;
    if(!++epoch){for(i=0;i<TRACK_WORDS;i++)active_seen[i]=zero_seen[i]=0;epoch=1;}
    for(g=t==12u?1u:0u;g<GROUPS;g++)for(k=0;k<4u;k++){
        e=raw_shadow[t][4u*g+k];
        if(!(e>>16))continue;
        active_groups[t]|=(u8)(1u<<g);
        i=(u32)(s32)(s16)e;
        if(i>=TRACK_WORDS){unsafe_mask|=bit;continue;}
        at=TRACK(t)+2u*i;
        if(active_seen[i]!=epoch){active_seen[i]=epoch;dest[t][used[t]++]=at;}
    }
    /* Zero-depth writes still saturate out-of-range words. Keep only zero-only
     * addresses; they must not be overwritten by a cached post value. */
    for(g=t==12u?1u:0u;g<GROUPS;g++)for(k=0;k<4u;k++){
        e=raw_shadow[t][4u*g+k];
        if(e>>16)continue;
        i=(u32)(s32)(s16)e;
        if(i>=TRACK_WORDS){unsafe_mask|=bit;continue;}
        at=TRACK(t)+2u*i;
        if(active_seen[i]==epoch || zero_seen[i]==epoch)continue;
        zero_seen[i]=epoch;zero_dest[t][zero_used[t]++]=at;
    }
    /* External zero groups may project an active destination before or after
     * its source group. Let the guarded full kernel retain their order. */
    if(active_groups[t] && !(active_groups[t]&(active_groups[t]-1u)) && !(active_groups[t]&0x20u)){
        sparse_safe[t]=1;
        for(g=t==12u?1u:0u;g<GROUPS;g++)if(!(active_groups[t]&(1u<<g)))
            for(k=0;k<4u;k++){
                e=raw_shadow[t][4u*g+k];
                if(e>>16)continue;
                i=(u32)(s32)(s16)e;
                if(i<TRACK_WORDS && active_seen[i]==epoch)sparse_safe[t]=0;
            }
    }
    for(g=t==12u?1u:0u;g<GROUPS;g++)if(active_groups[t]&(1u<<g))source_shadow[t][g]=source(t,g);
}
/* The writer supplies a track hint. Scan its 24 entries on publication;
 * every 64th block still scans all 312 to catch any unobserved writer. */
static __attribute__((noinline)) u32 refresh_machine(u32 scan)
{
    u32 t,k,v,any,changed=0,bit,at;
    for(t=0;t<TRACKS;t++){
        bit=1u<<t;if(!(scan&bit))continue;any=0;at=raw_address(t);
        for(k=0;k<ENTRIES;k++){
            v=SKIP_R32(at+4u*k);any|=v;
            if(!raw_ready || raw_shadow[t][k]!=v){raw_shadow[t][k]=v;changed|=bit;}
        }
        if(changed&bit){
            if(any)build_track(t);
            else{
                used[t]=0;zero_used[t]=1;active_groups[t]=0;sparse_safe[t]=0;
                zero_dest[t][0]=TRACK(t);unsafe_mask&=~bit;
            }
        }
    }
    if(changed){
        active_track_mask=zero_track_mask=total_used=0;active_count=zero_count=0;zero_all_count=0;sparse_track_mask=0;
        for(t=0;t<TRACKS;t++){
            if(active_groups[t]){active_track_mask|=1u<<t;active_ids[active_count++]=(u8)t;}
            if(zero_used[t]){zero_track_mask|=1u<<t;zero_ids[zero_count++]=(u8)t;}
            if(sparse_safe[t])sparse_track_mask|=1u<<t;
            total_used+=(u32)used[t]+(u32)zero_used[t];
            zero_offset[t]=(u16)zero_all_count;
            for(k=0;k<zero_used[t];k++)zero_all[zero_all_count++]=zero_dest[t][k];
        }
#ifndef SKIP_HOST
        modulation_skip_decider=unsafe_mask || total_used>DEST_LIMIT?modulation_skip_stock_decide:
            active_track_mask?modulation_skip_decide:modulation_skip_zero_decide;
#endif
    }
    raw_ready=1;valid_mask&=~changed;
    return changed;
}
/* The machine data is the conversion's copy of the unsmoothed mirror, which
 * only 400a30c4 writes (sound publication, machine change). Its hook adds one
 * on entry and one on exit, so a block that saw neither may skip comparing the
 * 312 longs. Every CHECK_EVERY blocks they are compared anyway: a difference
 * without a write is an unknown writer, and turns ON off. */
volatile u32 modulation_skip_machine_writes;
volatile u8 modulation_skip_machine_dirty[TRACKS];
static u32 machine_seen,ticks_irq;
static void capture_pre(u32 mask)
{u32 j,t,i;for(j=0;j<active_count;j++){t=active_ids[j];if(mask&(1u<<t))for(i=0;i<used[t];i++)pre[t][i]=SKIP_R16(dest[t][i]);}}
static void replay(u32 mask)
{u32 j,t,i;for(j=0;j<active_count;j++){t=active_ids[j];if(mask&(1u<<t))for(i=0;i<used[t];i++)SKIP_W16(dest[t][i],post[t][i]);}}
/* Measured against each of the six native source groups: a zero amount maps
 * 0000..7fff to itself, 8000..bfff to 7fff, c000..ffff to zero. */
static u32 zero_result(u32 v)
{return v<0x8000u?v:v<0xc000u?0x7fffu:0u;}
static void capture_zero(u32 mask)
{u32 j,t,i;for(j=0;j<zero_count;j++){t=zero_ids[j];if(mask&(1u<<t))for(i=0;i<zero_used[t];i++)zero_pre[t][i]=SKIP_R16(zero_dest[t][i]);}}
static __inline__ void project_list(const u32 *addresses,u32 n)
{
#ifndef SKIP_HOST
    extern void modulation_skip_project_all(const u32 *,u32);
    modulation_skip_project_all(addresses,n);
#else
    u32 i,v;
    for(i=0;i<n;i++){
        v=SKIP_R16(addresses[i]);if(v>=0x8000u)SKIP_W16(addresses[i],zero_result(v));
    }
#endif
}
static void project_zero(u32 mask)
{
    u32 bits,t;
    if(mask==ALL_TRACKS){project_list(zero_all,zero_all_count);return;}
    for(bits=mask&zero_track_mask;bits;bits&=bits-1u){
        t=31u-(u32)__builtin_clz(bits&-bits);
        project_list(zero_all+zero_offset[t],zero_used[t]);
    }
}
/* Dirty rows are captured; clean rows are restored. Each active/zero list
 * is walked once instead of twice. Accepted destinations stay within their
 * own row, and active/zero-only sets are disjoint, so the two row classes
 * cannot observe each other's writes. Source-group/native order is unchanged. */
static void prepare_partial(u32 dirty)
{
    u32 j,t,i,n;const u32 *at;
    for(j=0;j<active_count;j++){
        t=active_ids[j];at=dest[t];n=used[t];
        if(dirty&(1u<<t)){
            for(i=0;i<n;i++)pre[t][i]=SKIP_R16(at[i]);
        }else{
            for(i=0;i<n;i++)SKIP_W16(at[i],post[t][i]);
        }
    }
    for(j=0;j<zero_count;j++){
        t=zero_ids[j];n=zero_used[t];
        if(dirty&(1u<<t)){
            at=zero_dest[t];
            for(i=0;i<n;i++)zero_pre[t][i]=SKIP_R16(at[i]);
        }else project_list(zero_all+zero_offset[t],n);
    }
}
static u32 dirty_inputs(void)
{
    u32 j,t,g,i,mask=ALL_TRACKS&~valid_mask;
    for(j=0;j<active_count;j++){
        t=active_ids[j];
        for(g=t==12u?1u:0u;g<GROUPS;g++)if(active_groups[t]&(1u<<g)){
            u32 v=source(t,g);
            if(source_shadow[t][g]!=v){source_shadow[t][g]=v;mask|=1u<<t;}
        }
        if(valid_mask&(1u<<t))for(i=0;i<used[t];i++)
            if(SKIP_R16(dest[t][i])!=pre[t][i]){mask|=1u<<t;break;}
    }
    return mask;
}
static u32 many(u32 mask)
{
    /* dirty_inputs() only publishes the 13 track bits. At least twelve
     * dirty tracks means at most one missing bit, including the full mask. */
    u32 missing=ALL_TRACKS&~mask;
    _Static_assert(PARTIAL_LIMIT==TRACKS-1u,"dense-mask shortcut contract");
    return !(missing&(missing-1u));
}
/* Stock 40097aac processes one four-entry source group. When exactly one of
 * the first five groups is active and no external zero group touches its
 * destinations, the other groups are independent zero-only projections. */
static u32 sparse_ok(u32 mask)
{return !(mask&ALL_TRACKS&~sparse_track_mask);}
#ifndef SKIP_HOST
static void native_group(u32 row,s32 value,u32 entries)
{((void (*)(u32,s32,u32))0x40097aacu)(row,value,entries);}
#else
extern void modulation_skip_host_group(u32,s32,u32);
static void native_group(u32 row,s32 value,u32 entries)
{modulation_skip_host_group(row,value,entries);}
#endif
void modulation_skip_sparse(u32 working)
{
    u32 t,g,bits,mask=modulation_skip_run_mask;(void)working;
    /* The decision admits this path only for sparse_track_mask members. */
    for(bits=mask&ALL_TRACKS;bits;bits&=bits-1u){
        t=31u-(u32)__builtin_clz(bits&-bits);
        g=31u-(u32)__builtin_clz((u32)active_groups[t]);
        native_group(TRACK(t),(s16)source(t,g),RAW(t)+16u*g);
    }
    project_zero(mask);
}
/* IRQ, at the stock body: full stock, cached replay, full stock with a check,
 * the guarded kernel, or a single native source group for dirty tracks. */
u32 modulation_skip_decide(u32 working)
{
    u32 dirty,changed=0,writes,written,periodic,scan=0,t;
    if(modulation_skip_mode!=VERIFY && modulation_skip_mode!=ON)return RUN_STOCK;
    if(working!=WORKING){valid_mask=0;return RUN_STOCK;}
    modulation_skip_blocks++;
    writes=modulation_skip_machine_writes;written=writes!=machine_seen;machine_seen=writes;
    periodic=!(++ticks_irq%CHECK_EVERY);
    if(written || !raw_ready || modulation_skip_mode==VERIFY || periodic){
        for(t=0;t<TRACKS;t++)if(modulation_skip_machine_dirty[t]){
            scan|=1u<<t;modulation_skip_machine_dirty[t]=0;
        }
        if(!raw_ready || modulation_skip_mode==VERIFY || periodic || !scan)scan=ALL_TRACKS;
        changed=refresh_machine(scan);
        if(changed && !written && modulation_skip_blocks>1u){
            modulation_skip_errors++;
            if(modulation_skip_mode==ON){modulation_skip_mode=OFF;valid_mask=0;return RUN_STOCK;}
        }
    }
    if(unsafe_mask || total_used>DEST_LIMIT){valid_mask=0;return RUN_STOCK;}
    if(!active_track_mask && valid_mask==ALL_TRACKS && modulation_skip_mode==ON && !periodic){
        if(++count%CHECK_EVERY){project_zero(ALL_TRACKS);modulation_skip_hits++;modulation_skip_skips++;return SKIPPED;}
        periodic=1;
    }
    dirty=dirty_inputs();
    if(!dirty)modulation_skip_hits++;
    if(modulation_skip_mode==ON && !periodic && !(++count%CHECK_EVERY))periodic=1;
    if(modulation_skip_mode==ON && !periodic && !dirty){
        replay(ALL_TRACKS);project_zero(ALL_TRACKS);modulation_skip_skips++;return SKIPPED;
    }
    if(modulation_skip_mode==ON && !periodic && !many(dirty)){
        modulation_skip_run_mask=dirty;
        prepare_partial(dirty);
        capture_mask=dirty;check_mask=0;
        modulation_skip_skips++;modulation_skip_partial_runs++;
        return sparse_ok(dirty)?RUN_SPARSE:RUN_FILTERED;
    }
    check_mask=valid_mask&~dirty;
    capture_pre(ALL_TRACKS);capture_zero(ALL_TRACKS);capture_mask=ALL_TRACKS;
    return RUN_AND_CHECK;
}
void modulation_skip_after(u32 working)
{
    u32 j,t,i,v,bad=0,mask=capture_mask&(active_track_mask|zero_track_mask);(void)working;
    for(j=0;j<active_count;j++){t=active_ids[j];if(mask&(1u<<t))for(i=0;i<used[t];i++){
        v=SKIP_R16(dest[t][i]);if((check_mask&(1u<<t)) && v!=post[t][i])bad=1;
        post[t][i]=(u16)v;
    }}
    for(j=0;j<zero_count;j++){t=zero_ids[j];if(mask&(1u<<t))for(i=0;i<zero_used[t];i++)
        if(SKIP_R16(zero_dest[t][i])!=zero_result(zero_pre[t][i]))bad=1;}
    valid_mask|=capture_mask;
    if(bad){modulation_skip_errors++;if(modulation_skip_mode==ON)modulation_skip_mode=OFF;}
}

/* ---- trampoline: entered at 40097b04 with the stock frame (11 registers in
 * 60 bytes, return at 60, argument at 64). The stock path re-executes the two
 * moved instructions; RUN_AND_CHECK calls the stock body as a subroutine with a
 * second identical frame, so control comes back here before the epilogue. */
#ifndef SKIP_HOST
__asm__(
"        .text\n"
"        .balign 16\n"
"| Leaf entry for the validated all-zero case. Every exceptional condition\n"
"| rejoins the unchanged general decision before modifying any state.\n"
"        .globl modulation_skip_dispatch\n"
"modulation_skip_dispatch:\n"
"        movea.l modulation_skip_decider,%a0\n"
"        jmp (%a0)\n"
"        .globl modulation_skip_zero_decide\n"
"modulation_skip_zero_decide:\n"
"        move.l modulation_skip_mode,%d0\n"
"        cmpi.l #2,%d0\n"
"        beq.s 0f\n"
"        subq.l #1,%d0\n"
"        beq.w 9f\n"
"        moveq #0,%d0\n"
"        rts\n"
"0:\n"
"        move.l total_used,%d0\n"
"        cmpi.l #100,%d0\n"
"        bhi.w 8f\n"
"        tst.l unsafe_mask\n"
"        bne.w 8f\n"
"        tst.l active_track_mask\n"
"        bne.w 9f\n"
"        move.l 4(%sp),%d0\n"
"        cmpi.l #0x80006348,%d0\n"
"        bne.w 9f\n"
"        tst.l raw_ready\n"
"        beq.w 9f\n"
"        move.l valid_mask,%d0\n"
"        cmpi.l #0x1fff,%d0\n"
"        bne.w 9f\n"
"        move.l modulation_skip_machine_writes,%d0\n"
"        cmp.l machine_seen,%d0\n"
"        bne.w 9f\n"
"        move.l ticks_irq,%d0\n"
"        addq.l #1,%d0\n"
"        move.l %d0,%d1\n"
"        andi.l #63,%d1\n"
"        beq.w 9f\n"
"        move.l count,%d1\n"
"        addq.l #1,%d1\n"
"        movea.l %d1,%a0\n"
"        andi.l #63,%d1\n"
"        beq.w 9f\n"
"        move.l %d0,ticks_irq\n"
"        move.l %a0,count\n"
"        addq.l #1,modulation_skip_blocks\n"
"        move.l zero_all_count,-(%sp)\n"
"        pea zero_all\n"
"        jsr modulation_skip_project_all\n"
"        addq.l #8,%sp\n"
"        addq.l #1,modulation_skip_hits\n"
"        addq.l #1,modulation_skip_skips\n"
"        moveq #1,%d0\n"
"        rts\n"
"| Dense/unsafe matrices still refresh/check on the exact original blocks.\n"
"| A quiet intervening block only invalidates the cache and runs stock.\n"
"        .globl modulation_skip_stock_decide\n"
"8:\n"
"modulation_skip_stock_decide:\n"
"        move.l modulation_skip_mode,%d0\n"
"        subq.l #2,%d0\n"
"        bne.w 9f\n"
"        move.l 4(%sp),%d0\n"
"        cmpi.l #0x80006348,%d0\n"
"        bne.w 9f\n"
"        tst.l raw_ready\n"
"        beq.w 9f\n"
"        move.l modulation_skip_machine_writes,%d0\n"
"        cmp.l machine_seen,%d0\n"
"        bne.w 9f\n"
"        move.l ticks_irq,%d0\n"
"        addq.l #1,%d0\n"
"        move.l %d0,%d1\n"
"        andi.l #63,%d1\n"
"        beq.w 9f\n"
"        move.l %d0,ticks_irq\n"
"        addq.l #1,modulation_skip_blocks\n"
"        clr.l valid_mask\n"
"        moveq #0,%d0\n"
"        rts\n"
"9:      jmp modulation_skip_decide\n"
"        .globl modulation_skip_entry\n"
"modulation_skip_entry:\n"
"        move.l 64(%sp),-(%sp)\n"
"        movea.l modulation_skip_decider,%a0\n"
"        jsr (%a0)\n"
"        addq.l #4,%sp\n"
"        subq.l #1,%d0\n"
"        beq.s 3f\n"
"        bcc.s 1f\n"
"        moveq #0,%d0\n"
"        movea.l 64(%sp),%a2\n"
"        move.l %a2,%d3\n"
"        jmp 0x40097b0a\n"
"1:      cmpi.l #3,%d0\n"
"        beq.s 5f\n"
"        cmpi.l #2,%d0\n"
"        beq.s 4f\n"
"        move.l 64(%sp),-(%sp)\n"
"        jsr 2f\n"
"        jsr modulation_skip_after\n"
"        addq.l #4,%sp\n"
"        bra.s 3f\n"
"5:      move.l 64(%sp),-(%sp)\n"
"        jsr modulation_skip_sparse\n"
"        jsr modulation_skip_after\n"
"        addq.l #4,%sp\n"
"        bra.s 3f\n"
"4:      move.l 64(%sp),-(%sp)\n"
"        jsr modulation_skip_filtered\n"
"        jsr modulation_skip_after\n"
"        addq.l #4,%sp\n"
"3:      movem.l (%sp),%d2-%d7/%a2-%a6\n"
"        lea 60(%sp),%sp\n"
"        rts\n"
"2:      lea -60(%sp),%sp\n"
"        movem.l %d2-%d7/%a2-%a6,(%sp)\n"
"        movea.l 64(%sp),%a2\n"
"        move.l %a2,%d3\n"
"        jmp 0x40097b0a\n"
"| 400a30c4 (source, track): the stock prologue's two instructions moved here;\n"
"| one write counted before and one after, on the caller's stack, without\n"
"| touching a register (it runs in the task and inside the IRQ).\n"
"        .globl modulation_skip_machine_entry\n"
"modulation_skip_machine_entry:\n"
"        addq.l #1,modulation_skip_machine_writes\n"
"        lea -8(%sp),%sp\n"
"        movem.l %d0/%a0,(%sp)\n"
"        move.l 16(%sp),%d0\n"
"        cmpi.l #13,%d0\n"
"        bhs.s 6f\n"
"        lea modulation_skip_machine_dirty,%a0\n"
"        lea 0(%a0,%d0.l),%a0\n"
"        move.b #1,(%a0)\n"
"6:      movem.l (%sp),%d0/%a0\n"
"        addq.l #8,%sp\n"
"        move.l 8(%sp),-(%sp)\n"
"        move.l 8(%sp),-(%sp)\n"
"        jsr 4f\n"
"        addq.l #8,%sp\n"
"        addq.l #1,modulation_skip_machine_writes\n"
"        lea -8(%sp),%sp\n"
"        movem.l %d0/%a0,(%sp)\n"
"        move.l 16(%sp),%d0\n"
"        cmpi.l #13,%d0\n"
"        bhs.s 7f\n"
"        lea modulation_skip_machine_dirty,%a0\n"
"        lea 0(%a0,%d0.l),%a0\n"
"        move.b #1,(%a0)\n"
"7:      movem.l (%sp),%d0/%a0\n"
"        addq.l #8,%sp\n"
"        rts\n"
"4:      lea -12(%sp),%sp\n"
"        movem.l %d2/%a2-%a3,(%sp)\n"
"        jmp 0x400a30cc\n"
"| IRQ exclusion and one code halfword write with the core's cache sequence.\n"
"        .globl modulation_skip_lock,modulation_skip_unlock,modulation_skip_code16,modulation_skip_sync\n"
"modulation_skip_lock:\n"
"        moveq #0,%d0\n"
"        move.w %sr,%d0\n"
"        move.w #0x2700,%sr\n"
"        rts\n"
"modulation_skip_unlock:\n"
"        move.l 4(%sp),%d0\n"
"        move.w %d0,%sr\n"
"        rts\n"
"modulation_skip_code16:\n"
"        movea.l 4(%sp),%a0\n"
"        move.l 8(%sp),%d0\n"
"        move.w %d0,(%a0)\n"
"        move.l %a0,%d0\n"
"        andi.l #-16,%d0\n"
"        movea.l %d0,%a0\n"
"        cpushl %dc,(%a0)\n"
"        rts\n"
"modulation_skip_sync:\n"
"        nop\n"
"        move.l #0xa40ce100,%d0\n"
"        movec %d0,%cacr\n"
"        rts\n");
#endif

/* ---- install and UI */
static u32 be16(u32 at){return (u32)SKIP_MEM(at)[0]<<8|SKIP_MEM(at)[1];}
/* 0 installed; 1 the modulation site, 2 the machine-data writer differs. */
static u32 install(void)
{
    u32 sr,w=(u32)modulation_skip_entry,m=(u32)modulation_skip_machine_entry;
    if(be16(SITE)!=0x246fu || be16(SITE+2)!=0x0040u || be16(SITE+4)!=0x260au)return 1;
    if(be16(WRITER)!=0x4fefu || be16(WRITER+2)!=0xfff4u || be16(WRITER+4)!=0x48d7u ||
       be16(WRITER+6)!=0x0c04u)return 2;
    sr=modulation_skip_lock();
    modulation_skip_code16(WRITER+2,m>>16);modulation_skip_code16(WRITER+4,m&0xffffu);
    modulation_skip_code16(WRITER,0x4ef9u);
    modulation_skip_code16(SITE+2,w>>16);modulation_skip_code16(SITE+4,w&0xffffu);
    modulation_skip_code16(SITE,0x4ef9u);
    modulation_skip_sync();modulation_skip_unlock(sr);
    return 0;
}
static u32 ticks,seen_blocks,seen_hits,seen_skips,refused;
static char line[24]="MSKIP --- H-- S-- E000";
/* ON shows nothing unless something went wrong; VERIFY (diagnostic) always. */
static u32 shown(void)
{return MODULATION_SKIP_VERIFY || modulation_skip_errors || modulation_skip_mode==OFF || modulation_skip_mode==REFUSED;}
static void digits(char *at,u32 value,u32 k)
{while(k--){at[k]=(char)('0'+value%10u);value/=10u;}}
static void percent(char *at,u32 part,u32 whole)
{u32 v;if(!whole){at[0]=at[1]='-';return;}v=100u*part/whole;if(v>99u){at[0]=at[1]='!';}else digits(at,v,2);}
static void update(void)
{
    static const char tag[4][4]={"OFF","VER","ON ","NO1"};
    u32 b=modulation_skip_blocks,h=modulation_skip_hits,s=modulation_skip_skips,e=modulation_skip_errors,i;
    for(i=0;i<3;i++)line[6+i]=tag[modulation_skip_mode][i];
    if(modulation_skip_mode==REFUSED)line[8]=(char)('0'+refused);
    percent(line+11,h-seen_hits,b-seen_blocks);percent(line+15,s-seen_skips,b-seen_blocks);
    digits(line+19,e>999u?999u:e,3);
    seen_blocks=b;seen_hits=h;seen_skips=s;
}
static void skip_tick(void *controller)
{ticks++;if(ticks%30u==0){update();if(shown())((u8 *)controller)[0x20]=1;}}
static void skip_draw(void *bitmap,void *controller)
{
    (void)controller;
    if(ticks/120u%3u!=2u || !shown())return;
    SKIP_FILL(bitmap,18,54,127,63,0);
    SKIP_TEXT(bitmap,(void *)0x443da230u,20,55,0,"%s",line);
}
static int initialize(const struct st_smz_api *api)
{
    if(!api || !api->version || !api->ui || api->ui->version<7)return 1;
    refused=install();
    modulation_skip_mode=refused?REFUSED:MODULATION_SKIP_VERIFY?VERIFY:ON;
    update();
    return 0;                                   /* a refusal leaves stock untouched */
}
static const struct st_smz_subscription subscriptions[]={
    {ST_EV_TICK,(void (*)(void))skip_tick},
    {ST_EV_DRAW,(void (*)(void))skip_draw}};
const struct st_smz_module modulation_skip_module={"MODULATION SKIP",initialize,subscriptions,2};
