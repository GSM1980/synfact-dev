/* SPDX-License-Identifier: GPL-2.0-or-later
 * Stereo probe, AUDIO side (test only): silent. The page reads counters only.
 */
typedef unsigned int u32;
void stereo_probe_update(u32 pitch,void *voice,const unsigned short *params)
{(void)pitch;(void)voice;(void)params;}
void stereo_probe_render(void *out,void *voice)
{u32 i,*o=out;(void)voice;for(i=0;i<32;i++)o[i]=0;}
