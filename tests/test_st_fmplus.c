/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Split ColdFire multiplication versus native wide arithmetic; same public
 * voice API over changing controls, retriggers, rates and interleaved voices. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../mods/digimono-st/fmplus/voice.h"
void native_init(struct dm_fm_voice *);
void native_trig(struct dm_fm_voice *,unsigned,const uint16_t *);
void native_render(struct dm_fm_voice *,unsigned,const uint16_t *,int32_t,int16_t *);
static uint32_t seed=0x65815630;
static uint32_t next(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void)
{
    struct dm_fm_voice actual[8],reference[8];unsigned blocks=0;
    for(unsigned i=0;i<8;i++){dm_fm_init(actual+i);native_init(reference+i);}
    for(unsigned kind=9;kind<=11;kind++)for(unsigned t=0;t<8;t++){
        uint16_t p[7];int16_t a[34],b[32];
        dm_fm_init(actual+t);native_init(reference+t);
        for(unsigned block=0;block<512;block++){
            if(block%8==0)for(unsigned k=0;k<7;k++)p[k]=(uint16_t)(next()%0x7f01);
            if(block%17==0){dm_fm_trig(actual+t,kind,p);native_trig(reference+t,kind,p);}
            int pitch=(int)(next()%(142*64))-64;
            a[0]=1234;a[33]=-4321;
            dm_fm_render(actual+t,kind,p,pitch,a+1);native_render(reference+t,kind,p,pitch,b);
            assert(a[0]==1234 && a[33]==-4321 && !memcmp(a+1,b,sizeof b));
            assert(!memcmp(actual+t,reference+t,sizeof(*actual)));blocks++;
        }
    }
    puts("PASS: 12288 FM+ split/native-wide blocks, 3 engines/8 independent voices, full fractional controls, pitch bounds, retriggers, state and buffer guards");
    return blocks==12288?0:1;
}
