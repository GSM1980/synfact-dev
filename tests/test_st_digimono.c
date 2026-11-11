/* SPDX-License-Identifier: GPL-2.0-or-later
 * Run the real adapter and DSP. Only native firmware services are doubles.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syntakt/machines.h>

static unsigned fixture[8][0x708/4];
static unsigned short ext[8][24];
static unsigned ext_present=1,amp_idle;
static unsigned native_calls,chain_calls;
static const unsigned char slots[8]={0x12,0x13,0x14,0x15,0x16,0x17,0x1a,0x19};
static unsigned short input[8][71];
static const unsigned short *extended(const void *p)
{
    for(unsigned t=0;t<8;t++)if(p==input[t])return ext_present?ext[t]:0;
    assert(0);return 0;
}
static void native_update(unsigned pitch,void *voice,const void *p)
{
    const unsigned short *n=p;
    unsigned t=((unsigned *)voice-fixture[0])/(0x708/4);
    assert(pitch==60u<<16);
    assert(n[0x12]==0x4000 && n[0x13]==0x4000 && n[0x14]==0x4000);
    assert(n[0x15]==0 && n[0x16]==0x4000 && n[0x17]==0x4000);
    assert(n[0x18]==0 && n[0x19]==0x7f00);
    assert(n[0x2a]==input[t][0x2a]);
    native_calls++;
}
static void declick(void *voice){(void)voice;assert(chain_calls%3==0);chain_calls++;}
static void gain(void *a,void *voice){(void)a;(void)voice;assert(chain_calls%3==1);chain_calls++;}
static void mix(void *a,void *b,void *o,void *locals,void *voice)
{
    int *l=locals;int32_t *silence=b;
    assert(chain_calls%3==2);chain_calls++;
    assert(l[0]==0 && l[1]==1);
    for(unsigned i=0;i<32;i++)assert(!silence[i]);
    assert(((unsigned *)voice)[2]==0x3fffffffu && !((unsigned *)voice)[3]);
    memcpy(o,a,128);
}
#define DM_STOCK_BOUNDARIES
#define DM_TRACK_BASE ((uintptr_t)fixture)
#define DM_EXTENDED extended
#define DM_NATIVE_UPDATE native_update
#define DM_DECLICK declick
#define DM_GAIN gain
#define DM_MIX mix
#define DM_AMP_IDLE(t) ((void)(t),amp_idle)
#include "../mods/digimono-st/audio.c"

static st_u32 get_value(void *view,st_u32 id,unsigned char *locked)
{assert(id==DM_ENGINE+8);*locked=0;return *(unsigned *)view;}
#define DM_VALUE get_value
#include "../mods/digimono-st/main.c"

static const uint8_t patches[DM_TYPES][8]=DM_INIT_PATCHES;
static void prepare(unsigned t,unsigned kind)
{
    memset(fixture[t],0,sizeof fixture[t]);memset(input[t],0,sizeof input[t]);
    for(unsigned i=0;i<8;i++)input[t][slots[i]]=(unsigned short)(patches[kind][i]<<8);
    input[t][0x2a]=0x1234;ext[t][0]=(unsigned short)(kind<<8);
    fixture[t][0x56c/4]=0x12345678;
}
static void block(unsigned t,unsigned trig,unsigned note,int32_t *out)
{
    unsigned short saved[71];memcpy(saved,input[t],sizeof saved);
    fixture[t][0x38/4]=trig;
    dm_update(note<<16,fixture[t],input[t]);dm_render(out,fixture[t]);
    assert(!memcmp(saved,input[t],sizeof saved));
    assert(fixture[t][0x56c/4]==0x12345678);
    assert(!fixture[t][0x6e8/4] && !fixture[t][0x6d4/4] && !fixture[t][0x6d8/4]);
}
static void silent(const int32_t *out){for(unsigned i=0;i<32;i++)assert(!out[i]);}
static uint32_t rng=0x1984;
static uint32_t random_word(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}

int main(void)
{
    int32_t out[34],other[32];int16_t expected[32];unsigned cases=0;
    assert(dm_monomachine.type==71 && dm_monomachine.params==9);
    assert(dm_ui.type==71 && dm_ui.pages==2 && dm_layout[8]==8);
    for(unsigned kind=0;kind<DM_TYPES;kind++) {
        unsigned raw=kind<<8;
        dm_idle(0,&dm_monomachine,&raw,0);
        assert(!strcmp(dm_format(0,0,8,raw),dm_names[kind]));
        assert(dm_layout[0]==0 && !strcmp(dm_label[0],"TUNE"));
        for(unsigned i=1;i<8;i++) {
            assert(dm_layout[i]==(dm_labels[kind][i][0]?i:0xffffffffu));
            assert(dm_label[i][0]);
            if(dm_labels[kind][i][0])assert(!strcmp(dm_label[i],dm_labels[kind][i]));
            assert(dm_format(0,0,i,0xffff));
        }
        assert(dm_engine_type[kind]!=MONO_EXT7);
        for(unsigned trial=0;trial<12;trial++) {
            unsigned note=trial?random_word()%128:60;
            struct mono_voice ref;struct dm_m2k_voice mref;uint8_t p[7];uint16_t p8[7];
            prepare(0,kind);
            if(trial)for(unsigned i=1;i<8;i++)input[0][slots[i]]=(unsigned short)(random_word()&0xffff);
            mono_init(&ref);dm_m2k_init(&mref);
            for(unsigned i=0;i<7;i++) {
                unsigned k=input[0][slots[i+1]]>>8;p[i]=(uint8_t)(k>127?127:k);
                p8[i]=input[0][slots[i+1]]>0x7f00?0x7f00:input[0][slots[i+1]];
            }
            if(kind<DM_SID)mono_trig(&ref,dm_engine_type[kind]);else dm_m2k_trig(&mref,kind,p8);
            for(unsigned b=0;b<10;b++) {
                out[0]=123;out[33]=456;block(0,b==0,note,out+1);
                if(kind<DM_SID)mono_render(&ref,dm_engine_type[kind],p,mono_pitch_inc((int)note*128),expected,32);
                else dm_m2k_render(&mref,kind,p8,(int)note*64,dm_previous_pitch(&dm_tracks[0],0),expected);
                for(unsigned i=0;i<32;i++)assert(out[i+1]==(int32_t)expected[i]*65536);
                assert(out[0]==123 && out[33]==456);
                if(kind<DM_SID)assert(!memcmp(&ref,&dm_tracks[0].engine,sizeof ref));
                else assert(!memcmp(&mref,&dm_tracks[0].m2k,sizeof mref));
                cases++;
            }
        }
    }
    {unsigned raw=7<<8;dm_idle(0,0,&raw,1);
     for(unsigned v=0;v<128;v++)assert(!strcmp(dm_format(0,0,1,v<<8),mono_chord_name[mono_chord_type((int)v)]));}
    /* No trigger, missing TYPE, then a note-latched switch and null transport. */
    prepare(0,0);ext_present=0;block(0,0,60,out);silent(out);
    assert(dm_tracks[0].loaded==DM_DEFAULT_TYPE+1);
    block(0,1,60,out);assert(dm_tracks[0].live);
    ext_present=1;ext[0][0]=7<<8;block(0,0,60,out);
    assert(dm_tracks[0].loaded==DM_DEFAULT_TYPE+1);
    block(0,1,60,out);assert(dm_tracks[0].loaded==8);
    ext_present=0;block(0,1,60,out);assert(dm_tracks[0].loaded==8);
    /* Another backend invalidates +6f8: no stale oscillator on re-entry. */
    fixture[0][0x6f8/4]=0;block(0,0,60,out);silent(out);
    assert(dm_tracks[0].loaded==DM_DEFAULT_TYPE+1);ext_present=1;
    for(unsigned t=0;t<8;t++){prepare(t,t);block(t,1,48+t,out);}
    {struct dm_track saved=dm_tracks[1];
     ext[0][0]=5<<8;block(0,1,60,out);ext[0][0]=0;block(0,1,60,out);
     assert(!memcmp(&saved,&dm_tracks[1],sizeof saved));}
    /* Every new backend enters/leaves through both legacy and new TYPEs. */
    for(unsigned step=0;step<DM_TYPES;step++)for(unsigned t=0;t<8;t++){
        unsigned kind=(step+t)%DM_TYPES;
        for(unsigned i=0;i<8;i++)input[t][slots[i]]=patches[kind][i]<<8;
        ext[t][0]=kind<<8;block(t,1,48+t,out);
        assert(dm_tracks[t].loaded==kind+1);
    }
    /* SID PRCH receives the previous digital track's pitch, wrapping 1->8. */
    for(unsigned target=0;target<2;target++){
        unsigned prev=target?0:7;
        prepare(prev,0);block(prev,1,72,other);
        prepare(target,DM_SID);input[target][slots[5]]=64<<8;input[target][slots[6]]=127<<8;
        struct dm_m2k_voice ref;uint16_t p8[7];
        for(unsigned i=0;i<7;i++)p8[i]=input[target][slots[i+1]];
        dm_m2k_init(&ref);dm_m2k_trig(&ref,DM_SID,p8);
        dm_m2k_render(&ref,DM_SID,p8,60*64,72*64,expected);
        block(target,1,60,out);
        for(unsigned i=0;i<32;i++)assert(out[i]==(int32_t)expected[i]*65536);
        fixture[prev][0x6f8/4]=0;
        assert(dm_previous_pitch(&dm_tracks[target],target)==48*64+((p8[6]*24)>>8));
    }
    prepare(0,0);input[0][slots[0]]=0x4c00;block(0,1,60,out);
    assert(dm_tracks[0].increment==mono_pitch_inc(72*128));
    input[0][slots[0]]=0x3400;block(0,0,60,out);assert(dm_tracks[0].increment==mono_pitch_inc(48*128));
    input[0][slots[0]]=0x4080;block(0,0,60,out);assert(dm_tracks[0].increment==mono_pitch_inc(60*128+64));
    amp_idle=1;for(unsigned b=0;b<32;b++)block(0,0,60,out);silent(out);
    assert(!dm_tracks[0].live && dm_tracks[1].live);
    block(0,1,60,out);assert(dm_tracks[0].live);amp_idle=0;block(0,0,60,other);
    memset(out,1,128);dm_update(60<<16,(unsigned char *)fixture+4,input[0]);
    dm_render(out,(unsigned char *)fixture+4);silent(out);
    assert(dm_eco==0 && native_calls*3==chain_calls);
    printf("PASS: %u exact adapter/DSP blocks, 12 types/8 tracks, UI/chords, TYPE transport, reset, pitch, SID PRCH, AMP sleep and native call boundaries\n",cases);
    return 0;
}
