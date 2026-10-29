/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real core C on a native 32-bit host; sparse fake block device, no emulator.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mods/core-st/hzfs.c"
#include "../mods/core-st/file-info.h"
#define STOCK_BODY (64u+48000u*5u*2u)
extern int core_sample_write_guard(void *,const struct info *,void *);
extern int core_high_route(void *,const char *const *,void *);
extern void core_high_check_header(void *,const struct fn *,const u8 *);
extern void core_high_written(const struct fn *,const u8 *);
static u8 disk[12][ST_HZ_WINDOW];
static u32 writes,reads,live_allocs,alloc_fail,read_fail,write_fail,sink_bytes;
static u32 source_fail,fail_after_write;
static const char *error_message;
u32 fw_mmc_ready[]={1},fw_mmc_sectors[]={0x3a4000},fw_empty_string[]={0};
void core_fn_manager(void){}
void core_high_validate(void){}
static int disk_io(u32 lba,u32 n,void *p,int writing)
{
    u32 k=lba>>10,off=(lba&1023)*512;
    assert(k>=ST_HZ_FIRST && k<ST_HZ_FIRST+12 && n && !(n&511) && off+n<=ST_HZ_WINDOW);
    if(writing){writes++;if(write_fail || (fail_after_write && writes>=fail_after_write))return -1;
        memcpy(disk[k-ST_HZ_FIRST]+off,p,n);
    }else{reads++;if(read_fail)return -1;memcpy(p,disk[k-ST_HZ_FIRST]+off,n);}return 0;
}
int fw_mmc_read(u32 lba,u32 n,void *p){return disk_io(lba,n,p,0);}
int fw_mmc_write(u32 lba,u32 n,const void *p){return disk_io(lba,n,(void *)p,1);}
void *fw_malloc(u32 n){void *p;if(alloc_fail)return 0;p=malloc(n);if(p)live_allocs++;return p;}
void fw_free(void *p){assert(p && live_allocs);live_allocs--;free(p);}
void fw_info_copy(void *a,const void *b){memcpy(a,b,sizeof(struct info));}
void fw_string_ctor(void *out,const char *s,void *allocator)
{(void)allocator;*(void **)out=fw_empty_string;error_message=s;}
static const struct st_storage_io *native=&core_hzfs_native_io;
static u8 sample[ST_HZ_WINDOW+130];
static int source(void *opaque,u32 offset,u32 n,void *p)
{(void)opaque;if(source_fail && offset>=4096)return -1;memcpy(p,sample+offset,n);return 0;}
static int sink(void *opaque,u32 offset,u32 n,void *p)
{(void)opaque;assert(!memcmp(sample+offset,p,n));sink_bytes+=n;return 0;}
static void check_count(u32 expected)
{struct st_hzfs fs;assert(core_hzfs_open(&fs,native)==1);assert(core_hzfs_count(&fs)==expected);core_hzfs_close(&fs);assert(!live_allocs);}
static void file_guards(void)
{
    struct info info;u32 out[33],before=writes,k;u8 h[31]={0};const char *path;
    memset(&info,0,sizeof(info));info.address=0x02380000;info.type=8;info.kind=2;info.bytes=STOCK_BODY;
    assert(!core_sample_write_guard(0,&info,out));
    info.bytes=STOCK_BODY+2;assert(core_sample_write_guard(0,&info,out)==1 && !*(u8 *)out);
    info.bytes=ST_HZ_WINDOW;assert(core_sample_write_guard(0,&info,out)==1);
    info.bytes=0xffffffffu;assert(core_sample_write_guard(0,&info,out)==1);
    info.bytes=STOCK_BODY;info.address++;assert(core_sample_write_guard(0,&info,out)==1);
    info.address=0x00400000;assert(!core_sample_write_guard(0,&info,out));
    info.bytes=ST_HZ_WINDOW+1;assert(core_sample_write_guard(0,&info,out)==1);
    path="/samples/9721";assert(core_high_route(0,&path,out)==1 && *(u8 *)out);
    memcpy(&info,(u8 *)out+8,sizeof(info));assert(info.address==0x30000000 && info.id==9721 && info.permissions==6);
    assert(info.written.manager==(void *)core_fn_manager && info.written.invoke==(void *)core_high_written);
    ((void (*)(const struct fn *,const u8 *))info.written.invoke)(&info.written,h);
    info.bytes=0x1ff000;assert(!core_sample_write_guard(0,&info,out));
    wr(h+13,8);wr(h+21,info.id);wr(h+25,info.bytes);
    core_high_check_header(out,&info.validate,h);assert(*(u8 *)out);
    wr(h+21,64);core_high_check_header(out,&info.validate,h);assert(!*(u8 *)out);
    info.bytes=0xffffffffu;assert(core_sample_write_guard(0,&info,out)==1);
    k=(fw_mmc_sectors[0]>>10)-1;info.validate.data=k;info.address=k<<19;info.id=8185+k;
    info.bytes=ST_HZ_WINDOW;assert(!core_sample_write_guard(0,&info,out));
    info.bytes++;assert(core_sample_write_guard(0,&info,out)==1);
    path="/samples/16376";assert(core_high_route(0,&path,out)==1 && *(u8 *)out);
    memcpy(&info,(u8 *)out+8,sizeof(info));assert(info.bytes==0 && info.permissions==2);
    assert(core_sample_write_guard(0,&info,out)==1);
    path="/samples/64";assert(!core_high_route(0,&path,out));
    path="/samples/09721";assert(!core_high_route(0,&path,out));
    path="/samples/4294977017";assert(!core_high_route(0,&path,out));
    path="/samples/9721/";assert(!core_high_route(0,&path,out));
    assert(before==writes && error_message);
}
int main(void)
{
    struct st_hzfs fs;struct st_sample_entry e;u32 before,a=0,b=0,i,seq;
    file_guards();
    memset(disk,255,sizeof(disk));disk[1][ST_HZ_WINDOW-1]=0x73;before=writes;
    assert(core_hzfs_ensure(native)==ST_HZ_CORRUPT && writes==before);disk[1][ST_HZ_WINDOW-1]=255;
    read_fail=1;assert(core_hzfs_ensure(native)==ST_HZ_IO && writes==before);read_fail=0;
    alloc_fail=1;assert(core_hzfs_ensure(native)==ST_HZ_MEMORY && writes==before);alloc_fail=0;
    assert(core_hzfs_ensure(native)==1 && writes==before+2);check_count(0);
    assert(rd(disk[0])==0x485a4653 && rd(disk[0]+24)==0xe90);
    /* Shared Python/C golden catalogue CRC. */
    assert(rd(disk[0]+32)==0x1b940a04u);
    before=writes;assert(core_hzfs_ensure(native)==1 && writes==before);
    for(i=0;i<sizeof(sample);i++)sample[i]=(u8)i;
    memset(sample,0,64);wr(sample,0x53414d50);wr(sample+8,(sizeof(sample)-64)/2);
    assert(core_hzfs_put(native,"long",sizeof(sample),123,source,0,&a)==1 && a==1);check_count(1);
    assert(core_hzfs_open(&fs,native)==1);assert(core_hzfs_entry(&fs,0,&e)==1);
    assert(e.first==ST_HZ_DATA && e.blocks==2 && e.bytes==sizeof(sample));core_hzfs_close(&fs);
    assert(core_hzfs_get(native,a,sink,0)==1 && sink_bytes==sizeof(sample));assert(!live_allocs);
    before=writes;assert(core_hzfs_put(native,"long",sizeof(sample),123,source,0,&b)==ST_HZ_EXISTS && writes==before);
    source_fail=1;assert(core_hzfs_put(native,"interrupted",sizeof(sample),0,source,0,&b)==ST_HZ_IO);
    source_fail=0;check_count(1);
    /* Failed catalogue publication leaves the old entry visible. */
    fail_after_write=writes+1;assert(core_hzfs_remove(native,a)==ST_HZ_IO);fail_after_write=0;check_count(1);
    assert(core_hzfs_remove(native,a)==1);check_count(0);
    assert(core_hzfs_put(native,"second",sizeof(sample),0,source,0,&b)==1 && b==2);
    assert(core_hzfs_open(&fs,native)==1);assert(core_hzfs_entry(&fs,0,&e)==1);
    assert(e.first==ST_HZ_DATA+2); /* Deleted entry still reserved by older valid index. */
    core_hzfs_close(&fs);
    sink_bytes=0;disk[4][100]^=1;
    assert(core_hzfs_get(native,b,sink,0)==ST_HZ_CORRUPT && !sink_bytes);disk[4][100]^=1;
    assert(core_hzfs_remove(native,b)==1);check_count(0);
    /* CRC-valid malformed extents cannot be ignored in favour of another copy. */
    assert(core_hzfs_put(native,"bad",sizeof(sample),0,source,0,&b)==1);
    assert(core_hzfs_open(&fs,native)==1);i=fs.best;seq=rd(fs.copy[i]+8);core_hzfs_close(&fs);
    wr(disk[i]+ST_HZ_ENTRIES+4,0x5ff);wr(disk[i]+32,checksum(disk[i],rd(disk[i]+36)));before=writes;
    assert(core_hzfs_ensure(native)==ST_HZ_CORRUPT && writes==before && rd(disk[i]+8)==seq);
    assert(!live_allocs && reads);
    puts("PASS: high-zone format/CRC, streamed put/get, delete, aborted writes, union allocation, corruption, slot 64 and card guards");
    return 0;
}
