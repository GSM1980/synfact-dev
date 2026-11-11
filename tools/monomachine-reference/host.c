/* SPDX-License-Identifier: GPL-3.0-only
 * Host access to the actual bank generators. Output is raw OSC, before the
 * native Syntakt declick/gain/FLTR/AMP/FX chain. No synthetic AMP is added.
 */
#include "types.h"
struct compare_voice {struct mono_voice old;struct dm_m2k_voice m2k;};
unsigned dm_compare_bytes(void){return sizeof(struct compare_voice);}
void dm_compare_init(struct compare_voice *v){mono_init(&v->old);dm_m2k_init(&v->m2k);}
void dm_compare_block(struct compare_voice *v,unsigned type,int pitch_q7,const uint16_t *p,
                      int previous_q7,unsigned trigger,float *out)
{
    int16_t pcm[32];uint8_t old[7];
    for(unsigned k=0;k<7;k++)old[k]=(uint8_t)(p[k]>>8);
    if(type<DM_SID){
        if(trigger)mono_trig(&v->old,dm_engine_type[type]);
        mono_render(&v->old,dm_engine_type[type],old,mono_pitch_inc(pitch_q7),pcm,32);
    }else{
        if(trigger)dm_m2k_trig(&v->m2k,type,p);
        dm_m2k_render(&v->m2k,type,p,pitch_q7>>1,previous_q7>>1,pcm);
    }
    for(unsigned k=0;k<32;k++)out[k]=(float)pcm[k]/32768.0f;
}
