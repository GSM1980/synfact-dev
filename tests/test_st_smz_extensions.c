/* SPDX-License-Identifier: GPL-2.0-or-later; actual parser, no code execution. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mods/core-st/smz.h"
static void put(unsigned char *p,unsigned n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static void sum(unsigned char *b,unsigned size,const struct core_smz_view *v)
{unsigned i,s=0;for(i=80;i<v->image+v->bytes;i+=4)s+=core_smz_be(b+i);put(b+68,s);put(b+16,core_smz_adler(1,b+20,size-20));}
int main(int argc,char **argv)
{
    unsigned k,n,at;unsigned char *b,*bad;FILE *f;struct core_smz_reader r;struct core_smz_view v;
    assert(argc>=2);
    for(k=1;k<(unsigned)argc;k++){
        f=fopen(argv[k],"rb");assert(f);fseek(f,0,SEEK_END);n=ftell(f);rewind(f);b=malloc(n);assert(fread(b,1,n,f)==n);fclose(f);
        r=(struct core_smz_reader){b,core_smz_memory_read,n};assert(core_smz_validate(&r,&v));bad=malloc(n);r.context=bad;
        if(v.ui!=CORE_SMZ_NONE){
            at=v.image+v.ui+4;memcpy(bad,b,n);put(bad+at,5);sum(bad,n,&v);assert(!core_smz_validate(&r,&v));
            memcpy(bad,b,n);put(bad+at,0);sum(bad,n,&v);assert(!core_smz_validate(&r,&v));
            memcpy(bad,b,n);at=v.image+core_smz_be(b+v.image+v.ui+8);put(bad+at,32);sum(bad,n,&v);assert(!core_smz_validate(&r,&v));
        }
        if(v.nmods){
            memcpy(bad,b,n);at=v.image+v.modules+12;put(bad+at,17);sum(bad,n,&v);assert(!core_smz_validate(&r,&v));
            memcpy(bad,b,n);at=core_smz_be(b+v.image+v.modules+8);
            if(at==CORE_SMZ_NONE)put(bad+v.image+v.modules+8,0); /* init-only module */
            else put(bad+v.image+at,12);
            sum(bad,n,&v);assert(!core_smz_validate(&r,&v));
            if(at!=CORE_SMZ_NONE){
                unsigned invalid[]={0x100,0x104,0x10b,0x201,0x301,0x303,0x30b,0xffffffffu};
                for(unsigned q=0;q<sizeof(invalid)/sizeof(invalid[0]);q++){
                    memcpy(bad,b,n);put(bad+v.image+at,invalid[q]);sum(bad,n,&v);
                    assert(!core_smz_validate(&r,&v));
                }
                for(unsigned event=0x101;event<=0x103;event++){
                    memcpy(bad,b,n);put(bad+v.image+at,event);sum(bad,n,&v);
                    assert(core_smz_validate(&r,&v));
                }
                for(unsigned q=0;q<2;q++){
                    memcpy(bad,b,n);put(bad+v.image+at,q?0x302:11);sum(bad,n,&v);
                    assert(core_smz_validate(&r,&v));
                }
            }
        }
        free(bad);free(b);
    }
    puts("PASS: legacy banks, UI layouts/callbacks, modules-only/mixed banks and checksummed invalid metadata");return 0;
}
