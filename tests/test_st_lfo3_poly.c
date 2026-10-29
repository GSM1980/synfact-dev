/* Real LFO3 projection/state lifecycle; the core query is the only double. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <syntakt/host.h>
#include <syntakt/native.h>
typedef unsigned int u32;typedef unsigned short u16;typedef unsigned char u8;
struct lfo3_bank {u8 state[13][40];u32 rng[2];u16 params[13][8];u16 overlay[13][8];};
static struct lfo3_bank bank,*lfo3_bank=&bank;
static u8 owners[12]={0,0,0,3,4,5,6,7,8,9,10,11};
static u8 states[12]={0,1,2,3,4,5,6,7,8,9,10,11};
static u32 queries,available=1;
static u32 query(u32 op,const u32 *a)
{assert(op==ST_NATIVE_OWNERS);queries++;return available?(u32)(a?states:owners):0;}
static const struct st_machine_host_api services={.version=8,.native_query=query};
static const struct st_machine_host_api *host=&services;
#include "../mods/lfo3-st/poly.c"
int main(void)
{
    u16 input[13][8],original[13][8];u8 saved[13][40];const u16 *out;u32 t,i;
    for(t=0;t<13;t++)for(i=0;i<8;i++)original[t][i]=input[t][i]=(u16)(100*t+i);
    for(t=0;t<13;t++)for(i=0;i<40;i++)saved[t][i]=bank.state[t][i]=(u8)(9*t+i);
    out=lfo3_voice_bank(&input[0][0]);assert(out==&input[0][0]);
    for(t=0;t<13;t++)assert(!memcmp(out+8*t,original[t==1 || t==2?0:t],16));
    assert(!memcmp(bank.state,saved,sizeof saved)); /* native private phases untouched */
    input[0][7]=0x1234;out=lfo3_voice_bank(&input[0][0]);assert(out==&input[0][0] && out[15]==0x1234);
    states[1]=states[2]=0;assert(lfo3_voice_bank(&input[0][0])==&input[0][0]);
    assert(input[1][7]==0x1234); /* no restore of the old slave configuration */
    states[1]=1;states[2]=2;out=lfo3_voice_bank(&input[0][0]);assert(out[15]==0x1234);
    owners[1]=1;owners[2]=1;input[1][7]=0x5678;
    out=lfo3_voice_bank(&input[0][0]);assert(out==&input[0][0] && out[23]==0x5678);
    available=0;assert(lfo3_voice_bank(&input[0][0])==&input[0][0]);
    assert(!memcmp(bank.state,saved,sizeof saved) && queries==11);
    puts("PASS: LFO3 projects master locks in place, keeps its SRAM pointer and private phases, no slave restoration");
}
