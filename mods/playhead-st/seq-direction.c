/* SPDX-License-Identifier: GPL-2.0-or-later
 * Sequencer step order per track: the modes of every pattern's kit, and the
 * five step sites, installed only while some kit has a track that is not
 * FORWARD (seq-direction.s says what each replaces and why the step is
 * changed where it is used).
 *
 * Modes belong to a pattern: they are kept per live kit (each pattern has its
 * own, 41ad5134 + k * 1618h, k < 128), and the sequencer's pattern pointers
 * name their kit: 43bd7430 (playing) with 43bd743c, 43bd7438 (next) with
 * 43bd7444. persist.c saves and loads them with the kit.
 *
 * The sites go in and out together from a task with interrupts masked.
 * The sequencer runs in an interrupt, so while the UI task runs it is never
 * in the middle of a site: it sees all the stock instructions or all the
 * jumps. Before the first write each site must have its stock words, and the
 * code around the sites (the registers the stubs rely on) its stock CRC;
 * otherwise nothing is ever written.
 */
#include "seq-direction.h"
typedef unsigned int u32;typedef unsigned short u16;typedef unsigned char u8;
extern void playhead_trig_a(void),playhead_trig_b(void),playhead_trig_c(void),playhead_ui_copy(void),
    playhead_step_shown(void);
extern u32 playhead_seq_lock(void);
extern void playhead_seq_unlock(u32),playhead_seq_code16(u32,u32),playhead_seq_sync(void);
volatile u8 playhead_modes[PLAYHEAD_KITS][16];
u32 playhead_seq_state,playhead_seq_installed;
#define SITES 5u
static const struct {u32 at;u16 stock[3];} sites[SITES]={
    {0x4008cf78u,{0x2f05u,0x42a7u,0x7396u}},    /* + 2f01 */
    {0x4008cf8cu,{0x2f05u,0x42a7u,0x7396u}},    /* + 2f01 */
    {0x4008c27cu,{0x2f00u,0x2f08u,0x2f39u}},    /* + 43bd 7430 */
    {0x4008d230u,{0x139eu,0x0800u,0x2247u}},    /* + 5280 18dd */
    {0x4008b7beu,{0x41f9u,0x43bdu,0x8414u}}};   /* the LEDs' step */
static const struct {u32 from,to,crc;} contexts[4]={
    {0x4008cf72u,0x4008cfa4u,0xcc47c601u},     /* both trig pushes and calls */
    {0x4008c26au,0x4008c28eu,0xb810e12au},     /* the mid-pattern trig */
    {0x4008d1f8u,0x4008d24au,0x1be04482u},     /* the UI copy loop */
    {0x4008b7acu,0x4008b7cau,0xc43b9480u}};    /* the step the LEDs show */
static u32 be16(u32 at)
{return (u32)((const volatile u8 *)at)[0]<<8|((const volatile u8 *)at)[1];}
static u32 crc32(u32 at,u32 end)
{
    u32 c=0xffffffffu,j;
    for(;at<end;at++){
        c^=*(const volatile u8 *)at;
        for(j=0;j<8;j++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);
    }
    return ~c;
}
static u32 stock_sites(void)
{
    u32 i,k;
    for(i=0;i<SITES;i++)for(k=0;k<3;k++)if(be16(sites[i].at+2*k)!=sites[i].stock[k])return 0;
    return 1;
}
static u32 stock_code(void)
{
    u32 i;
    if(!stock_sites())return 0;
    for(i=0;i<4;i++)if(crc32(contexts[i].from,contexts[i].to)!=contexts[i].crc)return 0;
    return 1;
}
static void patch(u32 on)
{
    static void (*const stubs[SITES])(void)={playhead_trig_a,playhead_trig_b,playhead_trig_c,playhead_ui_copy,
        playhead_step_shown};
    u32 sr=playhead_seq_lock(),i,w,at;
    for(i=0;i<SITES;i++){
        at=sites[i].at;w=(u32)stubs[i];
        if(on){
            playhead_seq_code16(at+2,w>>16);playhead_seq_code16(at+4,w&0xffffu);
            playhead_seq_code16(at,0x4ef9u);     /* jmp abs.l, written last */
        }else{
            playhead_seq_code16(at,sites[i].stock[0]);playhead_seq_code16(at+2,sites[i].stock[1]);
            playhead_seq_code16(at+4,sites[i].stock[2]);
        }
    }
    playhead_seq_sync();playhead_seq_unlock(sr);
    playhead_seq_installed=on;
}
/* ---- the step a track plays: playhead_step, from the stubs (sequencer IRQ;
 * the LEDs' accessor in the UI task only peeks).
 *
 * j is the stock forwards count (0..length-1). REVERSE is its mirror. The
 * other modes choose the next step themselves, once per step: the first stub
 * that asks for a count the track has not seen (the tick's UI copy right after
 * the stock advance, or the trig at the step's tick 0) computes it; the two
 * last counts and their steps are kept, so every later ask for them (the trig,
 * the LEDs, live recording) gets the same step. A count that is not the next
 * one (a start, a resync, a pattern change) starts the mode again from it. */
/* 16-byte stride and these offsets are shared with the assembly fast path.
 * A cache miss still uses this canonical selector. IRQ updates finish before
 * the UI task resumes; setters invalidate under the existing IRQ exclusion. */
struct playhead_track {u8 count,step,last_count,last_step;signed char dir;
    u8 valid,kit,mode,length,phase,next_count,pad;u32 kit_address;};
_Static_assert(sizeof(struct playhead_track)==16,"assembly track stride");
_Static_assert(__builtin_offsetof(struct playhead_track,valid)==5 &&
    __builtin_offsetof(struct playhead_track,length)==8 &&
    __builtin_offsetof(struct playhead_track,next_count)==10 &&
    __builtin_offsetof(struct playhead_track,kit_address)==12,"assembly track offsets");
struct playhead_track playhead_tracks[16];
#define tracks playhead_tracks
static u32 seed=0x2545f491u;
static u32 random32(void)
{u32 x=seed;x^=x<<13;x^=x>>17;x^=x<<5;return seed=x;}
static u32 below(u32 n){return ((random32()>>16)*n)>>16;}   /* 0..n-1 */
static u32 track_length(const u8 *pattern,u32 t)
{
    int n=pattern[0x21379]?*(const short *)(pattern+0x499*t+969):*(const short *)(pattern+0x21374);
    return n>0?(u32)n:1u;
}
static u32 start(u32 mode,struct playhead_track *s,u32 j,u32 n)
{
    seed^=*(const volatile u32 *)0x43bcc3a4u;if(!seed)seed=1;   /* the native clock */
    s->dir=j+1u>=n?-1:1;
    s->phase=mode==PLAYHEAD_ANCHOR?1:0;
    if(mode==PLAYHEAD_ANCHOR || mode==PLAYHEAD_FOLD){s->dir=1;return 0;}
    return mode==PLAYHEAD_RANDOM?below(n):j;
}
static u32 next(u32 mode,struct playhead_track *s,u32 n)
{
    int q=s->step;
    if(n<=1u)return 0;
    if(q>(int)n-1)q=(int)n-1;                   /* a length made shorter */
    if(mode==PLAYHEAD_RANDOM)return below(n);
    if(mode==PLAYHEAD_WALK){
        if(random32()&0x10000u)return (u32)q+1u<n?(u32)q+1u:0u;
        return q?(u32)q-1u:n-1u;
    }
    if(mode==PLAYHEAD_WALK_PLUS){
        u32 r=random32()>>16;
        /* Exactly the same buckets as (r*5)>>16, without a multiply. */
        if(r<39322u)return (u32)q+1u<n?(u32)q+1u:0u;
        return r<52429u?(u32)q:q?(u32)q-1u:n-1u;
    }
    if(mode==PLAYHEAD_ANCHOR){
        if(q)return 0;
        q=s->phase;
        s->phase=(u8)((u32)q+1u<n?(u32)q+1u:1u);
        return (u32)q;
    }
    if(mode==PLAYHEAD_STUTTER){
        /* Every fourth visited step is played twice on the native grid. */
        s->phase=(u8)(s->phase<4u?s->phase+1u:0u);
        if(s->phase==4u)return (u32)q;
        return (u32)q+1u<n?(u32)q+1u:0u;
    }
    if(mode==PLAYHEAD_FOLD){
        /* Bounce through ranks 0..n-1; map ranks to alternating ends. */
        int rank=(int)s->phase+s->dir;
        if(rank>=(int)n-1){rank=(int)n-1;s->dir=-1;}
        else if(rank<=0){rank=0;s->dir=1;}
        s->phase=(u8)rank;
        return rank&1?n-1u-(u32)rank/2u:(u32)rank/2u;
    }
    q+=s->dir;                                  /* PLAYHEAD_PENDULUM: no end twice */
    if(q>=(int)n-1){q=(int)n-1;s->dir=-1;}
    else if(q<=0){q=0;s->dir=1;}
    return (u32)q;
}
u32 playhead_kit_index(u32 kit)
{
    if(kit<PLAYHEAD_KIT_BASE || kit>=PLAYHEAD_KIT_BASE+PLAYHEAD_KITS*PLAYHEAD_KIT_BYTES ||
       (kit-PLAYHEAD_KIT_BASE)%PLAYHEAD_KIT_BYTES)return PLAYHEAD_KITS;
    return (kit-PLAYHEAD_KIT_BASE)/PLAYHEAD_KIT_BYTES;
}
u32 playhead_active_kit(void)
{return playhead_kit_index(*(const volatile u32 *)0x43bd843cu);}
/* The kit of a pattern the sequencer holds: playing or next. */
static u32 pattern_kit(const u8 *pattern)
{
    if(pattern==*(const u8 *const volatile *)0x43bd8430u)return playhead_kit_index(*(const volatile u32 *)0x43bd843cu);
    if(pattern==*(const u8 *const volatile *)0x43bd8438u)return playhead_kit_index(*(const volatile u32 *)0x43bd8444u);
    return PLAYHEAD_KITS;
}
/* Already-resolved cache: called once for a new count, directly from the
 * register-ABI fast path too. No kit division or metadata revalidation. */
u32 playhead_advance(struct playhead_track *s,u32 j)
{
    u32 n=s->length,p=s->valid && j==s->next_count?next(s->mode,s,n):start(s->mode,s,j,n);
    s->last_count=s->valid?s->count:(u8)j;s->last_step=s->valid?s->step:(u8)p;
    s->valid=0;s->count=(u8)j;s->step=(u8)p;
    s->next_count=(u8)(j+1u<n?j+1u:0u);s->valid=1;
    return p;
}
u32 playhead_step(u32 t,u32 j,const u8 *pattern,u32 compute)
{
    u32 k,mode,n,p;struct playhead_track *s;
    if(t>=PLAYHEAD_TRACKS || !pattern || (k=pattern_kit(pattern))>=PLAYHEAD_KITS)return j;
    mode=playhead_modes[k][t];
    n=track_length(pattern,t);
    if(j>=n)return j;
    s=&tracks[t];
    if(mode<=PLAYHEAD_BACKWARDS){
        p=mode?n-1u-j:j;
        if(compute){
            s->valid=0;s->kit=(u8)k;s->mode=(u8)mode;s->length=(u8)n;
            s->kit_address=PLAYHEAD_KIT_BASE+k*PLAYHEAD_KIT_BYTES;s->valid=2;
        }
        return p;
    }
    if(s->valid && (s->kit!=k || s->mode!=mode || s->length!=n))s->valid=0;
    if(s->valid){
        if(j==s->count)return s->step;
        /* With length 2 the previous count is also the next count. Only
         * the sequencer may advance it; UI reads must retain the old step. */
        if(j==s->last_count && (!compute || j!=s->next_count))return s->last_step;
        if(!compute)return s->step;
    }else if(!compute)return j;
    s->kit=(u8)k;s->mode=(u8)mode;s->length=(u8)n;
    s->kit_address=PLAYHEAD_KIT_BASE+k*PLAYHEAD_KIT_BYTES;
    return playhead_advance(s,j);
}
/* The sites go in when some kit has a mode, and out when none has. */
static u32 sites_wanted(void)
{
    u32 k,t;
    for(k=0;k<PLAYHEAD_KITS;k++)for(t=0;t<PLAYHEAD_TRACKS;t++)if(playhead_modes[k][t])return 1;
    return 0;
}
static u32 update_sites(void)
{
    u32 want=sites_wanted();
    if(want && !playhead_seq_installed){
        if(playhead_seq_state==PLAYHEAD_UNCHECKED)
            playhead_seq_state=stock_code()?PLAYHEAD_READY:PLAYHEAD_REFUSED;
        if(playhead_seq_state!=PLAYHEAD_READY || !stock_sites())return 1;
        patch(1);
    }else if(!want && playhead_seq_installed)patch(0);
    return 0;
}
u32 playhead_seq_set(u32 k,u32 t,u32 mode)
{
    u32 sr;
    if(k>=PLAYHEAD_KITS || t>=PLAYHEAD_TRACKS || mode>=PLAYHEAD_MODES)return 0;
    sr=playhead_seq_lock();                      /* the IRQ sees the mode and its state together */
    tracks[t].valid=0;playhead_modes[k][t]=(u8)mode;
    playhead_seq_unlock(sr);
    if(update_sites()){                         /* refused: nothing plays, nothing shown */
        sr=playhead_seq_lock();playhead_modes[k][t]=0;playhead_seq_unlock(sr);
        return 1;
    }
    return 0;
}
void playhead_seq_load(u32 k,const u8 *modes)
{
    u32 t,sr;
    if(k>=PLAYHEAD_KITS)return;
    sr=playhead_seq_lock();
    for(t=0;t<PLAYHEAD_TRACKS;t++){
        playhead_modes[k][t]=modes && modes[t]<PLAYHEAD_MODES?modes[t]:0;
        if(tracks[t].kit==k)tracks[t].valid=0;
    }
    playhead_seq_unlock(sr);
    if(update_sites()){                         /* refused: every track forwards */
        sr=playhead_seq_lock();
        for(t=0;t<PLAYHEAD_TRACKS;t++)playhead_modes[k][t]=0;
        playhead_seq_unlock(sr);
    }
}
