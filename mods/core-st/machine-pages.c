/* SPDX-License-Identifier: GPL-2.0-or-later
 * Four native SYN pages. Only this view's owned page vector is resized.
 * Empty positions have control ID zero; LEVEL remains the native ninth dial.
 */
#include "machines.h"
#include <syntakt/native.h>
extern unsigned int core_native(unsigned int,const unsigned int *);
extern unsigned int core_native_view(unsigned int,void *,unsigned int);
typedef st_u32 u32;typedef core_u8 u8;
extern void *fw_malloc(u32);extern void fw_free(void *);
extern u32 fw_view_machine(void *);extern void fw_reset_increments(void *);
static u32 *override_layout;
extern void core_machine_action_select(const u32 *);
static int sync_view(void *raw,const struct core_machine_def *m)
{
    u8 *view=raw;u32 *old,*now,bytes,pages,i;
    old=*(u32 **)(view+0x7c);
    pages=m?(m->params+7)/8:1;
    if(core_machine_ui(m))pages=core_machine_ui(m)->pages;
    bytes=4*pages;
    if((u8 *)*(void **)(view+0x80)-(u8 *)old!=(int)bytes){
        now=old;
        if((u8 *)*(void **)(view+0x84)-(u8 *)old<(int)bytes){
            now=fw_malloc(bytes);if((u32)now<0x40000000u)return 0;
            *(u32 **)(view+0x7c)=now;*(u32 **)(view+0x84)=now+pages;fw_free(old);
        }
        for(i=0;i<pages;i++)now[i]=6;
        *(u32 **)(view+0x80)=now+pages;*(u32 *)(view+0x90)=0;
        fw_reset_increments(view+0x94);
    }
    i=*(u32 *)(view+0x90);
    if(i>=pages){*(u32 *)(view+0x90)=0;i=0;}
    return m?(int)i:0;
}
int core_machine_sync(void *view)
{
    core_native_view(ST_NATIVE_PAGE_SYNC,view,0);
    if(!core_machine_active || !core_machine_syn_view(view))return 0;
    return sync_view(view,core_machine_find(fw_view_machine(view)));
}
__attribute__((noinline)) static u32 *view_layout(void *view)
{
    u32 page;const struct core_machine_def *m;
    if(!core_machine_active || !core_machine_syn_view(view))return 0;
    m=core_machine_find(fw_view_machine(view));page=(u32)sync_view(view,m);
    return m?core_machine_layout(m,page):0;
}
u32 *core_machine_view_layout(void *view)
{core_native_view(ST_NATIVE_PAGE_SYNC,view,0);return view_layout(view);}
/* PAGE_NAV subscribers synchronize the view first (native.h), so the
 * per-knob path does not notify PAGE_SYNC again for the same view. */
u32 core_machine_nav(void *view,u32 knob)
{
    u32 *layout;
    u32 result=core_native_view(ST_NATIVE_PAGE_NAV,view,knob);
    if(result!=CORE_SMZ_NONE)return result;
    layout=view_layout(view);
    return layout && knob<8?layout[2+knob]:CORE_SMZ_NONE;
}
u32 core_machine_action_begin(void *view)
{
    u32 old=(u32)override_layout;
    core_native_view(ST_NATIVE_PAGE_BEGIN,view,0);
    override_layout=core_machine_view_layout(view);core_machine_action_select(override_layout);return old;
}
void core_machine_action_end(u32 old){override_layout=(void *)old;core_machine_action_select(override_layout);core_native_view(ST_NATIVE_PAGE_END,0,0);}
/* PAGE_LAYOUT: page 3 (POLY) and the LFO3 action pages 37/38 (0.12.16). */
u32 *core_machine_action_layout(u32 id){return id==6?override_layout:(id==3 || id-37u<2u?(u32 *)core_native(ST_NATIVE_PAGE_LAYOUT,&id):0);}
/* Native Sound records preserve their fixed 172-byte geometry. Copy the 16
 * extended words and last word; slot 0 is kept raw for the extra control,
 * while slots 11/18 retain their native codec. Loaded digital types only. */
extern int (*core_parameter_provider)(u32,u32 *,void *,u32 *);
/* Host API 7 (core 0.12.16): after a successful native Sound codec call the
 * provider (LFO3) sees query 25 {record, sound} or 26 {sound, record}. */
static void sound_codec_notify(u32 q,const void *a,const void *b)
{u32 args[3];if(core_parameter_provider){args[0]=(u32)a;args[1]=(u32)b;args[2]=1;core_parameter_provider(q,args,0,0);}}
void core_machine_sound_saved(void *record,const void *sound)
{
    u8 *r=record;const u8 *s=sound;u32 i;const struct core_machine_def *m;
    m=core_machine_find(s[0xa2]);if(m){
        for(i=0;i<32;i++)r[0x88+i]=s[0x80+i];
        r[0xa8]=s[0xa0];r[0xa9]=s[0xa1];
        if(m->params>24){r[0x1c]=s[0x14];r[0x1d]=s[0x15];}
    }
    sound_codec_notify(25,record,sound);
}
void core_machine_sound_loaded(void *sound,const void *record)
{
    u8 *s=sound;const u8 *r=record;u32 i;const struct core_machine_def *m;
    m=core_machine_find(s[0xa2]);if(m){
        for(i=0;i<32;i++)s[0x80+i]=r[0x88+i];
        s[0xa0]=r[0xa8];s[0xa1]=r[0xa9];
        if(m->params>24){s[0x14]=r[0x1c];s[0x15]=r[0x1d];}
    }
    sound_codec_notify(26,sound,record);
}
