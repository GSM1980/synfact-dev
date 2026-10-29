/* SPDX-License-Identifier: GPL-2.0-or-later
 * LFO3 page actions (copy/paste/clear/randomize/reload), ported from Synfact
 * research-runtime/lfo3-coherence-20261021/actions_proto/lfo3_actions.c with
 * elekloader's IDs (0x3d9.., LFO2 order). The added page answers as native
 * page IDs 37 (layout 4/5) and 38 (FX layout 30); native page clipboard/undo
 * objects keep owning their numeric vectors. Values go through the proxy
 * backend (ui.c), so actions read and write the LFO3 records.
 */
typedef unsigned int u32;typedef unsigned short u16;typedef unsigned char u8;
#define LFO3_ID 0x3d9u
typedef struct {u16 v[8];} rec_t;
typedef struct {u32 addr,size,tag,seq,count;rec_t r[13];} side_t;
typedef struct {const char *name,*title;u32 ids[9];} projection;
typedef struct {u32 begin,end,capacity;} vector;
extern u32 kits_base;
extern rec_t kit_shadow[128][13];
extern side_t side[8];
extern u32 lfo3_page_is_third(void *),lfo3_index(u32);
static const u8 words[10]={0,1,2,3,4,5,5,6,7,1};
static const u16 defaults[8]={0x7000,0x300,0x4000,0,0,0,0,0x4000};
static const projection layouts[2]={
    {"LFO3","LFO",{LFO3_ID,LFO3_ID+1,LFO3_ID+2,LFO3_ID+3,LFO3_ID+4,LFO3_ID+6,LFO3_ID+7,LFO3_ID+8,10}},
    {"LFO3","LFO",{LFO3_ID,LFO3_ID+9,LFO3_ID+2,LFO3_ID+3,LFO3_ID+4,LFO3_ID+6,LFO3_ID+7,LFO3_ID+8,0}}};
u32 lfo3_actions_page(void *view)
{
    if(!lfo3_page_is_third(view))return 0xffffffffu;
    return **(u32 **)((u8 *)view+0x7c)==30?38:37;
}
const projection *lfo3_actions_layout(u32 page){return page-37u<2u?&layouts[page-37u]:0;}
static u32 native_hash(u32 addr,u32 size)
{
    const u8 *p=(const u8 *)addr;const u32 *w=(const u32 *)addr;
    u32 h=0x811c9dc5u,i=0;
    if(!(addr&3u))for(;i+4<=size;i+=4)h=(h^*w++)*0x01000193u;
    for(;i<size;i++)h=(h^p[i])*0x01000193u;
    return h;
}
/* Hash-valid side entries of the persistence layer (shadow.c) only. */
static rec_t *temporary(u32 a,u32 size)
{
    u32 i,off,index;side_t *s;
    for(i=0;i<8;i++){
        s=&side[i];index=0;
        if(!s->addr)continue;
        if(s->addr==a && s->size==size && s->count==1){}
        else if(s->size==0x1618 && s->count==13 && a>=s->addr){
            off=a-s->addr;
            if(size==0x1618 && !off){}
            else if(size==0x8e && off==0xdce)index=12;
            else if(size==0x123 && off>=0x2a && off<0x2a+12*0x123u && !((off-0x2a)%0x123u))index=(off-0x2a)/0x123u;
            else continue;
        }else continue;
        if(s->tag==native_hash(s->addr,s->size))return &s->r[index];
    }
    return 0;
}
/* Native kit reload of one parameter: the value from the reloaded kit's
 * trailer (a side entry after a successful decode), else the default. */
void lfo3_actions_reload_word(u32 source,u32 kit,u32 track,u32 id)
{
    rec_t *r;u32 i=lfo3_index(id);
    if(!kits_base || kit>=128 || track>=13 || i>=10)return;
    r=temporary(source,0x1618);
    *(volatile u16 *)&kit_shadow[kit][track].v[words[i]]=r?r[track].v[words[i]]:defaults[words[i]];
}
/* Page copy: eight (id, value) pairs through the native resolver/getter. */
void lfo3_actions_capture(vector *out,void *app,u32 page)
{
    const projection *p=lfo3_actions_layout(page);u32 *pairs,i,id;void *proxy;u32 *vt;
    out->begin=out->end=out->capacity=0;
    if(!p)return;
    pairs=((u32 *(*)(u32))0x40103788u)(8*8);
    if(!pairs)return;
    out->begin=out->end=(u32)pairs;out->capacity=(u32)pairs+8*8;
    for(i=0;i<8;i++){
        id=p->ids[i];proxy=(void *)((u32 (*)(void *,u32,int))0x4001c422u)(app,id,-1);
        vt=*(u32 **)proxy;
        pairs[2*i]=id;
        pairs[2*i+1]=((u32 (*)(void *,u32))vt[10])(proxy,id);
        out->end+=8;
    }
}
static u32 lfo_page(u32 page){return page==4 || page==5 || page==30 || page==37 || page==38;}
u32 lfo3_actions_compatible(u32 source,u32 target)
{return lfo_page(source) && lfo_page(target) && (source-37u<2u || target-37u<2u);}
/* Paste between LFO pages: rewrite the eight captured IDs for the target. */
void lfo3_actions_translate(vector *v,u32 target,u32 source)
{
    u32 *pair=(u32 *)v->begin,i,id,base;
    if(!lfo3_actions_compatible(source,target))return;
    if(!v->begin || v->end-v->begin!=8*8){v->end=v->begin;return;}
    for(i=0;i<8;i++){
        if(target==37 || target==38)id=layouts[target-37].ids[i];
        else{
            base=target==5?0x60:0x56;
            id=i<5?base+i:i<8?base+i+1:target==30?0:10;
            if(target==30 && i==1)id=0x5f;
        }
        pair[2*i]=id;
    }
}
