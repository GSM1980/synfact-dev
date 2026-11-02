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
    struct arp_runtime r;struct arp_settings p={2,9,0,0,15,0xffff,{0}};
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
    printf("PASS: native-style lock refs, kind 1 outputs known once, chord order/timestamps, LEN/parent clipping/native-zero infinity/edit, generation, rests, allocation/queue rollback (%u releases)\n",nfree);
}
