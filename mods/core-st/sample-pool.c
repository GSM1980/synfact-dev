/* SPDX-License-Identifier: GPL-2.0-or-later
 * Generic project sample pool. Native stock PCM and its 64 descriptors remain
 * native. High-zone PCM uses the upper sample-store region. Boot transfers share
 * the stock sender, handshake and progress bar; no disk IO runs in an IRQ.
 */
#include "sample-pool.h"
#include "crc.h"
#include <syntakt/machines.h>
typedef pool_u8 u8;typedef pool_u16 u16;typedef pool_u32 u32;
#define PROJECT ((u8 *)0x40652514u)
#define STOCK_BLOB ((const u8 *)0x4d84f2c5u)
#define STOCK_BYTES (*(const u32 *)0x4d84f2c1u)
#define STOCK_AUDIO 0x4004f750u
#define WIRE_FIRST 0x4162u
#define IMAGE_MAGIC 0x454c5031u
extern void *fw_malloc(u32);extern void fw_free(void *);
extern int fw_mmc_read(u32,u32,void *);
extern u32 fw_mmc_ready[],fw_mmc_sectors[];
extern u32 core_machine_pages;
struct file_plan {u32 id,first,frames,crc,offset,pages,sum,last,bad;char name[17];};
struct pool_state {
    u8 image[4096] __attribute__((aligned(16)));
    u8 page[2048] __attribute__((aligned(16)));
    u8 read[3072] __attribute__((aligned(16)));
    u16 wanted[128],code[128];
    u32 frames[128],pointer[128],stock_frames[64],stock_pointer[64];
    char names[128][17];
    struct file_plan file[128];
    u32 files,data_pages,last_ordinal,boot_finished,seq,fault,query,query_age;
    struct st_machine_cursor cursors[8];
};
static struct pool_state *state;
u32 core_pool_boot_pages,core_pool_busy,core_pool_query_owner;
u32 core_pool_report[8]; /* available, planned files, loaded files, missing, bytes, errors, used, free */
static u8 *saved_record;static u32 saved_valid;
static u32 rd(const u8 *p){return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static void wr(u8 *p,u32 v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static void cp(void *d,const void *s,u32 n){u8 *a=d;const u8 *b=s;while(n--)*a++=*b++;}
static void zero(void *p,u32 n){u8 *q=p;while(n--)*q++=0;}
#define crc(c,p,n) core_crc_update(c,p,n)
static u32 adler(const u8 *p,u32 n)
{u32 a=1,b=0;while(n--){a+=*p++;if(a>=65521)a-=65521;b+=a;if(b>=65521)b-=65521;}return b<<16|a;}
int core_pool_available(void){return rd(core_pool_image)==IMAGE_MAGIC && rd(core_pool_image+4)==4096;}
int core_pool_valid(const u8 *project)
{
    const u8 *p=project+CORE_POOL_OFFSET;u32 i,v;
    if(rd(p)!=0x504f4f4c || rd(p+4)!=0x00010080u || rd(p+8)!=~crc(0,p+16,256))return 0;
    for(i=0;i<128;i++){v=(u32)p[16+2*i]<<8|p[17+2*i];
        if(v>64 && (!(v&CORE_POOL_HZ) || v==CORE_POOL_HZ))return 0;
    }return 1;
}
void core_pool_read(const u8 *project,u16 *out)
{
    u32 i;if(!core_pool_valid(project)){for(i=0;i<128;i++)out[i]=i<64?i+1:0;return;}
    for(i=0;i<128;i++)out[i]=(u16)((u32)project[CORE_POOL_OFFSET+16+2*i]<<8|project[CORE_POOL_OFFSET+17+2*i]);
}
void core_pool_write(u8 *project,const u16 *entries)
{
    u8 *p=project+CORE_POOL_OFFSET;u32 i;
    for(i=0;i<128;i++){p[16+2*i]=entries[i]>>8;p[17+2*i]=(u8)entries[i];}
    wr(p,0x504f4f4c);wr(p+4,0x00010080);wr(p+8,~crc(0,p+16,256));wr(p+12,0);
}
extern int core_parameter_provider(u32,u32 *,void *,u32 *);
void core_pool_empty(u8 *p)
{u16 e[128];u32 i;for(i=0;i<128;i++)e[i]=i<CORE_POOL_STOCK_SLOTS?i+1:0;
 core_pool_write(p,e);
 {u32 a=(u32)p;core_parameter_provider(27,&a,0,0);}}  /* NEW PROJECT (LFO3) */
/* NEW PROJECT initializes the live project through 4001d16a, not the cached
 * project constructor at 40018ab0. Run after the native body has finished.
 * A valid previous pool must be replaced here, even though LOAD preserves it. */
void core_pool_created(void)
{core_pool_empty(PROJECT);core_pool_sync();}
void core_pool_before_validate(u8 *p)
{
    saved_valid=p && core_pool_valid(p);
    if(saved_valid && !saved_record){saved_record=fw_malloc(CORE_POOL_RECORD);
        if((u32)saved_record<0x40000000u){saved_record=0;saved_valid=0;}}
    if(saved_valid)cp(saved_record,p+CORE_POOL_OFFSET,CORE_POOL_RECORD);
}
void core_pool_after_validate(u8 *p,u32 ok)
{if(p && (ok&255)){
    if(saved_valid)cp(p+CORE_POOL_OFFSET,saved_record,CORE_POOL_RECORD);
    else if(!core_pool_valid(p)){u16 e[128];core_pool_read(p,e);core_pool_write(p,e);}
 }saved_valid=0;}
void core_pool_after_snapshot(u8 *object,u8 *source)
{
    if(!object || !source)return;
    if(core_pool_valid(source))cp(object+4+CORE_POOL_OFFSET,source+CORE_POOL_OFFSET,CORE_POOL_RECORD);
    else if(!core_pool_valid(object+4)){u16 e[128];core_pool_read(source,e);core_pool_write(object+4,e);}
}
void core_pool_before_save(void *object,u32 slot)
{
    u8 *cached;
    if(slot>128 || !core_pool_valid(PROJECT))return;
    cached=((u8 *(*)(void *,u32))0x400c1700u)(object,slot);
    if(!cached || (u32)cached<0x40000000u)return;cached+=12;
    if(cached!=PROJECT)cp(cached+CORE_POOL_OFFSET,PROJECT+CORE_POOL_OFFSET,CORE_POOL_RECORD);
}
static int find_file(struct st_hzfs *fs,u32 id,struct st_sample_entry *out)
{u32 i;for(i=0;i<core_hzfs_count(fs);i++)if(core_hzfs_entry(fs,i,out)==1 && out->id==id)return 1;return 0;}
static void entry(u32 slot,u32 frames,u32 pointer)
{state->frames[slot]=frames;state->pointer[slot]=frames?pointer:0;}
static void account(void)
{
    u32 i,j,used=0;for(i=0;i<128;i++)if(state->frames[i] && state->pointer[i]>=CORE_POOL_PCM){
        for(j=0;j<i && state->pointer[j]!=state->pointer[i];j++){}
        if(j==i)used+=(2*state->frames[i]+2047)&~2047u;
    }
    core_pool_report[6]=used;core_pool_report[7]=CORE_POOL_END-CORE_POOL_PCM-used;
}
void core_pool_prepare(void)
{
    struct st_hzfs fs;struct st_sample_entry e;u32 i,j,pos=0,size=STOCK_BYTES,frames,n,room;
    int opened;
    core_pool_boot_pages=0;if(!core_pool_available() || state)return;
    state=fw_malloc(sizeof(*state));if(!state){core_pool_report[5]++;return;}
    if((u32)state<0x40000000u){state=0;core_pool_report[5]++;return;}
    zero(state,sizeof(*state));state->last_ordinal=0xffffffffu;
    cp(state->image,core_pool_image,4096);core_pool_read(PROJECT,state->wanted);
    if(!core_pool_valid(PROJECT))core_pool_write(PROJECT,state->wanted);
    /* Parse the already-built NATIVE blob, without rebuilding or changing it. */
    if(size>0x1fff100u)goto invalid;
    for(i=0;i<64;i++){
        if(pos>size || size-pos<4)goto invalid;frames=rd(STOCK_BLOB+pos);pos+=4;
        if(frames>240000u || 2*frames>size-pos)goto invalid;
        state->stock_frames[i]=frames;state->stock_pointer[i]=STOCK_AUDIO+pos;pos+=2*frames;
    }
    opened=core_hzfs_open(&fs,&core_hzfs_native_io)==1;
    for(i=0;i<128;i++){
        n=state->wanted[i];state->code[i]=n;
        if(n && n<=64){entry(i,state->stock_frames[n-1],state->stock_pointer[n-1]);continue;}
        if(!(n&CORE_POOL_HZ))continue;
        for(j=0;j<state->files && state->file[j].id!=(n&0x7fff);j++){}
        if(j<state->files)continue;
        room=CORE_POOL_END-CORE_POOL_PCM-state->data_pages*2048;
        if(!opened || !find_file(&fs,n&0x7fff,&e) || !e.frames || e.frames>room/2){core_pool_report[3]++;continue;}
        {struct file_plan *f=&state->file[state->files++];
         f->id=e.id;f->first=e.first;f->frames=e.frames;f->crc=e.crc;cp(f->name,e.name,16);f->name[16]=0;
         f->offset=state->data_pages;f->pages=(2*e.frames+2047)>>11;f->last=0xffffffffu;
         state->data_pages+=f->pages;}
    }
    if(opened)core_hzfs_close(&fs);
    core_pool_boot_pages=2+state->data_pages;core_pool_report[0]=1;
    core_pool_report[1]=state->files;core_pool_report[4]=state->data_pages*2048;return;
invalid:fw_free(state);state=0;core_pool_report[5]++;
}
/* Sender worker task, before AUDIO voices exist. Page indices use the native
 * sample-store mapping. Metadata/code pages are sent LAST, after file CRCs. */
const void *core_pool_boot_page(u32 ordinal,u32 *wire)
{
    struct file_plan *f;u32 j,at,n,skip,bytes,physical,offset,k;
    if(!state || ordinal>=core_pool_boot_pages)return 0;
    if(ordinal>=state->data_pages){
        if(!state->boot_finished){
            for(j=0;j<128;j++){
                if(state->wanted[j]&CORE_POOL_HZ){
                    for(k=0;k<state->files;k++)if(state->file[k].id==(state->wanted[j]&0x7fff)){
                        f=state->file+k;if(!f->bad && f->last+1==f->pages && ~f->sum==f->crc)
                            {entry(j,f->frames,CORE_POOL_PCM+f->offset*2048);cp(state->names[j],f->name,17);}
                        break;
                    }
                }
                wr(state->image+0xc00+8*j,state->frames[j]);wr(state->image+0xc04+8*j,state->pointer[j]);
                if(!state->frames[j])state->code[j]=0; /* allow a later retry after missing/failed PCM */
            }
            for(k=0;k<state->files;k++)if(!state->file[k].bad && ~state->file[k].sum==state->file[k].crc)core_pool_report[2]++;
            wr(state->image+8,adler(state->image+12,4096-12));state->boot_finished=1;account();
        }
        j=ordinal-state->data_pages;*wire=WIRE_FIRST+j;return state->image+2048*j;
    }
    *wire=WIRE_FIRST+2+ordinal;if(state->last_ordinal==ordinal)return state->page;
    state->last_ordinal=ordinal;zero(state->page,2048);
    for(j=0;j<state->files;j++){
        f=state->file+j;if(ordinal<f->offset || ordinal-f->offset>=f->pages)continue;
        offset=ordinal-f->offset;
        if((!offset && f->last!=0xffffffffu) || (offset && f->last!=offset-1)){f->bad=1;return state->page;}
        f->last=offset;if(f->bad)return state->page;
        at=offset*2048;bytes=2*f->frames-at;n=bytes<2048?bytes:2048;
        skip=(64+at)&511u;physical=(skip+n+511)&~511u;
        if(fw_mmc_read((f->first<<10)+((64+at)>>9),physical,state->read)){
            f->bad=1;core_pool_report[5]++;return state->page;
        }
        if(!offset){
            if(rd(state->read)!=0x53414d50u || rd(state->read+8)!=f->frames){f->bad=1;return state->page;}
            f->sum=crc(0,state->read,64);
            core_pool_wave_begin(CORE_POOL_HZ|f->id,f->frames);
        }
        f->sum=crc(f->sum,state->read+skip,n);core_pool_wave_feed(state->read+skip,n);
        if(offset+1==f->pages)core_pool_wave_finish(~f->sum==f->crc);
        cp(state->page,state->read+skip,n);return state->page;
    }return state->page;
}
#ifndef CORE_POOL_SLEEP
#define CORE_POOL_SLEEP ((void (*)(u32))0x40116134u)
#endif
static void sleep(void){CORE_POOL_SLEEP(500);}
int core_pool_ready(void)
{return state && state->boot_finished && !state->fault && !core_pool_busy && *(volatile u32 *)0x43bdc824u;}
static int send(u32 cmd,u32 dst,u32 len,const u8 *data,u32 bytes)
{
    volatile u32 *m=(void *)0x10000800u;u32 i,seq,sum,attempt,status;
    seq=state->seq%65535+1;state->seq=seq;
    sum=CORE_POOL_MAGIC+(seq<<16|cmd)+dst+len;
    for(i=0;i<bytes/4;i++){u32 v=rd(data+4*i);m[8+i]=v;sum+=v;}
    m[0]=0;m[2]=dst;m[3]=len;m[4]=sum;m[6]=0;m[1]=cmd;
    __asm__ volatile("":::"memory");m[0]=CORE_POOL_MAGIC;m[1]=seq<<16|cmd;
    /* Retry the SAME idempotent request/sequence. Never reuse an uncertain extent. */
    for(attempt=0;attempt<3;attempt++){
        for(i=0;i<200;i++){
            status=m[6];if(status>>16==seq)return (status&65535)==1?1:-1;sleep();
        }
        __asm__ volatile("":::"memory");m[1]=seq<<16|cmd;
    }
    state->fault=1;core_pool_report[5]++;return -1;
}
static u32 allocate(u32 bytes)
{
    u32 at=CORE_POOL_PCM,i,end,again;bytes=(bytes+2047)&~2047u;
    do{
        again=0;if(at>=CORE_POOL_END || bytes>CORE_POOL_END-at)return 0;
        for(i=0;i<128;i++)if(state->frames[i] && state->pointer[i]>=CORE_POOL_PCM){
            end=state->pointer[i]+((2*state->frames[i]+2047)&~2047u);
            if(at<end && state->pointer[i]<at+bytes){at=end;again=1;}
        }
    }while(again);return at;
}
struct stream {u8 buffer[992];u32 used,dst,total,done;};
static int flush(struct stream *s)
{
    u32 i,n=(s->used+3)&~3u;if(!n)return 0;
    for(i=s->used;i<n;i++)s->buffer[i]=0;
    if(send(2,s->dst,n,s->buffer,n)!=1)return -1;s->dst+=n;s->used=0;
    s->done+=n;core_pool_ui_progress(s->done,s->total);return 0;
}
static int sink(void *context,u32 at,u32 bytes,void *data)
{
    struct stream *s=context;u8 *p=data;u32 n;
    if(at<64){n=64-at;if(n>bytes)n=bytes;p+=n;bytes-=n;}
    core_pool_wave_feed(p,bytes);
    while(bytes){n=992-s->used;if(n>bytes)n=bytes;cp(s->buffer+s->used,p,n);
        s->used+=n;p+=n;bytes-=n;if(s->used==992 && flush(s))return -1;}
    return 0;
}
int core_pool_sync(void)
{
    struct st_hzfs fs;struct st_sample_entry e;struct stream stream;
    u32 i,j,n,frames,pointer,bytes,ordinal=0;u8 payload[4];int opened=0,result=1;
    if(!state || !state->boot_finished || state->fault || core_pool_busy || !*(volatile u32 *)0x43bdc824u)return -1;
    if(state->query){volatile u32 *m=(void *)0x10000800u;
        for(i=0;i<200 && m[6]>>16!=state->query;i++)sleep();
        if(m[6]>>16!=state->query)return -1;state->query=0;core_pool_query_owner=0;}
    core_pool_busy=1;
    /* Let an outstanding read-only System info query finish before taking its window. */
    {volatile u32 *m=(void *)0x10000800u;u32 seq=m[1]>>16;
     if(m[0]==0x48543133u && seq)for(i=0;i<200 && (m[6]>>16)!=seq;i++)sleep();
     if(m[0]==0x48543133u && seq && (m[6]>>16)!=seq){core_pool_busy=0;return -1;}}
    core_pool_query_owner=0;
    core_pool_read(PROJECT,state->wanted);
    for(i=0;i<128;i++)if(state->wanted[i]!=state->code[i]){
        if(send(4,i,0,0,0)!=1){result=-1;goto done;}
        entry(i,0,0);state->code[i]=0;state->names[i][0]=0;
    }
    for(i=0;i<128;i++)if(state->wanted[i] && state->wanted[i]!=state->code[i]){
        n=state->wanted[i];frames=pointer=0;
        if(n<=64){frames=state->stock_frames[n-1];pointer=state->stock_pointer[n-1];}
        else for(j=0;j<128;j++)if(state->code[j]==n && state->frames[j]){
            frames=state->frames[j];pointer=state->pointer[j];cp(state->names[i],state->names[j],17);break;}
        if(frames){
            wr(payload,pointer);if(send(5,i,frames,payload,4)!=1){result=-1;goto done;}
        }else if(n&CORE_POOL_HZ){
            if(!opened){opened=core_hzfs_open(&fs,&core_hzfs_native_io)==1;if(!opened){result=-1;goto done;}}
            if(!find_file(&fs,n&0x7fff,&e) || !e.frames || e.frames>(CORE_POOL_END-CORE_POOL_PCM)/2){result=-1;goto done;}
            frames=e.frames;bytes=(2*frames+3)&~3u;pointer=allocate(bytes);
            if(!pointer){result=-2;goto done;}
            core_pool_ui_file(n,++ordinal,bytes);
            if(send(1,pointer,bytes,0,0)!=1){result=-1;goto done;}
            zero(&stream,sizeof(stream));stream.dst=pointer;stream.total=bytes;
            core_pool_wave_begin(n,frames);
            if(core_hzfs_get(&core_hzfs_native_io,n&0x7fff,sink,&stream)!=1 || flush(&stream) ||
               send(3,i,frames,0,0)!=1){core_pool_wave_finish(0);result=-1;goto done;}
            core_pool_wave_finish(1);
            cp(state->names[i],e.name,16);state->names[i][16]=0;
        }
        entry(i,frames,pointer);state->code[i]=n;
    }
done:if(opened)core_hzfs_close(&fs);account();core_pool_busy=0;return result;
}
int core_pool_cached_name(u32 code,char *out)
{
    u32 i;if(!state)return 0;
    for(i=0;i<128;i++)if(state->code[i]==code && state->names[i][0]){cp(out,state->names[i],17);return 1;}
    return 0;
}
void core_pool_after_load(u32 index,u32 ok)
{
    u8 head[512];u16 entries[128];
    if(!(ok&255) || index>=128)return;
    if(!fw_mmc_read((index+0x40u)*0x2000u,512,head) && rd(head)==0xbeefbaceu){
        core_pool_read(head,entries);core_pool_write(PROJECT,entries);
    }
    core_pool_sync();
}
u32 core_pool_sample_source(u32 slot,const u8 **pcm,u32 *frames)
{
    u32 code;*pcm=0;*frames=0;if(!state || slot>=128)return 0;
    code=state->code[slot];*frames=state->frames[slot];
    if(code && code<=64)*pcm=STOCK_BLOB+state->stock_pointer[code-1]-STOCK_AUDIO;
    return *frames?code:0;
}
int core_pool_sample_name(u32 slot,char *out)
{
    u32 code,i;const u8 *pcm;u32 frames;void *fs;
    code=core_pool_sample_source(slot,&pcm,&frames);if(!code)return 0;
    if(code>64)return core_pool_cached_name(code,out);
    fs=((void *(*)(void))0x4016f0ceu)();if((u32)fs<0x40000000u)return 0;
    pcm=(const u8 *)fs+0x12e28+24*(code-1);
    for(i=0;i<16 && pcm[i];i++)out[i]=(char)pcm[i];out[i]=0;return i!=0;
}
const struct st_machine_cursor *core_pool_cursor(u32 track,u32 slot)
{
    const struct st_machine_cursor *c;if(!state || track>=8 || slot>=128)return 0;
    c=state->cursors+track;
    return c->frames==state->frames[slot] && c->pointer==state->pointer[slot]?c:0;
}
void core_pool_cursor_tick(void)
{
    volatile u32 *m=(void *)0x10000800u;u32 i,sum,seq,values[32];
    if(!core_pool_ready())return;
    if(core_pool_query_owner==2)return;
    if(state->query){
        seq=state->query;
        if(m[6]>>16!=seq){if(++state->query_age<8)return;
            state->query=0;core_pool_query_owner=0;zero(state->cursors,sizeof(state->cursors));return;}
        if((m[6]&65535u)==1){sum=seq;
            for(i=0;i<32;i++){values[i]=m[8+i];sum+=values[i];}
            if(sum==m[7] && m[6]==(seq<<16|1u))cp(state->cursors,values,sizeof(values));}
        state->query=0;core_pool_query_owner=0;return;
    }
    /* System info and PCM writes share this mailbox. Never replace a pending
     * request, and never wait in a UI idle callback. */
    if(m[0] && m[1]>>16 && m[6]>>16!=m[1]>>16)return;
    seq=state->seq%65535+1;state->seq=seq;state->query=seq;state->query_age=0;
    core_pool_query_owner=1;
    m[0]=0;m[2]=m[3]=0;m[4]=CORE_POOL_MAGIC+(seq<<16|6u);m[6]=0;
    __asm__ volatile("":::"memory");m[0]=CORE_POOL_MAGIC;m[1]=seq<<16|6u;
}
