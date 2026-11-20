/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdint.h>
#include "fmplus/voice.h"
void portable_init(struct dm_fm_voice *);
void portable_trig(struct dm_fm_voice *,unsigned,const uint16_t *);
void portable_render(struct dm_fm_voice *,unsigned,const uint16_t *,int32_t,int16_t *);
static struct dm_fm_voice actual[8],expected[8];
static uint32_t seed;
static uint32_t next(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static int equal(const void *a,const void *b,unsigned n)
{const unsigned char *x=a,*y=b;for(unsigned i=0;i<n;i++)if(x[i]!=y[i])return 0;return 1;}
static void seed_mac(const uint32_t *v)
{
    __asm__ volatile("move.l #0,%%macsr\n\tmove.l %0,%%acc0\n\tmove.l %1,%%acc1\n\t"
                     "move.l %2,%%acc2\n\tmove.l %3,%%acc3"
                     ::"r"(v[1]),"r"(v[2]),"r"(v[3]),"r"(v[4]):"cc");
    __asm__ volatile("move.l %0,%%accext01\n\tmove.l %1,%%accext23\n\tmove.l %2,%%macsr"
                     ::"r"(v[5]),"r"(v[6]),"r"(v[0]):"cc");
}
static void read_mac(uint32_t *v)
{
    uint32_t sr,a,b,c,d,e,f;
    __asm__ volatile("move.l %%macsr,%0\n\tmove.l #0,%%macsr":"=r"(sr)::"cc");
    __asm__ volatile("move.l %%acc0,%0\n\tmove.l %%acc1,%1\n\tmove.l %%acc2,%2\n\tmove.l %%acc3,%3"
                     :"=r"(a),"=r"(b),"=r"(c),"=r"(d));
    __asm__ volatile("move.l %%accext01,%0\n\tmove.l %%accext23,%1":"=r"(e),"=r"(f));
    __asm__ volatile("move.l %0,%%macsr"::"r"(sr):"cc");
    v[0]=sr;v[1]=a;v[2]=b;v[3]=c;v[4]=d;v[5]=e;v[6]=f;
}
unsigned check_fm(unsigned blocks,unsigned initial)
{
    seed=initial;
    for(unsigned kind=9;kind<12;kind++){
        for(unsigned t=0;t<8;t++){dm_fm_init(actual+t);portable_init(expected+t);}
        for(unsigned i=0;i<blocks;i++)for(unsigned t=0;t<8;t++){
            uint16_t knobs[7];int16_t a[34],b[32];uint32_t before[7],after[7];
            for(unsigned k=0;k<7;k++)knobs[k]=(uint16_t)(next()%0x7f01);
            int32_t pitch=(int32_t)(next()%(142*64))-64;
            static const uint32_t modes[4]={0,0x20,0xa0,0xb0};before[0]=modes[i&3];
            for(unsigned n=1;n<7;n++)before[n]=next();
            seed_mac(before);read_mac(before);
            if(i%17==0){dm_fm_trig(actual+t,kind,knobs);portable_trig(expected+t,kind,knobs);}
            a[0]=1234;a[33]=-4321;
            dm_fm_render(actual+t,kind,knobs,pitch,a+1);
            portable_render(expected+t,kind,knobs,pitch,b);
            read_mac(after);if(!equal(before,after,sizeof(before)))return 0x20000000+kind*100000+i*100+t;
            if(a[0]!=1234 || a[33]!=-4321 || !equal(a+1,b,sizeof(b)))return kind*100000+i*100+t;
            if(!equal(actual+t,expected+t,sizeof(actual[0])))return 0x10000000+kind*100000+i*100+t;
        }
    }
    return 0;
}
