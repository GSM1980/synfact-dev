/* SPDX-License-Identifier: GPL-2.0-or-later
 * Hook the five chorus-only entries (sites.s), all or none: every site's
 * bytes must be stock first. Task context, module init, before any view.
 */
typedef unsigned int u32;
typedef unsigned char u8;
extern void chorus_kit_entry(void),chorus_get_entry(void),chorus_set_entry(void),
    chorus_reverb_draw_entry(void),chorus_amp_entry(void);
extern u32 chorus_lock(void);
extern void chorus_unlock(u32),chorus_code16(u32,u32),chorus_sync(void);
static const struct {u32 at,bytes;u8 stock[10];void (*target)(void);} sites[]={
    {0x4000d870u,8,{0x2f,0x02,0x74,0x7c,0x22,0x2f,0x00,0x08},chorus_kit_entry},
    {0x4000d94au,10,{0x2f,0x02,0x22,0x2f,0x00,0x08,0x20,0x2f,0x00,0x0c},chorus_get_entry},
    {0x4000da32u,8,{0x2f,0x03,0x2f,0x02,0x22,0x2f,0x00,0x0c},chorus_set_entry},
    {0x40042ce0u,8,{0x4f,0xef,0xff,0xa0,0x48,0xd7,0x7c,0xfc},chorus_reverb_draw_entry},
    {0x40041e7cu,10,{0x12,0x3c,0x00,0x4a,0xb2,0x8e,0x67,0x44,0x60,0x5c},chorus_amp_entry}};
#define SITES (sizeof sites/sizeof sites[0])
/* Returns 0 when installed, else 2: a site's bytes are not stock. */
u32 chorus_install(void)
{
    u32 i,j,sr,target;
    for(i=0;i<SITES;i++)for(j=0;j<sites[i].bytes;j++)
        if(((volatile const u8 *)sites[i].at)[j]!=sites[i].stock[j])return 2;
    sr=chorus_lock();
    for(i=0;i<SITES;i++){
        target=(u32)sites[i].target;
        chorus_code16(sites[i].at+2,target>>16);chorus_code16(sites[i].at+4,target&0xffffu);
        for(j=6;j<sites[i].bytes;j+=2)chorus_code16(sites[i].at+j,0x4e71);
        chorus_code16(sites[i].at,0x4ef9);              /* entry word last */
    }
    chorus_sync();chorus_unlock(sr);
    return 0;
}
