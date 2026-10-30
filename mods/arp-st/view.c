/* SPDX-License-Identifier: GPL-2.0-or-later
 * OS 1.41 plain IView, using the Recorder's native shared-owner lifecycle.
 * Included by module.c; no private code patches or parameter-provider claim.
 */
#include "ui.h"
#include "length.h"
typedef unsigned char arp_u8;
#define UI_CALL(a,t) ((t)(a))
#define ARP_DIRTY UI_CALL(0x400ffc5eu,void (*)(void *))
#define ARP_CLOSE UI_CALL(0x400ffd02u,void (*)(void *))
#define ARP_FILLRECT UI_CALL(0x400f7df2u,void (*)(void *,int,int,int,int,arp_u32))
static arp_u8 *arp_view DATA=0,*arp_manager DATA=0;
static arp_u32 arp_vtable[19] DATA={0};
static struct arp_double_tap arp_taps DATA={0};
static unsigned int arp_selected DATA=0;
static arp_u32 arp_length_until DATA=0;
static unsigned int arp_length_show DATA=0;
static void *arp_proxy DATA=0;
static arp_u32 arp_ui_sound DATA=0;
static struct arp_settings arp_ui_parameters DATA=ARP_DEFAULT;
static arp_u32 proxy_sound(void *proxy)
{
    arp_u32 *vt,*inner;
    if(!proxy || !(vt=*(arp_u32 **)proxy) || vt[0x28/4]!=0x40012986u)return 0;
    inner=*(arp_u32 **)((arp_u8 *)proxy+0x10);
    if(!inner || !*inner)return 0;
    return ((arp_u32 (*)(void *))(*(arp_u32 **)inner)[0x28/4])(inner);
}
static int save_ui_settings(const struct arp_settings *p)
{
    if(proxy_sound(arp_proxy)!=arp_ui_sound || !arp_ui_sound || !eligible(arp_track))return 0;
    if(!arp_sound_store(arp_ui_sound,p))return 0;
    arp_sound_configure(arp_track,p,arp_ui_sound);
    /* Same native model notification as a Sound parameter edit (task only). */
    UI_CALL(0x40013084u,void (*)(void *))(arp_proxy);
    arp_sound_read(arp_ui_sound,&arp_ui_parameters);return 1;
}
/* Resource descriptors have identical glyph metrics/maps/pixels to DN 1.44.
 * The vtable belongs to Syntakt. These are borrowed ROM fonts, not owning
 * native objects copied from another view. */
static const arp_u32 arp_fonts[2][2]={{0x40240c54u,0x402a7354u},{0x40240c54u,0x402a9dc4u}};
static const char *arp_speeds[]={"1/1","1/2","1/3","1/4","1/5","1/6","1/8","1/10","1/12",
    "1/16","1/20","1/24","1/32","1/40","1/48","1/64","1/80","1/96"};
static arp_u32 view_word(const void *v,unsigned int off)
{return *(const arp_u32 *)((const arp_u8 *)v+off);}
static arp_u8 *arp_top(void *app)
{
    arp_u8 *m=(arp_u8 *)app+0x40,*n=*(arp_u8 **)(m+0x1c);
    return n && n!=m+0x14?*(arp_u8 **)(n+8):0;
}
static int arp_foreground(void)
{
    arp_u8 *n;
    if(!arp_view || !arp_manager || arp_view[0x30])return 0;
    n=*(arp_u8 **)(arp_manager+0x1c);
    return n && n!=arp_manager+0x14 && *(arp_u8 **)(n+8)==arp_view;
}
static void arp_dispose(void *v)
{
    if(v==arp_view){arp_view=arp_manager=0;arp_proxy=0;arp_ui_sound=0;arp_taps.pending=0;arp_length_show=0;}
    UI_CALL(0x400ffdfcu,void (*)(void *))(v);
}
static void arp_text(void *bmp,unsigned int font,int x,int y,unsigned int align,const char *s)
{
    if(font){
        const char *erase=x==14?"ARP XX":y==9?"OFS":y==26?"LEN":"XXX";
        /* Small labels use the native framed-text renderer: its alignment 1
         * means centered, and it takes a font descriptor rather than IFont. */
        UI_CALL(0x400f8c18u,void (*)(void *,const void *,int,int,arp_u32,arp_u32,const char *,const char *,const char *))
            (bmp,(const void *)arp_fonts[1][1],x,y,align,0,erase,"%s",s);return;
    }
    UI_CALL(0x400f8a0cu,void (*)(void *,const void *,int,int,arp_u32,const char *,const char *))
        (bmp,arp_fonts[0],x,y,align,"%s",s);
}
static void arp_shape(void *bmp,unsigned int kind,int x,int y,int x2,int y2,unsigned int col)
{
    if(kind==ARP_RECT)
        UI_CALL(0x400f7bd6u,void (*)(void *,int,int,int,int,arp_u32))(bmp,x,y,x2,y2,col);
    else ARP_FILLRECT(bmp,x,y,x2,y2,col);
}
static void arp_bitmap(void *bmp,unsigned int resource,int x,int y,unsigned int mode)
{
    arp_u32 rail[7];const void *p=(const void *)resource;
    if(resource==0xffffffffu){
        /* Same 27x27 image and mask as DN 40255b58/40255aec; constructed by
         * the native bitmap constructor, then destroyed on the UI task. */
        UI_CALL(0x400f70fau,void (*)(void *,arp_u32,arp_u32,const void *,const void *))
            (rail,27,27,(const void *)0x402eaad4u,(const void *)0x402d736cu);
        p=rail;
    }
    UI_CALL(0x400f8dacu,void (*)(void *,const void *,int,int,arp_u32))(bmp,p,x,y,mode);
    if(resource==0xffffffffu)UI_CALL(0x400f702cu,void (*)(void *))(rail);
}
static void arp_view_draw(void *v,void *bmp,arp_u32 phase)
{
    struct arp_canvas c={bmp,arp_text,arp_shape,arp_bitmap};
    struct arp_screen s;arp_u32 first,last,count,index;char length[12];(void)phase;
    if(v!=arp_view || !arp_view)return;
    first=*(const volatile arp_u32 *)0x444df618u;last=*(const volatile arp_u32 *)0x444df61cu;
    count=(last-first)/28u;if(count<24 || count>128)return;
    index=12u+((count-23u)*arp_ui_parameters.note_length>>7);
    s.settings=&arp_ui_parameters;s.track=arp_track;s.selected=arp_selected;
    s.show_length=arp_length_show && (int)(arp_length_until-arp_host->machine_telemetry[0])>0;
    s.speed_text=arp_speeds[arp_ui_parameters.speed];s.length_text=arp_format_note_length(length,arp_ui_parameters.note_length);
    s.length_bitmap=first+28u*index;
    ARP_FILLRECT(bmp,0,0,127,63,0);arp_draw(&s,&c);
}
static int arp_open(void *app,arp_u32 track)
{
    arp_u8 *root,*v,*main,*parameter;void *pair[2],*proxy;arp_u32 sound;
    unsigned int i;struct arp_settings p;
    if(track>=ARP_TRACKS || !eligible(track))return 0;
    main=arp_top(app);if(!main || view_word(main,0)!=0x401c81fcu)return 0;
    parameter=*(arp_u8 **)(main+0xc0);if(!parameter || view_word(parameter,0)!=0x401c97fcu)return 0;
    proxy=((void *(*)(void *,arp_u32,int))(*(arp_u32 **)parameter)[0xc4/4])(parameter,0x60u,-1);
    sound=proxy_sound(proxy);if(!sound)return 0;
    arp_sound_read(sound,&p);
    if(arp_view)return 0;
    root=arp_host->storage->alloc(16+0x50);if((arp_u32)root<0x40000000u)return 0;
    *(arp_u32 *)root=0x401c9670u;*(arp_u32 *)(root+4)=1;*(arp_u32 *)(root+8)=1;
    v=root+16;*(void **)(root+12)=v;
    UI_CALL(0x400ffba6u,void (*)(void *,const char *))(v,"ARPEGGIATOR");
    arp_activate(&p);
    for(i=0;i<sizeof(p);i++)((volatile arp_u8 *)&arp_ui_parameters)[i]=((const arp_u8 *)&p)[i];
    UI_CALL(0x400ffaf4u,void (*)(void *,arp_u32))(v,1);
    for(i=0;i<19;i++)arp_vtable[i]=((const arp_u32 *)0x40241bd4u)[i];
    arp_vtable[2]=(arp_u32)arp_dispose;*(arp_u32 **)v=arp_vtable+2;
    arp_track=track;arp_proxy=proxy;arp_ui_sound=sound;arp_view=v;arp_manager=(arp_u8 *)app+0x40;arp_selected=0;
    pair[0]=v;pair[1]=root;
    UI_CALL(0x40100bc2u,arp_u32 (*)(void *,void *,arp_u32))(arp_manager,pair,0);
    UI_CALL(0x4016dde8u,void (*)(void *))(pair+1);ARP_DIRTY(v);return 1;
}
static int arp_edit(unsigned int field,int delta)
{
    struct arp_settings p=arp_ui_parameters;int n,limit;
    if(field>5 || !delta)return 0;
    if(delta>127)delta=127;if(delta< -127)delta=-127;
    n=field==0?p.mode:field==1?p.speed:field==2?p.range:field==3?p.note_length:
        field==4?p.offset[arp_selected]:p.length;
    limit=field==0?4:field==1?17:field==2?3:field==3?127:field==4?24:15;
    n+=delta;if(n<(field==4?-24:0))n=field==4?-24:0;if(n>limit)n=limit;
    if(field==0)p.mode=n;else if(field==1)p.speed=n;else if(field==2)p.range=n;
    else if(field==3)p.note_length=n;else if(field==4)p.offset[arp_selected]=n;else p.length=n;
    if(!save_ui_settings(&p))return 0;
    if(field==3){arp_length_until=arp_host->machine_telemetry[0]+1500u;arp_length_show=1;}
    ARP_DIRTY(arp_view);return 1;
}
static void arp_view_tick(void *controller)
{
    arp_u32 now=arp_host->machine_telemetry[0];(void)controller;
    if(arp_length_show && (int)(now-arp_length_until)>=0){
        arp_length_show=0;if(arp_view)ARP_DIRTY(arp_view);
    }
}
static int arp_view_encoder(void *app,const void *event)
{
    arp_u32 knob=view_word(event,12);(void)app;
    if(!arp_foreground())return 0;
    if(knob>=1 && knob<=6)arp_edit(knob-1,(int)view_word(event,16));
    return 1;
}
static int arp_view_key(void *app,const void *event)
{
    arp_u32 key=view_word(event,12),flags=view_word(event,16),track=0xffffffffu;
    arp_u8 *top=arp_top(app);int action,main=top && view_word(top,0)==0x401c81fcu;
    if(main && view_word(top,0xa4))track=UI_CALL(0x4001ff74u,arp_u32 (*)(void *))(*(void **)(top+0xa4));
    action=arp_trig_tap(&arp_taps,key,flags,track,arp_host->machine_telemetry[0],main);
    if(action==ARP_TAP_OPEN){
        if(arp_open(app,track))return 1;
        arp_taps.consume_release=0;return 0;
    }
    if(action==ARP_TAP_CONSUME)return 1;
    if(!arp_foreground())return 0;
    if(key==13){if(flags&1)ARP_CLOSE(arp_view);return 1;}
    if(key==12){
        if((flags&1) && !(flags&8)){
            struct arp_settings p=arp_ui_parameters;p.enabled^=1u<<arp_selected;
            save_ui_settings(&p);ARP_DIRTY(arp_view);
        }
        return 1;
    }
    if(key>=14 && key<=17){
        if(flags&1){
            if(key==16 && arp_selected)arp_selected--;
            if(key==17 && arp_selected<15)arp_selected++;
            if(key==14 || key==15)arp_edit(4,key==14?1:-1);
            ARP_DIRTY(arp_view);
        }
        return 1;
    }
    /* Transport and track selection keep their native app handling. */
    return 0;
}
