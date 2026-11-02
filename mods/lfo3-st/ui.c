/* SPDX-License-Identifier: GPL-2.0-or-later
 * LFO3 page, values and publication. Ported from Synfact's LFO3 prototypes
 * (research-runtime/lfo3-coherence-20261021: page_proto, ui_bridge_proto,
 * widget_proto, frame_proto) with elekloader's IDs and core services:
 * PAGE_SYNC/PAGE_NAV for the core-owned key/control sites and the host API 7
 * parameter provider for metadata. Records live per kit Sound/FX and per pool
 * Sound in this module's memory (not saved in this stage).
 */
#include <syntakt/native.h>
#include <syntakt/host.h>
typedef unsigned int u32;typedef int s32;typedef unsigned short u16;typedef unsigned char u8;
#define FIELD(v,o) (*(u32 *)((u8 *)(v)+(o)))
#define LFO3_ID 0x3d9u                     /* engine IDs (type-79 block), LFO2 order */
#define LFO3_UI 0x4c0u
#define LFO_VIEW 0x402400ccu
typedef struct {u16 v[8];} rec_t;
struct lfo3_bank {u8 state[13][40];u32 rng[2];u16 params[13][8];u16 overlay[13][8];};
extern struct lfo3_bank *lfo3_bank;
extern volatile u32 lfo3_params_dirty;   /* locks_runtime.c; written under frame.busy */
/* Records: shadow.c (Synfact shadow layer, persisted with kits/projects). */
typedef struct {u32 addr,size,tag,seq,count;rec_t r[13];} side_t;
extern rec_t kit_shadow[128][13],pool_shadow[128];
extern side_t side[8];
extern u32 kits_base,pool_base;
extern rec_t *lfo3_shadow_owned_record(u32,u32,u32 *,u32);
extern rec_t *lfo3_shadow_owned_record_locked(u32,u32,u32 *,u32);
extern void lfo3_task_enter(void),lfo3_task_leave(void);
static const u8 word_index[10]={0,1,2,3,4,5,5,6,7,1};
static const u16 defaults[8]={0x7000,0x300,0x4000,0,0,0,0,0x4000};
static const u32 controls[8]={LFO3_ID+0,LFO3_ID+1,LFO3_ID+2,LFO3_ID+3,LFO3_ID+4,LFO3_ID+6,LFO3_ID+7,LFO3_ID+8};
static const char group_name[]="LFO3";
/* Live publication: the Sound/FX each engine was last published from. */
static struct {u32 source[13],valid;volatile u32 busy;u8 type[13];rec_t published[13];} frame;
/* Task-side publication lookups only. A hit retains the record's address,
 * never its values: every publication still checks all words and machine type.
 * Owners invalidate before allocation, release, cell reuse or relocation.
 * A publication holds their writer mutex across consecutive owned lookups;
 * misses release it before the unrelated, potentially expensive side hashes. */
static struct {u32 source;rec_t *record;} publication_owned[13];
static u32 publication_owned_valid;
void lfo3_publication_forget_owned(void){publication_owned_valid=0;}

u32 lfo3_index(u32 id){return id-LFO3_ID<10u?id-LFO3_ID:0xffffffffu;}
static const u32 *descriptor(u32 i){return (const u32 *)(0x4022d59cu+(0x60u+i)*52u);}

void lfo3_records_init(void)
{
    u32 k,t,i;
    for(k=0;k<128;k++){
        for(t=0;t<13;t++)for(i=0;i<8;i++)kit_shadow[k][t].v[i]=defaults[i];
        for(i=0;i<8;i++)pool_shadow[k].v[i]=defaults[i];
    }
}
static u32 record_hash(u32 address,u32 n)
{
    const u8 *p=(const u8 *)address;const u32 *w=(const u32 *)address;
    u32 h=0x811c9dc5u,i=0;
    if(!(address&3u))for(;i+4<=n;i+=4)h=(h^*w++)*0x01000193u;
    for(;i<n;i++)h=(h^p[i])*0x01000193u;
    return h;
}
/* The object's live record: kit/pool shadow, heap owner, or a side entry
 * whose native bytes still match (Synfact throughput_record). */
static rec_t *record_of_scoped(u32 a,u32 fx,u32 track,u32 *locked)
{
    u32 k,off,i,index,size=fx?0x8eu:0x123u;side_t *s;rec_t *owned;
    if(!a)return 0;
    if(kits_base && a>=kits_base && a-kits_base<128u*0x1618u){
        off=a-kits_base;k=off/0x1618u;off-=k*0x1618u;
        if(fx)return off==0xdce?&kit_shadow[k][12]:0;
        if(off>=0x2a && off<0x2a+12u*0x123u && !((off-0x2a)%0x123u))return &kit_shadow[k][(off-0x2a)/0x123u];
        return 0;
    }
    if(!fx && pool_base && a>=pool_base && a-pool_base<128u*0x123u && !((a-pool_base)%0x123u))
        return &pool_shadow[(a-pool_base)/0x123u];
    if(locked){
        if(!*locked){lfo3_task_enter();*locked=1;}
        if((publication_owned_valid&(1u<<track)) && publication_owned[track].source==a)
            return publication_owned[track].record;
        owned=lfo3_shadow_owned_record_locked(a,size,&i,0);
        if(owned){
            publication_owned[track].source=a;publication_owned[track].record=owned;
            publication_owned_valid|=1u<<track;return owned;
        }
        publication_owned_valid&=~(1u<<track);
        lfo3_task_leave();*locked=0;
    }else{owned=lfo3_shadow_owned_record(a,size,&i,0);if(owned)return owned;}
    for(i=0;i<8;i++){
        s=&side[i];index=0;
        if(!s->addr)continue;
        if(s->addr==a && s->size==size && s->count==1){}
        else if(s->size==0x1618 && s->count==13 && a>=s->addr){
            off=a-s->addr;
            if(fx && off==0xdce)index=12;
            else if(!fx && off>=0x2a && off<0x2a+12u*0x123u && !((off-0x2a)%0x123u))index=(off-0x2a)/0x123u;
            else continue;
        }else continue;
        if(s->tag==record_hash(s->addr,s->size))return &s->r[index];
    }
    return 0;
}
static rec_t *record_of(u32 a,u32 fx){return record_of_scoped(a,fx,0,0);}

/* ---- engine parameters (SRAM bank), as Synfact prepare_track ---- */
static void prepare_track(u32 t)
{
    u32 slot,id=0,flags=0,i;rec_t r=frame.published[t];
    slot=r.v[3]>>8;
    if((frame.valid&(1u<<t)) && slot<=70 && !(r.v[3]&255)){
        id=t==12?((u32 (*)(u32))0x400a6300u)(slot):((u32 (*)(u32,u32))0x400a63ecu)(slot,frame.type[t]);
        if(id)flags=((u32 (*)(u32))0x400a666au)(id);
    }
    if(!(flags&0x200))r.v[3]=0x7f00;
    for(i=0;i<8;i++)lfo3_bank->params[t][i]=r.v[i];
    lfo3_params_dirty|=1u<<t;                      /* the lock overlay copies this row again */
}
void lfo3_dest_forget(u32);
/* Synfact throughput_publication: the destination is resolved again only
 * when the values, machine type or validity change, not on every publish. */
static void copy_publication(u32 source,u32 track,u32 fx,u32 *locked)
{
    rec_t *r=record_of_scoped(source,fx,track,locked);const u16 *from=r?r->v:defaults;u32 i,bit=1u<<track,type,changed;
    type=r && !fx?*(const u8 *)(source+0xa2):0;
    changed=frame.type[track]!=type || !!(frame.valid&bit)!=(r!=0);
    for(i=0;i<8;i++)if(frame.published[track].v[i]!=from[i]){frame.published[track].v[i]=from[i];changed=1;}
    frame.source[track]=source;frame.type[track]=type;
    if(r)frame.valid|=bit;else frame.valid&=~bit;
    if(changed){prepare_track(track);lfo3_dest_forget(track);}
}
u32 lfo3_track_type(u32 t){return frame.type[t];}
u32 lfo3_track_valid(u32 t){return !!(frame.valid&(1u<<t));}
void lfo3_publication_begin(void){frame.busy++;}
void lfo3_publication_end(u32 source,u32 track,u32 fx,u32 accepted)
{
    if(!frame.busy)return;
    if(accepted && track<13 && (fx?track==12:track<12))copy_publication(source,track,fx,0);
    frame.busy--;
}
u32 lfo3_engine_busy(void){return frame.busy;}
static void refresh(const rec_t *r)
{
    u32 t;
    frame.busy++;
    for(t=0;t<13;t++)if((frame.valid&(1u<<t)) && record_of(frame.source[t],t==12)==r){
        frame.published[t]=*r;prepare_track(t);
    }
    frame.busy--;
}

/* After a task-side shadow mutation (copy, codec, constructor): every
 * published engine takes its source's current record again. */
/* Native LFO reset (40118e24): the same field writes on the LFO3 states;
 * auxiliary fields, RNG and prepared parameters survive (Synfact frame.c). */
void lfo3_native_reset(void)
{
    u32 t;u8 *s;
    if(!lfo3_bank)return;
    for(t=0;t<13;t++){
        s=lfo3_bank->state[t];*(u32 *)(s+0)=*(u32 *)(s+4)=*(u32 *)(s+8)=*(u32 *)(s+12)=0;
        *(u32 *)(s+16)=0x3fffffffu;s[36]=s[37]=0;
    }
}
u32 lfo3_irq_mask(void);
void lfo3_republish(void)
{
    u32 t,locked=0;
    if(lfo3_irq_mask())return;
    frame.busy++;
    for(t=0;t<13;t++)if(frame.source[t])copy_publication(frame.source[t],t,t==12,&locked);
    if(locked)lfo3_task_leave();
    frame.busy--;
}

/* ---- values: a Sound/FX proxy and its real object (Synfact actions) ---- */
static rec_t *proxy_record(void *proxy)
{
    u32 *vt,*inner,a,getter;
    if(!proxy || !(vt=*(u32 **)proxy))return 0;
    getter=vt[0x28/4];if(getter!=0x40012986u && getter!=0x400129ecu)return 0;
    inner=*(u32 **)((u8 *)proxy+0x10);if(!inner || !*inner)return 0;
    a=((u32 (*)(void *))((u32 *)*inner)[0x28/4])(inner);if(!a)return 0;
    return record_of(a,getter==0x400129ecu);
}
void *lfo3_actions_record(void *proxy){return proxy_record(proxy);}   /* lock UI (locks/lock_ui.c) */
static void *view_proxy(void *view,u32 id)
{
    u32 *vt;
    if(!view || !(vt=*(u32 **)view))return 0;
    return (void *)((u32 (*)(void *,u32,s32))vt[0xc4/4])(view,id,-1);
}
/* 40039160 (view, id, locked*) for the LFO3 IDs. With trigs held, as the
 * native/core preview (400391a8): the first selected step within the active
 * track's length whose lock the view reads (vt +90h, 40037942), marked
 * locked; without, the Sound's value. Pool Sound locks have no LFO3 words. */
static s32 held_lock(void *view,u32 id)
{
    u8 *selection=*(u8 **)((u8 *)view+0x6c);u32 length,step;s32 v;
    if(!selection || !(u8)((u32 (*)(void *))0x400c2d2au)(selection) ||
       !(u8)((u32 (*)(void *))0x400c34d6u)(selection))return -1;
    length=((u32 (*)(void *))0x40027f3au)(((void *(*)(void *))0x40019036u)(((void *(*)(void))0x4016e804u)()));
    for(step=0;step<length && step<64;step++)
        if((u8)((u32 (*)(void *,u32))0x400c2d94u)(selection,step) &&
           (v=((s32 (*)(void *,u32,u32))(*(u32 **)view)[0x90/4])(view,step,id))!=-1)return v;
    return -1;
}
s32 lfo3_ui_get(void *view,u32 id,u8 *locked);
s32 lfo3_ui_preview(void *view,u32 id,u8 *locked)
{
    s32 v=lfo3_index(id)<10?held_lock(view,id):-1;
    if(v==-1)return lfo3_ui_get(view,id,locked);
    if(locked)*locked=1;
    return v;
}
s32 lfo3_ui_get(void *view,u32 id,u8 *locked)
{
    u32 i=lfo3_index(id);rec_t *r;
    if(locked)*locked=0;
    if(i>=10)return 0;
    r=proxy_record(view_proxy(view,LFO3_ID));
    return r?r->v[word_index[i]]:defaults[word_index[i]];
}
/* Proxy backend (native Sound/FX getters and setters, relative calculator):
 * the native editor, FUNC snapping, alternate encoders and popups run as for
 * LFO2, with the values in the LFO3 records. */
s32 lfo3_actions_get(void *proxy,u32 id)
{
    u32 i=lfo3_index(id);rec_t *r=proxy_record(proxy);
    if(i>=10)return 0;
    return r?r->v[word_index[i]]:defaults[word_index[i]];
}
u32 lfo3_actions_store(void *proxy,u32 id,s32 value,s32 track,u32 publish,u32 notify)
{
    u32 i=lfo3_index(id);rec_t *r=proxy_record(proxy);
    (void)track;(void)publish;
    if(i>=10 || !r)return 0;
    *(volatile u16 *)&r->v[word_index[i]]=(u16)value;
    refresh(r);
    if(notify)((void (*)(void *))0x40013084u)(proxy);
    return 1;
}
s32 lfo3_native_relative(void *,u32,s32,s32);
s32 lfo3_actions_relative(void *proxy,u32 id,s32 value,s32 delta)
{return lfo3_native_relative(proxy,0x60+lfo3_index(id),value,delta);}
/* Native editor guard (view, id, delta, alternate, blocked*): -1 runs the
 * native editor. `alternate` is the pressed/FUNC encoder flag, passed on to
 * the proxy's relative route (FUNC snapping). Lock recording never reaches
 * LFO3: the editor's lock check (4000d870) has no group for these IDs. */
s32 lfo3_editor_guard(void *view,u32 id,s32 delta,u32 alternate,u8 *blocked)
{
    rec_t *r=proxy_record(view_proxy(view,id));(void)delta;(void)alternate;
    if(r)return -1;
    if(blocked)*blocked=1;
    return defaults[word_index[lfo3_index(id)]];
}
/* DEST: the native destination selectors with LFO3's mask 0x200 (machine,
 * LFO1 and LFO2 targets); an unavailable result keeps the current value. */
s32 lfo3_editor_destination_alt(void *callback,s32 current,s32 delta)
{
    s32 value=((s32 (*)(s32,s32,u32))0x4007c8d0u)(current,delta,0x200);(void)callback;
    return (u32)value<=0x4600u && !(value&255)?value:current;
}
s32 lfo3_editor_destination_relative(void *proxy,u32 id,s32 current,s32 delta)
{
    s32 value=((s32 (*)(void *,s32,s32,u32))0x40014b90u)(proxy,current,delta,0x200);(void)id;
    return (u32)value<=0x4600u && !(value&255)?value:current;
}
typedef struct {u32 begin,end,capacity;} vector_t;
typedef struct {vector_t *v;void *owner;} shared_t;
void lfo3_native_targets(void *,u32,shared_t *),lfo3_native_release(shared_t *);
u32 lfo3_destination_valid(void *model,u32 raw)
{
    shared_t list;u32 slot=(raw&0xffffu)>>8,engine,*p,found=0;
    if(raw>0xffffu || slot>70 || !model)return 0;
    engine=((u32 (*)(void *,u32))(*(u32 **)model)[0x50/4])(model,slot);
    if(!engine)return 0;
    lfo3_native_targets(model,0x200,&list);
    for(p=(u32 *)list.v->begin;(u32)p<list.v->end;p++)if(*p==engine){found=1;break;}
    lfo3_native_release(&list);
    return found;
}

/* ---- LFO view: third page ---- */
u32 lfo3_page_sync(void *view)
{
    u32 begin,end,capacity,index,*p,native_count,wanted;
    if(!view || FIELD(view,0)!=LFO_VIEW || FIELD(view,0x88)!=0x17)return 0;
    begin=FIELD(view,0x7c);end=FIELD(view,0x80);capacity=FIELD(view,0x84);
    if(!begin || (begin&3) || end<begin || capacity<end || (capacity&3) ||
       (end-begin!=4 && end-begin!=8 && end-begin!=12))return 0;
    p=(u32 *)begin;
    if(p[0]==30){
        if(end-begin!=4 && (end-begin!=8 || p[1]!=30))return 0;
        native_count=1;
    }else{
        if(end-begin<8 || p[0]!=4 || p[1]!=5 || (end-begin==12 && p[2]!=5))return 0;
        native_count=2;
    }
    wanted=(native_count+1)*4;
    if(end-begin==native_count*4){
        if(capacity-begin<wanted){
            p=((u32 *(*)(u32))0x40103788u)(wanted);
            if(!p)return 0;
            if(native_count==1){p[0]=30;p[1]=30;}else{p[0]=4;p[1]=5;p[2]=5;}
            FIELD(view,0x7c)=(u32)p;FIELD(view,0x80)=(u32)p+wanted;FIELD(view,0x84)=(u32)p+wanted;
            ((void (*)(void *))0x40103794u)((void *)begin);
        }else{p[native_count]=p[native_count-1];FIELD(view,0x80)=begin+wanted;}
    }
    index=FIELD(view,0x90);
    if(index>native_count){FIELD(view,0x90)=0;((void (*)(void *))0x400f6730u)((u8 *)view+0x94);}
    return 1;
}
u32 lfo3_page_is_third(void *view)
{
    if(!lfo3_page_sync(view))return 0;
    return FIELD(view,0x90)==(*(const u32 *)FIELD(view,0x7c)==30?1u:2u);
}
/* PAGE_NAV result: -1 keeps the native control routine. MULT uses the FX
 * track's alternate range in layout 30; SPH reads as SLEW when WAVE is RND. */
static u32 page_control(void *view,u32 knob)
{
    u8 changed;u32 id;
    if(!lfo3_page_is_third(view))return 0xffffffffu;
    if(knob>=9)return 0;
    if(knob==8)return 0xffffffffu;
    id=controls[knob];
    if(knob==5 && ((s32 (*)(void *,u32,u8 *))0x40039160u)(view,LFO3_ID+4,&changed)==0x600)id=LFO3_ID+5;
    if(id==LFO3_ID+1 && **(const u32 **)((const u8 *)view+0x7c)==30)id=LFO3_ID+9;
    return id;
}
void lfo3_page_wave(void *view,u32 canvas)
{
    u8 changed;s32 (*get)(void *,u32,u8 *)=(s32 (*)(void *,u32,u8 *))0x40039160u;
    int wave,phase,mode,speed,depth;
    if(!lfo3_page_is_third(view))return;
    wave=get(view,LFO3_ID+4,&changed);phase=get(view,LFO3_ID+6,&changed);
    mode=get(view,LFO3_ID+7,&changed);speed=get(view,LFO3_ID+0,&changed);depth=get(view,LFO3_ID+8,&changed);
    ((void (*)(void *,u32,u32,u32,int,int,int,int,int))0x400f322eu)(view,canvas,0,1,wave>>8,phase>>8,mode>>8,speed>>8,depth>>8);
}
const void *lfo3_actions_layout(u32);
int lfo3_page_native(u32 op,const u32 *a,u32 *result)
{
    u32 id;
    if(op==ST_NATIVE_PAGE_LAYOUT){                 /* pages 37/38: LFO3 projections */
        const void *p=lfo3_actions_layout(a[0]);
        if(!p)return 0;
        *result=(u32)p;return 1;
    }
    if(op==ST_NATIVE_PAGE_SYNC){lfo3_page_sync((void *)a[0]);return 0;}
    if(op==ST_NATIVE_PAGE_NAV){
        id=page_control((void *)a[0],a[1]);
        if(id==0xffffffffu)return 0;
        *result=id;return 1;
    }
    return 0;
}

/* ---- widgets ---- */
u8 lfo3_native_range_check(void *,u32,int);
u32 lfo3_widget_valid(void *model,u32 id,int value)
{return lfo3_native_range_check(model,0x60+lfo3_index(id),value);}
const char *lfo3_widget_header(void *view){return lfo3_page_is_third(view)?group_name:0;}
int lfo3_widget_view_get(void *view,u32 id,int track)
{return track<0?lfo3_ui_get(view,id,0):(int)descriptor(lfo3_index(id))[4];}

/* ---- core host API 7 provider: metadata of the LFO3 IDs ---- */
void lfo3_sound_encoded(u32,u32,u32),lfo3_sound_decoded(u32,u32,u32),lfo3_project_new(u32);
void lfo3_lock_ui_store(void *,u32,s32,s32,u32,u32);
int lfo3_parameter_query(u32 q,u32 *args,void *result,u32 *value)
{
    /* core 0.12.16 storage notifications (shadow.c, Synfact Sound codec layer) */
    if(q==25){lfo3_sound_encoded(args[0],args[1],args[2]);return 0;}
    if(q==26){lfo3_sound_decoded(args[0],args[1],args[2]);lfo3_republish();return 0;}
    if(q==27){lfo3_project_new(args[0]);lfo3_republish();return 0;}
    u32 id=q==18 || q==20 || q==24?args[1]:args[0],i=lfo3_index(id),*out=result;const u32 *d;
    if(q==6){if(args[0]-LFO3_UI>=10u)return 0;*value=LFO3_ID+args[0]-LFO3_UI;return 1;}
    if(i>=10)return 0;
    d=descriptor(i);
    switch(q){
    case 13:out[0]=d[2];out[1]=d[3];out[2]=d[4];*value=(u32)out;return 1;   /* range */
    case 18:*value=d[12];return 1;                                         /* short label */
    case 20:*value=d[10];return 1;                                         /* long label */
    case 9:*value=LFO3_UI+i;return 1;                                      /* engine -> UI */
    case 7:                                                                /* no Sound slot, but */
        *value=i==3 && (args[-1]==0x4003b5dcu || args[-1]==0x4003b702u)?LFO3_UI+3:0xffffffffu;
        return 1;                                                          /* DEST popup identity */
    case 21:*value=0;return 1;                                             /* never a destination */
    case 16:case 17:case 23:args[0]=0x60+i;return 0;                       /* LFO2 caches/formats */
    case 24:                                   /* Sound store: value, plus a lock when recording */
        lfo3_lock_ui_store((void *)args[0],id,(s32)args[2],(s32)args[3],args[4],args[5]);return 1;
    }
    return 0;
}
