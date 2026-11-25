/* SPDX-License-Identifier: GPL-2.0-or-later
 * Core 2.8: a pool slot's PCM for a machine's display (UI API 9): one level of
 * a canonical wavetable (wavetable_level) or a run of any sample's frames
 * (sample_read). UI task only: the PCM lives on AUDIO, so these read the
 * slot's file from eMMC, as the lazy waveform does (sample-wave.c), through
 * the same storage snapshot, and refuse when it changed while they read.
 */
#include "sample-pool.h"
#include <syntakt/machines.h>
#include "cold.h"
typedef pool_u8 u8;typedef pool_u32 u32;
extern void *fw_malloc(u32);extern void fw_free(void *);
extern int fw_mmc_read(u32,u32,void *);
static u32 be32(const u8 *p){return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
/* Bytes at..at+n of the slot's body (after its 64-byte header) into out:
 * every byte, or (frame > 2) the first two bytes of each frame-byte frame. */
static int read_body(u32 slot,const u32 *source,u32 at,u32 n,u8 *out,u32 frame)
{
    u32 check[5],done,chunk,skip,physical,i,k=0;u8 *buffer;
    buffer=fw_malloc(5120);if((u32)buffer<0x40000000u)return 0;
    if(fw_mmc_read(source[3],512,buffer) || be32(buffer)!=0x53414d50u ||
       be32(buffer+8)!=source[1]*source[2])goto failed;
    at+=64;
    for(done=0;done<n;done+=chunk){
        chunk=n-done;if(chunk>4096)chunk=4096;
        skip=(at+done)&511u;physical=(skip+chunk+511)&~511u;
        if(fw_mmc_read(source[3]+((at+done)>>9),physical,buffer))goto failed;
        if(frame<=2)for(i=0;i<chunk;i++)out[k++]=buffer[skip+i];
        else for(i=0;i<chunk;i+=frame){out[k++]=buffer[skip+i];out[k++]=buffer[skip+i+1];}
    }
    if(!core_pool_wave_storage(slot,check))goto failed;
    for(i=0;i<5;i++)if(check[i]!=source[i])goto failed;
    fw_free(buffer);return 1;
failed:fw_free(buffer);return 0;
}
/* The waves of slot's level (2048 >> level samples each, one after the
 * other, native 16-bit) into out, at most max waves: their count, or 0 for a
 * slot without a canonical wavetable, a busy pool, a short buffer or an IO
 * error (out is then undefined). No CRC: the boot or live load checked it. */
u32 core_pool_wavetable_level(u32 slot,u32 level,short *out,u32 max)
{
    u32 source[5],waves;
    if(level>=ST_WT_LEVELS || !out)return 0;
    waves=core_pool_sample_wavetable(slot);
    if(!waves || waves>max || !core_pool_wave_storage(slot,source) || !(source[0]&CORE_POOL_HZ))return 0;
    return read_body(slot,source,2*st_wt_offset(waves,level,0),2*waves*(ST_WT_WAVE>>level),(u8 *)out,2)?waves:0;
}
/* Frames first..first+count of any loaded slot (stock or HZFS) into out,
 * native 16-bit: a stereo sample's L. count 0 asks for the slot's frames (L,
 * R pairs for stereo). Returns the frames read (cut at the end), or 0. */
u32 core_pool_sample_read(u32 slot,u32 first,u32 count,short *out)
{
    u32 source[5];
    if(!core_pool_wave_storage(slot,source))return 0;
    if(!count)return source[1];
    if(!out || first>=source[1])return 0;
    if(count>source[1]-first)count=source[1]-first;
    return read_body(slot,source,2*source[2]*first,2*source[2]*count,(u8 *)out,2*source[2])?count:0;
}
