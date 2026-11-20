"""Digitakt's actual GR draw against COMP's bounded Syntakt LCD strip.

Executes the original Digitakt meter calculation with its GR getter/vline
as explicit boundaries, and Syntakt's actual page/fonts/bitmap drawing.
Checks every LCD pixel outside the meter strip, including page dots, plus
the real compact meter's audio-driven rise, release and redraw publication.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from test_st_comp_emu import NativeDraw,initialize,set_values,block,draw_frame,W,SP,RET,ID,DEFAULT
from elekloader.syx import Syx
from unicorn import Uc,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN,UC_HOOK_CODE
from unicorn.m68k_const import *


def native_heights(digitakt):
    raw=digitakt.read_bytes()
    assert hashlib.sha256(raw).hexdigest()=='f78ba80fa7b1da5fb0e1ff61ad61e9e71aafe79f4364fc49679f3651353e3cf6'
    u=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN);u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
    for at,n in ((0x40000000,0x300000),(SP-0x10000,0x20000),(W,0x10000)):
        u.mem_map(at,n)
    u.mem_write(0x40000400,Syx(raw).section(3))
    reduction=[0];lines=[]
    def hook(uc,pc,size,user):
        if pc not in (0x400784e0,0x400c1268):return
        sp=uc.reg_read(UC_M68K_REG_A7)
        if pc==0x400784e0:uc.reg_write(UC_M68K_REG_D0,(-reduction[0])&0xffffffff)
        else:lines.append(struct.unpack('>5I',bytes(uc.mem_read(sp+4,20))))
        target=int.from_bytes(uc.mem_read(sp,4),'big')
        uc.reg_write(UC_M68K_REG_A7,sp+4);uc.reg_write(UC_M68K_REG_PC,target)
    u.hook_add(UC_HOOK_CODE,hook)
    result={}
    for half_db in (0,1,2,3,6,12,24,36,48,62,63,80):
        reduction[0]=half_db<<22;lines.clear()
        u.reg_write(UC_M68K_REG_SR,0x2700);u.reg_write(UC_M68K_REG_A7,SP)
        u.mem_write(SP,struct.pack('>3I',RET,W,W+0x1000))
        u.emu_start(0x40038f80,0x40038fc8,count=1000)
        assert u.reg_read(UC_M68K_REG_PC)==0x40038fc8
        expected=half_db*50//63
        assert lines==([(W+0x1000,126,50,(51-expected)&0xffffffff,1)] if expected else []),lines
        result[half_db]=min(expected,50)
    print('PASS original Digitakt draw: x=126, y=50, half-dB quantization, 50 pixels / 31.5 dB')
    return result


def fixture(core,bank,sram=False):
    m=NativeDraw(core,bank);initialize(m)
    if sram:assert m.call('comp_place')==3
    # Repeated native text construction must not allocate over the LCD's
    # pixel buffer (the single-draw fixture's original nearby heap).
    view,bitmap,pixels,pages=W+0x6000,W+0x7000,W+0x9000,W+0x5000
    m.w(view,0x401ca37c);m.w(view+0x88,0x14)
    m.w(view+0x7c,pages);m.w(view+0x80,pages+4);m.w(view+0x84,pages+8);m.w(pages,32)
    m.call('comp_draw',view,bitmap,0);m.w(view+0x90,1)
    m.u.mem_write(W+0xa100,m.read(0x401c3dd0,128))
    m.w(W+0xa000,W+0xa100);m.w(W+0xa120,0x4000d94a);m.w(W+0xa124,0x4000da32)
    m.w(0x443db21c,0x402aa114);m.w(0x443db230,0x402aa114)
    m.call(0x400f72be,bitmap,128,64,pixels,0)
    return m,view,bitmap,pixels


def screen(m,pixels):
    words=struct.unpack('>256I',m.read(pixels,1024))
    return {(x,y) for x in range(128) for y in range(64) if words[2*x+y//32]&(1<<(31-y%32))}


def capture(pixels,path):
    if not path:return
    path.parent.mkdir(parents=True,exist_ok=True);rows=bytearray()
    for y in reversed(range(64)):
        row=bytearray(b'\0')
        for x in range(128):row.extend(bytes((218,240,235) if (x,y) in pixels else (20,26,28))*6)
        rows.extend(row*6)
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',768,384,8,2,0,0,0))+
                     chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))


def verify(core,bank,digitakt,out,sram=False):
    reference=native_heights(digitakt)
    m,view,bitmap,pixels=fixture(core,bank,sram)
    meter=m.word(m.sym['comp_hot'])+52
    area={(x,y) for x in (122,123) for y in range(1,51)}
    for values in ([0,0,0,0,0,0x7f00],DEFAULT[:5]+[0x7f00],
                   [0x7f00]*4+[0x700,0x7f00]):
        set_values(m,values);m.w(meter,0)
        m.u.mem_write(pixels,bytes(1024));draw_frame(m,view,bitmap)
        base=screen(m,pixels)
        assert not base&area
        assert {(125,22),(126,30),(127,24)}<=base,'native page indicators not drawn'
        assert not {(x,y) for x in (120,121,124) for y in range(64)}&base,'control enters a gutter'
        for half_db,height in reference.items():
            m.w(meter,half_db<<22)
            draw_frame(m,view,bitmap);actual=screen(m,pixels)
            expected=base|{(x,y) for x in (122,123) for y in range(51-height,51)}
            assert actual==expected,(values,half_db,actual^expected)
            if out and values[:5]==DEFAULT[:5] and half_db in (0,12,36,63):
                capture(actual,out/('comp-gr-%s.png'%{0:'0db',12:'6db',36:'18db',63:'full'}[half_db]))
        # Falls without a fresh framebuffer: the whole old bar is erased.
        m.w(meter,0);draw_frame(m,view,bitmap)
        assert screen(m,pixels)==base
    set_values(m,DEFAULT);m.w(meter,63<<22)
    draw_frame(m,view,bitmap);assert not screen(m,pixels)&area,'MIX 0 must show no attenuation'
    # The real IRQ publishes GR; the UI tick invalidates the visible view.
    # Silence releases it. No hand-seeded meter for this part.
    set_values(m,[0x4000,0,0,0,0x700,0x7f00]);m.w(meter,0)
    root=W+0xb000;m.w(0x444e2334,root);m.u.mem_write(root+96,b'\0')
    for _ in range(96):block(m,[2**30]*32,[-2**30]*32)
    peak=m.word(meter);assert peak>6*2**23
    assert m.read(root+96,1)==b'\0','audio must not request a global redraw'
    ctrl,node=W+0xc000,W+0xc100
    m.w(ctrl+0x1c,node);m.w(node,ctrl+0x14);m.w(node+8,view);m.w(view+0x2c,ctrl)
    m.invalidations.clear();m.call('comp_tick',ctrl)
    assert m.invalidations==[view],'visible COMP must invalidate when the pixel height changes'
    draw_frame(m,view,bitmap);height=min(50,(peak>>22)*50//63)
    m.invalidations.clear();m.call('comp_tick',ctrl);assert not m.invalidations,'same height must not repaint'
    assert screen(m,pixels)&area=={(x,y) for x in (122,123) for y in range(51-height,51)}
    if out:capture(screen(m,pixels),out/'comp-gr-audio.png')
    m.u.mem_write(root+96,b'\0')
    for _ in range(400):block(m,[0]*32,[0]*32)
    assert m.word(meter)<peak and m.read(root+96,1)==b'\0'
    for field in (0x17,0x30):
        m.u.mem_write(view+field,b'\1');m.call('comp_tick',ctrl)
        assert not m.invalidations,'hidden/closed COMP must not repaint'
        m.u.mem_write(view+field,b'\0')
    m.w(ctrl+0x1c,ctrl+0x14);m.call('comp_tick',ctrl)
    assert not m.invalidations,'COMP under an opaque overlay must not repaint'
    m.w(ctrl+0x1c,node);m.w(view+0x90,0);m.call('comp_tick',ctrl)
    assert not m.invalidations,'DRIVE must not repaint for COMP metering'
    m.w(view+0x90,1);m.call('comp_tick',ctrl);assert m.invalidations==[view]
    # Returning to DRIVE does not draw or clear any part of the meter strip.
    m.w(view+0x90,0);before=m.read(pixels,1024);m.call('comp_draw',view,bitmap,0)
    assert m.read(pixels,1024)==before
    if out:
        (out/'validacion-gr.json').write_text(json.dumps(dict(ok=True,
            native_reference='Digitakt 1.54, draw 40038f80, GR getter 400784e0',
            core_sha256=hashlib.sha256(core.read_bytes()).hexdigest(),
            bank_sha256=hashlib.sha256(bank.read_bytes()).hexdigest(),
            meter_bounds=dict(x0=122,x1=123,y0=1,y1=50),full_scale_db=31.5,
            pixel_exact_screens=36,other_pixels_unchanged=True,
            audio_rise_release_redraw=True,erase_and_bypass=True,sram=sram,
            device_access=False,limits='Native application/model selection, heap and some UI state are fixture boundaries; no hardware timing.'),indent=2)+'\n')
    print('PASS GR: native reference, 2x50 strip, 36 pixel-exact screens, no MUP/MIX/page-dot overlap, erase/bypass/audio/redraw/release/DRIVE')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('core','bank','digitakt'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--out',type=Path);p.add_argument('--sram',action='store_true')
    a=p.parse_args();verify(a.core,a.bank,a.digitakt,a.out,a.sram)
