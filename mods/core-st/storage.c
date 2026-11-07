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
int core_mods_stage_path(const char *p)
{
    static const char name[]="/mods-stage";core_u32 i;
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

/* V2 bank slots live wholly inside the old 16 MiB reservation. The legacy
 * record remains readable during migration, but new writes never touch it. */
int core_mods_slot_io(const struct core_mods_io *io,core_u32 slot,struct core_mods_slot *out)
{
    core_u8 b[512] __attribute__((aligned(16)));core_u32 n,state;
    if(slot>1 || !card_ok(io))return -1;
    if(io->read(CORE_MODS_SLOT_META(slot)>>9,512,b))return -1;
    n=get32(b+16);state=get32(b+12);
    if(get32(b)!=0x454d4232u || get32(b+4)!=2 || get32(b+8)==0 ||
       state<CORE_MODS_GOOD || state>CORE_MODS_TRYING ||
       !n || n>CORE_MODS_SLOT_CAP || get32(b+20)!=~n ||
       get32(b+32)!=crc32(b,32))return 0;
    out->base=CORE_MODS_BASE+slot*CORE_MODS_SLOT_BYTES;
    out->bytes=n;out->sequence=get32(b+8);out->state=state;
    out->stamp=get32(b+24);out->slot=slot;return 1;
}
int core_mods_select_io(const struct core_mods_io *io,struct core_mods_slot *primary,
                        struct core_mods_slot *fallback)
{
    struct core_mods_slot s[2],*good=0,*candidate=0;int r[2];core_u32 i;
    r[0]=core_mods_slot_io(io,0,&s[0]);r[1]=core_mods_slot_io(io,1,&s[1]);
    if(r[0]<0 && r[1]<0)return -1;
    for(i=0;i<2;i++)if(r[i]==1){
        if(s[i].state==CORE_MODS_GOOD && (!good || s[i].sequence>good->sequence))good=&s[i];
        if(s[i].state==CORE_MODS_CANDIDATE &&
           (!candidate || s[i].sequence>candidate->sequence))candidate=&s[i];
    }
    if(candidate && (!good || candidate->sequence>good->sequence)){
        *primary=*candidate;if(good)*fallback=*good;else fallback->bytes=0;return 1;
    }
    if(good){*primary=*good;fallback->bytes=0;return 1;}
    primary->bytes=fallback->bytes=0;return 0;
}
int core_mods_stage_io(const struct core_mods_io *io,struct core_mods_slot *out)
{
    struct core_mods_slot s[2],*good=0,*candidate=0;core_u32 i,max=0,slot,legacy;
    int r[2],old;
    r[0]=core_mods_slot_io(io,0,&s[0]);r[1]=core_mods_slot_io(io,1,&s[1]);
    if(r[0]<0 || r[1]<0)return 0; /* never risk the only good bank */
    for(i=0;i<2;i++)if(r[i]==1){
        if(s[i].sequence>max)max=s[i].sequence;
        if(s[i].state==CORE_MODS_GOOD && (!good || s[i].sequence>good->sequence))good=&s[i];
        if(s[i].state!=CORE_MODS_GOOD && (!candidate || s[i].sequence>candidate->sequence))candidate=&s[i];
    }
    old=core_mods_stat_io(io,&legacy);if(old<0)return 0;
    slot=good?1u-good->slot:old==1?1u:candidate?candidate->slot:0u;
    out->base=CORE_MODS_BASE+slot*CORE_MODS_SLOT_BYTES;
    out->bytes=0;out->sequence=max+1u;if(!out->sequence)return 0;
    out->state=CORE_MODS_CANDIDATE;out->stamp=0;out->slot=slot;return 1;
}
int core_mods_record_io(const struct core_mods_io *io,const struct core_mods_slot *p)
{
    core_u8 b[512] __attribute__((aligned(16)));struct core_mods_slot check;
    if(!p || p->slot>1 || !p->sequence || !p->bytes ||
       p->bytes>CORE_MODS_SLOT_CAP || !card_ok(io))return 0;
    clear(b);put32(b,0x454d4232u);put32(b+4,2);put32(b+8,p->sequence);
    put32(b+12,p->state);put32(b+16,p->bytes);put32(b+20,~p->bytes);
    put32(b+24,p->stamp);put32(b+32,crc32(b,32));
    if(io->write(CORE_MODS_SLOT_META(p->slot)>>9,512,b))return 0;
    return core_mods_slot_io(io,p->slot,&check)==1 &&
           check.sequence==p->sequence && check.state==p->state &&
           check.bytes==p->bytes && check.stamp==p->stamp;
}
int core_mods_invalidate_slot_io(const struct core_mods_io *io,core_u32 slot)
{
    core_u8 b[512] __attribute__((aligned(16)));struct core_mods_slot check;
    if(slot>1 || !card_ok(io))return 0;
    clear(b);
    if(io->write(CORE_MODS_SLOT_META(slot)>>9,512,b))return 0;
    return core_mods_slot_io(io,slot,&check)==0;
}
int core_mods_transition_io(const struct core_mods_io *io,core_u32 slot,
                            core_u32 from,core_u32 to)
{
    struct core_mods_slot p;
    if(!((from==CORE_MODS_CANDIDATE && to==CORE_MODS_TRYING) ||
         (from==CORE_MODS_TRYING && to==CORE_MODS_GOOD)))return 0;
    if(core_mods_slot_io(io,slot,&p)!=1 || p.state!=from)return 0;
    p.state=to;return core_mods_record_io(io,&p);
}
