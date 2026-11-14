/* SPDX-License-Identifier: GPL-2.0-or-later
 * Synthetic inputs around real linked bank functions and native MAIN code.
 * No device I/O. This fixture is never linked into a delivered bank.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef int s32;
struct engine_bank {u8 state[13][40];u32 rng[2];u16 params[13][8],overlay[13][8];};
struct event {u32 mask;u16 v[16];};
struct events {u32 stride;struct event queued[64],active[13];};
struct side {
    u32 kernel,callback,selector,bank_pointer,render_pointer,events_pointer;
    u32 active_pointer,dirty_pointer,frame;
};
struct configuration {struct side side[2];u32 kernel_bytes[2];};
_Static_assert(sizeof(struct engine_bank)==944,"fixture engine-bank layout");
_Static_assert(sizeof(struct event)==36,"fixture event layout");
static struct engine_bank banks[2] __attribute__((aligned(4)));
static struct events events[2];
static u16 work[2][1024],input[1024];
static u32 last_mac[2][8],initial_mac[8],args[2][4];
static u32 active,shape,mode,tempo,frame,flags,locks,seed,position;
static const u16 defaults[8]={0x7000,0x300,0x4000,0x7f00,0,0,0,0x4000};
extern u32 lfo_probe_call6(u32,u32,u32,u32,u32,u32,u32);
extern void lfo_probe_mac_get(u32 *),lfo_probe_mac_set(const u32 *);
static struct configuration *cfg;
static struct engine_bank *current_bank[2];

void *memset(void *d,int v,u32 n)
{volatile u8 *p=d;while(n--)*p++=(u8)v;return d;}
void *memcpy(void *d,const void *s,u32 n)
{volatile u8 *p=d;const volatile u8 *q=s;while(n--)*p++=*q++;return d;}
static u32 random_word(void){seed=seed*1664525u+1013904223u;return seed;}
static void put32(void *p,u32 n){*(u32 *)p=n;}
static u32 different(const void *a,const void *b,u32 n)
{const u8 *p=a,*q=b;u32 i;for(i=0;i<n;i++)if(p[i]!=q[i])return i+1;return 0;}
static void emac_start(void){lfo_probe_mac_set(initial_mac);}

/* flags: 1 both code/data in SRAM, 2 changing params, 4 alternating locks,
 * 8 silent nondefaults, 16 inactive fine DEP, 32 tempo changes, 64 trig each
 * block, 128 one active track is FX, 256 busy callback, 512 select side 1
 * for SRAM, 1024 default DEST zero, 2048 all eight controls locked. */
u32 lfo_probe_prepare(struct configuration *c,u32 count,u32 wave,u32 trigger_mode,
                      u32 locked,u32 scenario)
{
    u32 x,t,i,n;struct engine_bank *b;
    cfg=c;active=count;shape=wave;mode=trigger_mode;locks=locked;flags=scenario;
    frame=position=0;tempo=0x3840;seed=0x54415;
    initial_mac[0]=0x20;
    for(i=1;i<8;i++)initial_mac[i]=random_word();
    initial_mac[7]=0xffffffffu;
    memset(input,0,sizeof(input));
    for(t=0;t<13;t++){
        for(i=0;i<71;i++)input[14+t*71+i]=0x3000;
        for(i=0;i<8;i++)input[14+t*71+1+i]=input[14+t*71+9+i]=defaults[i];
    }
    for(x=0;x<2;x++){
        struct side *s=&c->side[x];
        b=current_bank[x]=&banks[x];
        memset(b,0,sizeof(*b));memset(&events[x],0,sizeof(events[x]));
        for(t=0;t<13;t++){
            put32(&b->state[t][16],0x3fffffff);
            for(i=0;i<8;i++)b->params[t][i]=defaults[i];
        }
        b->rng[0]=0x12345678;b->rng[1]=0x9abcdef0;
        *(u32 *)s->bank_pointer=(u32)b;
        *(u32 *)s->render_pointer=s->kernel;
        *(u32 *)s->events_pointer=(u32)&events[x];
        *(u32 *)s->active_pointer=0;*(u32 *)s->dirty_pointer=0x1fff;
        /* Busy and publication fields are explicit fixture inputs. */
        put32((u8 *)s->frame+52,0);put32((u8 *)s->frame+56,(scenario&256)?1:0);
        for(i=0;i<13;i++)((u8 *)s->frame)[60+i]=0;
    }
    if(scenario&1){
        x=(scenario&512)?1:0;
        b=current_bank[x]=(struct engine_bank *)0x8000f870u;
        memcpy(b,&banks[x],sizeof(*b));
        *(u32 *)c->side[x].bank_pointer=(u32)b;
        n=c->kernel_bytes[x];
        memcpy((void *)0x80007340u,(void *)c->side[x].kernel,n);
        *(u32 *)c->side[x].render_pointer=0x80007340u;
    }
    return 0;
}

u32 lfo_probe_next(void)
{
    u32 x,t,i,is_active,bits,changes=(flags&2)?frame:0;
    frame++;
    if(flags&32)tempo=(frame&1)?0x2837:0x3840;
    for(x=0;x<2;x++){
        struct engine_bank *b=current_bank[x];
        memcpy(work[x],input,sizeof(input));
        for(t=0;t<13;t++){
            u16 *p=b->params[t];
            is_active=(flags&128)?t==12:t<active;
            for(i=0;i<8;i++)p[i]=defaults[i];
            if(flags&1024)p[3]=0;
            if(is_active || (flags&8)){
                p[0]=(u16)(0x7100+t*13+changes*17);p[1]=0x300;p[2]=0x3100;
                p[3]=0x2f00;p[4]=(u16)(shape<<8);p[5]=(u16)((t*7)<<8);
                p[6]=(u16)(mode<<8);p[7]=(flags&8)?0x4000:0x7000;
            }
            if(flags&16){p[3]=0x2f00;p[7]=0x4001;}
            if(flags&2)*(u32 *)cfg->side[x].dirty_pointer|=1u<<t;
            events[x].active[t].mask=0;
            if(t<locks){
                events[x].active[t].mask=(flags&2048)?0xff:0x81;
                if(flags&2048)for(i=0;i<8;i++)events[x].active[t].v[i]=p[i];
                events[x].active[t].v[0]=(u16)(0x7300+changes*37+t);
                events[x].active[t].v[7]=(u16)(0x5000+changes*19);
            }
        }
        bits=locks?((1u<<locks)-1u):0;
        if((flags&4) && (frame&1))bits=0;
        *(u32 *)cfg->side[x].active_pointer=bits;
        args[x][0]=(u32)work[x];args[x][1]=tempo;
        args[x][2]=(flags&64)?0x1fff:(frame%4==1?0x1555:0);
    }
    emac_start();
    return 0;
}

/* The following measured calls include only this small argument wrapper and
 * the complete bank callback, selector or renderer (no fixture preparation). */
u32 lfo_probe_callback(u32 x)
{
    u32 r=((u32 (*)(u32,u32,u32))cfg->side[x].callback)(17,(u32)args[x],0);
    return r;
}
u32 lfo_probe_selector(u32 x)
{return ((u32 (*)(void))cfg->side[x].selector)();}
u32 lfo_probe_kernel(u32 x)
{
    struct engine_bank *b=current_bank[x];
    u32 fn=*(u32 *)cfg->side[x].render_pointer;
    ((void (*)(void *,u32,u32,void *,void *,void *))fn)
        (work[x],tempo,args[x][2],b->params,b->state,b->rng);
    return 0;
}

/* Continuous before/after execution preserves history across silent/active
 * transitions. Compare every working/state/RNG/overlay byte and physical
 * EMAC register, plus the C ABI. Deliberate corruption proves sensitivity. */
u32 lfo_probe_differential(struct configuration *c,u32 blocks,u32 scenario,u32 corrupt)
{
    u32 k,x,i,err;
    lfo_probe_prepare(c,4,6,1,4,scenario&~1u);
    if(scenario&1){
        memset((void *)0x800077c0u,0x5a,0x8000-0x77c0);
        memset((void *)0x8000fc20u,0xa5,0xff94-0xfc20);
    }
    for(k=0;k<blocks;k++){
        active=(k/16)%4==0?0:(k/16)%4==1?1:(k/16)%4==2?4:13;
        shape=(k/8)%7;mode=(k/7)%5;locks=(k/13)%4==0?0:(k/13)%4==1?1:(k/13)%4==2?4:13;
        flags=(scenario&~1u)|((k%17==0)?8u:0)|((k%19==0)?16u:0);
        lfo_probe_next();
        for(x=0;x<2;x++){
            if(scenario&1){
                memcpy((void *)0x8000f870u,current_bank[x],944);
                memcpy((void *)0x80007340u,(void *)c->side[x].kernel,c->kernel_bytes[x]);
                *(u32 *)c->side[x].bank_pointer=0x8000f870u;
                *(u32 *)c->side[x].render_pointer=0x80007340u;
            }
            emac_start();
            err=lfo_probe_call6(c->side[x].callback,17,(u32)args[x],0,0,0,0);
            if(err)return 0x10000000u+k;
            lfo_probe_mac_get(last_mac[x]);
            if(scenario&1){
                memcpy(current_bank[x],(void *)0x8000f870u,944);
                *(u32 *)c->side[x].bank_pointer=(u32)current_bank[x];
                *(u32 *)c->side[x].render_pointer=c->side[x].kernel;
                for(i=0x77c0;i<0x8000;i++)if(*(u8 *)(0x80000000u+i)!=0x5a)return 0x60000000u+i;
                for(i=0xfc20;i<0xff94;i++)if(*(u8 *)(0x80000000u+i)!=0xa5)return 0x60000000u+i;
            }
        }
        if(corrupt && k==blocks/2)current_bank[1]->state[12][0]^=1;
        if((err=different(work[0],work[1],sizeof(work[0]))))return 0x20000000u+k*2048+err;
        if((err=different(current_bank[0],current_bank[1],sizeof(banks[0]))))return 0x30000000u+k*1024+err;
        if((err=different(last_mac[0],last_mac[1],32)))return 0x40000000u+k*32+err;
        for(i=0;i<13;i++)if(events[0].active[i].mask!=events[1].active[i].mask)return 0x50000000u+k;
    }
    return 0;
}

/* Native LFO2 oracle: native LFO1 has no destination or depth. Restrict the
 * third destination to a non-LFO slot, so the two native LFOs do not interact.
 * Native tables/helpers and both native LFO loops execute unmodified. */
u32 lfo_probe_native_oracle(struct configuration *c,u32 waves,u32 steps)
{
    u32 w,m,k,t,i,err;u8 initial[520],actual[520];u32 native_seeds[2];
    struct engine_bank *b;
    for(w=0;w<waves;w++)for(m=0;m<5;m++){
        lfo_probe_prepare(c,13,w,m,0,0);b=current_bank[0];
        for(k=0;k<steps;k++){
            lfo_probe_next();memcpy(initial,b->state,520);
            for(t=0;t<13;t++)for(i=0;i<8;i++)
                work[0][14+t*71+9+i]=work[1][14+t*71+9+i]=b->params[t][i];
            native_seeds[0]=b->rng[0];native_seeds[1]=b->rng[1];
            memcpy((void *)0x402d701cu,native_seeds,8);
            emac_start();lfo_probe_kernel(0);memcpy(actual,b->state,520);
            if(different((void *)0x402d701cu,native_seeds,8))return 0x10000001;
            for(t=0;t<13;t++){
                u8 *native=(u8 *)(0x444da72cu+80*t);
                memset(native,0,80);put32(native+16,0x3fffffff);
                memcpy(native+40,initial+40*t,40);
                for(i=0;i<8;i++)work[1][14+t*71+9+i]=b->params[t][i];
            }
            emac_start();((void (*)(void *,u32,u32))0x40119266u)(work[1],tempo,args[1][2]);
            if((err=different(work[0],work[1],sizeof(work[0]))))return 0x20000000u+(w*5+m)*2048+err;
            for(t=0;t<13;t++)if((err=different(actual+40*t,(void *)(0x444da72cu+80*t+40),40)))
                return 0x30000000u+(w*5+m)*1024+t*40+err;
            if(different(b->rng,(void *)0x402d701cu,8))return 0x40000000u+w*5+m;
        }
    }
    return 0;
}

/* Kernel boundaries and mode switches, including values just either side
 * of the exact-specialization domain. No fixture resets between frames. */
u32 lfo_probe_edges(struct configuration *c,u32 cases)
{
    static const u32 phases[]={0,1,0x5265bfff,0x5265c000,0x80000000,0xffffffff};
    static const u32 fades[]={0,1,0x3fffffff,0x40000000,0x7fffffff,0xffffffff};
    static const u32 tempos[]={0,1,0x2837,0x3840,0x7fffffff,0xffffc7c0};
    u32 k,x,t,i,err;u8 state[40];
    lfo_probe_prepare(c,0,0,0,0,0);
    for(k=0;k<cases;k++){
        active=k%4==0?0:k%4==1?1:k%4==2?4:13;
        shape=(k/4)%7;mode=(k/28)%5;flags=(k&1)?16:0;
        lfo_probe_next();tempo=tempos[k%6];
        initial_mac[0]=0x20u; /* native MAIN IRQ contract */
        for(x=0;x<2;x++){args[x][1]=tempo;args[x][2]=(k&1)?0x1fff:0;}
        for(t=0;t<13;t++){
            for(i=0;i<40;i++)state[i]=(u8)random_word();
            put32(state,phases[(k+t)%6]);put32(state+16,fades[(k+t)%6]);
            state[36]=(u8)(k&1);state[37]=(u8)((k>>1)&1);
            for(x=0;x<2;x++)memcpy(current_bank[x]->state[t],state,40);
        }
        for(x=0;x<2;x++){
            emac_start();
            err=lfo_probe_call6(c->side[x].callback,17,(u32)args[x],0,0,0,0);
            if(err)return 0x10000000u+k;
            lfo_probe_mac_get(last_mac[x]);
        }
        if((err=different(work[0],work[1],sizeof(work[0]))))return 0x20000000u+k*2048+err;
        if((err=different(current_bank[0],current_bank[1],sizeof(banks[0]))))return 0x30000000u+k*1024+err;
        if((err=different(last_mac[0],last_mac[1],32)))return 0x40000000u+k*32+err;
    }
    return 0;
}

/* Every bit of each exact-default control must retain its meaning. Exercise
 * packed guards with valid aligned private records, arbitrary phase history,
 * zero/none destinations and signed/overflowing tempo multiplication. */
u32 lfo_probe_default_guards(struct configuration *c,u32 cases)
{
    static const u32 tempos[]={0,1,0x2837,0x3840,0x7fffffff,0x80000000,0xffffffff};
    u32 k,x,t,i,field,err;u8 state[40];
    lfo_probe_prepare(c,0,0,0,0,0);
    for(k=0;k<cases;k++){
        lfo_probe_next();
        for(t=0;t<13;t++){
            for(i=0;i<40;i++)state[i]=(u8)random_word();
            put32(state,random_word());
            put32(state+16,(k%5)?random_word()&0x3fffffffu:random_word());
            state[36]=(u8)(k&1);state[37]=(u8)((k>>1)&1);
            field=(k/16+t)%8;
            for(x=0;x<2;x++){
                u16 *p=current_bank[x]->params[t];
                memcpy(current_bank[x]->state[t],state,40);
                for(i=0;i<8;i++)p[i]=defaults[i];
                if(k&1)p[3]=0;
                if(k%17)p[field]^=(u16)(1u<<(k%16));
            }
        }
        for(x=0;x<2;x++){
            struct engine_bank *b=current_bank[x];
            emac_start();
            err=lfo_probe_call6(c->side[x].kernel,(u32)work[x],tempos[k%7],
                               (k&1)?0x1fff:0,(u32)b->params,(u32)b->state,(u32)b->rng);
            if(err)return 0x10000000u+k;
            lfo_probe_mac_get(last_mac[x]);
        }
        if((err=different(work[0],work[1],sizeof(work[0]))))return 0x20000000u+k*2048+err;
        if((err=different(current_bank[0],current_bank[1],sizeof(banks[0]))))return 0x30000000u+k*1024+err;
        if((err=different(last_mac[0],last_mac[1],32)))return 0x40000000u+k*32+err;
    }
    return 0;
}

u32 lfo_probe_lock_masks(struct configuration *c,u32 cases)
{
    u32 k,x,t,err;
    lfo_probe_prepare(c,13,0,1,13,0);
    for(k=0;k<cases;k++){
        lfo_probe_next();
        for(x=0;x<2;x++)for(t=0;t<13;t++){
            events[x].active[t].mask=((k+t)&255u)|((k&1)?0xff00u:0);
            events[x].active[t].v[0]=(u16)(0x7100+t*31+k);
            events[x].active[t].v[3]=(k&2)?0x2f00:0x7f00;
        }
        for(x=0;x<2;x++){
            emac_start();
            err=lfo_probe_call6(c->side[x].selector,0,0,0,0,0,0);
            if(err)return 0x10000000u+k;
            lfo_probe_mac_get(last_mac[x]);
        }
        if((err=different(current_bank[0],current_bank[1],sizeof(banks[0]))))return 0x30000000u+k*1024+err;
        if((err=different(last_mac[0],last_mac[1],32)))return 0x40000000u+k*32+err;
        if((err=different(&events[0],&events[1],sizeof(events[0]))))return 0x50000000u+k*4096+err;
    }
    return 0;
}
