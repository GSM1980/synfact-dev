/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared MAIN native-bus mono audition primitive. Inverted Q31, stereo,
 * 32 frames; caller owns its IRQ PCM publication. No IO/heap/EMAC state.
 */
#ifndef ELEKLOADER_ST_MIX_H
#define ELEKLOADER_ST_MIX_H
static __inline__ void st_main_mix_frame(int *bus,unsigned int frame,int x)
{
    int y;unsigned int a,b,r;
    for(b=0;b<2;b++){
        y=bus[2*frame+b];a=(unsigned int)y;r=a-(unsigned int)x;
        if(((a^(unsigned int)x)&(a^r))&0x80000000u)r=y<0?0x80000000u:0x7fffffffu;
        bus[2*frame+b]=(int)r;
    }
}
/* Preview/monitor bus: 8000dfd0 reaches MAIN without the FX track's analog
 * block, unlike 8000dad0 (device report 2026-10-28; USB MAIN = 8 x (ADC sum
 * 8000ded0 + 8000dfd0), 8000dad0 absent). Its 4008eeca stage is x8 instead
 * of x10: x+x/4 keeps the previous digital level (10/32 for full PCM16). */
#define ST_PREVIEW_BUS ((int *)0x8000dfd0u)
static __inline__ void st_preview_mix_frame(int *bus,unsigned int frame,int x)
{
    st_main_mix_frame(bus,frame,x+(x>>2));
}
/* Core 1.2 mixer FX-send events (modules.h). Each block is 32 frames:
 * a track's dry PCM is 32 mono Q31 words, the buses 32 stereo frames.
 * ST_FX_SEND_BUS is the delay's input at ev_delay_in, then the reverb's at
 * ev_reverb_in. ST_FX_TRACK_BUS is the inverted-Q31 bus 4008fa8c sums the
 * digital tracks into before the FX track's analog block (the preview note
 * above); the chorus adds its VOL return there at ev_main_mix. */
#define ST_TRACK_PCM(t) ((const int *)((t)<8?0x80003d50u+128u*(t):0x80003950u+128u*((t)-8)))
/* ev_track_send's table, once a block: each track's level x pan gains (native
 * inverted Q31) and 142-byte mixer record, then the native send-mute bits
 * (bit t set: track t's sends are muted). Borrowed for the call only. */
struct st_track_sends {
    struct {int left,right;const unsigned char *mix;unsigned int reserved;} track[12];
    unsigned int muted;
};
#define ST_FX_SEND_BUS ((int *)0x8000dbd0)
#define ST_FX_TRACK_BUS ((int *)0x8000dad0)
#endif
