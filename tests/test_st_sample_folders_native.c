/* Real storage implementation and native UI fixtures; no device access. */
#define main legacy_storage_main
#include "test_core_st_storage.c"
#undef main
int main(int argc,char **argv)
{
    struct st_hzfs fs;u32 i,new_id=0,len;FILE *f;u8 *p;
    assert(argc==3);assert(legacy_storage_main()==0);
    memset(disk,255,sizeof disk);writes=0;
    f=fopen(argv[1],"rb");assert(f);len=(u32)fread(disk[0],1,ST_HZ_WINDOW,f);fclose(f);
    memcpy(disk[1],disk[0],ST_HZ_WINDOW);
    /* Populate the fixture's first PCM extent with the legacy test source. */
    memcpy(disk[2],sample,ST_HZ_WINDOW);memcpy(disk[3],sample+ST_HZ_WINDOW,sizeof sample-ST_HZ_WINDOW);
    assert(core_hzfs_open(&fs,native)==1 && core_hzfs_count(&fs)==2);
    assert(st_hz_folder_count(fs.copy[fs.best])==2 && st_hz_member(fs.copy[fs.best],0)==2);
    p=fs.copy[fs.best];assert(rd(p+36)==len);core_hzfs_close(&fs);
    sink_bytes=0;assert(core_hzfs_get(native,1,sink,0)==1 && sink_bytes==sizeof sample);
    assert(core_hzfs_put(native,"recorded",sizeof sample,0,source,0,&new_id)==1 && new_id==3);
    assert(core_hzfs_remove(native,1)==1);
    assert(core_hzfs_open(&fs,native)==1);p=fs.copy[fs.best];
    assert(core_hzfs_count(&fs)==2 && rd(p+4)==2 && st_hz_folder_count(p)==2);
    assert(st_hz_member(p,0)==0 && st_hz_member(p,1)==0);
    assert(rd(st_hz_folder_record(p,0))==1 && rd(st_hz_folder_record(p,1)+4)==1);
    f=fopen(argv[2],"wb");assert(f);assert(fwrite(p,1,fs.length[fs.best],f)==fs.length[fs.best]);fclose(f);
    core_hzfs_close(&fs);
    /* Invalid memberships/cycles are CRC-valid corruption, never a reason
     * to format or fall back to the other copy and reuse its live extents. */
    for(i=0;i<4;i++) {
        memcpy(disk[0],disk[1],ST_HZ_WINDOW);p=disk[0];
        if(i==0)wr(p+ST_HZ_ENTRIES+64*rd(p+12),999);
        else if(i==1){u8 *d=(u8 *)st_hz_folder_record(p,0);wr(d+4,2);}
        else if(i==2)wr(p+4,99);
        else wr(p+12,8000);
        wr(p+32,checksum(p,rd(p+36)));len=writes;
        assert(core_hzfs_ensure(native)==ST_HZ_CORRUPT && writes==len);
    }
    assert(!live_allocs);puts("PASS: Python/C v2, duplicate leaf names, streamed get/put/remove, folder preservation and corrupt metadata refusal");return 0;
}
