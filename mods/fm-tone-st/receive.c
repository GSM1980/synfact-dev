/* SPDX-License-Identifier: GPL-2.0-or-later
 * Source-owned adapter for the generic 28-control MAIN/AUDIO transport.
 * Words: 0..15 = parameters 8..23 (fm_ext; parameter 23 is RATIO OFFSET C),
 * 16 = hidden placeholder, 17..19 = ratio offsets A/B1/B2. */
#include <syntakt/machines.h>
extern unsigned short fm_ext[],FM_OFFSETS[];
#ifndef FM_EXTENDED
#define FM_EXTENDED st_machine_extended
#endif
st_u32 fm_receive_controls(st_u32 track,const void *params)
{
    const unsigned short *p=FM_EXTENDED(params);st_u32 i,changed=0,v;
    if(track>=8 || !p)return 0;
    for(i=0;i<16;i++)if(fm_ext[16*track+i]!=p[i]){fm_ext[16*track+i]=p[i];changed=1;}
    for(i=0;i<4;i++){
        v=p[i?16+i:15]&0x7ffeu;
        if(FM_OFFSETS[4*track+i]!=v){FM_OFFSETS[4*track+i]=v;changed=1;}
    }
    return changed;
}
