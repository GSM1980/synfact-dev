/* Synfact FM TONE UI: same drawing source and private imported resources.
 * Core services own page navigation, storage, locks and parameter transport. */
#include <syntakt/machines.h>
#include "imported/ui-data.h"
#include "ratio.h"
typedef st_u32 u32;typedef unsigned char u8;
static u32 fm_get(void *,u32,u8 *);
#define FM_PAGE_IDS {{0x201,0x202,0x203,0x204,0x205,0x206,0x207,0x208}, \
 {0x239,0x23a,0x23b,0x23c,0,0,0,0},{0x209,0x20a,0x20b,0x20c,0x20d,0x20e,0x20f,0x210}, \
 {0x211,0x212,0x213,0x214,0x215,0x216,0x217,0}}
/* Digitone SYN views on the existing Syntakt parameter-view API. */
typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
typedef unsigned char u8;
#define DATA
#ifndef GET
#define GET ((u32 (*)(void *,u32,u8 *))0x40039160u)
#endif
#ifndef TYPE
#define TYPE ((u32 (*)(void *))0x40030622u)
#endif
#ifndef PIX
#define PIX ((void (*)(void *,s32,s32,u32))0x400f7140u)
#endif
#ifndef FILL
#define FILL ((void (*)(void *,s32,s32,s32,s32,u32))0x400f7df2u)
#endif
#ifndef RECT
#define RECT ((void (*)(void *,s32,s32,s32,s32,u32))0x400f7bd6u)
#endif
#ifndef TEXT
#define TEXT ((void (*)(void *,void *,s32,s32,u32,const char *,const char *))0x400f8a0cu)
#endif
/* Stock AMP.T/FLT.T toggle renderer. Borrows the boot-owned bitmap vector. */
#ifndef TOGGLE
#define TOGGLE ((void (*)(void *,u32,void *,s32,s32,u32))0x40070362u)
#endif
/*  ECO: page 4 position 8. The state is bit 0 of PHRT's Sound word
 * (0 = ECO, the default; 1 = x2). PHRT values are whole multiples of 0x100. */
#define ECO_ID 0x244u
#define PHRT_ID 0x214u
#define PHRT_SLOT 0x41u
/* : native track rail (TRK/LEV) stays visible on x<19 and the native
 * page indicator on x 125..127, so the Digitone grid starts at GX (four 20-px
 * columns ending at 123) and the compact diagram sits at x 19..43. */
#define GX 45
#define ALGO_X 19
static u32 active_view DATA=0;
static char formatted[32] DATA={0};
/* Per-frame reads of the 28 stored controls (fm_get), by engine ID - 0x219. */
static void *memo_view DATA=0;
static u32 memo_valid DATA=0,memo_value[28] DATA={0};
static u8 memo_locked[28] DATA={0};
static const u32 page_ids[4][8]=FM_PAGE_IDS;
static const char *ratio_c[19]=FM_RATIO_C;
static const char *ratio_a[36]=FM_RATIO_A;
static const u8 font_width[128]=FM_FONT_WIDTHS;
static const u8 font_glyphs[128][5]=FM_FONT_GLYPHS;
static u32 get(void *view,u32 id) {u8 locked=0;return fm_get(view,id,&locked)&65535u;}
static char *copy(char *p,const char *s) {while(*s)*p++=*s++;*p=0;return p;}
static char *number(char *p,u32 n) {
    if(n>=1000)*p++='0'+n/1000;
    if(n>=100)*p++='0'+n/100%10;
    if(n>=10)*p++='0'+n/10%10;
    *p++='0'+n%10;*p=0;return p;
}
static char *fixed(char *p,s32 raw,u32 signed_value,u32 digits) {
    u32 v,f,i,scale=digits==3?1000u:100u;
    if(raw<0){*p++='-';v=(u32)-raw;}else{if(signed_value && raw)*p++='+';v=(u32)raw;}
    p=number(p,v>>8);*p++='.';f=((v&255u)*scale)>>8;
    for(i=digits;i;i--){u32 d=i==3?100u:i==2?10u:1u;*p++='0'+f/d%10;}
    *p=0;return p;
}
char *fm_ui_format_to(u32 id,u32 raw,char *out) {
    u32 k,n=raw>>8,b2;char *p=out;
    if(id>=0x239u && id<=0x23cu) {
        s32 v=((s32)(raw*2000u)>>15)-1000;
        if(v==-1000)return copy(p,"-1.000"),out;
        if(!v)return copy(p,"0.000"),out;
        p=copy(p,v<0?"-0.":"+0.");n=v<0?(u32)-v:(u32)v;
        *p++='0'+n/100;*p++='0'+n/10%10;*p++='0'+n%10;*p=0;return out;
    }
    if(id==ECO_ID)return copy(p,raw?"ON":"OFF"),out;
    if(id<0x201u || id>0x218u)return 0;
    k=id-0x201u;
    if(k==0)return number(p,(n>7?7:n)+1),out;
    if(k==1 || k==3 || k==23) {
        p=copy(p,ratio_c[n>18?18:n]);
        if(k==3 && active_view) {
            b2=get((void *)active_view,0x218u)>>8;
            p=copy(p,"/");copy(p,ratio_c[b2>18?18:b2]);
        }
        return out;
    }
    if(k==2)return copy(p,ratio_a[n>35?35:n]),out;
    if(k==4)return fixed(p,(s32)raw-0x3f00,0,2),out;
    if(k==5 || k==6)return fixed(p,(s32)raw,0,2),out;
    if(k==7){s32 v=(s32)n-64;if(v<0){*p++='-';v=-v;}else if(v)*p++='+';number(p,(u32)v);return out;}
    if(k==17 || k==18 || k==21 || k==22)return copy(p,n?"ON":"OFF"),out;
    if(k==19){static const char *names[]={"OFF","ALL","C","A+B","A+B2"};return copy(p,names[n>4?4:n]),out;}
    return number(p,n),out;
}


#ifndef FM_PAGE_INDICATOR
#define FM_PAGE_INDICATOR ((void (*)(void *,void *,u32,u32,u32))0x4003a1c2u)
#endif
/* PIX (400f7140) with colour 1, inline: the native context holds the clip
 * size at +4/+8, the words per column at +12 and the 1-bit plane at +16,
 * column-major, bit 31 being the first of each word's 32 rows. Colour 0 and
 * computed colours still call PIX. */
static __inline__ void dot(void *ctx,s32 x,s32 y) {
    const s32 *c=ctx;
    if(x<0 || y<0 || x>=c[1] || y>=c[2])return;
    (*(u32 *const *)((const u8 *)ctx+16))[x*c[3]+(y>>5)]|=0x80000000u>>(y&31);
}
/* Colour 1 for each set bit of one column: rows y..y+n-1, row y in bit 31. */
static void column(void *ctx,s32 x,s32 y,u32 bits,u32 n) {
    const s32 *c=ctx;u32 *p,s,j;
    if(!bits || x<0 || x>=c[1])return;
    if(y<0 || y+(s32)n>c[2]) {
        for(j=0;j<n;j++)if(bits&(0x80000000u>>j))dot(ctx,x,y+(s32)j);
        return;
    }
    p=*(u32 *const *)((const u8 *)ctx+16)+x*c[3]+(y>>5);s=(u32)y&31u;
    p[0]|=bits>>s;
    if(s && (bits<<(32-s)))p[1]|=bits<<(32-s);
}
void fm_ui_text(void *ctx,s32 x,s32 y,const char *s,u32 align) {
    const char *p=s;u32 width=0,c,i;
    while(*p){c=(u8)*p++;if(c<128)width+=font_width[c]+1;}
    if(width)width--;
    if(align==2)x-=(s32)width/2;
    for(p=s;*p;p++) {
        c=(u8)*p;if(c>=128)continue;
        /* Glyph bits 6..2 are rows 0..4. */
        for(i=0;i<font_width[c];i++)column(ctx,x+(s32)i,y,(u32)(font_glyphs[c][i]&0x7cu)<<25,5);
        x+=font_width[c]+1;
    }
}
#define text fm_ui_text
static void line(void *ctx,s32 x0,s32 y0,s32 x1,s32 y1) {
    s32 dx=x1-x0,dy=y1-y0,n=dx<0?-dx:dx,j,ay=dy<0?-dy:dy;
    if(ay>n)n=ay;
    if(!n){dot(ctx,x0,y0);return;}
    for(j=0;j<=n;j++)dot(ctx,x0+dx*j/n,y0+dy*j/n);
}
static void empty(void *ctx,s32 x,s32 y) {
    s32 i;for(i=0;i<17;i+=2){dot(ctx,x+i,y);dot(ctx,x+i,y+16);}
    for(i=2;i<16;i+=2){dot(ctx,x,y+i);dot(ctx,x+16,y+i);}
}
static void envelope(void *ctx,s32 y,u32 attack,u32 decay,u32 end,u32 trig,u32 delay) {
    /* Exact coordinate formulas from Digitone 4004afba / 4004b1d0. */
    s32 start=GX+1+(delay*(trig?16u:12u)+0x3f80u)/0x7f00u;
    s32 a=start+(attack*(trig?19u:14u)+0x3f80u)/0x7f00u;
    s32 sustain=trig?a:a+14;
    s32 d=sustain+(decay*(trig?19u:14u)+0x3f80u)/0x7f00u;
    s32 e=y+(end*13u+0x3f80u)/0x7f00u,j;
    RECT(ctx,GX,y-1,GX+59,y+16,1);
    line(ctx,start,y,a,y+13);
    if(!trig)line(ctx,a,y+13,sustain,y+13);
    line(ctx,sustain,y+13,d,e);line(ctx,d,e,GX+57,e);
    for(j=y+2;j<y+14;j+=2){dot(ctx,a,j);dot(ctx,d,j);if(!trig)dot(ctx,sustain,j);}
    FILL(ctx,a-1,y+12,a+1,y+14,1);PIX(ctx,a,y+13,0);
    FILL(ctx,d-1,e-1,d+1,e+1,1);PIX(ctx,d,e,0);
}
/* Firmware-native graph and envelope-label resources are supplied separately.
 * fm_ui_draw first clears x >= ALGO_X, rows 0..CLEAR_Y, where every resource
 * lies: there only the set pixels are drawn (zeros would rewrite zeros, and
 * PIX costs a native call per pixel). Rows below keep the donor's zero writes. */
#define CLEAR_Y 51
static void resource(void *ctx,const u32 *pixels,u32 width,u32 height,s32 x,s32 y) {
    u32 i,j,w,n,bits,words=(height+31)/32;s32 k;
    for(i=0;i<width;i++)for(w=0;w<words;w++) {
        bits=pixels[i*words+w];n=height-32*w;if(n>32)n=32;
        /* k rows lie in the cleared area; the rest keep PIX, zeros included. */
        k=CLEAR_Y+1-y-(s32)(32*w);if(k<0)k=0;if(k>(s32)n)k=(s32)n;
        for(j=(u32)k;j<n;j++)PIX(ctx,x+i,y+32*w+j,(bits>>(31-j))&1u);
        if(k<32)bits&=~(0xffffffffu>>k);
        column(ctx,x+(s32)i,y+(s32)(32*w),bits,(u32)k);
    }
}
static const u32 algorithms[8][FM_ALGORITHM_WORDS]=FM_ALGORITHM_PIXELS;
static const u32 diamonds[32][19]=FM_DIAMOND_PIXELS;
static const u32 ratio_frame_c[19]=FM_RATIO_FRAME_C;
static const u32 ratio_frame_b[19]=FM_RATIO_FRAME_B;
static const u8 harm_waves[27][17]=FM_HARM_WAVES;
static const signed char knob_pointer[33][2]=FM_KNOB_POINTER;
/* : the doubled font of the former SYN track box, for the ALGO value. */
static void bold_text(void *ctx,s32 cx,s32 y,const char *s) {
    const char *p;u32 width=0,c,i,j,k;s32 x;
    for(p=s;*p;p++){c=(u8)*p;if(c<128)width+=2*font_width[c]+2;}
    if(width)width-=2;
    x=cx-(s32)width/2;
    for(p=s;*p;p++) {
        c=(u8)*p;if(c>=128)continue;
        for(i=0;i<font_width[c];i++) {
            u32 bits=0;
            for(j=0;j<5;j++)if(font_glyphs[c][i]&(1u<<(6-j)))bits|=0xc0000000u>>(2*j);
            for(k=0;k<2;k++)column(ctx,x+2*(s32)i+(s32)k,y,bits,10);
        }
        x+=2*font_width[c]+2;
    }
}
/*  MIX, as on the Digitone: no frame, X and Y on top, a hollow
 * rounded bar filled from the centre towards X (below 64) or Y. */
static void mix_view(void *ctx,s32 x,s32 y,u32 raw) {
    static const u8 xy[2][5]={{5,5,2,5,5},{5,5,2,2,2}};
    /* : -64 and +63 both reach the end (7 px); any non-zero shows. */
    s32 n=(s32)(raw>>8)-64,len=n>0?(n*7+62)/63:-((-n*7+63)/64),a,b;
    u32 g,r,c;
    for(g=0;g<2;g++)for(r=0;r<5;r++)for(c=0;c<3;c++)
        if(xy[g][r]&(4u>>c))dot(ctx,x+(g?14:2)+(s32)c,y+21-(s32)r);
    line(ctx,x+2,y+9,x+16,y+9);line(ctx,x+2,y+13,x+16,y+13);
    line(ctx,x+1,y+10,x+1,y+12);line(ctx,x+17,y+10,x+17,y+12);
    a=len<0?x+9+len:x+9;b=len<0?x+9:x+9+len;
    FILL(ctx,a,y+10,b,y+12,1);
}
/*  ADEL/BDEL: dial as the native unipolar knobs (17-px circle, pointer
 * over 270 degrees). */
static void knob_view(void *ctx,s32 x,s32 y,u32 raw) {
    s32 cx=x+9,cy=y+15,px=0,py=8,d=1-8;
    u32 k=((raw>>8)>127?127:(raw>>8))*32u/127u;
    while(px<=py) {
        dot(ctx,cx+px,cy+py);dot(ctx,cx-px,cy+py);dot(ctx,cx+px,cy-py);dot(ctx,cx-px,cy-py);
        dot(ctx,cx+py,cy+px);dot(ctx,cx-py,cy+px);dot(ctx,cx+py,cy-px);dot(ctx,cx-py,cy-px);
        if(d<0)d+=2*px+3;else{d+=2*(px-py)+5;py--;}
        px++;
    }
    line(ctx,cx,cy,cx+knob_pointer[k][0],cy+knob_pointer[k][1]);
}
static void fm_ui_draw(u8 *view,void *ctx,u32 page) {
    u32 r[24],i,id,v;char value[32];
    static const char *names[4][8]={
        {"ALGO","C","A","B","HARM","DTUN","FDBK","MIX"},
        {"C","A","B1","B2","","","",""},
        {"ATK","DEC","END","LEV","ATK","DEC","END","LEV"},
        {"ADEL","ATRG","ARST","PHRT","BDEL","BTRG","BRST",""}};
    /* Only the values this page draws outside its grid: ALGO, and on the
     * envelope page their delays/trig modes. The grid reads its own. */
    static const u8 envelope_values[10]={8,9,10,12,13,14,16,17,20,21};
    active_view=(u32)view;if(page>3)return;
    memo_view=view;memo_valid=0;
    r[0]=get(view,0x201u);
    if(page==2)for(i=0;i<10;i++)r[envelope_values[i]]=get(view,0x201u+envelope_values[i]);
    /* : x<19 keeps the native TRK/LEV rail drawn by 3a244. */
    FILL(ctx,ALGO_X,0,127,CLEAR_Y,0);
    /* The shared Syntakt header (pattern, name, tempo) is drawn by the
     * enclosing native view. Never erase or replace its rows 54..63. */
    resource(ctx,algorithms[(r[0]>>8)>7?7:r[0]>>8],FM_ALGORITHM_WIDTH,FM_ALGORITHM_HEIGHT,ALGO_X,0);
    /* Original Digitone grid: x=50,70,90,110; rows y=27 and 0. */
    for(i=0;i<8;i++) {
        s32 x=GX+(i%4)*20,y=i<4?27:0;
        id=page_ids[page][i];
        if(!id){empty(ctx,x+1,y+7);continue;}
        if(id==ECO_ID) {
            /* Stock AMP.T/FLT.T toggle; ON = ECO = PHRT bit 0 clear. */
            TOGGLE(0,(get(view,PHRT_ID)&1u)?0:0x100u,ctx,x,y+7,0);
            text(ctx,x+9,y+1,"ECO",2);
            continue;
        }
        v=get(view,id);fm_ui_format_to(id,v,value);
        if(page==1){
            s32 f=((s32)(v*2000u)>>15)-1000;
            if(!f)copy(value,"0.00");
            else if(f==-1000)copy(value,"-1.00");
            else{u32 z=f<0?(u32)-f:(u32)f;value[0]=f<0?'-':'+';value[1]='.';
                value[2]='0'+z/100;value[3]='0'+z/10%10;value[4]=0;}
        }
        if(page==2 && (i%4)<3)continue;
        if(page==2) {
            u32 fill=(v>>8)*14/127;
            RECT(ctx,x+5,y+9,x+12,y+24,1);
            if(fill)FILL(ctx,x+7,y+10,x+10,y+9+fill,1);
        }else if(page==0 && i==4) {
            s32 h=(s32)v-0x3f00;u32 w,j;
            if(h<0)h=-h;
            w=(u32)h>>8;
            if(w>26)w=26;
            RECT(ctx,x,y+7,x+18,y+23,1);
            for(j=1;j<17;j++)line(ctx,x+j,y+7+harm_waves[w][j-1],x+j+1,y+7+harm_waves[w][j]);
        }else if(page==0 && i>=5 && i<7) {
            u32 dial=i==4?(v-0x2500u)*0x7f00u/0x3400u:i==6?v*127u/120u:v;
            u32 step=(dial*31u+0x3f80u)/0x7f00u;if(step>31)step=31;
            resource(ctx,diamonds[step],19,17,x,y+7);
        }else if(page==3 && (i==1 || i==2 || i==5 || i==6)) {
            TOGGLE(0,v,ctx,x,y+7,0);
        }else if(page==3 && (i==0 || i==4)) {
            knob_view(ctx,x,y,v);
        }else if(page==0 && i==7) {
            mix_view(ctx,x,y,v);
        }else if(page==0 && i==0) {
            RECT(ctx,x+1,y+8,x+17,y+23,1);
            bold_text(ctx,x+9,y+11,value);
        }else {
            if(page==0 && (i==1 || i==3))resource(ctx,i==1?ratio_frame_c:ratio_frame_b,19,19,x,y+7);
            else RECT(ctx,x+1,y+8,x+17,y+23,1);
            /* RATIO B has two independent ratios in a shared control. */
            if(page==0 && i==3){char *s=value;while(*s && *s!='/')s++;if(*s){*s++=0;text(ctx,x+9,y+10,value,2);text(ctx,x+9,y+17,s,2);}}
            else text(ctx,x+9,y+14,value,2);
        }
        if(page==1 || (page==0 && i==2)) {
            FILL(ctx,x+5,46,x+13,52,0);
            text(ctx,x+9,47,names[page][i],2);
        }else if(!(page==0 && (i==1 || i==3)))text(ctx,x+9,y+1,names[page][i],2);
    }
    if(page==0)text(ctx,GX+39,28,"RATIO",2);
    if(page==1){FILL(ctx,GX,23,GX+78,27,0);text(ctx,GX+38,24,"RATIO OFFSET",2);}
    if(page==2) {
        envelope(ctx,34,r[8],r[9],r[10],r[17],r[16]);
        envelope(ctx,7,r[12],r[13],r[14],r[21],r[20]);
        text(ctx,GX,27,"A:",0);text(ctx,GX,0,"B:",0);
        for(i=0;i<3;i++){text(ctx,GX+14+20*i,27,names[2][i],2);text(ctx,GX+14+20*i,0,names[2][i+4],2);}
    }
    /* GET is the same native lock-preview query as 4003a244. Its out-byte
     * must survive the custom artwork. Use the stock XOR fill operation;
     * never modify the Sound or infer locks from the displayed value. */
    for(i=0;i<8;i++) {
        u8 locked=0,other=0;s32 x=GX+(i%4)*20,y=i<4?27:0;
        id=page_ids[page][i];if(!id || id==ECO_ID)continue;   /* ECO has no lock lane */
        fm_get(view,id,&locked);
        if(page==0 && i==3){fm_get(view,0x218u,&other);locked|=other;}
        if(!locked)continue;
        if(page==2 && (i%4)<3)FILL(ctx,x+5,y,x+23,y+5,0xffffffffu);
        else FILL(ctx,x,y+7,x+18,y+24,0xffffffffu);
    }
    /* : native page indicator (the four FM pages), as on stock pages. */
    FM_PAGE_INDICATOR(view,ctx,4,page,7);
    memo_view=0;
}

/* Donor IDs: 0x201..0x217 are indices 0..22; 0x204 (fast, physical B2) and
 * 0x218 (slow, physical B1) both read the one stored RATIO B control; 0x239
 * is RATIO OFFSET C (index 23), 0x23a..0x23c offsets A/B1/B2 (25..27). */
static u32 engine(u32 id)
{
    if(id==0x218u)return 0x21cu;
    if(id==0x239u)return 0x230u;
    return id>=0x201 && id<=0x217?0x219+id-0x201:id>=0x23a && id<=0x23c?0x232+id-0x23a:0;
}
/* The donor drawing and formatting keep their two-digit view of RATIO B.
 * During one fm_ui_draw each stored control is read once: the native GET
 * returns the value and its lock flag together, and with a trig held it
 * allocates, walks the selection and frees on every call. The donor drawing
 * read up to 42 times per frame (B1 and B2 both read control 0x21c). */
static u32 fm_get(void *view,u32 id,u8 *locked)
{
    u32 e=engine(id),k=e-0x219u,v;
    if(view==memo_view && k<28 && (memo_valid>>k&1u)){*locked=memo_locked[k];v=memo_value[k];}
    else {
        v=GET(view,e,locked);
        if(view==memo_view && k<28){memo_value[k]=v;memo_locked[k]=*locked;memo_valid|=1u<<k;}
    }
    if(id==0x204u || id==0x218u){v=fm_ratio_index(v);v=(id==0x204u?v%19u:v/19u)<<8;}
    return v;
}
static const char *format(const struct st_machine_ui_api *api,const struct core_machine_def *m,u32 index,u32 raw)
{
    u32 slow,fast;char *p=formatted;(void)m;
    if(index==3) {
        /* The popup composes "label=value": label "RATIO RB1", value
         * "<B1> RB2=<B2>". Elsewhere "<B2>/<B1>", as on the SYN page. */
        raw=fm_ratio_index(raw);slow=raw/19u;fast=raw%19u;
        if(api && api->format_context==ST_UI_FORMAT_POPUP) {
            p=copy(p,ratio_c[slow]);p=copy(p," RB2=");copy(p,ratio_c[fast]);
        }else {p=copy(p,ratio_c[fast]);p=copy(p,"/");copy(p,ratio_c[slow]);}
        return formatted;
    }
    if(index==24)return 0;
    return fm_ui_format_to(index<23?0x201+index:index==23?0x239:0x23a+index-25,raw,formatted);
}
static void draw(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,void *ctx,u32 page)
{(void)api;(void)m;fm_ui_draw(view,ctx,page);}
static u32 policy(u32 index)
{return index==4 || index==5 || index==6 || index==23 || index>=25?0x1f1:0xffffffffu;}
static const u32 layout[32]={0,1,2,3,4,5,6,7,23,25,26,27,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,
 8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,0xffffffffu};
const struct st_machine_ui fm_tone_ui={65,4,layout,draw,ST_MACHINE_NO_CALLBACK,ST_MACHINE_NO_CALLBACK,format,policy};
#include "events.h"
