/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native C receiver fixture. Explicit RAM buffers; no emulator or device IO.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
static unsigned int table[256],win[256],bell,stocks[128];
#define TABLE table
#define WIN win
#define DOORBELL bell
#define STOCK_TABLE stocks
#include "../mods/core-st/audio-pool.c"
static void request(u32 seq,u32 cmd,u32 dest,u32 length,const u32 *payload,u32 words)
{
    u32 i;memset(win,0,sizeof(win));win[0]=MAGIC;win[1]=seq<<16|cmd;win[2]=dest;win[3]=length;
    win[4]=win[0]+win[1]+dest+length;
    for(i=0;i<words;i++){win[8+i]=payload[i];win[4]+=payload[i];}
    audio_pool_receive();
}
int main(void)
{
    u32 payload[4]={0x12345678,0xabcdef12,0,0},pointer=LO;
    void *ram=VirtualAlloc((void *)0x42100000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(ram==(void *)0x42100000u);
    request(1,1,LO,8,0,0);assert(win[6]==0x10001);
    request(2,2,LO,8,payload,2);assert(win[6]==0x20001 && *(u32 *)LO==payload[0]);
    request(3,3,7,4,0,0);assert(win[6]==0x30001 && table[14]==4 && table[15]==LO);
    request(3,3,7,4,0,0);assert(win[6]==0x30001); /* lost ACK: idempotent */
    request(4,1,LO,8,0,0);assert((win[6]&65535)==2); /* live PCM cannot be overwritten */
    request(5,5,8,4,&pointer,1);assert((win[6]&65535)==1 && table[16]==4 && table[17]==LO);
    request(6,4,7,0,0,0);assert(!table[14]);
    request(7,1,LO,8,0,0);assert((win[6]&65535)==2); /* second reference still owns it */
    request(8,4,8,0,0,0);assert(!table[16]);
    request(9,1,LO-4,8,0,0);assert((win[6]&65535)==2);
    request(10,1,HI-4,8,0,0);assert((win[6]&65535)==2);
    request(11,1,LO,8,0,0);assert((win[6]&65535)==1);
    request(12,3,7,4,0,0);assert((win[6]&65535)==2 && !table[14]); /* incomplete transfer */
    request(13,2,LO+4,8,payload,2);assert((win[6]&65535)==2); /* out of order */
    request(14,2,LO,8,payload,2);assert((win[6]&65535)==1);
    request(15,3,128,4,0,0);assert((win[6]&65535)==2);
    request(16,3,7,4,0,0);assert((win[6]&65535)==1);
    request(17,5,8,5,&pointer,1);assert((win[6]&65535)==2); /* wrong extent size */
    stocks[0]=240000;stocks[1]=0x4004f754;pointer=stocks[1];
    request(18,5,0,240000,&pointer,1);assert((win[6]&65535)==1 && table[0]==240000);
    request(19,5,1,240001,&pointer,1);assert((win[6]&65535)==2);
    win[0]=MAGIC;win[1]=20<<16|2;win[2]=LO;win[3]=8;win[4]=0;win[6]=0;
    audio_pool_receive();assert(!win[6]); /* bad checksum has no side effects/ACK */
    audio_pool_cursor(0,LO,100,150,1);audio_pool_cursor(7,LO+256,300,40,1);
    audio_pool_cursor(8,0,0,0,0); /* invalid track cannot overwrite a record */
    request(21,6,0,0,0,0);
    assert(win[6]==0x150001 && win[8]==LO && win[9]==100 && win[10]==100 && win[11]==1);
    assert(win[36]==LO+256 && win[37]==300 && win[38]==40 && win[39]==1);
    {u32 i,sum=21;for(i=0;i<32;i++)sum+=win[8+i];assert(sum==win[7]);}
    request(22,6,0,1,0,0);assert((win[6]&65535)==2);
    assert(bell);assert(VirtualFree(ram,0,MEM_RELEASE));
    puts("PASS: core AUDIO pool staged publication, duplicate ACK, retirement, overlap, boundaries and checksums");return 0;
}
