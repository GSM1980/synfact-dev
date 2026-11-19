/* SPDX-License-Identifier: GPL-2.0-or-later
 * MAIN only: borrow the Chorus SRAM windows after the published bank directory
 * confirms Chorus is absent. Never change AUDIO's independent SRAM layout.
 */
#ifdef __mcoldfire__
typedef void (*stereo_copy_fn)(const volatile void *,void *);
typedef void (*stereo_mix4_fn)(s32 *,const s32 *const *,const s32 *,const s32 *,u32);
extern void st_stereo_copy32(const volatile void *,void *),st_stereo_copy64(const volatile void *,void *);
extern void st_stereo_mix4(s32 *,const s32 *const *,const s32 *,const s32 *,u32);
static stereo_copy_fn copy32=st_stereo_copy32,copy64=st_stereo_copy64;
static stereo_mix4_fn mix4_kernel=st_stereo_mix4;
extern const unsigned char st_stereo_copy_begin[],st_stereo_copy_end[];
extern const unsigned char st_stereo_mix_begin[],st_stereo_mix_end[];
extern const unsigned char st_stereo_resolve_begin[],st_stereo_resolve_end[];
extern const unsigned char st_stereo_remember_begin[],st_stereo_remember_end[];
extern void st_stereo_remember(void);
static void (*remember_kernel)(void)=st_stereo_remember;
extern u32 st_stereo_lock(void);
extern void st_stereo_unlock(u32),st_stereo_sync(void);
static const struct st_machine_host_api *stereo_host;
static u32 placement_done,placement_ticks;
u32 st_stereo_sram;
#define STEREO_STATE_AT ST_SRAM_CHORUS
#define STEREO_COPY_AT (STEREO_STATE_AT+sizeof(struct stereo_main_state))
#define STEREO_MIX_AT ST_SRAM_CHORUS_CODE
static __attribute__((noinline)) int unchanged(u32 at,const unsigned char *p,u32 n)
{u32 i;const volatile unsigned char *q=(const volatile unsigned char *)at;for(i=0;i<n;i++)if(q[i]!=p[i])return 0;return 1;}
static int zero_window(u32 at,u32 end)
{const volatile u32 *p=(const volatile u32 *)at;for(;at<end;at+=4,p++)if(*p)return 0;return 1;}
static __attribute__((noinline)) void copy_code(u32 at,const unsigned char *p,u32 n)
{volatile unsigned char *q=(volatile unsigned char *)at;u32 i;for(i=0;i<n;i++)q[i]=p[i];}
static void *moved(const void *f,const void *begin,u32 to)
{u32 a=(u32)f,b=(u32)begin;__asm__ volatile("":"+d"(a),"+d"(b));return (void *)(a-b+to);}
static void move_state(struct stereo_main_state *to)
{
    u32 i,old=(u32)stereo_state,*dst=(u32 *)to;
    const u32 *src=(const u32 *)stereo_state;
    for(i=0;i<sizeof(*to)/4;i++)dst[i]=src[i];
    stereo_state=to;
    if((u32)use>=old && (u32)use<old+sizeof(*to))
        use=(const struct block *)((u32)use+(u32)to-old);
}
/* bank_info is published AFTER all module initializers. Do not infer that
 * Chorus is absent at init: wait for the first ordinary UI tick. */
static int chorus_absent(void)
{
    static const unsigned char id[]="chorus-st";
    const unsigned char *p;u32 n,i;
    if(!stereo_host || !stereo_host->bank_info || !(p=*stereo_host->bank_info))return -1;
    for(n=0;n<32;n++,p+=40){
        if(!p[0])return 1;
        for(i=0;i<sizeof(id) && p[i]==id[i];i++){}
        if(i==sizeof(id))return 0;
    }
    return 0; /* malformed/unbounded directory: retain DDR */
}
static void placement_tick(void *controller)
{
    u32 c=(u32)(st_stereo_copy_end-st_stereo_copy_begin);
    u32 m=(u32)(st_stereo_mix_end-st_stereo_mix_begin),sr;int absent;
    u32 r=(u32)(st_stereo_resolve_end-st_stereo_resolve_begin),resolve_at;
    u32 g=(u32)(st_stereo_remember_end-st_stereo_remember_begin),remember_at;
    __asm__ volatile("":"+d"(c),"+d"(m),"+d"(r),"+d"(g));
    resolve_at=STEREO_COPY_AT+c;
    remember_at=resolve_at+r;
    (void)controller;

    if(st_stereo_sram){
        if(++placement_ticks%60u)return;
        if(unchanged(STEREO_COPY_AT,st_stereo_copy_begin,c) &&
           unchanged(STEREO_MIX_AT,st_stereo_mix_begin,m) &&
           unchanged(resolve_at,st_stereo_resolve_begin,r) &&
           unchanged(remember_at,st_stereo_remember_begin,g))return;
        sr=st_stereo_lock();
        copy32=st_stereo_copy32;copy64=st_stereo_copy64;mix4_kernel=st_stereo_mix4;
        widen_kernel=st_stereo_widen;
        resolve_kernel=st_stereo_resolve;
        remember_kernel=st_stereo_remember;
        move_state(&stereo_ddr);st_stereo_sram=0;
        st_stereo_unlock(sr);return;
    }
    if(placement_done || (absent=chorus_absent())<0)return;
    placement_done=1;
    if(!absent || remember_at+g>ST_SRAM_TAIL0_END || STEREO_MIX_AT+m>ST_SRAM_CHORUS_CODE_END ||
       !zero_window(ST_SRAM_CHORUS,ST_SRAM_TAIL0_END) ||
       !zero_window(ST_SRAM_CHORUS_CODE,ST_SRAM_CHORUS_CODE_END))return;
    copy_code(STEREO_COPY_AT,st_stereo_copy_begin,c);
    copy_code(STEREO_MIX_AT,st_stereo_mix_begin,m);
    copy_code(resolve_at,st_stereo_resolve_begin,r);
    copy_code(remember_at,st_stereo_remember_begin,g);
    if(!unchanged(STEREO_COPY_AT,st_stereo_copy_begin,c) ||
       !unchanged(STEREO_MIX_AT,st_stereo_mix_begin,m) ||
       !unchanged(resolve_at,st_stereo_resolve_begin,r) ||
       !unchanged(remember_at,st_stereo_remember_begin,g))return;
    st_stereo_sync();
    sr=st_stereo_lock();
    move_state((struct stereo_main_state *)STEREO_STATE_AT);
    copy32=(stereo_copy_fn)moved(st_stereo_copy32,st_stereo_copy_begin,STEREO_COPY_AT);
    copy64=(stereo_copy_fn)moved(st_stereo_copy64,st_stereo_copy_begin,STEREO_COPY_AT);
    mix4_kernel=(stereo_mix4_fn)moved(st_stereo_mix4,st_stereo_mix_begin,STEREO_MIX_AT);
    widen_kernel=(void (*)(const struct block *,s32 *))moved(st_stereo_widen,st_stereo_copy_begin,STEREO_COPY_AT);
    resolve_kernel=(u32 (*)(void))moved(st_stereo_resolve,st_stereo_resolve_begin,resolve_at);
    remember_kernel=(void (*)(void))moved(st_stereo_remember,st_stereo_remember_begin,remember_at);
    st_stereo_sram=1;
    st_stereo_unlock(sr);
}
#else
static void copy32(const volatile void *from,void *to)
{const volatile u32 *s=from;u32 *d=to,i;for(i=0;i<8;i++)d[i]=s[i];}
#endif
