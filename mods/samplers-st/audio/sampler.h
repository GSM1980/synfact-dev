/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SP_SAMPLER_H
#define SP_SAMPLER_H
typedef unsigned int sp_u32;typedef int sp_s32;
/* channels: 2 for interleaved L, R (core 1.8); 0 or 1 is mono. frames counts
 * frames (pairs when stereo). */
struct sp_sample {sp_u32 frames;const short *pcm;sp_u32 channels;};
enum {SP_ONE,SP_WERP,SP_REPITCH,SP_SLICE};
struct sp_state {
    struct sp_sample source;
    sp_u32 kind,initialized,slot,active,play,wrapped;
    sp_u32 start,end,loop,frame,fraction,step,br;
    sp_u32 position,segment,phase,running;
    sp_u32 old_start,old_end,old_loop,old_play;
    unsigned short control[8];
};
sp_u32 sp_min(sp_u32,sp_u32);
sp_u32 sp_scale(sp_u32 frames,sp_u32 raw);
sp_u32 sp_point(const struct sp_sample *,sp_u32 point);
void sp_window(struct sp_state *,sp_u32 start,sp_u32 end,sp_u32 loop,sp_u32 play,int restart);
void sp_one_window(struct sp_state *,sp_u32 pitch,sp_u32 tempo,int trigger);
void sp_werp_window(struct sp_state *,sp_u32 pitch,sp_u32 tempo,int trigger);
void sp_repitch_window(struct sp_state *,sp_u32 pitch,sp_u32 tempo,int trigger);
void sp_slice_window(struct sp_state *,sp_u32 pitch,sp_u32 tempo,int trigger);
void sp_pcm(struct sp_state *,sp_s32 *out,sp_s32 *right);
void sp_update(sp_u32 kind,sp_u32 pitch,void *voice,const unsigned short *params);
void sp_render(void *out,void *voice);
#endif
