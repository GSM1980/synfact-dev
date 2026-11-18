/* SPDX-License-Identifier: GPL-2.0-or-later
 * Production step selection with explicit native RAM/patch doubles. No OS
 * input: this does not replace the ColdFire/stock-site differential. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../mods/playhead-st/seq-direction.c"

void playhead_trig_a(void){} void playhead_trig_b(void){}
void playhead_trig_c(void){} void playhead_ui_copy(void){}
void playhead_step_shown(void){}
unsigned int playhead_seq_lock(void){return 0;}
void playhead_seq_unlock(unsigned int sr){(void)sr;}
void playhead_seq_sync(void){}
void playhead_seq_code16(unsigned int at,unsigned int v)
{unsigned char *p=(unsigned char *)at;p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v;}

static unsigned char pattern[0x22000];
static unsigned int selected(unsigned int t,unsigned int i,unsigned int length)
{
    unsigned int j=i%length,p=playhead_step(t,j,pattern,1);
    assert(p<length);
    assert(playhead_step(t,j,pattern,1)==p);
    assert(playhead_step(t,j,pattern,0)==p);
    return p;
}
static unsigned int folded(unsigned int rank,unsigned int length)
{return rank&1?length-1-rank/2:rank/2;}
int main(void)
{
    unsigned int flag,t,length,mode,i,k,p,want,previous=0,moves[3],counts=0;
    assert(sizeof(void *)==4);
    assert(VirtualAlloc((void *)0x43bc0000u,0x20000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(VirtualAlloc((void *)0x40080000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    *(unsigned int *)0x43bd8430u=(unsigned int)pattern;
    *(unsigned int *)0x43bd843cu=PLAYHEAD_KIT_BASE;
    *(unsigned int *)0x43bcc3a4u=0x1234567;
    /* Explicit patch boundary: provide the five preimages and bypass the
     * stock context CRC, which only the stock differential can verify. */
    for(i=0;i<SITES;i++)for(k=0;k<3;k++)playhead_seq_code16(sites[i].at+2*k,sites[i].stock[k]);
    playhead_seq_state=PLAYHEAD_READY;
    for(flag=0;flag<2;flag++)for(t=0;t<PLAYHEAD_TRACKS;t++)for(length=1;length<=64;length++){
        pattern[0x21379]=(unsigned char)flag;
        *(short *)(pattern+0x21374)=(short)length;
        *(short *)(pattern+0x499*t+969)=(short)length;
        for(mode=0;mode<PLAYHEAD_MODES;mode++){
            assert(!playhead_seq_set(0,t,mode));
            memset(moves,0,sizeof(moves));
            for(i=0;i<400*length;i++){
                p=selected(t,i,length);want=p;
                switch(mode){
                case PLAYHEAD_FORWARDS:want=i%length;break;
                case PLAYHEAD_BACKWARDS:want=length-1-i%length;break;
                case PLAYHEAD_ANCHOR:want=length==1 || !(i&1)?0:1+(i/2)%(length-1);break;
                case PLAYHEAD_STUTTER:want=(i/5*4+(i%5<4?i%5:3))%length;break;
                case PLAYHEAD_PENDULUM:
                case PLAYHEAD_FOLD:
                    k=length==1?0:i%(2*length-2);
                    if(k>=length)k=2*length-2-k;
                    want=mode==PLAYHEAD_FOLD?folded(k,length):k;break;
                }
                assert(p==want);
                if(i){
                    if(mode!=PLAYHEAD_FORWARDS && mode!=PLAYHEAD_BACKWARDS)
                        assert(length==1 || playhead_step(t,(i-1)%length,pattern,0)==previous);
                    k=(p+length-previous)%length;
                    if(mode==PLAYHEAD_WALK)assert(length==1 || k==1 || k==length-1);
                    if(mode==PLAYHEAD_WALK_PLUS && length>2){
                        assert(k==0 || k==1 || k==length-1);
                        moves[k==length-1?2:k]++;
                    }
                }
                previous=p;counts++;
            }
            if(mode==PLAYHEAD_WALK_PLUS && length>2){
                assert(moves[1]>400*length/2 && moves[1]<400*length*7/10);
                assert(moves[0]>400*length/10 && moves[2]>400*length/10);
            }
            assert(playhead_step(t,length+2,pattern,1)==length+2);
            /* New modes start their phrase on step 1 even if selected at
             * the last native count, then continue across the native wrap. */
            if(mode==PLAYHEAD_ANCHOR || mode==PLAYHEAD_FOLD){
                playhead_seq_set(0,t,mode);
                assert(playhead_step(t,length-1,pattern,1)==0);
                if(length>1)assert(playhead_step(t,0,pattern,1)==(mode==PLAYHEAD_FOLD?length-1:1));
            }
        }
        playhead_seq_set(0,t,PLAYHEAD_FORWARDS);
    }
    /* A shortened track discards the previous length's phase and cache. */
    pattern[0x21379]=0;*(short *)(pattern+0x21374)=8;
    playhead_seq_set(0,0,PLAYHEAD_FOLD);
    selected(0,0,8);selected(0,1,8);
    *(short *)(pattern+0x21374)=3;
    assert(playhead_step(0,1,pattern,1)==0);
    assert(playhead_step(0,2,pattern,1)==2);
    assert(playhead_step(0,0,pattern,1)==1);
    assert(playhead_step(PLAYHEAD_TRACKS,1,pattern,1)==1);
    playhead_seq_set(0,0,0xff);
    assert(playhead_modes[0][0]==PLAYHEAD_FOLD);
    playhead_seq_load(0,0);
    assert(!playhead_seq_installed);
    printf("PASS: %u selections; 9 modes; lengths 1..64, 13 tracks, both scale modes; cache, wraps, length changes\n",counts);
    return 0;
}
