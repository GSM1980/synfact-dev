/* SPDX-License-Identifier: GPL-2.0-or-later; actual MAIN sender/AUDIO receiver.
 * Native host only: both sides share one address map, the FPGA banks are
 * plain memory, and "frames" are simulated by FRAME_TAG = MAIN's tail byte. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "../mods/core-st/machines.h"
typedef unsigned int u32;
volatile u32 core_machine_active=1;
static struct core_machine_param param[28];
static struct core_machine_def defs[6];
static unsigned char test_owners[8]={0,1,2,3,4,5,6,7};
static u32 use_owners;
/* The production index -> Sound slot table (machines.c). */
static const u32 test_slots[28]={0x12,0x13,0x14,0x15,0x16,0x17,0x1a,0x19,
    0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,0x40,0x41,0x42,0x43,0x44,0x45,
    0,0x11,0x18,0x46};
const struct core_machine_def *core_machine_find(u32 type)
{return type>=64 && type<70?defs+type-64:0;}
u32 core_machine_slot(u32 index){return test_slots[index];}
static u32 test_shadow[8*CORE_MACHINE_SHADOW_ROW];
u32 *core_machine_shadow=test_shadow;   /* bind allocation: uninitialized */
static u32 fake_timer;
#define CORE_MACHINE_TIMER() (fake_timer+=7)
#include "../mods/core-st/machine-params.c"
#define TEL core_machine_telemetry
#define SNAP() (cadence=0,core_machine_snapshot(0))   /* a full compare block */
#define FRAME_TAG (*(const volatile u8 *)0x800021a9u)
#define PARAM_BANK(tag) ((const volatile u32 *)(0x10000c00u+(((tag)&1)<<9)))
#define UPPER
#include "../mods/core-st/audio-machines.c"
u32 audio_machine_updates[28],audio_machine_renders[28];
u8 audio_machine_types[80];
void audio_machine_flush(void *p,u32 n){(void)p;(void)n;}
void audio_machine_silent(void){}
static void map(u32 base,u32 size)
{assert(VirtualAlloc((void *)base,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)base);}
#define GATE (*(volatile u32 *)0x43bdc824u)
#define FPGA ((unsigned char *)0x10000c00u)
static u32 values[96];
static unsigned short *working(u32 t){return (unsigned short *)(0x80006364u+t*0x8e);}
static unsigned short *mirror(u32 t){return (unsigned short *)(0x800021ccu+t*0x8e);}
static int *lfo(u32 t,u32 n){return (int *)(0x444da748u+t*0x50+n*0x28);}
static const void *audio_params(u32 t){return (const void *)(0x80003c5cu+t*0x8e);}
static u32 *bank_of(u32 tag){return (u32 *)PARAM_BANK(tag);}
/* MAIN's saved machine of a track and AUDIO's received voice type. */
static void set_type(u32 t,u32 type){*(u8 *)(0x80002f50u+t)=(u8)type;*(u8 *)(0x80003c7eu+t*0x8e)=(u8)type;}
static void audio_restart(void){memset(values,0,sizeof values);extended=values;accepted=0;memset(row_type,0,8);}
/* Independent oracle of one effective control, from the native arrays. */
static u32 effective(u32 t,u32 owner,u32 index)
{
    u32 slot=test_slots[index],n;int v;
    if(!slot)return mirror(owner)[0];                    /* None destination: unmodulated */
    if(slot!=0x11 && slot!=0x18)return working(t)[slot];
    v=mirror(owner)[slot];
    for(n=0;n<2;n++)if((working(t)[n?12:4]>>8)==slot)v+=*lfo(owner,n);
    if(v<0)v=0;
    if(v>0x7f00)v=0x7f00;
    return (u32)v;
}
/* AUDIO offers exactly the effective values of t's machine (owner's descriptor). */
static void offered(u32 t,u32 owner)
{
    const struct core_machine_def *m=core_machine_find(*(u8 *)(0x80002f50u+owner));
    const u32 *w=(const u32 *)audio_machine_params(audio_params(t));u32 j,hi,lo;
    assert(m && m->params>8 && w==extended+12*t);
    for(j=0;j<(m->params-8+1)/2;j++){
        hi=effective(t,owner,8+2*j);lo=9+2*j<m->params?effective(t,owner,9+2*j):0;
        assert(w[j]==(hi<<16|lo));
    }
}
static u32 publish(void){u32 before=FRAME_TAG;SNAP();assert(FRAME_TAG==before%254+1);return FRAME_TAG;}
int main(void)
{
    u32 counts[]={8,28,9,27,25,26},t,i,tag,g,*b,saved[15];unsigned char copy[1024];
    map(0x10000000,0x10000);map(0x80000000,0x10000);map(0x43bd0000,0x10000);
    map(0x444d0000,0x10000); /* native LFO output accumulators */
    memset(FPGA,0xa5,1024);memset(test_shadow,0xcc,sizeof test_shadow);
    for(i=0;i<28;i++){param[i].min=0;param[i].max=0x7f00;}
    for(t=0;t<6;t++){defs[t].type=64+t;defs[t].params=counts[t];defs[t].parameter=param;audio_machine_types[64+t]=12+t;}
    for(t=0;t<8;t++){
        set_type(t,65);
        for(i=0;i<0x47;i++)working(t)[i]=(unsigned short)(0x100*t+i);
        working(t)[4]=working(t)[12]=0;           /* no LFO routed */
        mirror(t)[0x11]=0x4000;mirror(t)[0x18]=0x4000;
    }
    audio_restart();
    /* 1. Never-received rows are never offered: the FM receiver then keeps its
     *    neutral 0x4000 ratio offsets instead of reading zeros (silence). */
    GATE=0;SNAP();assert(!FRAME_TAG);
    for(i=0;i<1024;i++)assert(FPGA[i]==0xa5);
    audio_machine_receive();
    for(t=0;t<8;t++)assert(!audio_machine_params(audio_params(t)));
    /* 2. Boot with eight FM: first complete generation, all rows. */
    GATE=1;tag=publish();assert(tag==1);b=bank_of(tag);
    assert(b[0]==0x4d503333u && b[1]==1 && b[2]==0xff && b[3]==0);
    for(t=0;t<8;t++){assert(b[4+15*t]==(20u<<16|65u));for(i=11;i<13;i++)assert(b[4+15*t+i]==0xa5a5a5a5u);}
    assert(!audio_machine_params(audio_params(0)));     /* published, not yet received */
    audio_machine_receive();assert(accepted==1);
    for(t=0;t<8;t++)offered(t,t);
    *(u8 *)(0x80003c7eu+3*0x8e)=66;assert(!audio_machine_params(audio_params(3)));
    *(u8 *)(0x80003c7eu+3*0x8e)=65;offered(3,3);
    assert(!audio_machine_params((const char *)audio_params(3)+2) && !audio_machine_params(audio_params(8)));
    /* 3. Stable values: compare every block, never touch FPGA; AUDIO reads nothing. */
    memcpy(copy,FPGA,1024);
    assert(TEL[0]==2 && TEL[2]==1 && TEL[3]==8 && TEL[1]==14);   /* gate-off block, then one full row set */
    for(i=0;i<1500;i++){SNAP();assert(FRAME_TAG==1);}
    assert(!memcmp(copy,FPGA,1024));
    assert(TEL[0]==1502 && TEL[2]==1 && TEL[3]==8 && TEL[1]==1502*7);  /* compare only: no publication */
    memset(FPGA,0x5a,1024);audio_machine_receive();for(t=0;t<8;t++)offered(t,t);
    memcpy(FPGA,copy,1024);
    b[4+1]++;b[4+13]++;audio_machine_receive();offered(0,0);   /* valid but unannounced: unread */
    memcpy(FPGA,copy,1024);
    /* 4. LFO on a special slot: block-rate changes publish every block. The
     *    first write of the even bank is complete; afterwards a bank only
     *    receives the rows it holds stale. */
    working(2)[4]=0x11<<8;*lfo(2,0)=0x300;
    tag=publish();assert(tag==2 && b!=bank_of(tag));b=bank_of(tag);
    for(t=0;t<8;t++)assert(b[4+15*t]==(20u<<16|65u));
    audio_machine_receive();assert(accepted==2);offered(2,2);
    assert((((u32 *)audio_machine_params(audio_params(2)))[8]&0xffff)==0x4300); /* index 25 = slot 0x11 */
    for(i=0;i<40;i++){
        u32 *next=bank_of(FRAME_TAG+1);
        memcpy(saved,next+4+15*5,sizeof saved);next[4+15*5]=0xdeadbeefu;  /* fresh in that bank */
        next[4+15*2+9]=0xdeadbeefu;                                        /* stale word there */
        g=TEL[3];*lfo(2,0)=(int)(i*0x101)-0x2000;tag=publish();b=bank_of(tag);
        assert(TEL[3]==g+1 && TEL[4]==4 && TEL[5]);                     /* track 2 only */
        assert(b[4+15*5]==0xdeadbeefu && b[4+15*2+9]!=0xdeadbeefu);
        memcpy(b+4+15*5,saved,sizeof saved);
        audio_machine_receive();assert(accepted==tag);offered(2,2);offered(5,5);
    }
    *lfo(2,0)=-0x8000;tag=publish();audio_machine_receive();offered(2,2);   /* clamps at 0 */
    *lfo(2,0)=0x8000;tag=publish();audio_machine_receive();offered(2,2);    /* clamps at 7f00 */
    working(2)[4]=0;*lfo(2,0)=0;tag=publish();audio_machine_receive();offered(2,2);
    working(1)[12]=0x18<<8;*lfo(1,1)=-0x100;tag=publish();audio_machine_receive();offered(1,1);
    working(1)[12]=0;tag=publish();audio_machine_receive();offered(1,1);
    /* 5. Parameter lock and its retirement are two generations. */
    g=working(4)[0x38];working(4)[0x38]=0x7000;tag=publish();audio_machine_receive();offered(4,4);
    working(4)[0x38]=(unsigned short)g;tag=publish();audio_machine_receive();offered(4,4);
    mirror(4)[0]=0x2000;tag=publish();audio_machine_receive();offered(4,4);    /* ratio offset C */
    working(4)[0]=0x1234;SNAP();assert(FRAME_TAG==tag);       /* None modulation: ignored */
    mirror(4)[0]=0x4000;working(4)[0x46]=0x5000;tag=publish();audio_machine_receive();offered(4,4);
    /* 6. Checksum rejection retries the same complete bank on a later block. */
    g=accepted;working(1)[0x3b]=0x1234;working(0)[0x3b]=0x4321;tag=publish();b=bank_of(tag);
    b[4+15+13]^=1;audio_machine_receive();assert(accepted==g);
    assert((((u32 *)audio_machine_params(audio_params(1)))[2]&0xffff)!=0x1234);
    b[4+15+13]^=1;audio_machine_receive();assert(accepted==tag);offered(0,0);offered(1,1);
    /* 7. Torn: the header is invalidated first, so a rewrite in progress is
     *    never accepted (row recheck), and neither is a cleared magic. */
    working(3)[0x40]=0x0777;tag=publish();b=bank_of(tag);g=accepted;
    b[1]=0;audio_machine_receive();assert(accepted==g);b[1]=tag;
    b[0]=0;audio_machine_receive();assert(accepted==g);b[0]=0x4d503333u;
    audio_machine_receive();assert(accepted==tag);offered(3,3);
    /* 8. Lagging frame (two blocks): its bank already holds n+2, the other n+1. */
#define NEXT(x) ((x)%254+1)
    g=accepted;
    for(i=1;i<=3;i++){working(5)[0x37]=(unsigned short)i;publish();}
    *(u8 *)0x800021a9u=(u8)NEXT(g);audio_machine_receive();
    assert(accepted==NEXT(NEXT(g)) && (((u32 *)audio_machine_params(audio_params(5)))[0]&0xffff)==2);
    audio_machine_receive();assert(accepted==NEXT(NEXT(g)));  /* nothing newer reachable yet */
    *(u8 *)0x800021a9u=(u8)NEXT(NEXT(NEXT(g)));audio_machine_receive();
    assert(accepted==NEXT(NEXT(NEXT(g))));offered(5,5);
    /* 9. Generation wrap: 1..254, never 0, alternating banks. */
    for(i=0;i<600;i++){
        g=FRAME_TAG;working(7)[0x45]=(unsigned short)i;tag=publish();
        assert(tag>=1 && tag<=254 && (tag&1)!=(g&1));
        audio_machine_receive();assert(accepted==tag);offered(7,7);
    }
    /* 10. Mask, machine and owner changes. */
    set_type(6,1);SNAP();assert(FRAME_TAG==accepted);  /* leaving only: no rewrite */
    assert(!audio_machine_params(audio_params(6)));                    /* stock voice */
    set_type(6,65);SNAP();assert(FRAME_TAG==accepted);offered(6,6);
    set_type(6,1);working(0)[0x36]^=0x100;tag=publish();assert(bank_of(tag)[2]==0xbf);
    audio_machine_receive();assert(!row_type[6]);
    set_type(6,65);tag=publish();assert(bank_of(tag)[2]==0xff);       /* same values: republished */
    audio_machine_receive();offered(6,6);
    set_type(6,67);tag=publish();assert(bank_of(tag)[4+15*6]==(19u<<16|67u));
    audio_machine_receive();offered(6,6);
    set_type(6,66);tag=publish();assert(bank_of(tag)[4+15*6]==(1u<<16|66u));
    audio_machine_receive();offered(6,6);
    set_type(6,68);tag=publish();assert(bank_of(tag)[4+15*6]==(17u<<16|68u));   /* 25: index 24 alone */
    audio_machine_receive();offered(6,6);
    set_type(6,69);tag=publish();assert(bank_of(tag)[4+15*6]==(18u<<16|69u));   /* 26: 24 + LFO slot 11 */
    working(6)[4]=working(6)[12]=0x11<<8;*lfo(6,0)=0x123;*lfo(6,1)=0x456;     /* both LFOs, one slot */
    tag=publish();audio_machine_receive();offered(6,6);
    assert((((u32 *)audio_machine_params(audio_params(6)))[8]&0xffff)==0x4000+0x123+0x456);
    working(6)[4]=working(6)[12]=0;*lfo(6,0)=*lfo(6,1)=0;tag=publish();audio_machine_receive();offered(6,6);
    set_type(6,64);SNAP();assert(FRAME_TAG==accepted); /* 8 controls: no extension */
    audio_machine_receive();assert(!audio_machine_params(audio_params(6)));
    set_type(6,65);tag=publish();audio_machine_receive();offered(6,6);
    use_owners=1;test_owners[3]=2;set_type(2,67);*(u8 *)(0x80003c7eu+3*0x8e)=67;
    working(2)[4]=0x11<<8;*lfo(2,0)=0x222;*lfo(3,0)=-0x333;            /* owner's LFO output */
    memcpy(working(3),working(2),0x8e);tag=publish();
    assert(bank_of(tag)[4+15*3]==(19u<<16|67u));
    for(i=1;i<11;i++)assert(bank_of(tag)[4+15*3+i]==bank_of(tag)[4+15*2+i]);
    audio_machine_receive();offered(3,2);offered(2,2);
    working(2)[4]=working(3)[4]=0;*lfo(2,0)=*lfo(3,0)=0;
    test_owners[3]=7;set_type(3,65);tag=publish();                     /* invalid forward owner */
    assert(bank_of(tag)[4+15*3]==(20u<<16|65u));audio_machine_receive();offered(3,3);
    use_owners=0;set_type(2,65);tag=publish();audio_machine_receive();
    for(t=0;t<8;t++)offered(t,t);
    /* 11. Frames not ready (boot sender owns the page): everything republished. */
    g=accepted;GATE=0;SNAP();assert(!FRAME_TAG);memset(FPGA,0x5a,1024);
    GATE=1;SNAP();tag=FRAME_TAG;assert(tag==NEXT(g));
    b=bank_of(tag);for(t=0;t<8;t++)assert(b[4+15*t]==(20u<<16|65u));
    assert(bank_of(tag+1)[0]==0x5a5a5a5au);
    audio_machine_receive();assert(accepted==tag);for(t=0;t<8;t++)offered(t,t);
    SNAP();assert(FRAME_TAG==tag);                     /* none changed: no new tag */
    /* 12. Receiver restart without any MAIN change: the persistent bank. */
    working(0)[0x36]^=0x100;tag=publish();SNAP();assert(FRAME_TAG==tag);
    audio_restart();for(t=0;t<8;t++)assert(!audio_machine_params(audio_params(t)));
    audio_machine_receive();assert(accepted==tag);for(t=0;t<8;t++)offered(t,t);
    /* 13. stock -> FM -> stock -> FM on every track, then stock-only. */
    for(i=0;i<4;i++){
        for(t=0;t<8;t++)set_type(t,i&1?65:1);
        memcpy(copy,FPGA,1024);SNAP();
        if(!(i&1)){assert(!FRAME_TAG && !memcmp(copy,FPGA,1024));
            audio_machine_receive();for(t=0;t<8;t++)assert(!audio_machine_params(audio_params(t)));}
        else{assert(FRAME_TAG);audio_machine_receive();for(t=0;t<8;t++)offered(t,t);}
    }
    for(t=0;t<8;t++)set_type(t,1);memcpy(copy,FPGA,1024);
    SNAP();assert(!FRAME_TAG && !memcmp(copy,FPGA,1024));
    memcpy(copy,values,sizeof values);audio_machine_receive();assert(!memcmp(copy,values,sizeof values));
    /* 14. Randomized blocks against the independent oracle: machine types,
     *     every slot, mirror, routing and accumulators of both LFOs. */
    {u32 seed=12345,k,types[]={1,64,65,66,67,68,69};
     for(k=0;k<20000;k++){
         for(i=0;i<3;i++){
             seed=seed*1103515245u+12345u;t=(seed>>8)&7;
             switch((seed>>12)&7){
             case 0:set_type(t,types[(seed>>16)%7]);break;
             case 1:working(t)[4]=(unsigned short)(((seed>>16)&1?0x11:(seed>>17)&1?0x18:0x36)<<8);break;
             case 2:working(t)[12]=(unsigned short)(((seed>>16)&1?0x18:(seed>>17)&1?0x11:0)<<8);break;
             case 3:*lfo(t,(seed>>16)&1)=(int)(seed>>4)%0x9000-0x4800;break;
             case 4:mirror(t)[(seed>>16)&1?0x11:0x18]=(unsigned short)((seed>>4)&0x7fff);break;
             default:working(t)[(seed>>16)%0x47]=(unsigned short)(seed>>3);
             }
         }
         SNAP();audio_machine_receive();
         for(t=0;t<8;t++){u32 ty=*(u8 *)(0x80002f50u+t);
             if(ty>=65)offered(t,t);else assert(!audio_machine_params(audio_params(t)));}
     }}
    /* 15. Cadence: partial blocks compare only trigged or LFO-routed tracks. */
    {for(t=0;t<8;t++)set_type(t,65);SNAP();audio_machine_receive();g=FRAME_TAG;
     for(t=0;t<8;t++){working(t)[4]=working(t)[12]=0;}SNAP();g=FRAME_TAG;audio_machine_receive();
     working(3)[0x38]^=0x100;cadence=1;
     for(i=0;i<3;i++){core_machine_snapshot(0);assert(FRAME_TAG==g);}         /* edit waits */
     core_machine_snapshot(0);assert(FRAME_TAG==NEXT(g));audio_machine_receive();offered(3,3);
     g=FRAME_TAG;working(5)[0x39]^=0x100;cadence=1;
     core_machine_snapshot(1u<<5);assert(FRAME_TAG==NEXT(g));                 /* trig: same block */
     audio_machine_receive();offered(5,5);
     working(6)[4]=0x37<<8;working(6)[8]=0x5000;SNAP();g=FRAME_TAG;audio_machine_receive();
     working(6)[0x37]^=0x100;core_machine_snapshot(0);assert(FRAME_TAG==NEXT(g)); /* routed LFO */
     audio_machine_receive();offered(6,6);
     working(6)[8]=0x4000;SNAP();g=FRAME_TAG;working(6)[0x37]^=0x100;
     core_machine_snapshot(0);assert(FRAME_TAG==g);}                             /* depth 0: not routed */
    puts("PASS: cadence (4-block edits, trig and routed-LFO blocks), 20000 randomized blocks vs oracle, never-received rows withheld, 8xFM boot, stable values without FPGA writes/reads, "
         "LFO slots/clamps, locks and retirement, stale-row-only rewrites, checksum retry, torn headers, "
         "lagging frames, 600-generation wrap, mask/type/owner changes, ready gate, receiver restart, "
         "stock/FM transitions");
    return 0;
}

unsigned int core_native(unsigned int op,const unsigned int *a){(void)a;return op==15 && use_owners?(u32)test_owners:0;}
unsigned int core_native_view(unsigned int op,void *v,unsigned int b){(void)v;(void)b;return op==10?0xffffffffu:0;}
