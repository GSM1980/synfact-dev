/* Independent fixture driver, compiled only for cfbench. No firmware input. */
typedef unsigned int u32;
typedef int s32;
/* Freestanding fixture support for the compiler's aggregate initialization. */
void *memset(void *out,int value,u32 n)
{volatile unsigned char *p=out;u32 i;for(i=0;i<n;i++)p[i]=(unsigned char)value;return out;}
typedef void (*callback)(void);
typedef int (*accessor)(u32,s32 *);
extern u32 st_probe_call(u32,u32,u32,u32,u32,u32);
extern void st_probe_mac_get(u32 *),st_probe_mac_set(const u32 *);
struct vector {s32 gain,step,shift;short d[32];s32 input[64],expected[64];u32 mac[8];};
u32 st_probe_kernels(u32 fn,struct vector *v,u32 count)
{
    u32 n,i,before[8],after[8];s32 out[64];
    for(n=0;n<count;n++,v++){
        for(i=0;i<64;i++)out[i]=v->input[i];
        st_probe_mac_set(v->mac);st_probe_mac_get(before);
        if(st_probe_call(fn,(u32)(out+1),(u32)v->d,(u32)v->gain,(u32)v->step,(u32)v->shift))return 0x10000000u+n+1;
        st_probe_mac_get(after);
        for(i=0;i<64;i++)if(out[i]!=v->expected[i])return n+1;
        for(i=0;i<8;i++)if(before[i]!=after[i])return 0x20000000u+n+1;
    }
    return 0;
}
struct config {
    callback old[4],now[4];accessor old_d,now_d;
    int (*old_init)(void *),(*now_init)(void *);
    void (*tick)(void *);u32 *sram;u32 mode,blocks;
};
static u32 rng=0x54415u;
static u32 random_word(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static volatile u32 *const win=(volatile u32 *)0x10000000u;
static s32 *const pcm=(s32 *)0x80003d50u;
static const u32 gain_address[4]={0x80006308,0x800062c0,0x8000e160,0x8000e110};
static const u32 bus_address[3]={0x8000dbd0,0x8000dad0,0x8000dfd0};
static u32 print_pcm(void)
{u32 t,i,h=0x811c9dc5u;for(t=0;t<8;t++)for(i=0;i<32;i+=8)h=h*33u^(u32)pcm[32*t+i];return h;}
static void publish(u32 seq,u32 mask,u32 print,int torn)
{win[0x5b0/4]=seq+(u32)torn;win[0x7d0/4]=seq;win[0x5b4/4]=mask;win[0x5c0/4]=print;}
void st_probe_noop(void){}
u32 st_probe_callbacks(struct config *c)
{
    u32 b,t,i,k,stage,mask,print,api[2],ui[12]={0},host[25]={0};
    unsigned char info[120]={0},*published=0;const char *name="samplers-st";
    s32 incoming[3][64],actual[3][64],old_d[32],now_d[32],next_gain[4][8];
    callback old_fn,now_fn;u32 result_old,result_now;
    c->old_init(0);c->now_init(0);
    if(c->tick){
        for(i=0;name[i];i++)info[i]=(unsigned char)name[i];
        if(c->mode&2u){name="chorus-st";for(i=0;name[i];i++)info[40+i]=(unsigned char)name[i];}
        host[0]=14;host[24]=(u32)&published;ui[0]=8;ui[10]=(u32)host;api[0]=3;api[1]=(u32)ui;
        c->now_init(api);c->tick(0);if(*c->sram)return 0x30000001u;
        published=info;
        for(i=0;i<0x480/4;i++)((u32 *)0x80007340u)[i]=0x5a5a5a5au;
        for(i=0;i<0x3b0/4;i++)((u32 *)0x8000f870u)[i]=0xa5a5a5a5u;
        if(c->mode&4u)*(u32 *)0x800077c0u=0x12345678u;
        if(c->mode&8u)*(u32 *)0x8000fc20u=0x87654321u;
    }
    for(b=0;b<c->blocks;b++){
        if(c->tick && b==96 && c->mode){
            c->tick(0);
            if(*c->sram!=(c->mode==1u))return 0x30000002u;
            for(i=0;i<0x480/4;i++)if(((u32 *)0x80007340u)[i]!=0x5a5a5a5au)return 0x30000003u;
            for(i=0;i<0x3b0/4;i++)if(((u32 *)0x8000f870u)[i]!=0xa5a5a5a5u)return 0x30000004u;
        }
        if(c->tick && b==192 && c->mode==1u){
            *(volatile unsigned char *)0x8000fc20u=0;
            for(i=0;i<60;i++)c->tick(0);
            if(*c->sram)return 0x30000005u;
        }
        mask=b<64?0:random_word()&255u;
        win[0x5c4/4]=b>=32 && b%31?0x53545232u:0;
        for(i=0;i<8*32;i++)pcm[i]=(s32)random_word();
        print=print_pcm();publish(b*3+1,mask,print,0);
        for(t=0;t<8;t++){
            ((volatile unsigned char *)win)[0x5b8+t]=(unsigned char)(random_word()%17u);
            for(i=0;i<16;i++)win[(0x5d0+64*t)/4+i]=random_word();
        }
        for(k=0;k<4;k++)for(t=0;t<8;t++)
            next_gain[k][t]=random_word()%4u==0?0:(s32)random_word();
        for(k=0;k<3;k++)for(i=0;i<64;i++)((s32 *)bus_address[k])[i]=(s32)random_word();
        for(stage=0;stage<4;stage++){
            /* Native gain kernels run after render-in, each before its
             * corresponding event. Keep the other rows at their prior end
             * until their own kernel has run; compare every bus as before. */
            for(k=0;k<4;k++)if((stage==1 && k==0) || (stage==2 && k==1) || (stage==3 && k>=2))
                for(t=0;t<8;t++)((s32 *)gain_address[k])[t]=next_gain[k][t];
            for(k=0;k<3;k++)for(i=0;i<64;i++)incoming[k][i]=((s32 *)bus_address[k])[i];
            old_fn=c->old[stage];now_fn=c->now[stage];
            if(st_probe_call((u32)now_fn,0,0,0,0,0))return 0x40000000u+b*4+stage;
            for(k=0;k<3;k++)for(i=0;i<64;i++){
                actual[k][i]=((s32 *)bus_address[k])[i];((s32 *)bus_address[k])[i]=incoming[k][i];
            }
            old_fn();
            for(k=0;k<3;k++)for(i=0;i<64;i++)
                if(actual[k][i]!=((s32 *)bus_address[k])[i])return 1+b*4+stage;
            if(stage==0 && b>=64){
                for(i=0;i<8*32;i++)pcm[i]=(s32)((u32)pcm[i]+0x77777u);
                publish(b*3+2,b%5?mask^255u:0,print_pcm(),b%7==0);
                if(b%3)for(i=0;i<8*32;i++)pcm[i]=(s32)((u32)pcm[i]-0x77777u);
            }
        }
        for(t=0;t<8;t++){
            for(i=0;i<32;i++)old_d[i]=now_d[i]=0x5a5a5a5a;
            result_old=(u32)c->old_d(t,old_d);result_now=(u32)c->now_d(t,now_d);
            if(result_old!=result_now)return 0x50000000u+b;
            for(i=0;i<32;i++)if(old_d[i]!=now_d[i])return 0x60000000u+b;
        }
    }
    return 0;
}

/* Controlled stable-gain block: 8 stereo tracks, selected tracks sending to
 * both FX buses. This excludes native mixing, FX and every other mod. */
u32 st_probe_bench_prepare(struct config *c,u32 send_mask,u32 mode)
{
    u32 i,k,t;static u32 api[2],ui[12],host[25];
    static unsigned char info[80];static unsigned char *published;
    for(i=0;i<8*32;i++)pcm[i]=(s32)(1000000+i*17);
    for(t=0;t<8;t++){
        ((volatile unsigned char *)win)[0x5b8+t]=14;
        for(i=0;i<16;i++)win[(0x5d0+64*t)/4+i]=0x00640065u+t;
    }
    win[0x5c4/4]=0x53545232;
    publish(1,255,print_pcm(),0);
    for(k=0;k<4;k++)for(t=0;t<8;t++)
        ((s32 *)gain_address[k])[t]=(k==2 || (k<2 && (send_mask&(1u<<t))))?-0x40000000:0;
    c->old_init(0);c->now_init(0);
    if(mode){
        for(i=0;i<80;i++)info[i]=0;info[0]='x';published=info;
        for(i=0;i<25;i++)host[i]=0;host[0]=14;host[24]=(u32)&published;
        for(i=0;i<12;i++)ui[i]=0;ui[0]=8;ui[10]=(u32)host;
        api[0]=3;api[1]=(u32)ui;c->now_init(api);
        for(i=0;i<(0x8000u-0x77c0u)/4;i++)((u32 *)0x800077c0u)[i]=0;
        for(i=0;i<(0xff94u-0xfc20u)/4;i++)((u32 *)0x8000fc20u)[i]=0;
        c->tick(0);if(!*c->sram)return 1;
    }
    return 0;
}
u32 st_probe_bench_next(void)
{
    u32 i,k,seq=win[0x5b0/4]+1;
    publish(seq,255,print_pcm(),0);
    for(k=0;k<3;k++)for(i=0;i<64;i++)((s32 *)bus_address[k])[i]=0;
    return 0;
}

/* Include failed checks and rebuilding in full-block instruction counts. */
u32 st_probe_bench_dynamic(u32 mode)
{
    u32 k,t,mask=win[0x5b4/4];st_probe_bench_next();
    if(mode==1 || mode==2){
        for(k=0;k<3;k++)for(t=0;t<8;t++){
            s32 *g=(s32 *)gain_address[k];
            if(g[t] && (mode==1 || t==7))g[t]=(s32)((u32)g[t]+0x10000u);
        }
    }else if(mode==3)win[0x5b4/4]=mask^0xaau;
    return 0;
}
