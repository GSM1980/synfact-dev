/* SPDX-License-Identifier: GPL-2.0-or-later
 * Sound-owned metadata. No disk IO, native-field tags, or owning pointers.
 * Native task lifecycle comes through the shared bus. Each cell publishes
 * an inactive settings bank before one aligned index store; MAIN IRQ readers
 * preempt the task writer, never observe a partly copied settings record.
 */
#include "persist.h"
#include <syntakt/sound-meta.h>
#define DATA __attribute__((section(".data")))
#define KIT 0x1618u
#define SOUND 0x123u
#define RECORD 0x107u
#define KITS 0x41ad5134u
#define POOL 0x41b93ec9u
#define SLOT 0x5c00u
#define CARRIER 0x5ac0u
#define CARRIER_BYTES 320u
#define KIT_SLOTS 0x2e0200u
#define POOL_SLOTS 0x390204u
#define SIDE 1024u
struct cell {arp_u32 address,bank;struct arp_settings p[2];};
struct assoc {arp_u32 address,hash,loaded;struct arp_settings p;};
struct kit_assoc {arp_u32 address,hash,tags[12];struct arp_settings p[12];};
/* GCC may compare an induction pointer to a field just beyond live[1663].
 * Keep that sentinel address inside the SMZ image: its relocation must be in
 * bounds even when ARP is the last module in a bank. It is never a live slot. */
static struct cell live[1665] DATA={{0}},side[SIDE] DATA={{0}};
static struct assoc encoded[16] DATA={{0}};
static struct kit_assoc encoded_kit[16] DATA={{0}};
static unsigned int next_assoc DATA=0,next_kit DATA=0;
/* Native recursive task mutex, same initialization/ABI as LFO3 and malloc.
 * IRQ readers use the published bank and never acquire a mutex. */
static arp_u32 writer_mutex[4] DATA={0};
static void enter(void){((void (*)(void *))0x40001980u)(writer_mutex);}
static void leave(void){((void (*)(void *))0x40001aa2u)(writer_mutex);}
arp_u32 arp_persist_denied DATA=0;
static const struct arp_settings defaults={0,9,1,30,15,0xffff,{0}};
static void copy_settings(struct arp_settings *d,const struct arp_settings *s)
{unsigned int i;for(i=0;i<sizeof(*s);i++)((volatile unsigned char *)d)[i]=i==5?0:((const unsigned char *)s)[i];}
static arp_u32 hash(const unsigned char *p,unsigned int n)
{arp_u32 h=0x811c9dc5u;while(n--)h=(h^*p++)*0x1000193u;return h;}
static arp_u32 word(const unsigned char *p)
{return ((arp_u32)p[0]<<24)|((arp_u32)p[1]<<16)|((arp_u32)p[2]<<8)|p[3];}
static void put(unsigned char *p,arp_u32 v)
{p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static int valid(const struct arp_settings *p)
{
    unsigned int i;if(p->mode>4 || p->speed>=18 || p->range>3 || p->note_length>=128 || p->length>=16)return 0;
    for(i=0;i<16;i++)if(p->offset[i]<-24 || p->offset[i]>24)return 0;
    return 1;
}
static int ordinary(const struct arp_settings *p)
{
    unsigned int i;
    if(p->mode || p->speed!=9 || p->range!=1 || p->note_length!=30 ||
       p->length!=15 || p->enabled!=0xffff)return 0;
    for(i=0;i<16;i++)if(p->offset[i])return 0;
    return 1;
}
static void pack(unsigned char *d,const struct arp_settings *p)
{
    unsigned int i;d[0]=p->mode;d[1]=p->speed;d[2]=p->range;d[3]=p->note_length;d[4]=p->length;d[5]=0;
    d[6]=p->enabled>>8;d[7]=p->enabled;for(i=0;i<16;i++)d[8+i]=(unsigned char)p->offset[i];
}
static int unpack(struct arp_settings *p,const unsigned char *s)
{
    unsigned int i;p->mode=s[0];p->speed=s[1];p->range=s[2];p->note_length=s[3];p->length=s[4];
    ((volatile unsigned char *)p)[5]=0;
    p->enabled=((unsigned int)s[6]<<8)|s[7];for(i=0;i<16;i++)p->offset[i]=(signed char)s[8+i];
    return !s[5] && valid(p);
}
static struct cell *find(arp_u32 a,int create)
{
    unsigned int i,index;arp_u32 off;struct cell *free=0,*c;
    if(a>=KITS && a<KITS+128*KIT){
        off=a-KITS;index=off/KIT;off%=KIT;
        if(off>=0x2a && off<0x2a+12*SOUND && !((off-0x2a)%SOUND))return &live[index*12+(off-0x2a)/SOUND];
    }
    if(a>=POOL && a<POOL+128*SOUND && !((a-POOL)%SOUND))return &live[1536+(a-POOL)/SOUND];
    if(!a)return 0;
    index=((a>>2)*0x9e3779b1u)&(SIDE-1);
    for(i=0;i<SIDE;i++){
        c=&side[(index+i)&(SIDE-1)];
        if(c->address==a)return c;
        if((!c->address || c->address==0xffffffffu) && !free)free=c;
        if(!c->address)break;
    }
    if(!create)return 0;
    if(!free){arp_persist_denied++;return 0;}
    copy_settings(&free->p[0],&defaults);free->bank=0;
    __asm__ volatile("" ::: "memory");free->address=a;return free;
}
void arp_persist_init(void)
{
    unsigned int i;for(i=0;i<1664;i++){copy_settings(&live[i].p[0],&defaults);live[i].bank=0;}
    for(i=0;i<SIDE;i++)side[i].address=0;
    for(i=0;i<16;i++)encoded[i].address=0;
    for(i=0;i<16;i++)encoded_kit[i].address=0;
    next_assoc=next_kit=arp_persist_denied=0;
}
const struct arp_settings *arp_sound_settings(arp_u32 sound)
{struct cell *c=find(sound,0);return c?&c->p[*(volatile arp_u32 *)&c->bank&1]:&defaults;}
void arp_sound_read(arp_u32 sound,struct arp_settings *p)
{enter();copy_settings(p,arp_sound_settings(sound));leave();}
static int store(arp_u32 sound,const struct arp_settings *p)
{
    struct cell *c;unsigned int bank;
    if(!sound || !valid(p))return 0;
    c=find(sound,0);if(!c && ordinary(p))return 1;
    if(!c)c=find(sound,1);
    if(!c)return 0;
    bank=(c->bank^1)&1;copy_settings(&c->p[bank],p);
    __asm__ volatile("" ::: "memory");*(volatile arp_u32 *)&c->bank=bank;return 1;
}
int arp_sound_store(arp_u32 sound,const struct arp_settings *p)
{int ok;enter();ok=store(sound,p);leave();return ok;}
/* Only callbacks authorized by the native full-container scopes call these.
 * The pattern codec ends at 5ab7h; [5ac0h,5c00h) is its slot's checked carrier.
 * Semantic owner is kit/pool index, never that pattern's settings. */
static unsigned char *carrier(arp_u32 container,unsigned int index)
{return (unsigned char *)(container+0x200u+index*SLOT+CARRIER);}
static int carrier_valid(const unsigned char *p,unsigned int index)
{return word(p)==0x41504331u && word(p+316)==(hash(p,316)^index);}
/* A missing carrier already decodes all thirteen rows as these defaults.
 * Keep the empty representation when a whole Kit and its pool Sound are OFF. */
static int row_ordinary(const unsigned char *p)
{
    static const unsigned char d[20]={0,9,1,30,15,0,255,255,
        0x18,0x86,0x61,0x18,0x86,0x61,0x18,0x86,0x61,0x18,0x86,0x61};
    unsigned int i;for(i=0;i<20;i++)if(p[i]!=d[i])return 0;return 1;
}
static arp_u32 row_record(arp_u32 container,unsigned int index,unsigned int row)
{return row==12?container+POOL_SLOTS+index*RECORD:container+KIT_SLOTS+index*0x1600u+0x2eu+row*RECORD;}
/* Six-bit signed offsets leave four bytes for binding each row to its native
 * Sound record. A stock import cannot inherit a valid old ARP carrier. */
static void row_pack(unsigned char *d,const struct arp_settings *p,arp_u32 record)
{
    unsigned int i,j,v;
    d[0]=p->mode;d[1]=p->speed;d[2]=p->range;d[3]=p->note_length;d[4]=p->length;d[5]=0;
    d[6]=p->enabled>>8;d[7]=p->enabled;
    for(i=0;i<12;i++)d[8+i]=0;
    for(i=0;i<16;i++){
        v=(unsigned int)(p->offset[i]+24);
        for(j=0;j<6;j++)if(v&(1u<<j))d[8+(i*6+j)/8]|=1u<<((i*6+j)%8);
    }
    put(d+20,hash((const unsigned char *)record,RECORD));
}
static int row_unpack(struct arp_settings *p,const unsigned char *d,arp_u32 record)
{
    unsigned int i,j,v;
    if(word(d+20)!=hash((const unsigned char *)record,RECORD))return 0;
    p->mode=d[0];p->speed=d[1];p->range=d[2];p->note_length=d[3];p->length=d[4];
    ((volatile unsigned char *)p)[5]=0;p->enabled=((unsigned int)d[6]<<8)|d[7];
    for(i=0;i<16;i++){
        v=0;for(j=0;j<6;j++)if(d[8+(i*6+j)/8]&(1u<<((i*6+j)%8)))v|=1u<<j;
        p->offset[i]=(int)v-24;
    }
    return !d[5] && valid(p);
}
static void carrier_init(unsigned char *p,arp_u32 container,unsigned int index)
{unsigned int i;put(p,0x41504331u);for(i=0;i<13;i++)row_pack(p+4+i*24,&defaults,row_record(container,index,i));}
static void save_row(arp_u32 container,unsigned int index,unsigned int row,const struct arp_settings *p)
{
    unsigned char *c=carrier(container,index);unsigned int i;int present=carrier_valid(c,index);
    if(ordinary(p)){
        if(!present){put(c,0);return;}
        for(i=0;i<13;i++)if(i!=row && !row_ordinary(c+4+i*24))break;
        if(i==13){put(c,0);return;}
    }
    if(!present)carrier_init(c,container,index);
    row_pack(c+4+row*24,p,row_record(container,index,row));put(c+316,hash(c,316)^index);
}
/* A whole Kit writes rows 0..11. Building defaults for those rows first and
 * sealing the 320-byte carrier after each row repeats most of the work during
 * every project save, especially the first save of a new project. Row 12 is
 * the pool Sound and must survive a later Kit refresh. */
static void save_kit(arp_u32 container,unsigned int index,arp_u32 kit)
{
    unsigned char *c=carrier(container,index);unsigned int i;int present=carrier_valid(c,index);
    for(i=0;i<12;i++)if(!ordinary(arp_sound_settings(kit+0x2a+i*SOUND)))break;
    if(i==12 && (!present || row_ordinary(c+4+12*24))){put(c,0);return;}
    if(!present){
        put(c,0x41504331u);
        row_pack(c+4+12*24,&defaults,row_record(container,index,12));
    }
    for(i=0;i<12;i++)
        row_pack(c+4+i*24,arp_sound_settings(kit+0x2a+i*SOUND),row_record(container,index,i));
    put(c+316,hash(c,316)^index);
}
static void load_row(struct arp_settings *p,arp_u32 container,unsigned int index,unsigned int row)
{
    const unsigned char *c=carrier(container,index);
    if(!carrier_valid(c,index) || !row_unpack(p,c+4+row*24,row_record(container,index,row)))copy_settings(p,&defaults);
}
static int record_slot(arp_u32 record,arp_u32 container,unsigned int *index,unsigned int *row)
{
    arp_u32 off,k;
    if(!container)return 0;
    if(record>=container+POOL_SLOTS && record<container+POOL_SLOTS+128*RECORD &&
       !((record-container-POOL_SLOTS)%RECORD)){*index=(record-container-POOL_SLOTS)/RECORD;*row=12;return 1;}
    if(record<container+KIT_SLOTS)return 0;
    off=record-container-KIT_SLOTS;k=off/0x1600u;off%=0x1600u;
    if(k>=128 || off<0x2e || off>=0x2eu+12*RECORD || (off-0x2e)%RECORD)return 0;
    *index=k;*row=(off-0x2e)/RECORD;return 1;
}
static void remember(arp_u32 record,const struct arp_settings *p,arp_u32 source)
{
    struct assoc *a;unsigned int i;
    for(i=0;i<16;i++)if(encoded[i].address==record)break;
    if(i==16)i=next_assoc++&15;
    a=&encoded[i];a->address=0;a->loaded=0;copy_settings(&a->p,p);
    a->hash=hash((const unsigned char *)source,RECORD);a->address=record;
}
static const struct arp_settings *remembered(arp_u32 record)
{
    unsigned int i;for(i=0;i<16;i++)if(encoded[i].address==record && encoded[i].hash==hash((const unsigned char *)record,RECORD))return &encoded[i].p;
    return &defaults;
}
static const struct arp_settings *loaded_settings(arp_u32 record,int consume)
{
    unsigned int i;
    for(i=0;i<16;i++)if(encoded[i].address==record && encoded[i].loaded){
        if(consume)encoded[i].loaded=0;
        if(encoded[i].hash==hash((const unsigned char *)record,RECORD))return &encoded[i].p;
    }
    return 0;
}
static int kit_index(arp_u32 record,arp_u32 container,unsigned int *index)
{
    arp_u32 off;
    if(!container || record<container+KIT_SLOTS)return 0;
    off=record-container-KIT_SLOTS;*index=off/0x1600u;
    return *index<128 && !(off%0x1600u);
}
static struct kit_assoc *kit_remember(arp_u32 record)
{
    unsigned int i;for(i=0;i<16;i++)if(encoded_kit[i].address==record)break;
    if(i==16)i=record==0x405ff554u?0:record==0x405fd354u?1:2+(next_kit++%14);
    encoded_kit[i].address=record;
    return &encoded_kit[i];
}
static struct kit_assoc *kit_remembered(arp_u32 record)
{
    unsigned int i;for(i=0;i<16;i++)if(encoded_kit[i].address==record &&
        encoded_kit[i].hash==hash((const unsigned char *)record,0x14d2u))return &encoded_kit[i];
    return 0;
}
static void kit_seal(struct kit_assoc *entry)
{
    unsigned int i;entry->hash=hash((const unsigned char *)entry->address,0x14d2u);
    for(i=0;i<12;i++)entry->tags[i]=hash((const unsigned char *)(entry->address+0x2eu+i*RECORD),RECORD);
}
static struct kit_assoc *sub_record(arp_u32 record,unsigned int *row)
{
    unsigned int i;arp_u32 off;
    for(i=0;i<16;i++)if(encoded_kit[i].address && record>=encoded_kit[i].address){
        off=record-encoded_kit[i].address;
        if(off>=0x2e && off<0x2eu+12*RECORD && !((off-0x2e)%RECORD)){*row=(off-0x2e)/RECORD;return &encoded_kit[i];}
    }
    return 0;
}
static void sound_meta(const arp_u32 *a)
{
    arp_u32 kind=a[0],d=a[1],s=a[2],n=a[3],off;
    unsigned int i,k,row;struct arp_settings p;
    if(kind==ST_SOUND_COPY || kind==ST_SOUND_NEW){
        if(n==SOUND){copy_settings(&p,kind==ST_SOUND_NEW?&defaults:arp_sound_settings(s));arp_sound_store(d,&p);}
        else if(n==KIT)for(i=0;i<12;i++){
            copy_settings(&p,kind==ST_SOUND_NEW?&defaults:arp_sound_settings(s+0x2a+i*SOUND));arp_sound_store(d+0x2a+i*SOUND,&p);
        }
        return;
    }
    if(kind==ST_SOUND_FREE){
        for(i=0;i<SIDE;i++)if(side[i].address && side[i].address!=0xffffffffu && side[i].address-d<n)side[i].address=0xffffffffu;
        for(i=0;i<16;i++){
            if(encoded[i].address-d<n)encoded[i].address=0;
            if(encoded_kit[i].address-d<n)encoded_kit[i].address=0;
        }return;
    }
    if(kind==ST_SOUND_RELOCATE){
        /* Native relocation uses disjoint extents; source metadata is retired
         * by the allocator notification, after the copied records are set. */
        for(i=0;i<SIDE;i++)if(side[i].address && side[i].address!=0xffffffffu && side[i].address>=s && side[i].address-s<n && SOUND<=n-(side[i].address-s)){
            off=side[i].address-s;copy_settings(&p,arp_sound_settings(s+off));arp_sound_store(d+off,&p);
        }return;
    }
    if(kind==ST_SOUND_PROJECT_NEW){
        if(d)for(i=0;i<128;i++)put(carrier(d,i),0);
        return;
    }
    if(kind==ST_SOUND_KIT_ENCODE || kind==ST_SOUND_KIT_DECODE){
        arp_u32 record=kind==ST_SOUND_KIT_ENCODE?d:s,kit=kind==ST_SOUND_KIT_ENCODE?s:d;
        if(kit_index(record,n,&k)){
            if(kind==ST_SOUND_KIT_ENCODE)save_kit(n,k,kit);
            else for(i=0;i<12;i++){
                load_row(&p,n,k,i);arp_sound_store(kit+0x2a+i*SOUND,&p);
            }
        }else{
            struct kit_assoc *entry=kind==ST_SOUND_KIT_ENCODE?kit_remember(record):kit_remembered(record);
            for(i=0;i<12;i++)if(kind==ST_SOUND_KIT_ENCODE)copy_settings(&entry->p[i],arp_sound_settings(kit+0x2a+i*SOUND));
            else arp_sound_store(kit+0x2a+i*SOUND,entry?&entry->p[i]:&defaults);
            if(kind==ST_SOUND_KIT_ENCODE)kit_seal(entry);
        }
        return;
    }
    if(kind==ST_SOUND_KIT_RECORD_COPY){
        struct kit_assoc *from=kit_remembered(s),*to=kit_remember(d);
        int known=kit_index(s,n,&k);
        for(i=0;i<12;i++){
            if(known)load_row(&p,n,k,i);else copy_settings(&p,from?&from->p[i]:&defaults);
            copy_settings(&to->p[i],&p);
        }
        kit_seal(to);return;
    }
    if(kind==ST_SOUND_RECORD_COPY){
        const struct arp_settings *loaded=loaded_settings(s,0);struct kit_assoc *entry;
        if(loaded)copy_settings(&p,loaded);
        else if(record_slot(s,n,&k,&row))load_row(&p,n,k,row);
        else if((entry=sub_record(s,&row))!=0)copy_settings(&p,entry->tags[row]==hash((const unsigned char *)s,RECORD)?&entry->p[row]:&defaults);
        else copy_settings(&p,remembered(s));
        remember(d,&p,d);
        if(loaded)for(i=0;i<16;i++)if(encoded[i].address==d)encoded[i].loaded=1;
        return;
    }
    if(kind==ST_SOUND_RECORD_BIND){
        struct kit_assoc *entry;
        if(s==0x1600u && kit_index(d,n,&k)){
            entry=kit_remembered(d);
            for(i=0;i<12;i++)save_row(n,k,i,entry?&entry->p[i]:&defaults);
        }else if(s==RECORD && record_slot(d,n,&k,&row))save_row(n,k,row,remembered(d));
        return;
    }
    if(kind==ST_SOUND_ENCODE){
        const struct arp_settings *value=arp_sound_settings(s);struct kit_assoc *entry;
        if(record_slot(d,n,&k,&row))save_row(n,k,row,value);
        else if((entry=sub_record(d,&row))!=0){
            for(i=0;i<12;i++)if(i!=row && entry->tags[i]!=hash((const unsigned char *)(entry->address+0x2eu+i*RECORD),RECORD))copy_settings(&entry->p[i],&defaults);
            copy_settings(&entry->p[row],value);kit_seal(entry);
        }else remember(d,value,d);return;
    }
    if(kind==ST_SOUND_DECODE){
        struct kit_assoc *entry;const struct arp_settings *loaded=loaded_settings(s,1);
        if(loaded){
            arp_sound_store(d,loaded);
            if(record_slot(s,n,&k,&row))save_row(n,k,row,loaded);
        }
        else if(record_slot(s,n,&k,&row)){load_row(&p,n,k,row);arp_sound_store(d,&p);}
        else if((entry=sub_record(s,&row))!=0)arp_sound_store(d,entry->tags[row]==hash((const unsigned char *)s,RECORD)?&entry->p[row]:&defaults);
        else arp_sound_store(d,remembered(s));
        return;
    }
    if(kind==ST_SOUND_S1_SAVE){
        unsigned char *out=(unsigned char *)(s+ST_SOUND_S1_EXTENSION);struct kit_assoc *entry;
        const struct arp_settings *value;
        if(record_slot(d,n,&k,&row)){load_row(&p,n,k,row);value=&p;}
        else if((entry=sub_record(d,&row))!=0)value=entry->tags[row]==hash((const unsigned char *)d,RECORD)?&entry->p[row]:&defaults;
        else value=remembered(d);
        put(out,0x41525031u);pack(out+4,value);put(out+28,hash(out,28)^hash((const unsigned char *)s,RECORD));return;
    }
    if(kind==ST_SOUND_S1_LOAD){
        const unsigned char *in=(const unsigned char *)(s+ST_SOUND_S1_EXTENSION);
        if(!(n&255u) || word(in)!=0x41525031u || word(in+28)!=(hash(in,28)^hash((const unsigned char *)s,RECORD)) || !unpack(&p,in+4))copy_settings(&p,&defaults);
        remember(d,&p,s);
        for(i=0;i<16;i++)if(encoded[i].address==d)encoded[i].loaded=1;
        return;
    }
}
void arp_sound_meta(const arp_u32 *a)
{enter();sound_meta(a);leave();}
