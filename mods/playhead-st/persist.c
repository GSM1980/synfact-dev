/* SPDX-License-Identifier: GPL-2.0-or-later
 * The modes saved with each pattern's kit.
 *
 * A pattern's kit is encoded into a 1600h-byte slot of the project (and of
 * the native save and reload buffers); the native codec uses [0, 14d2h) and
 * LFO3 [14e0h, 15d0h). The modes go in the slot's free tail at 15e0h
 * (persist.h), which the stock firmware never reads: a project saved here
 * opens on a stock unit, and a project without the record opens with every
 * track FORWARD.
 *
 * The kit codec, copies and lifecycle reach this module as the Sound
 * metadata notifications that LFO3's established object/codec adapter
 * publishes on the core's native bus (<syntakt/sound-meta.h>, host API 10);
 * no firmware site is hooked here. Without LFO3 in the bank nothing arrives
 * and the modes last until a restart.
 *
 *   KIT_ENCODE (slot, kit, container)  a known slot gets the kit's modes
 *   KIT_DECODE (kit, slot, container)  the kit gets the slot's (none: FORWARD)
 *   COPY / RELOCATE (dst, src, 1618h)  a kit copied: its modes go with it
 *   NEW (kit, 0, 1618h)                a kit initialised: FORWARD
 *   FREE (base, 0, bytes)              forget kits that were there
 *   PROJECT_NEW (container)            clear the records of its 128 slots
 *
 * A known slot is one by construction, as LFO3's kit_slot(): the two
 * project containers and the event's own container (+2e0200h + k * 1600h,
 * k < 128) and the native save and reload buffers; a SysEx export buffer of
 * 14d2h bytes is never written past the native record. Kits outside the
 * live array (undo, clipboard, decode buffers) keep their modes in a small
 * table keyed by address and checked against a hash of the kit's bytes, so
 * a stale entry is never used.
 */
#include <syntakt/modules.h>
#include <syntakt/native.h>
#include <syntakt/sound-meta.h>
#include "persist.h"
#include "seq-direction.h"
typedef unsigned int u32;typedef unsigned char u8;
#define KIT_SLOT 0x1600u
#define KIT_SLOTS 0x2e0200u
#define SAVE_BUFFER 0x40600554u
#define RELOAD_BUFFER 0x405fe354u
#define SIDE 16u
static u32 mutex[4];                     /* the native recursive task mutex, as LFO3's and ARP's */
void playhead_enter(void){((void (*)(void *))0x40001980u)(mutex);}
void playhead_leave(void){((void (*)(void *))0x40001aa2u)(mutex);}
static struct {u32 address,hash;u8 modes[PLAYHEAD_TRACKS];} side[SIDE];
static u32 side_next;
static const struct st_persistence_api *persistence;
static u32 fnv(const volatile u8 *p,u32 n)
{u32 h=0x811c9dc5u;while(n--)h=(h^*p++)*0x1000193u;return h;}
static u32 kit_hash(u32 kit){return fnv((const volatile u8 *)kit,PLAYHEAD_KIT_BYTES);}
static u32 be32(const volatile u8 *p){return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static void put32(volatile u8 *p,u32 v){p[0]=(u8)(v>>24);p[1]=(u8)(v>>16);p[2]=(u8)(v>>8);p[3]=(u8)v;}
static u32 in_container(u32 a,u32 base)
{
    u32 off=a-(base+KIT_SLOTS);
    return base && a>=base+KIT_SLOTS && off<PLAYHEAD_KITS*KIT_SLOT && off%KIT_SLOT==0;
}
static u32 known_slot(u32 a,u32 container)
{
    return a && ((persistence && persistence->kit_slot(a)==KIT_SLOT) || a==SAVE_BUFFER || a==RELOAD_BUFFER || in_container(a,0x43c0dfc0u) ||
                 in_container(a,0x43ff45d0u) || in_container(a,container));
}
/* A kit's modes: the live table, the side table, or all FORWARD. */
static void modes_of(u32 kit,u8 *out)
{
    u32 k=playhead_kit_index(kit),t,i;
    for(t=0;t<PLAYHEAD_TRACKS;t++)out[t]=0;
    if(k<PLAYHEAD_KITS){for(t=0;t<PLAYHEAD_TRACKS;t++)out[t]=playhead_modes[k][t];return;}
    for(i=0;i<SIDE;i++)if(side[i].address==kit){
        if(side[i].hash==kit_hash(kit))for(t=0;t<PLAYHEAD_TRACKS;t++)out[t]=side[i].modes[t];
        else side[i].address=0;
        return;
    }
}
static void forget(u32 begin,u32 bytes)
{u32 i;for(i=0;i<SIDE;i++)if(side[i].address-begin<bytes)side[i].address=0;}
static void store(u32 kit,const u8 *modes)
{
    u32 k=playhead_kit_index(kit),t,i,any=0;
    if(k<PLAYHEAD_KITS){playhead_seq_load(k,modes);return;}
    forget(kit,1);
    for(t=0;t<PLAYHEAD_TRACKS;t++)any|=modes[t];
    if(!any || !kit)return;                 /* no entry: FORWARD */
    i=side_next++%SIDE;
    side[i].address=kit;side[i].hash=kit_hash(kit);
    for(t=0;t<PLAYHEAD_TRACKS;t++)side[i].modes[t]=modes[t];
}
static void encode(u32 slot,u32 kit)
{
    volatile u8 *r=(volatile u8 *)(slot+PLAYHEAD_RECORD);u8 m[PLAYHEAD_TRACKS];u32 t,any=0;
    modes_of(kit,m);
    for(t=0;t<PLAYHEAD_TRACKS;t++)any|=m[t];
    for(t=0;t<PLAYHEAD_RECORD_BYTES;t++)r[t]=0;
    if(!any)return;                          /* no record: FORWARD */
    put32(r,PLAYHEAD_MAGIC);
    for(t=0;t<PLAYHEAD_TRACKS;t++)r[4+t]=m[t];
    put32(r+20,fnv(r,20));
}
static u32 decode(u32 slot,u8 *m)
{
    const volatile u8 *r=(const volatile u8 *)(slot+PLAYHEAD_RECORD);u32 t;
    for(t=0;t<PLAYHEAD_TRACKS;t++)m[t]=0;
    if(be32(r)!=PLAYHEAD_MAGIC || be32(r+20)!=fnv(r,20) || r[17] || r[18] || r[19])return 0;
    for(t=0;t<PLAYHEAD_TRACKS;t++)if(r[4+t]>=PLAYHEAD_MODES)return 0;
    for(t=0;t<PLAYHEAD_TRACKS;t++)m[t]=r[4+t];
    return 1;
}
static void meta(const u32 *a)
{
    u8 m[PLAYHEAD_TRACKS];u32 k,i;
    switch(a[0]){
    case ST_SOUND_KIT_ENCODE:if(known_slot(a[1],a[3]))encode(a[1],a[2]);break;
    case ST_SOUND_KIT_DECODE:
        if(!known_slot(a[2],a[3]) || !decode(a[2],m))for(i=0;i<PLAYHEAD_TRACKS;i++)m[i]=0;
        store(a[1],m);break;
    case ST_SOUND_COPY:if(a[3]==PLAYHEAD_KIT_BYTES){modes_of(a[2],m);store(a[1],m);}break;
    case ST_SOUND_RELOCATE:
        if(a[3]==PLAYHEAD_KIT_BYTES){modes_of(a[2],m);forget(a[2],1);store(a[1],m);}break;
    case ST_SOUND_NEW:
        if(a[3]==PLAYHEAD_KIT_BYTES){for(i=0;i<PLAYHEAD_TRACKS;i++)m[i]=0;store(a[1],m);}break;
    case ST_SOUND_FREE:forget(a[1],a[3]);break;
    case ST_SOUND_PROJECT_NEW:
        if(a[1])for(k=0;k<PLAYHEAD_KITS;k++)
            for(i=0;i<PLAYHEAD_RECORD_BYTES;i++)
                ((volatile u8 *)(a[1]+KIT_SLOTS+k*KIT_SLOT+PLAYHEAD_RECORD))[i]=0;
        break;
    }
}
/* A mode changed in a live kit. The native edits of a kit end in
 * KitActive::kitChanged (40172414, its vtable +5Ch): it re-encodes the kit
 * into its mirror slots (job "KitActive::updateSingleMirror", 4001082e), and
 * the mirrors are what survives a power cycle without a project save. The
 * KitActive object is application singleton +130h. Host API 20 resolves it;
 * the fallback checks the same stock vtables, live kits and entry. The re-encode reaches
 * KIT_ENCODE above, which writes the record. */
#define KIT_ACTIVE_VTABLE 0x401c3694u
#define KIT_CHANGED 0x40172414u
u32 playhead_persist_changed(u32 k)
{
    if(persistence)return persistence->kit_changed(k);
    u32 app=*(volatile u32 *)0x444e23f4u;
    const volatile u32 *o;
    if(app<0x40000000u || app>=0x48000000u || (app&3u) || *(volatile u32 *)app!=0x401c5708u)return 0;
    o=(const volatile u32 *)(app+0x130u);
    if(k>=PLAYHEAD_KITS || o[0]!=KIT_ACTIVE_VTABLE || o[4]!=PLAYHEAD_KIT_BASE ||
       ((const u32 *)KIT_ACTIVE_VTABLE)[0x5c/4]!=KIT_CHANGED)return 0;
    ((void (*)(volatile void *,u32,u32))KIT_CHANGED)((volatile void *)o,k,0);
    return 1;
}
int playhead_native(u32 op,const u32 *args,u32 *result)
{
    (void)result;
    if(op!=ST_NATIVE_SOUND_META || !args)return 0;
    playhead_enter();meta(args);playhead_leave();
    return 0;                                /* an observer */
}
void playhead_persist_init(const struct st_machine_host_api *host)
{
    if(!host || host->version<5)return;      /* every operation reaches playhead_native */
    persistence=host->version>=20?host->persistence:0;
    host->native_interest((void (*)(void))playhead_native,
                          host->version>=10?1u<<ST_NATIVE_SOUND_META:0u);
}
