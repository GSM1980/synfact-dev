/* Native musical timeline arithmetic, no firmware substitutes or device IO. */
#include <assert.h>
#include <stdio.h>
#include "../mods/arp-st/clock.h"
int main(void)
{
    struct arp_clock c={0};unsigned int i,k,due=0,n,begin,end,step,tempo;
    const unsigned int den[]={1,2,3,4,5,6,8,10,12,16,20,24,32,40,48,64,80,96};
    assert(!arp_clock_poll(&c,0,28800,1,&due));
    for(i=0;i<18;i++)for(k=0;k<3;k++){
        tempo=(unsigned int[]){3600,14400,36000}[k];step=2*tempo;
        assert(arp_clock_arm(&c,0,i,1));assert(c.period*den[i]==86400000u);
        n=0;for(begin=0;begin<86400000u;begin=end){
            end=begin+step;if(end>86400000u)end=86400000u;
            if(arp_clock_poll(&c,begin,end,1,&due)){assert(due==n*c.period);n++;}
        }
        assert(n==den[i] && !c.late);
    }
    assert(arp_clock_arm(&c,28800,9,1));assert(!arp_clock_poll(&c,0,28800,1,&due));
    assert(arp_clock_poll(&c,28800,57600,1,&due) && due==28800);
    assert(arp_clock_arm(&c,0xfffffff0u,17,7));
    assert(arp_clock_poll(&c,0xfffffff0u,0x7070u,7,&due) && due==0xfffffff0u);
    n=due+c.period;assert(!arp_clock_poll(&c,0x7070u,n,7,&due));
    assert(arp_clock_poll(&c,n,n+28800u,7,&due) && due==n);
    assert(!arp_clock_poll(&c,n,n+28800u,8,&due) && !c.armed);
    assert(arp_clock_arm(&c,0,17,1));assert(arp_clock_poll(&c,100000000,100028800,1,&due));
    assert(due==100000000 && c.late==1);assert(!arp_clock_poll(&c,100028800,100057600,1,&due));
    arp_clock_stop(&c);assert(!arp_clock_poll(&c,100000000,100028800,1,&due));
    assert(!arp_clock_arm(&c,0,18,1) && !c.armed);assert(!arp_clock_arm(&c,0,0,0));
    assert(arp_clock_arm(&c,0,17,1));assert(!arp_clock_poll(&c,100,99,1,&due));
    /* Changing tempo changes native frame spans, not the musical period. */
    n=0;for(begin=0;begin<86400000u;begin=end){
        step=begin<43200000u?28800u:72000u;end=begin+step;
        if(end>86400000u)end=86400000u;
        if(arp_clock_poll(&c,begin,end,1,&due)){assert(due==n*900000u);n++;}
    }
    assert(n==96 && !c.late);
    puts("PASS: 18 divisions x 3 tempos, exclusive frame end, wrap, generation, stop, late recovery, tempo change");
}
