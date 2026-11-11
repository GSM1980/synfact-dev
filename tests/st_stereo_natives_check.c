/* SPDX-License-Identifier: GPL-2.0-or-later
 * Target-side checks of the stereo engine's replacements for native calls,
 * against the native routines of a core's AUDIO image, run in analog-emu's
 * cfbench (tests/test_st_stereo_natives_emu.py). Built with the bank's
 * compiler and flags and linked after the AUDIO image; never in a bank.
 *
 * Each check builds random chain state and buffers, runs the native routine
 * and the replacement on the same copies, and returns
 * (mismatching words << 16) | (cases where the replacement ran).
 */
#include "../mods/samplers-st/stereo/audio.c"
static u32 seed;
static u32 rng(void){seed=seed*1664525u+1013904223u;return seed;}
static s32 sample(void){s32 x=(s32)rng();return x>>(rng()&15);}
static void fill(void *to,u32 bytes){u32 i,*p=to;for(i=0;i<bytes/4;i++)p[i]=rng();}
static u32 state[40],rows[8*142/4+1],buffers[2][256];
/* The native drive against drive_r, on R buffers of claimed tracks. */
u32 check_drive(u32 cases,u32 start)
{
    u32 k,t,mask,off,bad=0,ran=0,macsr=get_macsr();s32 *r;
    volatile u32 *live=(volatile u32 *)0x800083b0u;
    seed=start;set_macsr(32);
    for(k=0;k<cases;k++){
        mask=rng()&0xffu;if(!mask)mask=1u<<(rng()&7);
        fill(rows,sizeof(rows));fill(state,sizeof(state));
        off=rng()&1;                               /* half the cases: all off */
        for(t=0;t<8;t++){
            short *word=(short *)((u8 *)rows+142*t+0x50);
            u32 pick=off?1+(rng()&1):rng()&7;
            /* mostly off; sometimes on, or off with a wet weight left */
            *word=pick==0?(short)(1+(rng()&0x7fff)):pick==1?0:(short)-(s32)(rng()&0x7fff);
            if(off || (rng()&3))state[t]=0;          /* 0x800083b0 + 4t: wet */
            if(rng()&1)state[8+t]=0x3fffffffu;        /* 0x800083d0 + 4t: dry at its target */
        }
        for(t=0;t<8;t++)for(r=(s32 *)buffers[0]+32*t;r<(s32 *)buffers[0]+32*t+32;r++)
            *r=mask&(1u<<t)?sample():0;           /* the engine zeroes unclaimed R */
        copy_words((u32 *)ROWS,rows,8*142/4);     /* 284 words */
        /* native */
        copy_words((u32 *)live,state,40);copy_words((u32 *)ST_STEREO_R(0),buffers[0],256);
        DRIVE(ST_STEREO_R(0),ROWS);
        copy_words(buffers[1],(u32 *)ST_STEREO_R(0),256);
        /* replacement */
        copy_words((u32 *)live,state,40);copy_words((u32 *)ST_STEREO_R(0),buffers[0],256);
        if(drive_off(mask))ran++;
        drive_r(mask);
        for(t=0;t<8;t++)if(mask&(1u<<t)){
            u32 i;const u32 *a=buffers[1]+32*t,*b=(const u32 *)ST_STEREO_R(t);
            for(i=0;i<32;i++)if(a[i]!=b[i])bad++;
        }
    }
    set_macsr(macsr);
    return bad<<16|ran;
}
/* One R drive pass of eight claimed tracks, for instruction counts. */
void bench_drive_native(void){u32 m=get_macsr();set_macsr(32);DRIVE(ST_STEREO_R(0),ROWS);set_macsr(m);}
void bench_drive_replacement(void){u32 m=get_macsr();set_macsr(32);drive_r(0xffu);set_macsr(m);}
/* The sampler's native voice tail on L and R (sp_render before 1.4) against
 * st_voice_filter2, 40003d42 and st_voice_gains2: outputs, buffers, the
 * voice and R's filter, under several MACSR modes and with whatever an
 * earlier routine left in ACC0. */
#include "../mods/samplers-st/audio/voice-stereo.c"
#define FILTER ((void (*)(void *,void *,void *))0x400068eau)
#define CONTROL ((void (*)(void *))0x40003d42u)
#define RAMP ((void (*)(void *,void *))0x400045a0u)
#define OUTPUT ((void (*)(void *,void *,void *,void *,void *))0x400049f4u)
static u32 voice[2][452],rfilter[2][4],pcm[2][64],outs[2][64],zero[33];
static void set_acc0(u32 v){__asm__ volatile("move.l %0,%%acc0"::"d"(v));}
static void native_tail(u32 *v,s32 *a,s32 *ra,s32 *out,s32 *rout,u32 *rf)
{
    int locals[2]={0,1};u32 ramp,start[2];
    FILTER(a,a,(u8 *)v+0x6f8);
    rf[0]=v[0x6f8/4];rf[1]=v[0x6fc/4];FILTER(ra,ra,rf);
    CONTROL(v);
    ramp=v[0x3d4/4];RAMP(a,v);v[0x3d4/4]=ramp;RAMP(ra,v);
    start[0]=v[6];start[1]=v[7];OUTPUT(a,zero,out,locals,v);
    v[6]=start[0];v[7]=start[1];OUTPUT(ra,zero,rout,locals,v);
}
static void fused_tail(u32 *v,s32 *a,s32 *ra,s32 *out,s32 *rout,u32 *rf)
{
    rf[0]=v[0x6f8/4];rf[1]=v[0x6fc/4];
    st_voice_filter2(a,ra,(u32 *)((u8 *)v+0x6f8),rf);
    CONTROL(v);
    st_voice_gains2(a,ra,out,rout,v);
}
u32 check_voice(u32 cases,u32 start)
{
    static const u32 modes[4]={0x20,0xa0,0x00,0x30};
    u32 k,i,bad=0,macsr=get_macsr(),acc,mode;
    seed=start;
    for(k=0;k<cases;k++){
        fill(voice[0],sizeof(voice[0]));fill(rfilter[0],sizeof(rfilter[0]));
        for(i=0;i<64;i++)pcm[0][i]=(u32)sample();
        voice[0][0x34/4]=(rng()&7)==0;voice[0][0x38/4]=rng()&1;voice[0][0x3c/4]=rng()&1;
        if(rng()&1)voice[0][0x3e8/4]=0x80000000u;
        if(rng()&1)voice[0][0x3d0/4]=0x80000000u;
        voice[0][0x3dc/4]=rng()%3;voice[0][0x3e4/4]=rng()&1;voice[0][0x3d8/4]=rng()&1;
        voice[0][0x6f8/4]>>=1+(rng()&3);voice[0][0x6fc/4]>>=1+(rng()&3);    /* coefficients */
        mode=modes[k&3];acc=rng();if(k&4)acc=0;
        copy_words(voice[1],voice[0],452);copy_words(pcm[1],pcm[0],64);copy_words(rfilter[1],rfilter[0],4);
        set_macsr(mode);set_acc0(acc);
        native_tail(voice[0],(s32 *)pcm[0],(s32 *)pcm[0]+32,(s32 *)outs[0],(s32 *)outs[0]+32,rfilter[0]);
        set_macsr(mode);set_acc0(acc);
        fused_tail(voice[1],(s32 *)pcm[1],(s32 *)pcm[1]+32,(s32 *)outs[1],(s32 *)outs[1]+32,rfilter[1]);
        for(i=0;i<64;i++)bad+=(pcm[0][i]!=pcm[1][i])+(outs[0][i]!=outs[1][i]);
        for(i=0;i<0x708/4;i++)bad+=voice[0][i]!=voice[1][i];
        for(i=0;i<4;i++)bad+=rfilter[0][i]!=rfilter[1][i];
    }
    set_macsr(macsr);
    return bad<<16|cases;
}
/* One stereo voice tail, native and fused, for instruction counts. */
void bench_voice_native(void){native_tail(voice[0],(s32 *)pcm[0],(s32 *)pcm[0]+32,(s32 *)outs[0],(s32 *)outs[0]+32,rfilter[0]);}
void bench_voice_replacement(void){fused_tail(voice[1],(s32 *)pcm[1],(s32 *)pcm[1]+32,(s32 *)outs[1],(s32 *)outs[1]+32,rfilter[1]);}
/* The native multimode on R against st_multimode_tail entered with the L
 * pass's targets: R's buffer, R's signal words and the control state after,
 * for random rows, types, flags, coefficients, weights and signal memory. The
 * L pass is the native routine itself on the same starting state, with the
 * same scratch left by an earlier track. */
static u32 mstate[2][88+12+8+24+8+16],scratch[8],mrow[142/4+1],mbuf[3][32];
#define MULTI_SPANS {{0x80008740u,88},{0x80008550u,12},{0x800088e0u,8},{0x80008840u,24},{0x80008580u,8},{0x80008920u,16}}
static void multi_state(u32 *to,int save)
{
    static const u32 spans[6][2]=MULTI_SPANS;u32 i,j,*p;
    for(i=0;i<6;i++)for(p=(u32 *)spans[i][0],j=0;j<spans[i][1];j++)if(save)*to++=p[j];else p[j]=*to++;
}
u32 check_multi(u32 cases,u32 start)
{
    u32 k,t,i,bad=0,x,trig;s32 word;u8 *row;
    seed=start;
    for(k=0;k<cases;k++){
        t=rng()&7;x=rng();trig=rng()&0xffu;word=(rng()&3)?0:(s32)(short)rng();
        /* parameters 0..127 in Q8, the type byte 0..7 (sometimes out of range) */
        for(i=0;i<sizeof(mrow)/2;i++)((unsigned short *)mrow)[i]=(unsigned short)((rng()%128)<<8|(rng()&0xff));
        row=(u8 *)mrow+0x52;row[0]=(u8)((rng()&7)==0?rng():rng()&7);row[4]=(u8)(rng()&0x7f);
        fill(mstate[0],sizeof(mstate[0]));
        {u8 *f=(u8 *)&mstate[0][88];for(i=0;i<16;i++)f[i]=(u8)(rng()&1);}         /* 0x80008550..: flags */
        for(i=0;i<8;i++)mstate[0][88+4+i]=rng()&7;                              /* 0x80008560: types */
        for(i=0;i<88;i++)mstate[0][i]=(u32)((s32)mstate[0][i]>>(1+(rng()&3)));
        fill(scratch,sizeof(scratch));
        for(i=0;i<32;i++)mbuf[0][i]=(u32)sample();
        { u32 j;for(j=0;j<142;j++)((u8 *)(ROWS+142*t))[j]=((u8 *)mrow)[j]; }
        /* L pass: native, from the state; its coefficients after are the targets */
        multi_state(mstate[0],0);copy_words((u32 *)0x80008900u,scratch,8);
        copy_words(mbuf[1],mbuf[0],32);set_macsr(32);
        MULTI(ROWS+142*t+0x52,mbuf[1],x,t,trig,word);
        copy_words(before+MULTI_COEFFICIENTS(t),(u32 *)(0x80008740u+32*t),8);
        /* R: native, the same start, the scratch the earlier track left */
        multi_state(mstate[0],0);copy_words((u32 *)0x80008900u,scratch,8);
        copy_words(mbuf[1],mbuf[0],32);set_macsr(32);
        MULTI(ROWS+142*t+0x52,mbuf[1],x,t,trig,word);
        multi_state(mstate[1],1);
        if(get_macsr()!=32)bad++;
        /* R: the tail */
        multi_state(mstate[0],0);fill((u32 *)0x80008900u,32);
        copy_words(mbuf[2],mbuf[0],32);set_macsr(32);
        MULTI_R(t,ROWS+142*t+0x52,mbuf[2],x,t,trig,word);
        if(get_macsr()!=32)bad++;
        for(i=0;i<32;i++)bad+=mbuf[1][i]!=mbuf[2][i];
        multi_state(mstate[0],1);
        for(i=0;i<sizeof(mstate[0])/4;i++)bad+=mstate[0][i]!=mstate[1][i];
    }
    return bad<<16|cases;
}
void bench_multi_native(void){u32 m=get_macsr();set_macsr(32);MULTI(ROWS+0x52,mbuf[1],0,0,0,0);set_macsr(m);}
void bench_multi_replacement(void){u32 m=get_macsr();set_macsr(32);MULTI_R(0,ROWS+0x52,mbuf[2],0,0,0,0);set_macsr(m);}
/* One track of each filter type, for instruction counts: which 0 native, 1 tail. */
void bench_multi_type(u32 type,u32 which)
{
    u32 m=get_macsr(),i;u8 *row=(u8 *)ROWS+0x52;
    for(i=0;i<142;i++)((u8 *)ROWS)[i]=(u8)(0x40+i);
    row[0]=(u8)type;row[4]=0x20;((volatile u32 *)0x80008560u)[0]=type;
    set_macsr(32);
    if(which)MULTI_R(0,ROWS+0x52,mbuf[2],0,0,0,0);else MULTI(ROWS+0x52,mbuf[1],0,0,0,0);
    set_macsr(m);
}
/* The claim's release: after a loud burst then silence, the native multimode
 * of each type (and the render filter, type 8) must fall under
 * ST_STEREO_QUIET; exact 0 is not reached by types 0 and 1. Returns
 * (blocks until under the threshold << 16) | (blocks until exactly 0, or
 * 0xffff if never within the run). */
u32 check_tail(u32 type)
{
    u32 k,i,quiet=0xffff,zero=0xffff,mx;u8 *row=(u8 *)ROWS+0x52;static s32 tb[32];static u32 v[452];
    for(i=0;i<142;i++)((u8 *)ROWS)[i]=(u8)(0x40+i);
    row[0]=(u8)(type&7);row[4]=0x20;((volatile u32 *)0x80008560u)[0]=type&7;
    for(i=0;i<452;i++)v[i]=0;v[0x6f8/4]=0x10000000;v[0x6fc/4]=0x08000000;
    set_macsr(32);
    for(k=0;k<8000;k++){
        for(i=0;i<32;i++)tb[i]=k<4?(s32)((i&1)?0x40000000:-0x3ff00000)+(s32)(k*12345):0;
        if(type==8)FILTER(tb,tb,(u8 *)v+0x6f8);else MULTI(ROWS+0x52,tb,0,0,0,0);
        if(k<4)continue;
        for(mx=0,i=0;i<32;i++){u32 a=tb[i]<0?0u-(u32)tb[i]:(u32)tb[i];if(a>mx)mx=a;}
        if(quiet==0xffff && !st_stereo_audible(tb))quiet=k-4;
        if(!mx){zero=k-4;break;}
    }
    return quiet<<16|zero;
}
