/* syntakt/trig-pages.h (list item 37): the TRIG page vector that POLY and ARP
 * share, on the host. Every module order, every transition, growth, the
 * current page across transitions and a failed allocation; the TRIG view the
 * UI controller shows, the page popup and the first page's "TRIG" title. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned int allocations,frees,resets,fail;
static unsigned int *new_mock(unsigned int bytes)
{
    if(fail)return 0;
    allocations++;return malloc(bytes);
}
static void delete_mock(void *p){free(p);frees++;}
static void reset_mock(void *p){(void)p;resets++;}
/* The UI controller (4016de80) with its view manager at +40h; descriptors
 * by page ID as 4007b9d0 returns them (stock titles as COW text); the printf
 * popup 400c33d0 into a buffer. */
static unsigned char ui[0x100],node[16],main_screen[0x100];
static unsigned char *ui_mock(void){return ui;}
static struct {unsigned int length,capacity;int count;char text[24];} stock_titles[4]=
    {{15,15,0,"Trig Parameters"},{15,15,0,"Trig Parameters"},{9,9,0,"Euclidean"},{11,11,-1,"Arpeggiator"}};
static unsigned int descriptors[27][11],layouts;
static unsigned int *layout_mock(unsigned int id){layouts++;return id<27?descriptors[id]:0;}
static unsigned int popup_object,popups;static char popup[64];
static void message_mock(unsigned int object,const char *format,...)
{
    va_list a;assert(object==popup_object);
    va_start(a,format);vsnprintf(popup,sizeof popup,format,a);va_end(a);popups++;
}
#define ST_TRIG_NEW new_mock
#define ST_TRIG_DELETE delete_mock
#define ST_TRIG_RESET reset_mock
#define ST_TRIG_UI ui_mock
#define ST_TRIG_LAYOUT layout_mock
#define ST_TRIG_MESSAGE message_mock
#include <syntakt/trig-pages.h>
static unsigned char view[0x100];
static unsigned int *pages(void){return *(unsigned int **)(view+0x7c);}
static void native(unsigned int first,unsigned int capacity)
{
    unsigned int *p=malloc(4*capacity);
    p[0]=first;p[1]=3;*(unsigned int **)(view+0x7c)=p;*(unsigned int **)(view+0x80)=p+2;
    *(unsigned int **)(view+0x84)=p+capacity;*(unsigned int *)(view+0x90)=0;
}
int main(void)
{
    unsigned int from,to,module,page,i,cases=0,first,mask;
    assert(sizeof(void *)==4);
    /* Masks from sizes, page counts and first indices. */
    native(2,8);
    for(i=0;i<8;i++){*(unsigned int **)(view+0x80)=pages()+i;assert(st_trig_mask(view)==(i>=3 && i<=5?i-2:0));}
    assert(st_trig_pages(0)==2 && st_trig_pages(1)==3 && st_trig_pages(2)==4 && st_trig_pages(3)==5);
    assert(st_trig_first(1,ST_TRIG_POLY)==2 && st_trig_first(2,ST_TRIG_ARP)==2 && st_trig_first(3,ST_TRIG_ARP)==3);
    free(pages());
    /* Every state, every module, add or remove, from every current page. */
    for(from=0;from<4;from++)for(module=1;module<=2;module++)for(to=0;to<2;to++)
    for(page=0;page<st_trig_pages(from);page++)for(i=0;i<2;i++){
        unsigned int want=to?from|module:from&~module,before=allocations,k,old;
        native(i?26:2,i?2:8);
        assert(st_trig_set(view,ST_TRIG_POLY,from&1) && st_trig_set(view,ST_TRIG_ARP,(from>>1)&1));
        assert(st_trig_mask(view)==from && st_trig_count(view)==st_trig_pages(from));
        *(unsigned int *)(view+0x90)=page;resets=0;before=allocations;
        assert(st_trig_set(view,module,to));
        assert(st_trig_mask(view)==want && st_trig_count(view)==st_trig_pages(want));
        assert(pages()[0]==(i?26u:2u) && pages()[1]==3);
        for(k=2;k<st_trig_pages(want);k++)assert(pages()[k]==ST_TRIG_ADDED_ID);
        /* The same page, or 0 when it went away. */
        old=page;k=*(unsigned int *)(view+0x90);
        if(old<2)assert(k==old);
        else if((from&1) && old==2)assert(k==(want&1?2u:0u));
        else {
            first=st_trig_first(from,ST_TRIG_ARP);
            assert(k==(want&2?st_trig_first(want,ST_TRIG_ARP)+old-first:0u));
        }
        assert(resets==(k!=old));
        assert(k<st_trig_pages(want));
        /* Setting again changes nothing. */
        resets=0;before=allocations;assert(st_trig_set(view,module,to) && allocations==before && !resets);
        free(pages());cases++;
    }
    /* Either order reaches the same vector. */
    for(mask=0;mask<4;mask++){
        unsigned int a[5],b[5];
        native(2,2);st_trig_set(view,ST_TRIG_POLY,mask&1);st_trig_set(view,ST_TRIG_ARP,mask&2);
        assert(st_trig_count(view)==st_trig_pages(mask));memcpy(a,pages(),4*st_trig_count(view));free(pages());
        native(2,2);st_trig_set(view,ST_TRIG_ARP,mask&2);st_trig_set(view,ST_TRIG_POLY,mask&1);
        memcpy(b,pages(),4*st_trig_count(view));assert(!memcmp(a,b,4*st_trig_pages(mask)));free(pages());
    }
    /* A full vector grows once, keeps the native pages, frees the old one. */
    native(1,2);frees=0;allocations=0;assert(st_trig_set(view,ST_TRIG_ARP,1) && allocations==1 && frees==1);
    assert(pages()[0]==1 && st_trig_count(view)==4 && *(unsigned int **)(view+0x84)==pages()+4);
    assert(st_trig_set(view,ST_TRIG_POLY,1) && allocations==2 && st_trig_count(view)==5);
    assert(st_trig_set(view,ST_TRIG_ARP,0) && allocations==2 && st_trig_count(view)==3);
    free(pages());
    /* No memory: unchanged. */
    native(2,2);fail=1;assert(!st_trig_set(view,ST_TRIG_ARP,1) && st_trig_count(view)==2);fail=0;free(pages());
    /* No vector, or a short one: nothing. */
    memset(view,0,sizeof view);assert(!st_trig_set(view,ST_TRIG_ARP,1) && !st_trig_count(view));
    /* The TRIG view the UI controller's manager (+40h) shows: the highest
     * opaque node (+1Ch) is the MainScreen, its parameter view (+C0h) is TRIG. */
    *(unsigned int *)view=ST_TRIG_VIEW;*(unsigned int *)(view+0x88)=0x13;
    *(unsigned char **)(ui+0x40+0x1c)=node;*(unsigned char **)(node+8)=main_screen;
    *(unsigned int *)main_screen=0x401c81fcu;*(unsigned char **)(main_screen+0xc0)=view;
    assert(st_trig_current()==view);
    *(unsigned char **)(ui+0x40+0x1c)=ui+0x40+0x14;assert(!st_trig_current());  /* empty stack */
    *(unsigned char **)(ui+0x40+0x1c)=node;*(unsigned int *)(view+0x88)=0x12;assert(!st_trig_current());
    *(unsigned int *)(view+0x88)=0x13;*(unsigned int *)main_screen=0;assert(!st_trig_current());
    *(unsigned int *)main_screen=0x401c81fcu;
    /* The popup: the shown page's title (its descriptor, as the core's hook
     * answers), its number and the page count, in the view's popup (+6Ch). */
    for(i=1;i<=2;i++){descriptors[i][0]=1;descriptors[i][1]=(unsigned int)stock_titles[i-1].text;}
    descriptors[3][1]=(unsigned int)stock_titles[2].text;descriptors[26][1]=(unsigned int)stock_titles[0].text;
    popup_object=0x4900aa00u;*(unsigned int *)(view+0x6c)=popup_object;
    native(2,8);assert(st_trig_set(view,ST_TRIG_POLY,1) && st_trig_set(view,ST_TRIG_ARP,1));
    *(unsigned int *)(view+0x90)=1;st_trig_popup(view);assert(popups==1 && !strcmp(popup,"Euclidean (2/5)"));
    descriptors[3][1]=(unsigned int)stock_titles[3].text;     /* ARP answers for page 3 on its page */
    *(unsigned int *)(view+0x90)=4;st_trig_popup(view);assert(popups==2 && !strcmp(popup,"Arpeggiator (5/5)"));
    *(unsigned int *)(view+0x6c)=0;st_trig_popup(view);assert(popups==2);           /* no popup object */
    *(unsigned int *)(view+0x6c)=popup_object;*(unsigned int *)(view+0x90)=5;st_trig_popup(view);assert(popups==2);
    /* The first page reads "TRIG": its stock descriptor (ID 2 digital, 1
     * analog) takes a leaked static title once; MIDI (26) and page 3 stay. */
    *(unsigned int *)(view+0x90)=0;st_trig_popup(view);assert(!strcmp(popup,"Trig Parameters (1/5)"));
    st_trig_name_first(view);
    {
        const char *t=(const char *)descriptors[2][1];
        assert(!strcmp(t,"TRIG") && ((const unsigned int *)t)[-3]==4 && ((const unsigned int *)t)[-2]==4 && ((const int *)t)[-1]==-1);
        assert(descriptors[2][0]==1 && descriptors[1][1]==(unsigned int)stock_titles[0].text);
        st_trig_popup(view);assert(!strcmp(popup,"TRIG (1/5)"));
        layouts=0;st_trig_name_first(view);assert(layouts==1 && descriptors[2][1]==(unsigned int)t);  /* once */
        descriptors[2][1]=(unsigned int)"TRIG";st_trig_name_first(view);   /* another module's "TRIG" stays */
        assert(!strcmp((const char *)descriptors[2][1],"TRIG") && descriptors[2][1]!=(unsigned int)t);
    }
    free(pages());native(1,8);assert(st_trig_set(view,ST_TRIG_ARP,1));st_trig_name_first(view);
    assert(!strcmp((const char *)descriptors[1][1],"TRIG"));free(pages());
    native(26,8);st_trig_name_first(view);assert(descriptors[26][1]==(unsigned int)stock_titles[0].text);free(pages());
    assert(!strcmp((const char *)descriptors[3][1],"Arpeggiator") && !strcmp(stock_titles[0].text,"Trig Parameters"));
    printf("PASS: TRIG pages shared by POLY and ARP: %u transitions, both orders, growth, current page kept or 0, no memory; "
           "the UI controller's TRIG view, the page popup, \"TRIG\" on the first page\n",cases);
    return 0;
}
