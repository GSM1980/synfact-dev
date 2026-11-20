/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual ENS adapter; native services expose stateful ramp boundaries.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned voices[8][0x708/4],enabled=1,idle,stage,claimed;
static int rbuf[8][32];
static unsigned short extended[24]={4<<8};
static unsigned ramp,start[2];
static int *claim(unsigned t){claimed++;return enabled?rbuf[t]:0;}
static void update(unsigned pitch,void *v,const void *p){(void)pitch;(void)v;(void)p;}
static void declick(void *v){(void)v;assert(!stage);stage=1;}
static void gain(void *a,void *voice){
    (void)a;unsigned *v=voice;
    assert(stage==1 || stage==2);
    if(stage==1)ramp=v[0x3d4/4];else assert(v[0x3d4/4]==ramp);
    v[0x3d4/4]++;stage++;
}
static void mix(void *a,void *b,void *o,void *l,void *voice){
    (void)b;(void)l;unsigned *v=voice;
    assert(stage>=2 && stage<=4);
    if(stage!=4){start[0]=v[6];start[1]=v[7];}
    else assert(v[6]==start[0] && v[7]==start[1]);
    v[6]++;v[7]++;memcpy(o,a,128);
    stage=stage==3?4:0;
}
#define DM_STOCK_BOUNDARIES
#define DM_TRACK_BASE ((uintptr_t)voices)
#define DM_EXTENDED(p) ((void)(p),extended)
#define DM_NATIVE_UPDATE update
#define DM_DECLICK declick
#define DM_GAIN gain
#define DM_MIX mix
#define DM_AMP_IDLE(t) ((void)(t),idle)
#define DM_STEREO_CLAIM claim
#define DM_STEREO_PREVIOUS(t) rbuf[t]
#include "../mods/digimono-st/audio.c"
int main(void){
    const unsigned slots[8]={0x12,0x13,0x14,0x15,0x16,0x17,0x1a,0x19};
    unsigned short p[71]={0},knobs[8]={64,63,63,63,0,64,127,127};
    int out[32]={0};int16_t l[32],r[32];struct dm_ens_voice ref;
    for(unsigned i=0;i<8;i++)p[slots[i]]=knobs[i]<<8;
    dm_ens_init(&ref);
    for(unsigned b=0;b<256;b++){
        voices[0][0x38/4]=b==0 || b==128;
        if(voices[0][0x38/4])dm_ens_trig(&ref);
        dm_update(60<<16,voices[0],p);
        unsigned before=voices[0][0x3d4/4];
        dm_render(out,voices[0]);assert(stage==0 && claimed==b+1);
        assert(voices[0][0x3d4/4]==before+1);
        dm_ens_render(&ref,dm_tracks[0].m2p,60*64,l,r);
        for(unsigned i=0;i<32;i++){
            assert(out[i]==l[i]*65536 && rbuf[0][i]==r[i]*65536);
        }
    }
    /* Width and TYPE changes continue publishing the R tail while active. */
    p[slots[7]]=0;voices[0][0x38/4]=0;
    dm_update(60<<16,voices[0],p);dm_render(out,voices[0]);assert(!memcmp(out,rbuf[0],128));
    extended[0]=2<<8;voices[0][0x38/4]=1;
    dm_update(60<<16,voices[0],p);dm_render(out,voices[0]);assert(!memcmp(out,rbuf[0],128));
    /* AMP sleep feeds silence until filter tails end, then stops claiming. */
    idle=1;voices[0][0x38/4]=0;
    for(unsigned b=0;b<32;b++){dm_update(60<<16,voices[0],p);dm_render(out,voices[0]);}
    assert(!dm_tracks[0].live && !memcmp(out,rbuf[0],128));
    unsigned before=claimed;dm_render(out,voices[0]);assert(claimed==before && !dm_tracks[0].stereo_tail);
    /* An unavailable service folds both channels into a centered mono signal. */
    enabled=0;idle=0;extended[0]=4<<8;p[slots[7]]=127<<8;voices[0][0x38/4]=1;
    dm_update(60<<16,voices[0],p);dm_render(out,voices[0]);assert(!dm_tracks[0].stereo_tail && !stage);
    puts("PASS: ENS stereo PCM/ramp state, retriggers, width/TYPE transitions, AMP tail and service fallback");
}
