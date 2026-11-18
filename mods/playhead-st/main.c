/* SPDX-License-Identifier: GPL-2.0-or-later
 * playhead-st: the order the active track plays its steps in. TRK + UP / DOWN
 * opens a centered selector for FORWARD (stock), REVERSE, PENDULUM,
 * RANDOM, WALK, ANCHOR, STUTTER, FOLD, WALK+. TRK + LEFT /
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
 * (seq-direction.s; README.md has the numbers). The selector runs only in
 * the UI task, with cached drawing. While closed its extra DRAW and ENC
 * callbacks return at their first state test.
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
#define KEY_YES 12u
#define KEY_NO 13u
#define KEY_UP 14u
#define KEY_DOWN 15u
#define HELD ((u32 (*)(u32))0x40102f40u)
#define APP ((u8 *(*)(void))0x4016e9c8u)
#define TRACK_INDEX ((u32 (*)(void *))0x4001ff74u)
#define UI ((u8 *(*)(void))0x4016e044u)
#define MAIN_SCREEN 0x401c849cu
#define FILL ((void (*)(void *,int,int,int,int,int))0x400f7fb6u)    /* x0,y0,x1,y1, colour 0 clear, 1 set,
                                                                      negative XOR (400f7ec4); y grows up */
#define TEMPO 0x402d7360u
#define TEMPO_SPLIT ((u32 (*)(u32,u32,u32,u32))0x40132902u)        /* the whole BPM */
#define TEXT_WIDTH ((int (*)(u32,int,const void *))0x400f9482u)    /* font, -1, a std::string */
#define HEADER_FONT 0x443db1f0u
#define BITMAP_CTOR ((void (*)(void *,u32,u32,const void *,const void *))0x400f72beu)
#define BLIT ((void (*)(void *,const void *,int,int,u32))0x400f8f70u)
static u32 taken[4];                    /* releases of presses consumed by this module */
static struct {u32 open,kit,track,cursor,opening_key,error,dirty;u8 *view;} playhead_selector;
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
static u32 selector_context(void)
{
    u8 *app;
    return main_screen()==playhead_selector.view && playhead_active_kit()==playhead_selector.kit &&
        (app=APP()) && TRACK_INDEX(app+0x30)==playhead_selector.track;
}
static int playhead_key(void *brain,void *event)
{
    u32 key=word(event,12),flags=word(event,16),bit,t,k,refused;u8 *app,*ui;
    (void)brain;
    if(key>=128u)return playhead_selector.open!=0;
    bit=1u<<(key&31u);
    if(!(flags&1u)){
        if(key==playhead_selector.opening_key)playhead_selector.opening_key=0;
        if(!(taken[key>>5]&bit))return 0;
        taken[key>>5]&=~bit;return 1;
    }
    if(flags&8u && !(taken[key>>5]&bit))return 0;
    if(playhead_selector.open){
        taken[key>>5]|=bit;
        if(!selector_context()){
            playhead_selector.open=0;
        }else if(key==KEY_UP || key==KEY_DOWN){
            if(key==playhead_selector.opening_key)return 1; /* held opener: no accidental movement */
            playhead_selector.cursor=key==KEY_UP?
                (playhead_selector.cursor?playhead_selector.cursor-1u:PLAYHEAD_MODES-1u):
                (playhead_selector.cursor+1u<PLAYHEAD_MODES?playhead_selector.cursor+1u:0u);
            playhead_selector.error=0;playhead_selector.dirty=1;
        }else if(!(flags&8u) && key==KEY_NO){
            playhead_selector.open=0;
        }else if(!(flags&8u) && key==KEY_YES){
            k=playhead_selector.kit;t=playhead_selector.track;
            if(playhead_modes[k][t]==playhead_selector.cursor)playhead_selector.open=0;
            else{
                playhead_enter();refused=playhead_seq_set(k,t,playhead_selector.cursor);playhead_leave();
                if(!refused){playhead_persist_changed(k);playhead_selector.open=0;}
                else {playhead_selector.error=1;playhead_selector.dirty=1;}
            }
        }else return 1;                /* modal: underlying pages and tracks cannot be edited */
    }else{
        if(key!=KEY_UP && key!=KEY_DOWN)return (taken[key>>5]&bit)!=0;
        if(flags&8u)return (taken[key>>5]&bit)!=0;
        if(!HELD(KEY_TRK) || !(app=APP()) || !(playhead_selector.view=main_screen()) ||
           (t=TRACK_INDEX(app+0x30))>=PLAYHEAD_TRACKS || (k=playhead_active_kit())>=PLAYHEAD_KITS)return 0;
        taken[key>>5]|=bit;
        playhead_selector.kit=k;playhead_selector.track=t;
        playhead_selector.cursor=playhead_modes[k][t];
        playhead_selector.opening_key=key;playhead_selector.error=0;
        playhead_selector.dirty=1;playhead_selector.open=1;
    }
    if((ui=UI()))ui[0x20]=1;            /* existing controller redraw flag; only on UI interaction */
    return 1;
}
/* The icons: one-pixel strokes, centred in the seven rows of the tempo's
 * digits; a row a halfword from the top, bit width-1 the left column. */
static const struct {u8 width;unsigned short rows[7];} icons[PLAYHEAD_MODES]={
    {7,{0x000,0x004,0x002,0x07f,0x002,0x004,0x000}},   /* FORWARD: selector only */
    {7,{0x000,0x010,0x020,0x07f,0x020,0x010,0x000}},   /* REVERSE: an arrow to the left */
    {9,{0x000,0x044,0x082,0x1ff,0x082,0x044,0x000}},   /* PENDULUM: an arrow both ways */
    {7,{0x03e,0x041,0x055,0x049,0x055,0x041,0x03e}},   /* RANDOM: a die's five */
    {7,{0x006,0x000,0x030,0x000,0x006,0x000,0x030}},   /* WALK: steps, left and right */
    {7,{0x008,0x01c,0x008,0x008,0x049,0x02a,0x01c}},   /* ANCHOR: ring, shank and flukes */
    {7,{0x000,0x054,0x056,0x057,0x056,0x054,0x000}},   /* STUTTER: repeated bars and play */
    {7,{0x041,0x022,0x014,0x008,0x014,0x022,0x041}},   /* FOLD: outside to centre and back */
    {9,{0x0c0,0x004,0x032,0x00f,0x0c2,0x004,0x030}}};  /* WALK+: footsteps and right arrow */
/* Borrowed native Bitmap, with our own column-major pixels and mask. Native
 * FILL/BLIT were checked on OS 1.42: y=0 is bit 31 of column word 0, y=63
 * is bit 0 of word 1. Build only when the selector changes; one native blit
 * per rendered frame, no allocation, timer or sequencer/audio work. */
static u32 selector_pixels[256],selector_mask[256],selector_bitmap[8],selector_ready;
static const u8 selector_font[38][5]={
 {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
 {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
 {2,5,7,5,5},{6,5,6,5,6},{7,4,4,4,7},{6,5,5,5,6},{7,4,6,4,7},
 {7,4,6,4,4},{7,4,5,5,7},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,7},
 {5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{5,7,7,7,5},{7,5,5,5,7},
 {6,5,6,4,4},{7,5,5,7,1},{6,5,6,5,5},{7,4,7,1,7},{7,2,2,2,2},
 {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},
 {7,1,2,4,7},{0,2,7,2,0},{1,1,2,4,4}};
static void selector_rect(u32 x0,u32 y0,u32 x1,u32 y1,u32 ink)
{
    u32 x,w,lo,hi,mask;
    for(w=y0>>5;w<=y1>>5;w++){
        lo=w==(y0>>5)?y0&31u:0u;hi=w==(y1>>5)?y1&31u:31u;
        mask=(0xffffffffu>>lo)&(0xffffffffu<<(31u-hi));
        for(x=x0;x<=x1;x++)if(ink)selector_pixels[2*x+w]|=mask;else selector_pixels[2*x+w]&=~mask;
    }
}
static void selector_text(u32 x,u32 y,const char *s,u32 ink)
{
    u32 r,c,index;const u8 *g;
    for(;*s;s++,x+=4u){
        if(*s>='0' && *s<='9')index=(u32)(*s-'0');
        else if(*s>='A' && *s<='Z')index=10u+(u32)(*s-'A');
        else if(*s=='+')index=36u;else if(*s=='/')index=37u;else continue;
        g=selector_font[index];
        for(r=0;r<5u;r++)for(c=0;c<3u;c++)if(g[r]&(4u>>c))selector_rect(x+c,y-r,x+c,y-r,ink);
    }
}
static void selector_render(void)
{
    u32 i,r,c,bits,w,x,y,ink;char track[7]={'T','R','K',' ',0,0,0};
    if(!selector_ready){
        for(i=4;i<=123u;i++){selector_mask[2*i]=0x3fffffffu;selector_mask[2*i+1u]=0xfffffffcu;}
        BITMAP_CTOR(selector_bitmap,128,64,selector_pixels,selector_mask);selector_ready=1;
    }
    for(i=0;i<256u;i++)selector_pixels[i]=0;
    selector_rect(4,2,123,2,1);selector_rect(4,61,123,61,1);
    selector_rect(4,3,4,60,1);selector_rect(123,3,123,60,1);
    selector_rect(8,51,119,51,1);
    selector_text(10,58,playhead_selector.error?"MODES N/A":"PLAYHEAD",1);
    if(playhead_selector.track<12u){
        i=playhead_selector.track+1u;
        if(i>=10u){track[4]='1';track[5]=(char)('0'+i-10u);}else track[4]=(char)('0'+i);
        selector_text(86,58,track,1);
    }else selector_text(86,58,"FX TRK",1);
    for(i=0;i<PLAYHEAD_MODES;i++){
        x=i<5u?8u:66u;y=48u-8u*(i<5u?i:i-5u);ink=i!=playhead_selector.cursor;
        if(!ink)selector_rect(x,y-6u,x+53u,y+1u,1);
        w=icons[i].width;
        for(r=0;r<7u;r++)for(bits=icons[i].rows[r],c=0;c<w;c++)
            if(bits&(1u<<(w-1u-c)))selector_rect(x+3u+(9u-w)/2u+c,y-r,x+3u+(9u-w)/2u+c,y-r,ink);
        selector_text(x+16u,y-1u,names[i],ink);
    }
    selector_text(30,8,"YES SET NO CANCEL",1);playhead_selector.dirty=0;
}
/* Global DRAW is after native overlays, unlike the contextual header icon. */
static void playhead_selector_draw(void *bitmap,void *ctrl)
{
    if(!playhead_selector.open)return;
    if(!selector_context()){
        playhead_selector.open=0;if(ctrl)((u8 *)ctrl)[0x20]=1;return;
    }
    if(playhead_selector.dirty)selector_render();
    BLIT(bitmap,selector_bitmap,0,0,0);
}
static int playhead_selector_enc(void *brain,void *event)
{
    (void)brain;(void)event;
    if(!playhead_selector.open)return 0;
    if(!selector_context()){playhead_selector.open=0;return 0;}
    return 1;
}
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
    if(!playhead_seq_installed || playhead_selector.open)return;
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
    {ST_EV_DRAW,(void (*)(void))playhead_selector_draw},
    {ST_EV_ENC,(void (*)(void))playhead_selector_enc},
    {ST_EV_NATIVE,(void (*)(void))playhead_native}};
const struct st_smz_module playhead_module={"PLAYHEAD",initialize,subscriptions,5};
