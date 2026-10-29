/* Real registry/page/draw dispatch with real FM/SP layouts. Covers the native
 * SourcePageView route missed by tests that called only base navigation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <syntakt/host.h>
#include "../mods/core-st/machines.c"
#include "../mods/core-st/machine-pages.c"
#include "../mods/core-st/machine-ui.c"
extern const struct core_machine_def fm_tone,sp_one_shot,sp_werp,sp_repitch,sp_slice;
extern const struct st_machine_ui fm_tone_ui,sp_one_ui,sp_werp_ui,sp_repitch_ui,sp_slice_ui;
const struct st_machine_host_api core_machine_host_api={0};
u32 fw_source_layout[11]={1,2};const u32 fw_lane_ids[54]={0};
static unsigned char view[512];static u32 type=44,selecting,allocations,paints,resets,last_type,last_page,guard,encoders;
void *fw_malloc(u32 n){return VirtualAlloc((void *)(0x48000000u+0x10000u*allocations++),n,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);}
void fw_free(void *p){if((u32)p>=0x40000000u)assert(VirtualFree(p,0,MEM_RELEASE));else free(p);}
u32 fw_view_machine(void *v){assert(v==view);return type;}
u32 fw_parameter_selecting(void *m){assert(m==(void *)0x1234);return selecting;}
void fw_reset_increments(void *p){assert(p==view+0x94);resets++;}
void fw_invalidate(void *p){(void)p;}
u32 core_native(u32 op,const u32 *a){(void)a;return op==10?CORE_SMZ_NONE:0;}
u32 core_native_view(u32 op,void *v,u32 b){(void)v;(void)b;return op==10?CORE_SMZ_NONE:op==14?guard:0;}
int core_event_view(u32 event,void *v,void *data,u32 id)
{assert(event==3 && v==view && data==(void *)0x4567);(void)id;encoders++;return 0;}
u32 core_stock_slot_to_engine(u32 s,u32 b){(void)b;return s;}
void core_machine_copy_page(void *p,u32 b){(void)p;(void)b;assert(0);}
const struct st_machine_wave *core_pool_wave(u32 s){(void)s;return 0;}
void core_pool_wave_idle(u32 s,u32 n){(void)s;(void)n;}
int core_pool_sample_name(u32 s,char *out){(void)s;(void)out;return 0;}
const struct st_machine_cursor *core_pool_cursor(u32 t,u32 s){(void)t;(void)s;return 0;}
void core_machine_dispatch_delta(void *v,u32 id,int delta,u32 knob){(void)v;(void)id;(void)delta;(void)knob;assert(0);}
static void paint(const struct st_machine_ui_api *a,const struct core_machine_def *m,void *v,void *ctx,u32 page)
{assert(a==&core_machine_ui_api && v==view && ctx==(void *)0x7777);paints++;last_type=m->type;last_page=page;}
int main(void)
{
    struct core_machine_def d[]={fm_tone,sp_one_shot,sp_werp,sp_repitch,sp_slice};
    struct st_machine_ui u[]={fm_tone_ui,sp_one_ui,sp_werp_ui,sp_repitch_ui,sp_slice_ui};
    u32 i,p,k,id,index,*layout,*old,old_end,old_page,old_alloc,old_resets;
    for(i=0;i<5;i++)u[i].draw=paint;
    core_machine_ui_definitions=u;assert(core_machine_bind(d,5));
    *(u32 *)view=0x401ca0dc;*(u32 *)(view+0x88)=0x14;*(u32 *)(view+0x6c)=0x1234;
    *(u32 **)(view+0x7c)=malloc(4);**(u32 **)(view+0x7c)=6;
    *(u32 **)(view+0x80)=*(u32 **)(view+0x84)=*(u32 **)(view+0x7c)+1;
    for(i=0;i<5;i++){
        /* Merely highlighting any new machine must neither paint nor resize
         * the underlying view. Test the native flag's low-byte bool ABI. */
        old=*(u32 **)(view+0x7c);old_end=*(u32 *)(view+0x80);old_page=*(u32 *)(view+0x90);
        old_alloc=allocations;old_resets=resets;type=d[i].type;selecting=0xff;
        assert(!core_machine_syn_view(view) && !core_machine_view_layout(view));
        assert(core_machine_nav(view,0)==CORE_SMZ_NONE);
        assert(core_machine_editor_event(view,(void *)0x4567,core_machine_engine(d+i))==1 && !encoders);
        core_machine_draw(view,(void *)0x7777);assert(paints==i);
        assert(*(u32 **)(view+0x7c)==old && *(u32 *)(view+0x80)==old_end && *(u32 *)(view+0x90)==old_page);
        assert(allocations==old_alloc && resets==old_resets);
        /* Confirm: all pages and every knob resolve through the very same
         * view-layout service used by SourcePageView, including page zero. */
        selecting=0;assert(core_machine_syn_view(view));core_machine_sync(view);
        if(type==66 || type==67 || type==69){
            /* Ordinary cached metadata must not reapply the stock five-note
             * FUNC quantizer; encoder-only lookup still borrows fine TUNE. */
            u32 args[2]={core_machine_engine(d+i),0},result;
            assert(!core_machine_query(ST_CACHED,args,0,&result) && args[0]==0x13);
            args[0]=core_machine_engine(d+i);
            assert(!core_machine_query(ST_ENCODER_CACHED,args,0,&result) && args[0]==0x1f1);
        }
        guard=1;assert(core_machine_editor_event(view,(void *)0x4567,core_machine_engine(d+i))==1 && !encoders);guard=0;
        assert(!core_machine_editor_event(view,(void *)0x4567,core_machine_engine(d+i)) && encoders==1);encoders=0;
        for(p=0;p<u[i].pages;p++){
            *(u32 *)(view+0x90)=p;layout=core_machine_view_layout(view);assert(layout);
            for(k=0;k<8;k++){
                index=u[i].layout[8*p+k];id=index<d[i].params?core_machine_engine(d+i)+index:0;
                assert(layout[2+k]==id && core_machine_nav(view,k)==id);
            }
        }
        core_machine_draw(view,(void *)0x7777);assert(paints==i+1 && last_type==type && last_page==u[i].pages-1);
    }
    *(u32 *)(view+0x88)=0x12;assert(!core_machine_view_layout(view));core_machine_draw(view,(void *)0x7777);assert(paints==5);
    fw_free(*(void **)(view+0x7c));
    core_machine_active=0;core_machine_draw((void *)1,(void *)1);assert(paints==5);
    puts("PASS: SourcePageView resolves all FM/SP pages through one layout service; selector highlight paints nothing/resizes nothing; confirmation enables shared drawing; non-SYN views excluded");return 0;
}
