/* SPDX-License-Identifier: GPL-2.0-or-later
 * Stand-ins for the four native chain calls of the stereo engine's R pass
 * (tests/test_st_stereo_engine_emu.py). The first call of a pass (drive,
 * or base-width when the engine's own drive ran) logs the chain state it
 * finds, as the native chain would read it; every call then writes that
 * state, as the native chain does. Built for the
 * target with the bank's flags, run in Unicorn; never in a bank.
 */
typedef unsigned int u32;
static const u32 control[5][2]={{0x800083b0u,40},{0x80008550u,12},{0x80008740u,88},
    {0x800088e0u,8},{0x800049e0u,24}};
static const u32 signal[3][2]={{0x80008580u,8},{0x800088a0u,16},{0x80008920u,16}};
u32 stub_log[172+40],stub_calls,stub_pass;
static void touch(u32 salt)
{
    u32 i,j,*p;
    for(i=0;i<5;i++)for(p=(u32 *)control[i][0],j=0;j<control[i][1];j++)p[j]=p[j]*69069u+salt;
    for(i=0;i<3;i++)for(p=(u32 *)signal[i][0],j=0;j<signal[i][1];j++)p[j]^=0x9e3779b9u+salt+j;
}
static void first(void)
{
    u32 i,j,k=0;const u32 *p;
    if(stub_pass++)return;
    for(i=0;i<5;i++)for(p=(const u32 *)control[i][0],j=0;j<control[i][1];j++)stub_log[k++]=p[j];
    for(i=0;i<3;i++)for(p=(const u32 *)signal[i][0],j=0;j<signal[i][1];j++)stub_log[k++]=p[j];
}
void stub_drive(void){first();stub_calls++;touch(1);}
void stub_width(void){first();touch(2);}
void stub_multi(void){touch(3);}
void stub_envelope(void){touch(4);}
u32 stub_tails,stub_tail_track,stub_tail_targets[8];
void stub_tail_record(u32 t)
{
    u32 i;const u32 *targets=(const u32 *)0x80008900u;
    stub_tails++;stub_tail_track=t;
    for(i=0;i<8;i++)stub_tail_targets[i]=targets[i];
    touch(3);
}
/* The multimode's second part (0x40001ea4), entered by the engine's
 * multimode_tail with the native frame: t at 48(sp); its epilogue. */
__asm__(".text\n\t.even\n\t.globl stub_multimode_tail\n"
        "stub_multimode_tail:\n\t"
        "move.l 48(%sp),-(%sp)\n\tjsr stub_tail_record\n\taddq.l #4,%sp\n\t"
        "move.l #32,%macsr\n\tmovem.l (%sp),%d2-%d7/%a2-%a6\n\tlea 100(%sp),%sp\n\trts\n");
