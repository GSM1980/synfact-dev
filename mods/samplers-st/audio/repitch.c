/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sampler.h"
void sp_repitch_window(struct sp_state *s,sp_u32 pitch,sp_u32 tempo,int trigger)
{
    sp_u32 start=sp_scale(s->source.frames,s->control[4]);
    sp_u32 end=sp_min(start+sp_scale(s->source.frames,s->control[5]),s->source.frames);
    sp_u32 bars=sp_min(s->control[6]>>8,3),ratio=(tempo<<16)/(337500u<<bars),width=end-start,step;
    (void)pitch;
    if(trigger || start!=s->old_start || end!=s->old_end){
        sp_window(s,start,end,start,trigger?s->control[1]>>8:s->play,trigger);
        s->old_start=start;s->old_end=end;
    }
    if(ratio && width>0xffffffffu/ratio)step=0x4000;
    else step=(width*ratio)>>16;
    if(step)s->step=sp_min(step,0x4000);
}
void sp_repitch_update(sp_u32 pitch,void *voice,const unsigned short *p){sp_update(SP_REPITCH,pitch,voice,p);}
void sp_repitch_render(void *out,void *voice){sp_render(out,voice);}
