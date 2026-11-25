/* SPDX-License-Identifier: GPL-2.0-or-later
 * Samplers 1.6.2: st_machine_cursor_publish (machines.h) finds the pool
 * service's cursor record {454c4301, fn} at +32 through the ABI directory
 * (40040f50: its channel table is service + b80h, with {454c4801, table} at
 * +40), wherever the core puts the service. From core 2.2.0 to 2.6.x it looked
 * at 42100770 and the SP playhead never reached MAIN. The directory and a
 * service are mapped on the host, as AUDIO has them. No emulator or device IO.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <syntakt/machines.h>
static st_u32 published[5],calls;
static void cursor(st_u32 t,st_u32 p,st_u32 f,st_u32 pos,st_u32 a)
{published[0]=t;published[1]=p;published[2]=f;published[3]=pos;published[4]=a;calls++;}
static void publish_at(st_u32 base)
{
    volatile st_u32 *api=(volatile st_u32 *)0x40040f50u,*service=(volatile st_u32 *)base;
    const st_u32 table=base+0xb80u;
    memset((void *)base,0,0xc00);calls=0;
    api[0]=0x454c5001u;api[2]=128;api[3]=0;
    st_machine_cursor_publish(3,1,2,3,4);assert(!calls);              /* the service is not installed */
    api[3]=table;
    st_machine_cursor_publish(3,1,2,3,4);assert(!calls);              /* no records yet */
    service[10]=0x454c4801u;service[11]=table;service[8]=0x454c4301u;service[9]=(st_u32)cursor;
    st_machine_cursor_publish(3,0x1234,96000,512,1);
    assert(calls==1 && published[0]==3 && published[1]==0x1234 && published[2]==96000 && published[3]==512 && published[4]==1);
    service[11]=table+4;st_machine_cursor_publish(3,1,2,3,4);assert(calls==1);   /* a table that is not this service's */
    service[11]=table;service[8]=0;st_machine_cursor_publish(3,1,2,3,4);assert(calls==1); /* no cursor record */
    service[8]=0x454c4301u;api[0]=0;st_machine_cursor_publish(3,1,2,3,4);assert(calls==1); /* no directory */
}
int main(void)
{
    volatile st_u32 *old=(volatile st_u32 *)0x42100770u;
    assert(VirtualAlloc((void *)0x40040000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x40040000u);
    assert(VirtualAlloc((void *)0x42100000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x42100000u);
    assert(VirtualAlloc((void *)0x44040000u,0x20000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x44040000u);
    publish_at(0x4404ff50u);                     /* core 2.2 and later */
    /* The old fixed address is not read: a record left there is ignored. */
    old[0]=0x454c4301u;old[1]=(st_u32)cursor;((volatile st_u32 *)0x40040f50u)[0]=0;calls=0;
    st_machine_cursor_publish(3,1,2,3,4);assert(!calls);
    publish_at(0x42100750u);                     /* cores 1.8 to 2.1: the same records */
    puts("PASS: the SP cursor record is found through the AUDIO ABI directory at both service places, never at a fixed address");
    return 0;
}
