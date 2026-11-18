/* SPDX-License-Identifier: GPL-2.0-or-later
 * playhead-st: the order the active track plays its steps in. TRK + UP / DOWN
 * the next / the previous mode of the cycle FORWARD (stock), REVERSE,
 * PENDULUM, RANDOM, WALK, ANCHOR, STUTTER, FOLD, WALK+. TRK + LEFT /
 * RIGHT stay native. Modes belong to the pattern: each pattern's kit keeps
 * its tracks' modes, saved and loaded with it (persist.c).
 *
 * The keys: the native KeyEvent IDs TRK 2 (MainScreen's key switch 40033f0a:
 * case 2 pushes the TrackSelectionView), UP 14, DOWN 15. With TRK held
 * (40102d7c), the module takes UP's or DOWN's press, its repeats and its
 * release; nothing native sees them. The active track is the application's
 * (4016e804) track index at +30h (4001ff74, as 40019036 reads it).
 *
 * Backwards, a track plays length - 1 - step of the stock forwards count, so
 * it starts on its last step and stays in phase with the pattern. Pendulum,
 * random and walk choose their next step once per step (seq-direction.c).
 *
 * The cost: nothing in the sequencer while every track goes forwards (the
 * stock code is untouched); while one track is in another mode, a few dozen
 * instructions per track and tick in the sequencer interrupt
 * (seq-direction.s; README.md has the numbers). The key handler runs once
 * per key event in the UI task and returns at its first compare for every
 * key but UP and DOWN.
 *
 * The header: while the active track is not FORWARD its mode's 7x7 icon is
 * drawn two pixels left of the metronome, in the MainScreen's own layer (the
 * contextual DRAW right after the MainScreen view, 40100830), so the popups
 * and modals above it cover it. Its pixels are inverted (XOR) where they fall:
 * the header there is lit, so the icon comes out dark, with no box. The MainScreen's header
 * (40032b0e..40032b72) draws the tempo "%d.%d" right-aligned to x 125 in the
 * font 443da1f0 and the metronome at x 117 - the tempo's width, on rows
 * 55..62; the icon takes rows 56..62, as the tempo's digits, centred in
 * them, its right column two pixels left of the metronome. The width
 * is the firmware's: 4013273e(the tempo 402d7010, 120, 1, 1) gives the whole
 * BPM, and 400f92be the text's width, of "<BPM>.0" (every fraction digit is
 * as wide as 0 in the header's font). With every track FORWARD the handler
 * returns at its first test.
 */
#include <syntakt/modules.h>
#include "seq-direction.h"
#include "persist.h"
typedef unsigned int u32;typedef unsigned char u8;
#define KEY_TRK 2u
#define KEY_UP 14u
#define KEY_DOWN 15u
#define HELD ((u32 (*)(u32))0x40102f40u)
#define APP ((u8 *(*)(void))0x4016e9c8u)
#define TRACK_INDEX ((u32 (*)(void *))0x4001ff74u)
#define UI ((u8 *(*)(void))0x4016e044u)
#define MESSAGE ((void (*)(u32,const char *,...))0x400c347cu)   /* printf popup */
#define MAIN_SCREEN 0x401c849cu
#define FILL ((void (*)(void *,int,int,int,int,int))0x400f7fb6u)    /* x0,y0,x1,y1, colour 0 clear, 1 set,
                                                                      negative XOR (400f7ec4); y grows up */
#define TEMPO 0x402d7360u
#define TEMPO_SPLIT ((u32 (*)(u32,u32,u32,u32))0x40132902u)        /* the whole BPM */
#define TEXT_WIDTH ((int (*)(u32,int,const void *))0x400f9482u)    /* font, -1, a std::string */
#define HEADER_FONT 0x443db1f0u
static u32 taken;                       /* bit k: the press of key 14+k (UP, DOWN) was taken */
static const char *const names[PLAYHEAD_MODES]={"FORWARD","REVERSE","PENDULUM","RANDOM","WALK",
    "ANCHOR","STUTTER","FOLD","WALK+"};
static u32 word(const void *p,u32 at){return *(const u32 *)((const u8 *)p+at);}
/* The MainScreen when it is the highest opaque view (UI controller +40h,
 * node +1Ch), else 0. */
static u8 *main_screen(void)
{
    u8 *ui=UI(),*m,*n,*main;
    if(!ui)return 0;
    m=ui+0x40;n=*(u8 **)(m+0x1c);
    if(!n || n==m+0x14 || !(main=*(u8 **)(n+8)) || word(main,0)!=MAIN_SCREEN)return 0;
    return main;
}
/* The popup of the MainScreen's parameter view (as the page popups). */
static u32 popup_target(void)
{
    u8 *main=main_screen(),*view;u32 vt;
    if(!main || !(view=*(u8 **)(main+0xc0)))return 0;
    vt=word(view,0);
    if(vt<0x40000408u || vt>=0x40339600u || ((const u32 *)vt)[0x88/4]!=0x4003792cu)return 0;
    return word(view,0x6c);
}
static void show(u32 t,const char *what)
{
    u32 popup=popup_target();
    if(!popup)return;
    if(t<12u)MESSAGE(popup,"TRK %d %s",(int)t+1,what);
    else MESSAGE(popup,"FX TRK %s",what);
}
static int playhead_key(void *brain,void *event)
{
    u32 key=word(event,12),flags=word(event,16),bit,t,k,mode,refused;u8 *app;
    (void)brain;
    if(key!=KEY_UP && key!=KEY_DOWN)return 0;
    bit=1u<<(key-KEY_UP);
    if(!(flags&1u)){                    /* release: the one of a press taken */
        if(!(taken&bit))return 0;
        taken&=~bit;return 1;
    }
    if(flags&8u)return (taken&bit)!=0;  /* repeat */
    if(!HELD(KEY_TRK))return 0;
    taken|=bit;
    if(!(app=APP()) || (t=TRACK_INDEX(app+0x30))>=PLAYHEAD_TRACKS ||
       (k=playhead_active_kit())>=PLAYHEAD_KITS)return 1;
    playhead_enter();
    mode=playhead_modes[k][t];
    if(key==KEY_UP)mode=mode+1u<PLAYHEAD_MODES?mode+1u:0u;
    else mode=mode?mode-1u:PLAYHEAD_MODES-1u;
    refused=playhead_seq_set(k,t,mode);
    playhead_leave();
    if(!refused)playhead_persist_changed(k);   /* as a native kit edit: kept over a power cycle */
    if(refused)show(t,"MODES N/A");
    else show(t,names[mode]);
    return 1;
}
/* The icons: one-pixel strokes, centred in the seven rows of the tempo's
 * digits; a row a halfword from the top, bit width-1 the left column. */
static const struct {u8 width;unsigned short rows[7];} icons[PLAYHEAD_MODES]={
    {0,{0}},
    {7,{0x000,0x010,0x020,0x07f,0x020,0x010,0x000}},   /* REVERSE: an arrow to the left */
    {9,{0x000,0x044,0x082,0x1ff,0x082,0x044,0x000}},   /* PENDULUM: an arrow both ways */
    {7,{0x03e,0x041,0x055,0x049,0x055,0x041,0x03e}},   /* RANDOM: a die's five */
    {7,{0x006,0x000,0x030,0x000,0x006,0x000,0x030}},   /* WALK: steps, left and right */
    {7,{0x008,0x01c,0x008,0x008,0x049,0x02a,0x01c}},   /* ANCHOR: ring, shank and flukes */
    {7,{0x000,0x054,0x056,0x057,0x056,0x054,0x000}},   /* STUTTER: repeated bars and play */
    {7,{0x041,0x022,0x014,0x008,0x014,0x022,0x041}},   /* FOLD: outside to centre and back */
    {9,{0x0c0,0x004,0x032,0x00f,0x0c2,0x004,0x030}}};  /* WALK+: footsteps and right arrow */
/* A COW std::string the firmware can read (as syntakt/trig-pages.h's titles):
 * length, capacity, a count of -1 (leaked: never written), then the text. */
static struct {u32 length,capacity;int count;char text[8];} tempo_text={0,7,-1,{0}};
static const char *const tempo_string=tempo_text.text;
static int tempo_width(void)
{
    u32 bpm=TEMPO_SPLIT(*(const volatile u32 *)TEMPO,120,1,1),n=0,i;char digits[4];
    const u32 *font=*(const u32 *const *)(HEADER_FONT+4);
    if(bpm>999u)bpm=999u;
    do{digits[n++]=(char)('0'+bpm%10u);bpm/=10u;}while(bpm);
    for(i=0;i<n;i++)tempo_text.text[i]=digits[n-1u-i];
    tempo_text.text[n]='.';tempo_text.text[n+1]='0';tempo_text.text[n+2]=0;tempo_text.length=n+2;
    if(!font)return 0;
    if(!font[2])return (int)((font[0]+1u)*(n+2u)-1u);   /* fixed width, as 400f930e */
    return TEXT_WIDTH((u32)font,-1,&tempo_string);
}
/* DRAW | VIEW: after each complete native view, below the views above it. */
static void playhead_draw(void *view,void *bitmap,u32 unused)
{
    u32 t,k,mode,r,c,bits,w;int x;u8 *app;
    (void)unused;
    if(!playhead_seq_installed)return;   /* every kit's tracks FORWARD */
    if(!view || *(const u32 *)view!=MAIN_SCREEN || view!=main_screen())return;
    if(!(app=APP()) || (t=TRACK_INDEX(app+0x30))>=PLAYHEAD_TRACKS ||
       (k=playhead_active_kit())>=PLAYHEAD_KITS || !(mode=playhead_modes[k][t]) || mode>=PLAYHEAD_MODES)return;
    w=icons[mode].width;
    x=115-tempo_width()-(int)w;          /* the metronome is at 117 - the tempo's width */
    if(x<20)return;
    for(r=0;r<7u;r++)for(bits=icons[mode].rows[r],c=0;c<w;c++)
        if(bits&(1u<<(w-1u-c)))FILL(bitmap,x+(int)c,62-(int)r,x+(int)c,62-(int)r,-1);
}
/* Nothing is patched here. With host API 10 (core 0.12.22) the modes are
 * saved with the kits (persist.c); without it they last until a restart. */
static int initialize(const struct st_smz_api *api)
{
    if(!api || !api->version)return 1;
    playhead_persist_init(api->version>=3 && api->ui && api->ui->version>=7 ? api->ui->host:0);
    return 0;
}
static const struct st_smz_subscription subscriptions[]={
    {ST_EV_KEY,(void (*)(void))playhead_key},
    {ST_EV_DRAW|ST_EV_VIEW,(void (*)(void))playhead_draw},
    {ST_EV_NATIVE,(void (*)(void))playhead_native}};
const struct st_smz_module playhead_module={"PLAYHEAD",initialize,subscriptions,3};
