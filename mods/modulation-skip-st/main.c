/* SPDX-License-Identifier: GPL-2.0-or-later
 * modulation-skip-st: skips the stock modulation 40097afc (the velocity/
 * modulation matrix of the MAIN audio IRQ, ~5.7 CPU points) in blocks where
 * it would write exactly what it wrote last time.
 *
 * The stock body writes, per track t, working[t] + 2*index for 4 entries of
 * 12 velocity groups and 13x5 source groups; entry = {amount, index} longs of
 * the track's unsmoothed machine data. Its output depends only on (memo rule,
 * validated in Unicorn): the velocity and
 * source tables (45 longs), the machine data it reads (13 x 24 longs) and the
 * values of its destination words on entry. When all of them equal the last
 * real run's, it would write the same: write those final values and return.
 *
 * Site: 40097b04, the stock body after its prologue, where core-st's
 * modulation adapter jumps when no module replaces it (POLY with groups does,
 * and that path is untouched). Its six bytes (two instructions) are replaced
 * by `jmp abs.l`; the stock path re-executes them. Install once at init,
 * nothing if the bytes differ (header `MSKIP NO1`).
 * Modes: VERIFY always runs stock and compares (errors counted, never skips);
 * ON skips, and every 64th would-skip block runs stock and compares; the first
 * difference turns it OFF (stock forever). Header page 3 of 3 (System info,
 * irq-timer-st, this): `MSKIP ON  H99 S98 E000` = mode, % of blocks whose
 * inputs matched, % skipped, differences found.
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
#define DESTS (12*4+13*5*4)
#define INPUTS (6+13+26+13*24)
#define MAP_BASE 0x80006000u
#define MAP_WORDS 0x1000u
#define CHECK_EVERY 64u
enum {OFF,VERIFY,ON,REFUSED};
enum {RUN_STOCK,SKIPPED,RUN_AND_CHECK};
extern void modulation_skip_entry(void),modulation_skip_machine_entry(void);
extern u32 modulation_skip_lock(void);
extern void modulation_skip_unlock(u32),modulation_skip_code16(u32,u32),modulation_skip_sync(void);
u32 modulation_skip_mode;                       /* OFF until installed */
static u32 shadow[INPUTS],valid,n,count,capture;
static u32 dest[DESTS];static u16 pre[DESTS],post[DESTS];
static u8 seen[MAP_WORDS],epoch;
volatile u32 modulation_skip_blocks,modulation_skip_hits,modulation_skip_skips,modulation_skip_errors;
/* The inputs in the order they are compared: velocity, sources, then each
 * track's machine data (track 12's first group is not read; comparing it only
 * costs a needless real run if it changes). */
static const u32 ranges[3][2]={{0x80004670u,0x80004688u},{0x800046b8u,0x800046ecu},{0x800046f8u,0x80004760u}};
/* Compare with the last run's inputs and store the current ones; 1 if equal. */
static u32 tables_equal(void)
{
    u32 *s=shadow,a,v,eq=1,i;
    for(i=0;i<3;i++)for(a=ranges[i][0];a<ranges[i][1];a+=4){v=SKIP_R32(a);if(*s!=v){eq=0;*s=v;}s++;}
    return eq;
}
static u32 machine_equal(void)
{
    u32 *s=shadow+6+13+26,a,v,t,eq=1;
    for(t=0;t<13;t++)for(a=RAW(t);a<RAW(t)+0x60u;a+=4){v=SKIP_R32(a);if(*s!=v){eq=0;*s=v;}s++;}
    return eq;
}
/* The machine data is the conversion's copy of the unsmoothed mirror, which
 * only 400a30c4 writes (sound publication, machine change). Its hook adds one
 * on entry and one on exit, so a block that saw neither may skip comparing the
 * 312 longs. Every CHECK_EVERY blocks they are compared anyway: a difference
 * without a write is an unknown writer, and turns ON off. */
volatile u32 modulation_skip_machine_writes;
static u32 machine_seen,ticks_irq;
static void add(u32 at)
{
    u32 i;
    if(at-MAP_BASE<2u*MAP_WORDS){
        if(seen[(at-MAP_BASE)>>1]==epoch)return;
        seen[(at-MAP_BASE)>>1]=epoch;
    }else for(i=0;i<n;i++)if(dest[i]==at)return;
    dest[n]=at;pre[n]=SKIP_R16(at);n++;
}
/* The destination words, as the stock body computes them (MVS.W of each
 * entry's low word), each once, with its value on entry. */
static void build(void)
{
    u32 t,g,k,src;
    if(!++epoch){for(k=0;k<MAP_WORDS;k++)seen[k]=0;epoch=1;}
    n=0;
    for(t=0;t<12;t++)for(k=0;k<4;k++)
        add(TRACK(t)+2u*(u32)(s32)(s16)SKIP_R32(RAW(t)+4u*k));
    for(t=0;t<13;t++)for(g=1;g<=5;g++)for(k=0;k<4;k++){
        src=RAW(t)+0x10u*g+4u*k;
        add(TRACK(t)+2u*(u32)(s32)(s16)SKIP_R32(src));
    }
}
static u32 entries_equal(void)
{u32 i;for(i=0;i<n;i++)if(SKIP_R16(dest[i])!=pre[i])return 0;return 1;}
/* IRQ, at the stock body: RUN_STOCK, SKIPPED (outputs written) or RUN_AND_CHECK
 * (run stock, then modulation_skip_after). */
u32 modulation_skip_decide(u32 working)
{
    u32 i,same,writes,written;
    if(modulation_skip_mode!=VERIFY && modulation_skip_mode!=ON)return RUN_STOCK;
    if(working!=WORKING){valid=0;return RUN_STOCK;}
    modulation_skip_blocks++;
    writes=modulation_skip_machine_writes;written=writes!=machine_seen;machine_seen=writes;
    same=tables_equal();
    if(written || modulation_skip_mode==VERIFY || !(++ticks_irq%CHECK_EVERY)){
        if(!machine_equal()){
            same=0;
            if(!written && valid){                   /* changed with no write seen */
                modulation_skip_errors++;
                if(modulation_skip_mode==ON){modulation_skip_mode=OFF;valid=0;return RUN_STOCK;}
            }
        }
    }
    if(same && valid && entries_equal()){
        modulation_skip_hits++;
        if(modulation_skip_mode==ON && ++count%CHECK_EVERY){
            for(i=0;i<n;i++)SKIP_W16(dest[i],post[i]);
            modulation_skip_skips++;return SKIPPED;
        }
        capture=0;return RUN_AND_CHECK;
    }
    machine_equal();                            /* the run's machine data, for later checks */
    build();valid=0;capture=1;return RUN_AND_CHECK;
}
void modulation_skip_after(u32 working)
{
    u32 i,v,bad=0;(void)working;
    for(i=0;i<n;i++){v=SKIP_R16(dest[i]);if(!capture && v!=post[i])bad=1;post[i]=(u16)v;}
    valid=1;
    if(bad){modulation_skip_errors++;if(modulation_skip_mode==ON)modulation_skip_mode=OFF;}
}

/* ---- trampoline: entered at 40097b04 with the stock frame (11 registers in
 * 60 bytes, return at 60, argument at 64). The stock path re-executes the two
 * moved instructions; RUN_AND_CHECK calls the stock body as a subroutine with a
 * second identical frame, so control comes back here before the epilogue. */
#ifndef SKIP_HOST
__asm__(
"        .text\n"
"        .globl modulation_skip_entry\n"
"modulation_skip_entry:\n"
"        move.l 64(%sp),-(%sp)\n"
"        jsr modulation_skip_decide\n"
"        addq.l #4,%sp\n"
"        tst.l %d0\n"
"        bne.s 1f\n"
"        movea.l 64(%sp),%a2\n"
"        move.l %a2,%d3\n"
"        jmp 0x40097b0a\n"
"1:      subq.l #1,%d0\n"
"        beq.s 3f\n"
"        move.l 64(%sp),-(%sp)\n"
"        jsr 2f\n"
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
"        move.l 8(%sp),-(%sp)\n"
"        move.l 8(%sp),-(%sp)\n"
"        jsr 4f\n"
"        addq.l #8,%sp\n"
"        addq.l #1,modulation_skip_machine_writes\n"
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
