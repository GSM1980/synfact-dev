/* SPDX-License-Identifier: GPL-2.0-or-later
 * Separate oracle for the explicitly accepted suspend-without-output policy.
 * Historical full-state differential tests remain unchanged in the included
 * fixture. REF_SINGLE is the old kernel with only its loop count changed to 1;
 * validate that transformation against its untouched 13-engine version first.
 */
#include "st_lfo3_cfbench.c"
#define REF_SINGLE 0x405e0000u

static u32 single_reference(u32 x,const u16 *params,u32 suspend)
{
    struct engine_bank *b=current_bank[x];u32 t,err;
    for(t=0;t<13;t++){
        const u16 *p=params+8*t;u32 dest=p[3]>>8;
        if(suspend && (p[7]==0x4000 || dest<1 || dest>70))continue;
        err=lfo_probe_call6(REF_SINGLE,(u32)((u8 *)work[x]+142*t),tempo,
                           (args[x][2]>>t)&1,(u32)p,(u32)b->state[t],(u32)b->rng);
        if(err)return err;
    }
    return 0;
}

/* Prove the test-only loop-count edit and per-engine invocation preserve
 * every state/work/RNG/EMAC byte when none of the engines is suspended. */
u32 lfo_idle_reference_check(struct configuration *c,u32 blocks)
{
    u32 k,x,err;
    lfo_probe_prepare(c,13,0,0,0,2|32|64);
    for(k=0;k<blocks;k++){
        shape=k%7;mode=(k/7)%5;lfo_probe_next();
        for(x=0;x<2;x++){
            emac_start();
            if(x)err=single_reference(x,current_bank[x]->params,0);
            else {lfo_probe_kernel(0);err=0;}
            if(err)return 0x10000000u+k;
            lfo_probe_mac_get(last_mac[x]);
        }
        if((err=different(work[0],work[1],sizeof(work[0]))))return 0x20000000u+k*2048+err;
        if((err=different(current_bank[0],current_bank[1],944)))return 0x30000000u+k*1024+err;
        if((err=different(last_mac[0],last_mac[1],32)))return 0x40000000u+k*32+err;
    }
    return 0;
}

/* Oracle against the real complete new callback, including real lock overlay.
 * scenario 1 relocates candidate code+bank to SRAM with neighbour canaries;
 * 2 stresses phase/fade/tempo boundaries; 4 changes every control bit;
 * 8 exhausts destination high bytes/depth boundaries; 16 exhausts lock masks.
 * Skipped state, RNG and working data must remain byte-identical to the
 * reference that simply makes no oscillator call. No zero-output guesswork.
 */
u32 lfo_idle_oracle(struct configuration *c,u32 blocks,u32 scenario,u32 corrupt)
{
    static const u32 phases[]={0,1,0x5265bfff,0x5265c000,0x80000000,0xffffffff};
    static const u32 fades[]={0,1,0x3fffffff,0x40000000,0x7fffffff,0xffffffff};
    static const u32 tempos[]={0,1,0x2837,0x3840,0x7fffffff,0x80000000,0xffffffff};
    static const u16 depths[]={0,0x3fff,0x4000,0x4001,0x7fff,0x8000,0xffff};
    u32 k,x,t,i,err;u8 state[40];
    lfo_probe_prepare(c,0,0,0,0,2|4|32|64);
    if(scenario&1){
        memset((void *)0x800077c0u,0x5a,0x8000-0x77c0);
        memset((void *)0x8000fc20u,0xa5,0xff94-0xfc20);
    }
    for(k=0;k<blocks;k++){
        active=k%4==0?0:k%4==1?1:k%4==2?4:13;
        shape=(k/4)%7;mode=(k/28)%5;locks=(k/9)%14;
        lfo_probe_next();
        if(scenario&2){tempo=tempos[k%7];for(x=0;x<2;x++)args[x][1]=tempo;}
        for(t=0;t<13;t++){
            if(scenario&2){
                for(i=0;i<40;i++)state[i]=(u8)random_word();
                put32(state,phases[(k+t)%6]);put32(state+16,fades[(k+t)%6]);
                state[36]=(u8)(k&1);state[37]=(u8)((k>>1)&1);
                for(x=0;x<2;x++)memcpy(current_bank[x]->state[t],state,40);
            }
            for(x=0;x<2;x++){
                u16 *p=current_bank[x]->params[t];
                if(scenario&4)p[(k/16+t)%8]^=(u16)(1u<<(k%16));
                if(scenario&8){p[3]=(u16)(((k+t)%256)<<8|(k&255));p[7]=depths[(k/256+t)%7];}
                if(scenario&16){
                    events[x].active[t].mask=((k+t)&255u)|((k&1)?0xff00u:0);
                    for(i=0;i<8;i++)events[x].active[t].v[i]=p[i];
                    events[x].active[t].v[3]=(k&2)?0x2f00:0x7f00;
                    events[x].active[t].v[7]=(k&4)?0x4000:0x7000;
                }
            }
        }
        for(x=0;x<2;x++){
            if(x && (scenario&1)){
                memcpy((void *)0x8000f870u,current_bank[x],944);
                memcpy((void *)0x80007340u,(void *)c->side[x].kernel,c->kernel_bytes[x]);
                *(u32 *)c->side[x].bank_pointer=0x8000f870u;
                *(u32 *)c->side[x].render_pointer=0x80007340u;
            }
            emac_start();
            if(!x){const u16 *p=(const u16 *)lfo_probe_selector(0);err=single_reference(0,p,1);}
            else err=lfo_probe_call6(c->side[x].callback,17,(u32)args[x],0,0,0,0);
            if(err)return 0x10000000u+k;
            lfo_probe_mac_get(last_mac[x]);
            if(x && (scenario&1)){
                memcpy(current_bank[x],(void *)0x8000f870u,944);
                *(u32 *)c->side[x].bank_pointer=(u32)current_bank[x];
                *(u32 *)c->side[x].render_pointer=c->side[x].kernel;
                for(i=0x77c0;i<0x8000;i++)if(*(u8 *)(0x80000000u+i)!=0x5a)return 0x60000000u+i;
                for(i=0xfc20;i<0xff94;i++)if(*(u8 *)(0x80000000u+i)!=0xa5)return 0x60000000u+i;
            }
        }
        if(corrupt && k==blocks/2)current_bank[1]->state[12][0]^=1;
        if((err=different(work[0],work[1],sizeof(work[0]))))return 0x20000000u+k*2048+err;
        if((err=different(current_bank[0],current_bank[1],944)))return 0x30000000u+k*1024+err;
        if((err=different(last_mac[0],last_mac[1],32)))return 0x40000000u+k*32+err;
        if((err=different(&events[0],&events[1],sizeof(events[0]))))return 0x50000000u+k*4096+err;
    }
    return 0;
}

u32 lfo_idle_native_oracle(struct configuration *c,u32 waves,u32 steps)
{
    struct configuration candidate=*c;candidate.side[0]=c->side[1];
    candidate.kernel_bytes[0]=c->kernel_bytes[1];
    return lfo_probe_native_oracle(&candidate,waves,steps);
}
