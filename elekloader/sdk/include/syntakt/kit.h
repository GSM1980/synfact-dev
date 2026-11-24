/* SPDX-License-Identifier: GPL-2.0-or-later
 * The playing Kit's Sounds on MAIN (OS 1.42), for a machine's UI that needs
 * another track's machine or stored value now: what the UI and the sequencer
 * use, not the copies AUDIO was last sent.
 *
 * The SRAM copies (80002f50 + t, a track's machine type; 800021cc + t * 8Eh,
 * its Sound's values) are written only when the native publish (400a2e1a,
 * {sound, track}: it copies the Sound's +14h, 8Eh bytes, then 400a2d3c the
 * machine byte) sends a track's Sound to AUDIO, at its trig. After a
 * pattern change, or a track cleared or given another machine, they keep the
 * old Sound until that track plays. The Kit has the new one at once.
 *
 * The playing Kit is *43bd843c (as playhead-st and arp-st read it), one of
 * the 128 live Kits at 41ad6134 + k * 1618h; track t's Sound is at
 * +2Ah + t * 123h. In a Sound, the stored values are halfwords at +14h + 2 *
 * slot (the slots core_machine_defaults writes; 400a2e1a copies them to
 * 800021cc + t * 8Eh) and the machine type is the byte at +A2h (400a2d3c
 * copies it to 80002f50 + t).
 * Read only, UI task or MAIN; no IO, no heap.
 */
#ifndef SYNTAKT_KIT_H
#define SYNTAKT_KIT_H
#define ST_KIT_PLAYING 0x43bd843cu
#define ST_KIT_BASE 0x41ad6134u
#define ST_KIT_BYTES 0x1618u
#define ST_KIT_COUNT 128u
#define ST_KIT_SOUND_AT 0x2au
#define ST_KIT_SOUND_BYTES 0x123u
#define ST_SOUND_VALUES 0x14u
#define ST_SOUND_MACHINE 0xa2u
/* Track t's (0..11) Sound in the playing Kit, or 0 when no live Kit plays. */
static __inline__ const volatile unsigned char *st_kit_sound(unsigned int t)
{
    const unsigned int kit=*(const volatile unsigned int *)ST_KIT_PLAYING;
    if(t>=12u || kit<ST_KIT_BASE || kit>=ST_KIT_BASE+ST_KIT_COUNT*ST_KIT_BYTES || (kit-ST_KIT_BASE)%ST_KIT_BYTES)return 0;
    return (const volatile unsigned char *)(kit+ST_KIT_SOUND_AT+t*ST_KIT_SOUND_BYTES);
}
/* Its machine type (stock < 64, a bank's machine 64..79), or 0xff without a Kit. */
static __inline__ unsigned int st_kit_track_machine(unsigned int t)
{
    const volatile unsigned char *s=st_kit_sound(t);
    return s?s[ST_SOUND_MACHINE]:0xffu;
}
/* Its stored value in a Sound slot (0..70), or 0 without a Kit. */
static __inline__ unsigned int st_kit_track_value(unsigned int t,unsigned int slot)
{
    const volatile unsigned char *s=st_kit_sound(t);
    return s && slot<0x47u?*(const volatile unsigned short *)(s+ST_SOUND_VALUES+2u*slot):0;
}
#endif
