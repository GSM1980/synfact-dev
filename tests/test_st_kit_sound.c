/* SPDX-License-Identifier: GPL-2.0-or-later
 * syntakt/kit.h (MD KIT 0.3, SY MnM 0.8.2): a track's machine type and stored
 * values from the playing Kit, which the instance limits now read instead of
 * the SRAM copies AUDIO was last sent. The playing-Kit pointer and the live
 * Kits are mapped on the host at their OS 1.42 addresses. No emulator or
 * device IO.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <syntakt/kit.h>
int main(void)
{
    volatile unsigned int *playing=(volatile unsigned int *)ST_KIT_PLAYING;
    unsigned char *kits=(unsigned char *)ST_KIT_BASE,*sound;unsigned int k=5,t;
    assert(VirtualAlloc((void *)0x43bd0000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x43bd0000u);
    assert(VirtualAlloc((void *)0x41ad0000u,0xc0000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x41ad0000u);
    memset(kits,0,ST_KIT_COUNT*ST_KIT_BYTES);
    /* No live Kit plays: nothing is read. */
    *playing=0;
    assert(!st_kit_sound(0) && st_kit_track_machine(0)==0xffu && !st_kit_track_value(0,0x36));
    *playing=ST_KIT_BASE+k*ST_KIT_BYTES+2;assert(!st_kit_sound(0));            /* not a Kit's start */
    *playing=ST_KIT_BASE+ST_KIT_COUNT*ST_KIT_BYTES;assert(!st_kit_sound(0));   /* past the 128 Kits */
    *playing=ST_KIT_BASE-ST_KIT_BYTES;assert(!st_kit_sound(0));
    *playing=ST_KIT_BASE+k*ST_KIT_BYTES;
    for(t=0;t<12;t++){
        sound=kits+k*ST_KIT_BYTES+0x2a+t*0x123;
        assert(st_kit_sound(t)==sound);
        sound[0xa2]=(unsigned char)(t<8?70+t%2:3);                             /* MD KIT / SY MnM, a stock one */
        *(unsigned short *)(sound+0x14+2*0x36)=(unsigned short)((t+1)<<8);    /* TYPE */
        *(unsigned short *)(sound+0x14+2*0x46)=0x1234;                         /* the last slot */
    }
    for(t=0;t<12;t++){
        assert(st_kit_track_machine(t)==(t<8?70+t%2:3u));
        assert(st_kit_track_value(t,0x36)>>8==t+1 && st_kit_track_value(t,0x46)==0x1234);
    }
    assert(!st_kit_sound(12) && st_kit_track_machine(12)==0xffu && !st_kit_track_value(3,0x47));
    /* Another pattern's Kit: the answer follows the pointer at once, as the UI does. */
    *playing=ST_KIT_BASE+(k+1)*ST_KIT_BYTES;
    assert(st_kit_track_machine(0)==0 && !st_kit_track_value(0,0x36));
    puts("PASS: a track's machine type (+A2h) and stored values (+14h + 2 * slot) from the playing Kit, nothing without a live Kit");
    return 0;
}
