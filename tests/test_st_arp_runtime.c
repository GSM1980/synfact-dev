/* Bounded generator state/failure cases; emulator separately tests native pools. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/arp-st/runtime.h"
static unsigned int *lastq;
static unsigned int nodes[8][20],used[8],queued[64],times[64],gates[64],nq,nfree,fail_queue,fail_alloc;
static unsigned int *alloc(void){unsigned int i;if(fail_alloc)return 0;for(i=0;i<8;i++)if(!used[i]){used[i]=1;memset(nodes[i],0,80);return nodes[i];}return 0;}
static void copy(unsigned int *d,const unsigned int *s)
{memcpy(d,s,80);if(!s[0] && s[17])(*(unsigned int *)s[17])++;}
static void release(unsigned int *e)
{unsigned int i=(unsigned int)(e-nodes[0])/20;assert(i<8 && used[i]);used[i]=0;if(e[17]){assert(*(unsigned int *)e[17]);(*(unsigned int *)e[17])--;}nfree++;}
static int queue(unsigned int *e,unsigned int time)
{if(fail_queue)return 0;assert(nq<64);gates[nq]=e[12];queued[nq]=e[6];times[nq++]=time;assert(e[3]==1 && !(e[9]&0x8000));lastq=e;release(e);return 1;}
int main(void)
{
    const struct arp_runtime_api api={alloc,copy,release,queue};
    struct arp_runtime r;struct arp_settings p={2,9,0,0,15,0,0xffff,{0}};
    unsigned int e[20]={0},refs=1,i;int chord[]={4,7};
    e[1]=1;e[2]=0;e[3]=1;e[6]=60;e[9]=0x10601;e[12]=10800000;e[17]=(unsigned int)&refs;
    arp_runtime_init(&r,&api);
    assert(arp_runtime_start(&r,&p,e,0,1,chord,2) && refs==2);
    for(i=0;i<800;i++)arp_runtime_frame(&r,&p,i*28800,(i+1)*28800,1);
    assert(nq==4 && !r.source && refs==1);
    assert(queued[0]==60 && queued[1]==64 && queued[2]==67 && queued[3]==60);
    /* A sequence root's outputs are native kind 1 (above); the runtime knows
     * its own queued output once, as superseded after its root ended. */
    assert(arp_runtime_owns(&r,lastq)==2 && !arp_runtime_owns(&r,lastq));
    assert(times[0]==0 && times[1]==5400000 && times[2]==10800000 && times[3]==16200000);
    for(i=0;i<4;i++)assert(gates[i]==253125);
    assert(arp_runtime_start(&r,&p,e,0,2,chord,2));
    arp_runtime_frame(&r,&p,0,28800,3);assert(!r.source && refs==1);
    fail_alloc=1;assert(!arp_runtime_start(&r,&p,e,0,3,0,0) && refs==1);fail_alloc=0;
    assert(arp_runtime_start(&r,&p,e,0,3,0,0));fail_queue=1;
    assert(!arp_runtime_frame(&r,&p,0,28800,3) && refs==2);fail_queue=0;
    arp_runtime_stop(&r);assert(refs==1);arp_runtime_stop(&r);assert(refs==1);
    e[0]=1;assert(arp_runtime_start(&r,&p,e,0,4,0,0) && refs==2);
    arp_runtime_stop(&r);assert(refs==1);
    e[11]=123;assert(!arp_runtime_start(&r,&p,e,0,4,0,0) && refs==1);e[11]=0;
    e[0]=0;p.enabled=0;assert(arp_runtime_start(&r,&p,e,0,5,0,0));
    assert(!arp_runtime_frame(&r,&p,0,28800,5));arp_runtime_stop(&r);assert(refs==1);
    /* Long LEN is bounded by a finite parent; native-zero infinity keeps the
     * chord alive and reads a changed LEN at the following output. */
    p.enabled=0xffff;p.note_length=126;e[12]=10000;
    assert(arp_runtime_start(&r,&p,e,0,6,0,0));
    assert(arp_runtime_frame(&r,&p,0,28800,6) && gates[nq-1]==10000);
    assert(!arp_runtime_frame(&r,&p,28800,57600,6) && refs==1);
    e[12]=0;p.note_length=0;assert(arp_runtime_start(&r,&p,e,0,7,0,0));
    assert(arp_runtime_frame(&r,&p,0,28800,7) && gates[nq-1]==253125);
    p.note_length=30;
    assert(arp_runtime_frame(&r,&p,5400000,5428800,7) && gates[nq-1]==2784375);
    arp_runtime_stop(&r);assert(refs==1);
    e[12]=0xffffffff;p.note_length=127;assert(arp_runtime_start(&r,&p,e,0,8,0,0));
    assert(arp_runtime_frame(&r,&p,0,28800,8) && gates[nq-1]==2136268810u);
    arp_runtime_stop(&r);assert(refs==1);
    p.note_length=255;assert(!arp_runtime_start(&r,&p,e,0,9,0,0) && refs==1);
    /* The same runtime accepts all audio tracks, including analog 9..12,
     * while FX/out-of-range events cannot index its per-track storage. */
    p.note_length=30;
    for(i=0;i<12;i++){
        e[2]=i;assert(arp_runtime_start(&r,&p,e,0,10,0,0));
        assert(r.track==i);arp_runtime_stop(&r);assert(refs==1);
    }
    e[2]=12;assert(!arp_runtime_start(&r,&p,e,0,10,0,0) && refs==1);
    e[2]=13;assert(!arp_runtime_start(&r,&p,e,0,10,0,0) && refs==1);
    for(i=0;i<8;i++)assert(!used[i]);
    /* List item 27: RANGE 4..6 (shown -2..-4) falls by octaves; 0..3 still
     * climb. A note below 0 is skipped, not wrapped. */
    {
        static const int shown[7]={-4,-3,-2,1,2,3,4};
        static const struct {unsigned char mode,range;unsigned int root,slots,count,notes[8];} c[]={
            {2,4,60,4,4,{60,48,60,48}},{3,5,60,6,6,{60,48,36,60,48,36}},
            {1,6,60,8,8,{60,48,36,24,60,48,36,24}},{2,3,60,8,8,{60,72,84,96,60,72,84,96}},
            {2,6,20,8,4,{20,8,20,8}},{4,4,60,8,0,{0}}};
        unsigned int k,j,v;
        for(j=0;j<7;j++)assert(arp_range_label(arp_range_value((int)j))==shown[j] &&
                               arp_range_position(arp_range_value((int)j))==(int)j);
        for(v=0;v<=ARP_RANGE_MAX;v++)assert(arp_range_value(arp_range_position(v))==v);
        for(k=0;k<sizeof(c)/sizeof(c[0]);k++){
            p.mode=c[k].mode;p.range=c[k].range;p.note_length=0;e[2]=0;e[6]=c[k].root;
            e[12]=2700000u*c[k].slots;nq=0;
            assert(arp_runtime_start(&r,&p,e,0,20+k,0,0));
            for(i=0;i<4000 && r.source;i++)arp_runtime_frame(&r,&p,i*28800,(i+1)*28800,20+k);
            arp_runtime_stop(&r);assert(refs==1);
            if(!c[k].count){   /* CYCLE: only the root and the octave below */
                unsigned int low=0,high=0;
                for(j=0;j<nq;j++){assert(queued[j]==60 || queued[j]==48);low|=queued[j]==48;high|=queued[j]==60;}
                assert(nq==c[k].slots && low && high);continue;
            }
            assert(nq==c[k].count);
            for(j=0;j<nq;j++)assert(queued[j]==c[k].notes[j]);
        }
        for(i=0;i<8;i++)assert(!used[i]);
        p.mode=2;p.range=1;
    }
    printf("PASS: falling RANGE -2..-4 in TRUE/UP/DOWN/CYCLE, rising 1..4 unchanged, shown order -4..4\n");
    /* List item 28: SCALE snaps each output to the nearest note of the
     * pattern's scale (the lower one on a tie); no scale, no change. */
    {
        static const unsigned int major=0xab5; /* 0,2,4,5,7,9,11 */
        static const unsigned int pentatonic_minor=0x4a9; /* 0,3,5,7,10 */
        unsigned int root,n,k,cases=0;int note,got,d,best;
        assert(arp_scale_snap(61,0)==61 && arp_scale_snap(-3,major)==-3);
        assert(arp_scale_snap(61,major)==60 && arp_scale_snap(66,major)==65 && arp_scale_snap(70,major)==69);
        assert(arp_scale_snap(61,major|(2u<<16))==61);           /* D major: C# is in it */
        assert(arp_scale_snap(60,major|(2u<<16))==59);           /* C -> B (tie B/C#: lower) */
        assert(arp_scale_snap(62,pentatonic_minor)==63 && arp_scale_snap(61,pentatonic_minor)==60);
        for(k=0;k<2;k++)for(root=0;root<12;root++)for(n=0;n<128;n++){
            unsigned int mask=k?pentatonic_minor:major,key=mask|(root<<16);
#define IN(x) (mask>>(unsigned int)((((x)-(int)root)%12+12)%12)&1u)
            got=arp_scale_snap((int)n,key);best=1000;
            for(note=(int)n-6;note<=(int)n+6;note++)if(IN(note)){d=note>(int)n?note-(int)n:(int)n-note;if(d<best)best=d;}
            d=got>(int)n?got-(int)n:(int)n-got;
            assert(d==best && IN(got));
            if(d && IN((int)n-d))assert(got==(int)n-d);   /* a tie goes down */
#undef IN
            cases++;
        }
        /* The runtime: a chromatic chord arpeggiated with SCALE on plays the
         * scale's notes; with it off, the chord as it is. */
        {int chromatic[2]={1,2};
        p.mode=2;p.range=0;p.note_length=0;p.length=15;p.enabled=0xffff;p.scale=1;e[2]=0;e[6]=60;e[12]=2700000u*3;
        arp_scale_key=major;nq=0;assert(arp_runtime_start(&r,&p,e,0,40,chromatic,2));
        for(i=0;i<4000 && r.source;i++)arp_runtime_frame(&r,&p,i*28800,(i+1)*28800,40);
        arp_runtime_stop(&r);assert(refs==1 && nq==3 && queued[0]==60 && queued[1]==60 && queued[2]==62);
        p.scale=0;nq=0;assert(arp_runtime_start(&r,&p,e,0,41,chromatic,2));
        for(i=0;i<4000 && r.source;i++)arp_runtime_frame(&r,&p,i*28800,(i+1)*28800,41);
        arp_runtime_stop(&r);assert(refs==1 && nq==3 && queued[0]==60 && queued[1]==61 && queued[2]==62);
        arp_scale_key=0;}
        printf("PASS: SCALE snaps to the nearest scale note (lower on a tie): %u cases, 128 notes x 12 roots x 2 scales; runtime outputs\n",cases);
    }
    printf("PASS: native-style lock refs, kind 1 outputs known once, chord order/timestamps, LEN/parent clipping/native-zero infinity/edit, generation, rests, allocation/queue rollback (%u releases)\n",nfree);
}
