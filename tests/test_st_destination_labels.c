/* Production registry/label query with every current FM/SP descriptor. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "../mods/core-st/machines.c"
extern const struct core_machine_def fm_tone,sp_one_shot,sp_werp,sp_repitch,sp_slice;
const struct st_machine_action_codec *core_machine_action_codec;
u32 fw_source_layout[11]={0};const u32 fw_lane_ids[54]={0};
void *fw_malloc(u32 n){return VirtualAlloc((void *)0x48000000u,n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
void fw_free(void *p){assert(VirtualFree(p,0,MEM_RELEASE));}
void core_machine_copy_page(void *p,u32 b){(void)p;(void)b;assert(0);}
u32 core_stock_slot_to_engine(u32 s,u32 b){(void)b;return s;}
u32 *core_machine_action_layout(u32 id){(void)id;return 0;}
const char *core_machine_format(u32 id,u32 raw){(void)id;(void)raw;return 0;}
u32 core_machine_policy(u32 id){(void)id;return CORE_SMZ_NONE;}
int main(void)
{
    struct core_machine_def defs[5]={fm_tone,sp_one_shot,sp_werp,sp_repitch,sp_slice};
    static const char *const labels[5][28]={
      {"ALGO","RTOC","RTOA","RTOB","HARM","DTUN","FDBK","MIX","ATKA","DECA","ENDA","LEVA",
       "ATKB","DECB","ENDB","LEVB","ADEL","ATRG","ARST","PHRT","BDEL","BTRG","BRST","C","---","A","B1","B2"},
      {"TUNE","PLAY","BR","SAMP","STRT","LEN","LOOP","OVER"},
      {"TUNE","PLAY","BR","SAMP","SEG","MODE","BARS","OVER"},
      {"TUNE","PLAY","BR","SAMP","STRT","LEN","BARS","OVER"},
      {"TUNE","PLAY","BR","SAMP","SLCE","LEN","GRID","OVER"}};
    u32 i,j,args[2],result=0,count=0;assert(core_machine_bind(defs,5));
    for(i=0;i<5;i++)for(j=0;j<32;j++){
        args[0]=0;args[1]=core_machine_engine(defs+i)+j;
        if(j<defs[i].params){
            assert(core_machine_query(ST_LABEL,args,0,&result));
            assert(!strcmp((const char *)result,labels[i][j]));count++;
        }else assert(!core_machine_query(ST_LABEL,args,0,&result));
    }
    args[1]=0x21a;assert(core_machine_query(ST_LONG_LABEL,args,0,&result));
    assert(!strcmp((const char *)result,"RATIO C"));
    for(j=0;j<0x1f9;j++){args[1]=j;assert(!core_machine_query(ST_LABEL,args,0,&result));}
    for(j=0;j<3;j++){args[1]=(u32[]){0x3f9,0x7fffffff,0xffffffffu}[j];assert(!core_machine_query(ST_LABEL,args,0,&result));}
    core_machine_active=0;args[1]=0x219;assert(!core_machine_query(ST_LABEL,args,0,&result));
    puts("PASS: 60 FM/SP short labels, hidden/extra controls, unused positions, all 505 native IDs, invalid/absent bank fallback; long names unchanged");return 0;
}
