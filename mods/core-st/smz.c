/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "smz.h"
typedef core_u32 u32;typedef core_u8 u8;
u32 core_smz_be(const void *data)
{const u8 *p=data;return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static void put(u8 *p,u32 n)
{p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
u32 core_smz_adler(u32 sum,const u8 *p,u32 n)
{
    u32 a=sum&65535u,b=sum>>16;
    while(n--){a+=*p++;if(a>=65521u)a-=65521u;b+=a;if(b>=65521u)b-=65521u;}
    return b<<16|a;
}
static int span(u32 at,u32 n,u32 total){return at<=total && n<=total-at;}
static int read_at(const struct core_smz_reader *r,u32 at,void *p,u32 n)
{return span(at,n,r->bytes) && r->read(r->context,at,p,n)==0;}
static int word(const struct core_smz_reader *r,u32 at,u32 *v)
{u8 b[4];if(!read_at(r,at,b,4))return 0;*v=core_smz_be(b);return 1;}
static int string(const struct core_smz_reader *r,u32 base,u32 bytes,u32 at,u32 max)
{
    u8 c;u32 i;
    if(at>=bytes)return 0;
    for(i=0;i<max && i<bytes-at;i++){
        if(!read_at(r,base+at+i,&c,1))return 0;
        if(!c)return i!=0;
        if(c<32 || c>126)return 0;
    }
    return 0;
}
static int relocated(const struct core_smz_reader *r,const struct core_smz_view *v,u32 at)
{
    u32 lo=0,hi=v->nrel,mid,x;
    while(lo<hi){mid=lo+(hi-lo)/2;if(!word(r,v->relocs+4*mid,&x))return 0;
        if(x<at)lo=mid+1;else hi=mid;}
    return lo<v->nrel && word(r,v->relocs+4*lo,&x) && x==at;
}
static int immutable(const struct core_smz_reader *r,const struct core_smz_view *v,u32 at,u32 n)
{
    u32 lo=0,hi=v->nrel,mid,x;
    while(lo<hi){mid=lo+(hi-lo)/2;if(!word(r,v->relocs+4*mid,&x))return 0;
        if(x+4<=at)lo=mid+1;else hi=mid;}
    return lo==v->nrel || (word(r,v->relocs+4*lo,&x) && x>=at+n);
}
static int text(const struct core_smz_reader *r,const struct core_smz_view *v,u32 at,u32 max)
{
    u32 n=0;u8 c;
    if(!string(r,v->image,v->bytes,at,max))return 0;
    do{if(!read_at(r,v->image+at+n,&c,1))return 0;n++;}while(c);
    return immutable(r,v,at,n);
}
volatile u32 core_smz_failure;
int core_smz_validate(const struct core_smz_reader *r,struct core_smz_view *v)
{
    u8 h[80],buf[512],d[40],p[24],ae[16];
    u32 n,at,k,i,j,sum=1,expected,main_end,prev=0,x,y,types=0,audio_types=0;
    u32 count,params,param_at,table,code_lo,asz,acount,abytes,audio_sum;
    core_smz_failure=0;
    if(r->bytes<80 || r->bytes>CORE_MODS_MAX || (r->bytes&15) || !read_at(r,0,h,80)){core_smz_failure=__LINE__;return 0;}
    if(core_smz_be(h)!=0x53414645u || core_smz_be(h+4)!=2 ||
       core_smz_be(h+8)!=CORE_SMZ_ABI || core_smz_be(h+12)!=r->bytes ||
       core_smz_be(h+20)!=48){core_smz_failure=__LINE__;return 0;}
    /* Bit 1 adds a machine-only UI callback directory, never a patch plan. */
    if(core_smz_be(h+24)&~7u){core_smz_failure=__LINE__;return 0;}
    v->ui=core_smz_be(h+24)&2u?core_smz_be(h+28):CORE_SMZ_NONE;
    if(!(core_smz_be(h+24)&2u) && core_smz_be(h+28)){core_smz_failure=__LINE__;return 0;}
    v->modules=core_smz_be(h+32);v->nmods=core_smz_be(h+36);
    if(core_smz_be(h+24)&4u){
        if(!v->nmods || v->nmods>16 || (v->modules&3)){core_smz_failure=__LINE__;return 0;}
    }else if(v->modules || v->nmods){core_smz_failure=__LINE__;return 0;}
    v->descriptors=core_smz_be(h+40);v->count=count=core_smz_be(h+44);
    if((!count && !v->nmods) || count>CORE_SMZ_MACHINES || (v->descriptors&3) ||
       (!count && v->ui!=CORE_SMZ_NONE)){core_smz_failure=__LINE__;return 0;}
    if(core_smz_be(h+48)!=0x534d5a34u || core_smz_be(h+60)!=0 ||
       core_smz_be(h+64)!=CORE_SMZ_NONE){core_smz_failure=__LINE__;return 0;}
    v->bytes=core_smz_be(h+52);v->nrel=core_smz_be(h+56);v->relocs=80;
    if(v->nrel>(r->bytes-80)/4){core_smz_failure=__LINE__;return 0;}
    v->image=80+4*v->nrel;
    if(!v->bytes || (v->bytes&3) || !span(v->image,v->bytes,r->bytes) ||
       !span(v->descriptors,40*count,v->bytes)){core_smz_failure=__LINE__;return 0;}
    main_end=v->image+v->bytes;
    if(main_end>CORE_SMZ_MAIN_MAX){core_smz_failure=__LINE__;return 0;}
    x=core_smz_be(h+72);
    if(x>r->bytes-48){core_smz_failure=__LINE__;return 0;}
    v->audio=48+x;v->audio_bytes=core_smz_be(h+76);
    if((v->audio&15) || v->audio<main_end || v->audio_bytes<32 ||
       v->audio_bytes>CORE_SMZ_AUDIO_MAX || !span(v->audio,v->audio_bytes,r->bytes)){core_smz_failure=__LINE__;return 0;}
    abytes=(v->audio_bytes+CORE_SMZ_PAGE-1)&~(CORE_SMZ_PAGE-1);
    if(!span(v->audio,abytes,r->bytes) || v->audio+abytes!=r->bytes){core_smz_failure=__LINE__;return 0;}
    expected=core_smz_be(h+16);
    for(at=20;at<r->bytes;at+=n){
        n=r->bytes-at;if(n>sizeof(buf))n=sizeof(buf);
        if(!read_at(r,at,buf,n)){core_smz_failure=__LINE__;return 0;}sum=core_smz_adler(sum,buf,n);
    }
    if(sum!=expected){core_smz_failure=__LINE__;return 0;}
    sum=0;
    for(at=80;at<main_end;at+=4){if(!word(r,at,&x)){core_smz_failure=__LINE__;return 0;}sum+=x;}
    if(sum!=core_smz_be(h+68)){core_smz_failure=__LINE__;return 0;}
    /* All alignment padding is canonical zero. */
    for(at=main_end;at<v->audio;at++){u8 c;if(!read_at(r,at,&c,1)||c){core_smz_failure=__LINE__;return 0;}}
    for(at=v->audio+v->audio_bytes;at<r->bytes;at++){u8 c;if(!read_at(r,at,&c,1)||c){core_smz_failure=__LINE__;return 0;}}
    for(i=0;i<v->nrel;i++){
        if(!word(r,80+4*i,&x) || (x&1) || (i && x<prev) || !span(x,4,v->bytes) ||
           !word(r,v->image+x,&y) || y>=v->bytes){core_smz_failure=__LINE__;return 0;}
        prev=x+4;
    }
    for(i=0;i<count;i++){
        table=v->descriptors+40*i;
        if(!read_at(r,v->image+table,d,40)){core_smz_failure=__LINE__;return 0;}
        if(!immutable(r,v,table,8) || !immutable(r,v,table+28,12)){core_smz_failure=__LINE__;return 0;}
        x=core_smz_be(d);params=core_smz_be(d+4);param_at=core_smz_be(d+20);
        if(x<64 || x>=80 || (types&(1u<<(x-64))) || !params || params>CORE_SMZ_STORED_PARAMS ||
           (param_at&3) || !span(param_at,24*params,v->bytes) ||
           /* OS 1.41's native menu/icon dispatch supports categories 1..14. */
           core_smz_be(d+28)!=44 || core_smz_be(d+32)-1u>=14u ||
           core_smz_be(d+36)>127){core_smz_failure=__LINE__;return 0;}
        types|=1u<<(x-64);
        for(j=8;j<=16;j+=4)if(!relocated(r,v,table+j) ||
            !text(r,v,core_smz_be(d+j),16)){core_smz_failure=__LINE__;return 0;}
        if(!relocated(r,v,table+20)){core_smz_failure=__LINE__;return 0;}
        x=core_smz_be(d+24);
        if(x!=CORE_SMZ_NONE && ((x&1) || !span(x,2,v->bytes) || !relocated(r,v,table+24))){core_smz_failure=__LINE__;return 0;}
        for(j=0;j<params;j++){
            k=param_at+24*j;
            if(!read_at(r,v->image+k,p,24) || !relocated(r,v,k) || !relocated(r,v,k+4) ||
               !text(r,v,core_smz_be(p),9) || !immutable(r,v,k+8,16) ||
               !text(r,v,core_smz_be(p+4),64)){core_smz_failure=__LINE__;return 0;}
            x=core_smz_be(p+8);y=core_smz_be(p+12);n=core_smz_be(p+16);
            if(y>0xffff || x>y || n<x || n>y || core_smz_be(p+20)>0x1f8){core_smz_failure=__LINE__;return 0;}
        }
    }
    if(!read_at(r,v->audio,ae,16) || core_smz_be(ae)!=0x534d4133u ||
       core_smz_be(ae+8)!=v->audio_bytes){core_smz_failure=__LINE__;return 0;}
    audio_sum=core_smz_be(ae+12);sum=1;
    for(at=v->audio+16;at<v->audio+v->audio_bytes;at+=n){
        n=v->audio+v->audio_bytes-at;if(n>sizeof(buf))n=sizeof(buf);
        if(!read_at(r,at,buf,n)){core_smz_failure=__LINE__;return 0;}sum=core_smz_adler(sum,buf,n);
    }
    if(sum!=audio_sum){core_smz_failure=__LINE__;return 0;}
    acount=core_smz_be(ae+4);if(acount!=count){core_smz_failure=__LINE__;return 0;}
    asz=v->audio_bytes;code_lo=16+16*count+CORE_SMZ_EXT_STATE;
    if(code_lo>asz || (acount && code_lo==asz)){core_smz_failure=__LINE__;return 0;}
    for(i=0;i<count;i++){
        if(!read_at(r,v->audio+16+16*i,ae,16)){core_smz_failure=__LINE__;return 0;}
        x=core_smz_be(ae);if(x<64 || x>=80 || (audio_types&(1u<<(x-64))) || core_smz_be(ae+12)){core_smz_failure=__LINE__;return 0;}
        audio_types|=1u<<(x-64);
        for(j=4;j<=8;j+=4){y=core_smz_be(ae+j);
            if((y&1) || y<CORE_SMZ_AUDIO+code_lo || y>CORE_SMZ_AUDIO+asz-2){core_smz_failure=__LINE__;return 0;}}
    }
    if(types!=audio_types){core_smz_failure=__LINE__;return 0;}
    if(v->ui!=CORE_SMZ_NONE){
        if((v->ui&3) || !span(v->ui,32*count,v->bytes)){core_smz_failure=__LINE__;return 0;}
        for(i=0;i<count;i++){
            u32 u=v->ui+32*i,desc=v->descriptors+40*i,pages,layout,index;
            if(!word(r,v->image+u,&x) || !word(r,v->image+desc,&y) || x!=y ||
               !immutable(r,v,u,8) || !word(r,v->image+u+4,&pages) || !pages || pages>4 ||
               !word(r,v->image+desc+4,&params) || !word(r,v->image+u+8,&layout) ||
               !relocated(r,v,u+8) || (layout&3) || !span(layout,32*pages,v->bytes) ||
               !immutable(r,v,layout,32*pages)){core_smz_failure=__LINE__;return 0;}
            for(j=0;j<8*pages;j++){
                if(!word(r,v->image+layout+4*j,&index) || (index!=CORE_SMZ_NONE && index>=params)){
                    core_smz_failure=__LINE__;return 0;}
            }
            for(j=12;j<32;j+=4){
                if(!word(r,v->image+u+j,&x) ||
                   (x==CORE_SMZ_NONE?!immutable(r,v,u+j,4):
                    ((x&1) || !span(x,2,v->bytes) || !relocated(r,v,u+j)))){
                    core_smz_failure=__LINE__;return 0;}
            }
        }
    }
    if(v->nmods){
        u32 u,subs,nsubs,fn;
        if(!span(v->modules,16*v->nmods,v->bytes)){core_smz_failure=__LINE__;return 0;}
        for(i=0;i<v->nmods;i++){
            u=v->modules+16*i;
            if(!word(r,v->image+u,&x) || !relocated(r,v,u) || !text(r,v,x,32) ||
               !word(r,v->image+u+4,&fn) || (fn==CORE_SMZ_NONE?!immutable(r,v,u+4,4):
                   ((fn&1) || !span(fn,2,v->bytes) || !relocated(r,v,u+4))) ||
               !immutable(r,v,u+12,4) || !word(r,v->image+u+12,&nsubs) || nsubs>16 ||
               !word(r,v->image+u+8,&subs) ||
               (nsubs?((subs&3) || !relocated(r,v,u+8) || !span(subs,8*nsubs,v->bytes)):
                       (subs!=CORE_SMZ_NONE || !immutable(r,v,u+8,4)))){core_smz_failure=__LINE__;return 0;}
            for(j=0;j<nsubs;j++)if(!immutable(r,v,subs+8*j,4) || !word(r,v->image+subs+8*j,&x) ||
                !(x<12 || x==0x101 || x==0x102 || x==0x103 || x==0x302) ||
                !word(r,v->image+subs+8*j+4,&fn) || (fn&1) || !span(fn,2,v->bytes) ||
                !relocated(r,v,subs+8*j+4)){core_smz_failure=__LINE__;return 0;}
        }
    }
    return 1;
}
int core_smz_memory_read(void *ctx,u32 at,void *out,u32 n)
{const u8 *p=(const u8 *)ctx+at;u8 *q=out;while(n--)*q++=*p++;return 0;}
void core_smz_relocate(u8 *file,const struct core_smz_view *v)
{
    u32 i,at,base=(u32)(file+v->image);
    for(i=0;i<v->nrel;i++){at=core_smz_be(file+v->relocs+4*i);
        put(file+v->image+at,base+core_smz_be(file+v->image+at));}
}
