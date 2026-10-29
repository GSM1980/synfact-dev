/* SPDX-License-Identifier: GPL-2.0-or-later
 * A single opaque file in the native low eMMC gap documented by Synfact
 * docs/research/emmc-storage-1-41.md. No allocation, IRQ work or file execution.
 * The sector record is transport metadata, separate from the file's bytes.
 */
#include "storage.h"
static core_u32 get32(const core_u8 *p)
{return (core_u32)p[0]<<24|(core_u32)p[1]<<16|(core_u32)p[2]<<8|p[3];}
static void put32(core_u8 *p,core_u32 n)
{p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static core_u32 crc32(const core_u8 *p,core_u32 n)
{
    core_u32 c=0xffffffffu,i;
    while(n--){c^=*p++;for(i=0;i<8;i++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}
    return ~c;
}
static void clear(core_u8 *p){core_u32 i;for(i=0;i<512;i++)p[i]=0;}
static int card_ok(const struct core_mods_io *io)
{core_u32 n=io->sectors();return n!=0xffffffffu && n>CORE_MODS_LBA;}
int core_mods_path(const char *p)
{
    static const char name[]="/mods";core_u32 i;
    if(!p)return 0;
    for(i=0;i<sizeof(name);i++)if(p[i]!=name[i])return 0;
    return 1;
}
int core_mods_span(core_u32 address,core_u32 size)
{return address==CORE_MODS_BASE && size<=CORE_MODS_MAX;}
int core_mods_stat_io(const struct core_mods_io *io,core_u32 *size)
{
    core_u8 record[512] __attribute__((aligned(16)));core_u32 n;
    *size=0;
    if(!card_ok(io) || io->read(CORE_MODS_LBA,512,record)!=0)return -1;
    n=get32(record+12);
    if(get32(record)!=0x454c4d4fu || get32(record+4)!=0x44533031u ||
       get32(record+8)!=1 || get32(record+16)!=~n ||
       !core_mods_span(CORE_MODS_BASE,n) || get32(record+20)!=crc32(record,20))return 0;
    *size=n;return 1;
}
int core_mods_begin_io(struct core_mods_store *s,const struct core_mods_io *io,core_u32 size)
{
    core_u8 record[512] __attribute__((aligned(16)));
    if(!core_mods_span(CORE_MODS_BASE,size) || !card_ok(io))return 0;
    /* A rejected request leaves an earlier file intact. Once accepted,
     * replacement is destructive; an interrupted file must not be published. */
    clear(record);s->pending=0;s->commit_ok=0;
    if(io->write(CORE_MODS_LBA,512,record)!=0)return 0;
    s->bytes=size;s->pending=1;return 1;
}
int core_mods_commit_io(struct core_mods_store *s,const struct core_mods_io *io,core_u32 size)
{
    core_u8 record[512] __attribute__((aligned(16)));core_u32 check;
    s->commit_ok=0;
    if(!s->pending || size!=s->bytes || !core_mods_span(CORE_MODS_BASE,size))return 0;
    s->pending=0;
    if(!card_ok(io))return 0;
    clear(record);put32(record,0x454c4d4fu);put32(record+4,0x44533031u);
    put32(record+8,1);put32(record+12,size);put32(record+16,~size);
    put32(record+20,crc32(record,20));
    if(io->write(CORE_MODS_LBA,512,record)!=0 ||
       core_mods_stat_io(io,&check)!=1 || check!=size)return 0;
    s->commit_ok=1;return 1;
}
