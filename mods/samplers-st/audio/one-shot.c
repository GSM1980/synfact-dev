/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sampler.h"
void sp_one_window(struct sp_state *s,sp_u32 pitch,sp_u32 tempo,int trigger)
{
    sp_u32 start=sp_scale(s->source.frames,s->control[4]);
    sp_u32 end=start+sp_scale(s->source.frames,s->control[5]);
    sp_u32 loop=sp_scale(s->source.frames,s->control[6]?s->control[6]-1:s->control[4]);
    sp_u32 play=trigger?s->control[1]>>8:s->play;(void)pitch;(void)tempo;
    end=sp_min(end,s->source.frames);
    if(trigger || start!=s->old_start || end!=s->old_end || loop!=s->old_loop){
        sp_window(s,start,end,loop,play,trigger);
        s->old_start=start;s->old_end=end;s->old_loop=loop;
    }
}
void sp_one_shot_update(sp_u32 pitch,void *voice,const unsigned short *p){sp_update(SP_ONE,pitch,voice,p);}
void sp_one_shot_render(void *out,void *voice){sp_render(out,voice);}
