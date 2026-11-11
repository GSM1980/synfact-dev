/* SPDX-License-Identifier: GPL-2.0-or-later
 * Pages that bank modules add to the native TRIG ParameterView (list item 37).
 *
 * MainScreen (4003209c) gives the TRIG view (vtable 401c97fc) two native
 * pages: page IDs {2,3} on a digital track, {1,3} analog, {26,3} MIDI, in the
 * owned vector view+7c..+80 (capacity end +84). The current page index is
 * view+90; native TRIG steps through the vector and wraps, the native page
 * indicator (4003a1c2) counts it. A module adds its pages after the native
 * two, as page ID 3 (the second TRIG page, so the native view beneath an
 * added page is always a valid TRIG page), and draws or opens its own view
 * over them:
 *
 *   POLY  one page, index 2;
 *   ARP   two pages, after POLY's when POLY's is there.
 *
 * No other state: the vector's size says which are present (2 none, 3 POLY,
 * 4 ARP, 5 both), so each module sets only its own part, from its PAGE_SYNC
 * handler, in either order, with or without the other in the bank. A module
 * that adds pages here must be listed in this header first, in that order.
 * This is the core's shape (docs/CORE-CONTRIBUTING.md): the core could own
 * the vector later; it has no resident room for it today.
 */
#ifndef ELEKLOADER_ST_TRIG_PAGES_H
#define ELEKLOADER_ST_TRIG_PAGES_H
#define ST_TRIG_VIEW 0x401c97fcu
#define ST_TRIG_NATIVE_PAGES 2u
#define ST_TRIG_ADDED_ID 3u
#define ST_TRIG_POLY 1u
#define ST_TRIG_ARP 2u
#ifndef ST_TRIG_NEW
#define ST_TRIG_NEW ((unsigned int *(*)(unsigned int))0x40103788u)   /* as POLY 0.8 */
#define ST_TRIG_DELETE ((void (*)(void *))0x40103794u)
#define ST_TRIG_RESET ((void (*)(void *))0x400f6730u)                /* the view's increments */
#endif
/* The UI controller (4016de80, a 0xf0-byte singleton; its constructor
 * 400095a2 builds the view manager at +40h, and the key and draw loops pass
 * it to the views). Not the application (4016e804, the project with its
 * patterns at +3394h): until ARP 1.9 / POLY 1.5 st_trig_current read the
 * application's +40h, found no MainScreen and answered 0 outside page
 * actions, so the page popup read page 3's "Euclidean" on POLY's page. */
#ifndef ST_TRIG_UI
#define ST_TRIG_UI ((unsigned char *(*)(void))0x4016de80u)
#endif
#ifndef ST_TRIG_LAYOUT
#define ST_TRIG_LAYOUT ((unsigned int *(*)(unsigned int))0x4007b9d0u) /* a page's descriptor (the core's hook) */
#define ST_TRIG_MESSAGE ((void (*)(unsigned int,const char *,...))0x400c33d0u) /* printf popup */
#endif
static __inline__ unsigned int st_trig_word(const void *view,unsigned int at)
{return *(const unsigned int *)((const unsigned char *)view+at);}
/* Header titles. The TRIG view's title (40037dd0) is "%s (%d/%d)" with the
 * second word of its page's 44-byte descriptor (4007b9d0, which the core
 * hooks: page 3 asks PAGE_LAYOUT). That word is a GCC COW std::string: a
 * pointer to the text, with length, capacity and reference count in the 12
 * bytes before it. A count of -1 (leaked) makes every copy (401be778) clone
 * the text, so a static title is never written. Added pages are page 3, so
 * without their own descriptor they read "Euclidean" and the native editor
 * would take page 3's (Euclidean's) controls. */
struct st_trig_title {unsigned int length,capacity;int count;char text[24];};
#define ST_TRIG_TITLE(s) {sizeof(s)-1u,sizeof(s)-1u,-1,s}
#define ST_TRIG_DESCRIPTOR_WORDS 11u
/* An added page's descriptor: page 3's first word and ninth control (LEVEL),
 * the module's title and its eight control IDs (0: none). */
static __inline__ const unsigned int *st_trig_descriptor(unsigned int *out,const unsigned int *page3,
    const struct st_trig_title *title,const unsigned int *ids)
{
    unsigned int i;
    out[0]=page3[0];out[1]=(unsigned int)title->text;
    for(i=0;i<8;i++)out[2+i]=ids?ids[i]:0;
    out[10]=page3[10];return out;
}
static __inline__ unsigned int st_trig_count(const void *view)
{
    unsigned int begin=st_trig_word(view,0x7c),end=st_trig_word(view,0x80);
    return begin && end>=begin?(end-begin)/4u:0;
}
/* The added pages present, ST_TRIG_POLY|ST_TRIG_ARP; 0 for any other size. */
static __inline__ unsigned int st_trig_mask(const void *view)
{
    unsigned int n=st_trig_count(view);
    return n>=3 && n<=5?n-2:0;
}
static __inline__ unsigned int st_trig_pages(unsigned int mask)
{return ST_TRIG_NATIVE_PAGES+(mask&ST_TRIG_POLY?1u:0u)+(mask&ST_TRIG_ARP?2u:0u);}
/* The module whose added page index i is (0: a native page). */
static __inline__ unsigned int st_trig_owner(const void *view,unsigned int i)
{
    unsigned int mask=st_trig_mask(view);
    if(i<ST_TRIG_NATIVE_PAGES || i>=st_trig_pages(mask))return 0;
    return (mask&ST_TRIG_POLY) && i==ST_TRIG_NATIVE_PAGES?ST_TRIG_POLY:ST_TRIG_ARP;
}
/* The TRIG view of the MainScreen the UI shows (UI task only): the highest
 * opaque view of the stack (manager ui+40h, node +1Ch, view +8). */
static __inline__ unsigned char *st_trig_current(void)
{
    unsigned char *ui=ST_TRIG_UI(),*m,*n,*main,*trig;
    if(!ui)return 0;
    m=ui+0x40;n=*(unsigned char **)(m+0x1c);
    if(!n || n==m+0x14 || !(main=*(unsigned char **)(n+8)) || st_trig_word(main,0)!=0x401c81fcu)return 0;
    trig=*(unsigned char **)(main+0xc0);
    if(!trig || st_trig_word(trig,0)!=ST_TRIG_VIEW || st_trig_word(trig,0x88)!=0x13u)return 0;
    return trig;
}
/* The page popup as the native page key shows it (4003c50a: the next page,
 * then the view's title 40037dd0 in its popup +6Ch): "<title> (n/m)", the
 * title read from the shown page's descriptor through the core's hook, so an
 * added page reads its module's. For a module that moves the page itself. */
static __inline__ void st_trig_popup(const void *view)
{
    const unsigned int *pages=(const unsigned int *)st_trig_word(view,0x7c),*d;
    unsigned int i=st_trig_word(view,0x90),n=st_trig_count(view);
    if(!pages || i>=n || !st_trig_word(view,0x6c) || !(d=ST_TRIG_LAYOUT(pages[i])) || !d[1])return;
    ST_TRIG_MESSAGE(st_trig_word(view,0x6c),"%s (%d/%d)",(const char *)d[1],(int)i+1,(int)n);
}
/* The first TRIG page's title (list item 37, user's choice): "TRIG", not the
 * stock "Trig Parameters", on audio tracks (page ID 2 digital, 1 analog;
 * MIDI's 26 stays stock). The core asks PAGE_LAYOUT only for page 3, so the
 * stock descriptor itself (4007b9d0's table) takes a leaked static title;
 * the stock string is left as it is. A module that adds pages calls it from
 * its PAGE_SYNC; with POLY and ARP the second finds it done. */
static __inline__ void st_trig_name_first(const void *view)
{
    static const struct st_trig_title name=ST_TRIG_TITLE("TRIG");
    const unsigned int *pages=(const unsigned int *)st_trig_word(view,0x7c);
    unsigned int *d;const char *t;
    if(!pages || st_trig_count(view)<ST_TRIG_NATIVE_PAGES || (pages[0]!=1 && pages[0]!=2) ||
       !(d=ST_TRIG_LAYOUT(pages[0])))return;
    t=(const char *)d[1];
    if(t==name.text || (t && t[0]=='T' && t[1]=='R' && t[2]=='I' && t[3]=='G' && !t[4]))return;
    d[1]=(unsigned int)name.text;
}
/* The first index of one module's pages under mask. */
static __inline__ unsigned int st_trig_first(unsigned int mask,unsigned int module)
{return module==ST_TRIG_POLY?ST_TRIG_NATIVE_PAGES:ST_TRIG_NATIVE_PAGES+(mask&ST_TRIG_POLY?1u:0u);}
/* Index i under old, the same page under now (0 if it went away). */
static __inline__ unsigned int st_trig_remap(unsigned int i,unsigned int old,unsigned int now)
{
    unsigned int k;
    if(i<ST_TRIG_NATIVE_PAGES)return i;
    if((old&ST_TRIG_POLY) && i==ST_TRIG_NATIVE_PAGES)return now&ST_TRIG_POLY?ST_TRIG_NATIVE_PAGES:0;
    k=i-st_trig_first(old,ST_TRIG_ARP);
    return (old&ST_TRIG_ARP) && k<2 && (now&ST_TRIG_ARP)?st_trig_first(now,ST_TRIG_ARP)+k:0;
}
/* Add (want) or remove one module's pages. The first two entries are kept,
 * the added ones are page ID 3, the current page stays the same page or
 * becomes 0. Returns 0 when the vector cannot be grown (unchanged). */
static __inline__ int st_trig_set(void *raw,unsigned int module,int want)
{
    unsigned char *view=raw;unsigned int *pages=*(unsigned int **)(view+0x7c),*grown,old,now,n,i,page;
    if(!pages || st_trig_count(view)<ST_TRIG_NATIVE_PAGES)return 0;
    old=st_trig_mask(view);now=want?old|module:old&~module;
    if(now==old && st_trig_count(view)==st_trig_pages(old))return 1;
    n=st_trig_pages(now);
    if(st_trig_word(view,0x84)-(unsigned int)pages<4u*n){
        grown=ST_TRIG_NEW(4u*n);if(!grown)return 0;
        grown[0]=pages[0];grown[1]=pages[1];
        ST_TRIG_DELETE(pages);pages=grown;
        *(unsigned int **)(view+0x7c)=pages;*(unsigned int **)(view+0x84)=pages+n;
    }
    for(i=ST_TRIG_NATIVE_PAGES;i<n;i++)pages[i]=ST_TRIG_ADDED_ID;
    *(unsigned int **)(view+0x80)=pages+n;
    page=st_trig_word(view,0x90);i=st_trig_remap(page,old,now);
    if(i!=page){*(unsigned int *)(view+0x90)=i;ST_TRIG_RESET(view+0x94);}
    return 1;
}
#endif
