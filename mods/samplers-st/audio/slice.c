/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sampler.h"
void sp_slice_window(struct sp_state *s,sp_u32 pitch,sp_u32 tempo,int trigger)
{
    sp_u32 grid=sp_min(s->control[6]>>8,4),count=4u<<grid,which=s->control[4]>>8;
    sp_u32 length=sp_min(s->control[5]>>8,63)+1,first,last,start,end;
    (void)tempo;
    which=which?sp_min(which,count)-1:((pitch>>16)-12)&(count-1);
    first=which<<(6-grid);last=sp_min(first+(length<<(6-grid)),256);
    if(trigger || first!=s->old_start || last!=s->old_end){
        start=sp_point(&s->source,first);end=sp_point(&s->source,last);
        sp_window(s,start,end,start,s->control[1]>>8,trigger);
        s->old_start=first;s->old_end=last;
    }
}
void sp_slice_update(sp_u32 pitch,void *voice,const unsigned short *p){sp_update(SP_SLICE,pitch,voice,p);}
void sp_slice_render(void *out,void *voice){sp_render(out,voice);}
