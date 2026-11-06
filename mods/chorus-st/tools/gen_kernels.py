# SPDX-License-Identifier: GPL-2.0-or-later
"""Write kernels.s: the chorus's EMAC kernels and its own filter coefficients.

    python mods/chorus-st/tools/gen_kernels.py

Generated from design.json (decimator and halfband reconstructor). The
Digitone's tables (modulation LUT, HPF) are not here: tables.s takes them from
imported/, which tools/import_digitone.py rebuilds from the owner's OS file.
kernels.s is a checked-in build input; rerun this after changing design.json.

Mono path (chorus 0.2): the track sends sum to one 48 kHz signal; it is
decimated, filtered and delayed once, and two modulated taps of the same ring
make the stereo wet. Loops are rolled: on the unit, the audio IRQ pays for
code fetched from DDR more than for loop instructions.
"""
import json
from pathlib import Path
FOLDER=Path(__file__).resolve().parents[1]

# The kernels that run every active block, first and contiguous between
# chorus_hot_code and chorus_hot_code_end: chorus_place copies that span to
# MAIN SRAM (<syntakt/sram.h>). They branch only within themselves and reach
# memory through their arguments, so the copy runs as is.
# 882 bytes: inside ST_SRAM_CHORUS_CODE_END. The HPF (about 90 instructions
# a block) runs from DDR to stay there.
HOT=('chorus24_mix','chorus24_decimate','chorus24_delay','chorus24_upsample',
     'chorus24_return_stable')

def hot_first(lines):
    head,chunks,name=[],{},None
    for line in lines:
        if line.startswith('.globl chorus24_'):name=line.split()[1];chunks[name]=[]
        (chunks[name] if name else head).append(line)
    order=list(HOT)+[n for n in chunks if n not in HOT]
    out=head+['.balign 4','.globl chorus_hot_code','chorus_hot_code:']
    for n in order:
        out+=chunks[n]
        if n==HOT[-1]:out+=['.globl chorus_hot_code_end','chorus_hot_code_end:']
    return out

SAVE_ALL=['lea -44(%sp),%sp','movem.l %d2-%d7/%a2-%a6,(%sp)']
RESTORE_ALL=['movem.l (%sp),%d2-%d7/%a2-%a6','lea 44(%sp),%sp','rts']

def assembly():
    design=json.loads((FOLDER/'design.json').read_text())
    positions=design['decimator']['positions']
    taps=len(positions);regs=['%d0','%d1','%d2','%d3','%d4','%d5','%d6','%a3']
    assert taps==16 and max(positions)<24
    a=['.text']

    # chorus24_mix(dst[32], src[n], gains[n], steps[n], n, ramp): the mono
    # send sum, dst = -sum(gain*src) at 48 kHz. Four samples at a time in the
    # four 48-bit accumulators, every track into them, one saturating
    # extraction per sample (MACSR OMC). With ramp, each track's gain steps
    # before every sample and is written back.
    a+=['.globl chorus24_mix','chorus24_mix:']+SAVE_ALL+[
        'move.l #0xa0,%macsr','moveq #0,%d0','move.l %d0,%acc0','move.l %d0,%acc1',
        'move.l %d0,%acc2','move.l %d0,%acc3','move.l 48(%sp),%a0','moveq #0,%d7',
        'tst.l 68(%sp)','bne.w mix_ramp_chunk']
    for kind in ('stable','ramp'):
        a+=[f'mix_{kind}_chunk:','move.l 52(%sp),%a1','move.l 56(%sp),%a2',
            'move.l 60(%sp),%a3','move.l 64(%sp),%d6',f'mix_{kind}_track:',
            'move.l (%a1)+,%a4','lea (0,%a4,%d7.l),%a4','move.l (%a2),%d4']
        if kind=='ramp':a+=['move.l (%a3)+,%d5']
        a+=['move.l (%a4)+,%d0']
        for acc in range(4):
            if kind=='ramp':a+=['add.l %d5,%d4']
            load=',(%a4)+,%d0' if acc<3 else ''
            a+=[f'msac.l %d4,%d0{load},%acc{acc}']
        a+=['move.l %d4,(%a2)+' if kind=='ramp' else 'addq.l #4,%a2',
            'subq.l #1,%d6',f'bne.w mix_{kind}_track']
        for acc in range(4):a+=[f'movclr.l %acc{acc},%d0','move.l %d0,(%a0)+']
        a+=['addi.l #16,%d7','cmpi.l #128,%d7',f'bne.w mix_{kind}_chunk','bra.w mix_done']
    a+=['mix_done:']+RESTORE_ALL

    # chorus24_decimate(in, low[16], coeffs, ring): 48 -> 24 kHz, sixteen taps
    # in two passes of eight (coefficients in registers); y[j] uses x[2j-p].
    a+=['.globl chorus24_decimate','chorus24_decimate:']+SAVE_ALL+[
        'move.l #0xa0,%macsr','movclr.l %acc0,%d7']
    for group,start in enumerate((0,8)):
        a+=['move.l 56(%sp),%a6']
        if start:a+=[f'lea {start*4}(%a6),%a6']
        a+=['movem.l (%a6),%d0-%d6/%a3','move.l 48(%sp),%a0','lea 128(%a0),%a2']
        a+=['move.l 52(%sp),%a6','move.l 60(%sp),%a1'] if group else ['move.l 52(%sp),%a1']
        a+=[f'dec_loop{group}:']
        if group:a+=['move.l (%a6)+,%d7','move.l %d7,%acc0']
        a+=[f'move.l {-4*positions[start]}(%a0),%a4']
        for k in range(8):
            load=f',{-4*positions[start+k+1]}(%a0),%a4' if k<7 else ''
            a+=[f'mac.l {regs[k]},%a4{load},%acc0']
        a+=['movclr.l %acc0,%d7']
        if group:a+=['add.l %d7,%d7','sats.l %d7']
        a+=['move.l %d7,(%a1)+','lea 8(%a0),%a0','cmp.l %a0,%a2',f'bne.w dec_loop{group}']
    a+=RESTORE_ALL

    # chorus24_hpf(ring, state[2] {x, y}, coefficient): the one-pole HPF of
    # V56 on the sixteen new 24 kHz samples, in place.
    a+=['.globl chorus24_hpf','chorus24_hpf:','lea -16(%sp),%sp','movem.l %d2-%d5,(%sp)',
        'move.l 20(%sp),%a0','move.l 24(%sp),%a1','move.l 28(%sp),%d1','move.l %d1,%d2',
        'asr.l #1,%d2','addi.l #0x40000000,%d2','move.l (%a1),%d3','move.l 4(%a1),%d5',
        'moveq #16,%d4','hp_loop:','msac.l %d2,%d3,(%a0),%d3,%acc0','mac.l %d2,%d3,%acc0',
        'mac.l %d1,%d5,%acc0','movclr.l %acc0,%d5','move.l %d5,(%a0)+','subq.l #1,%d4',
        'bne.w hp_loop','move.l %d3,(%a1)','move.l %d5,4(%a1)','movem.l (%sp),%d2-%d5',
        'lea 16(%sp),%sp','rts']

    # chorus24_delay(up, ring, nodes[2][2] {from, to}, w): two taps of one ring,
    # left and right, each moving linearly over the block (Q22 positions);
    # V56's fractional interpolation. Two MACs per sample avoid overflowing
    # x0-x1; preserve factor two of DN wet.
    a+=['.globl chorus24_delay','chorus24_delay:']+SAVE_ALL
    for ch in (0,1):
        a+=['move.l 48(%sp),%a0']
        if ch:a+=['addq.l #4,%a0']
        a+=['move.l 52(%sp),%a1','move.l 56(%sp),%a2']
        if ch:a+=['addq.l #8,%a2']
        a+=['move.l 60(%sp),%d4','move.l (%a2)+,%a4','move.l (%a2),%d0','sub.l %a4,%d0',
            'asr.l #4,%d0','move.l %d0,%a5','moveq #16,%d7',f'delay_loop{ch}:',
            'add.l %a5,%a4','move.l %a4,%d2','move.l %d2,%d3','moveq #22,%d6','asr.l %d6,%d3',
            # Drop every integer bit before Q22 -> positive Q31 fraction.
            # A shift of only nine retained integer parity as the sign bit.
            'lsl.l #8,%d2','lsl.l #2,%d2','lsr.l #1,%d2',
            'move.l %d4,%d0','sub.l %d3,%d0','andi.l #255,%d0',
            'lea (%a1,%d0.l*4),%a3','move.l #0x7fffffff,%d6','sub.l %d2,%d6',
            'move.l 4(%a3),%d1','mac.l %d6,%d1,(%a3),%d0,%acc0',
            'mac.l %d2,%d0,%acc0','movclr.l %acc0,%d0','add.l %d0,%d0','sats.l %d0',
            'move.l %d0,(%a0)','addq.l #8,%a0','addq.l #1,%d4',
            'subq.l #1,%d7',f'bne.w delay_loop{ch}']
    a+=RESTORE_ALL

    # chorus24_upsample(up, out, coeffs): 15-tap halfband 24 -> 48 kHz, stereo:
    # identity odd phase and eight-coefficient even phase.
    a+=['.globl chorus24_upsample','chorus24_upsample:']+SAVE_ALL+[
        'move.l 48(%sp),%a0','move.l 52(%sp),%a1',
        'move.l 56(%sp),%a6','movem.l (%a6),%d0-%d5/%a3-%a4','lea 128(%a0),%a2','up_loop:',
        'move.l 0(%a0),%d7']
    for k in range(8):
        reg=f'%d{k}' if k<6 else f'%a{k-3}'
        a+=[f'mac.l {reg},%d7,{-8*k+4}(%a0),%d7,%acc0']
        load=f',{-8*k-8}(%a0),%d7' if k<7 else ''
        a+=[f'mac.l {reg},%d7{load},%acc1']
    a+=['movclr.l %acc0,%d7','move.l %d7,(%a1)+','movclr.l %acc1,%d7','move.l %d7,(%a1)+',
        'move.l -24(%a0),(%a1)+','move.l -20(%a0),(%a1)+',
        'lea 8(%a0),%a0','cmp.l %a0,%a2','bne.w up_loop']+RESTORE_ALL

    # chorus24_return_stable(bus, wet, gain): bus += gain*wet, 32 stereo frames.
    a+=['.globl chorus24_return_stable','chorus24_return_stable:',
        'lea -12(%sp),%sp','movem.l %d2-%d4,(%sp)',
        'move.l 16(%sp),%a0','move.l 20(%sp),%a1','move.l 24(%sp),%d2',
        'moveq #32,%d4','stable_loop:',
        'move.l (%a1)+,%d0','mac.l %d2,%d0,(%a0),%d0,%acc0',
        'move.l (%a1)+,%d1','mac.l %d2,%d1,4(%a0),%d1,%acc1',
        'movclr.l %acc0,%d3','add.l %d0,%d3','sats.l %d3','move.l %d3,(%a0)+',
        'movclr.l %acc1,%d3','add.l %d1,%d3','sats.l %d3','move.l %d3,(%a0)+',
        'subq.l #1,%d4','bne.w stable_loop','movem.l (%sp),%d2-%d4','lea 12(%sp),%sp','rts']

    # chorus24_history(input, input_tail, up, up_tail): keep the 24 newest
    # mono input samples and 8 newest stereo 24 kHz frames for the next block.
    a+=['.globl chorus24_history','chorus24_history:','lea -24(%sp),%sp',
        'movem.l %d2-%d7,(%sp)']
    for dst,src,words in ((28,32,24),(36,40,16)):
        a+=[f'move.l {dst}(%sp),%a0',f'move.l {src}(%sp),%a1']
        for off in range(0,words*4,32):
            a+=[f'movem.l {off}(%a1),%d0-%d7',f'movem.l %d0-%d7,{off}(%a0)']
    a+=['movem.l (%sp),%d2-%d7','lea 24(%sp),%sp','rts']

    a=hot_first(a)
    a+=['.section .rodata','.balign 16']
    tables={'chorus24_decimator_coeffs':[round(design['decimator']['coefficients'][k]*2**30) for k in positions],
            'chorus24_up_coeffs':[round(x*2**31) for x in design['reconstructor']['phase_coefficients']]}
    for name,values in tables.items():
        a+=['.globl '+name,name+':']+[f'.long {v}' for v in values]
    return '\n'.join(a)+'\n'


if __name__=='__main__':
    text='| Generated by tools/gen_kernels.py from design.json; do not edit.\n'+assembly()
    (FOLDER/'kernels.s').write_text(text,newline='\n')
    print('wrote',FOLDER/'kernels.s')
