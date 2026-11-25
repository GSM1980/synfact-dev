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
    meter=m.word(m.sym['comp_hot'])+56   # comp_runtime.meter (comp.h)
    area={(x,y) for x in (122,123) for y in range(1,51)}
    # 1.1.0: F reads the peak reduction since the last repaint: a box (x 48..70,
    # y 11..19, rounded corners) with "-6.5" inside and the label GR, in the
    # page's label font. Native text is a boundary here (the font objects are
    # filled by boot constructors this fixture does not run): COMP's calls to the
    # printf-style text (400f8bd0) are recorded with their font, position,
    # alignment and text, formatted here; the box is compared pixel by pixel.
    fbox={(x,y) for x in range(44,75) for y in range(0,21)}
    def gr_text(h):return ('-' if h else '')+'%d.%d'%(h//2,5*(h&1))
    assert [gr_text(h) for h in (0,1,13,63)]==['0.0','-0.5','-6.5','-31.5']
    texts=m.texts                 # recorded by NativeDraw (test_st_comp_emu.py)
    def f_render():
        bmp,pix=W+0xe000,W+0xe400
        m.u.mem_write(pix,bytes(1024));m.call(0x400f72be,bmp,128,64,pix,0)
        for box in ((49,19,69,19),(49,11,69,11),(48,12,48,18),(70,12,70,18)):m.call(0x400f7fb6,bmp,*box,1)
        return screen(m,pix)&fbox
    box=f_render();assert len(box)==2*21+2*7
    def check_f():
        h=m.word(m.sym['gr_shown'])
        assert screen(m,pixels)&fbox==box,'F box'
        assert texts[-2:]==[(13,2,gr_text(h)),(0,2,'GR')],('F texts',texts[-2:],gr_text(h))
        texts.clear();return h
    xored=set()
    def with_bar(base,height):
        return ((base-xored)|{(x,y) for x in (122,123) for y in range(51-height,51)})^xored
    # The firmware's empty-control placeholder in F (a dotted box on the unit):
    # COMP clears the slot before its box, so nothing of it is left.
    set_values(m,DEFAULT[:5]+[0x7f00]);m.w(meter,0)
    m.u.mem_write(pixels,bytes(1024));m.call(0x40030b84,view,bitmap)
    for x in range(46,73,2):
        for y in (2,23):m.call(0x400f7fb6,bitmap,x,y,x,y,1)
    for y in range(2,24,2):
        for x in (46,72):m.call(0x400f7fb6,bitmap,x,y,x,y,1)
    m.call('comp_draw',view,bitmap,0);check_f()
    assert not {(x,y) for x in range(45,74) for y in range(0,26)}&screen(m,pixels)-box,'F placeholder left'
    for values in ([0,0,0,0,0,0x7f00],DEFAULT[:5]+[0x7f00],
                   [0x7f00]*4+[0x700,0x7f00]):
        set_values(m,values);m.w(meter,0)
        m.u.mem_write(pixels,bytes(1024));draw_frame(m,view,bitmap)
        assert check_f()==0
        base=screen(m,pixels)-fbox
        assert not base&area
        assert {(125,22),(126,30),(127,24)}<=base,'native page indicators not drawn'
        assert not {(x,y) for x in (120,121,124) for y in range(64)}&base,'control enters a gutter'
        for half_db,height in reference.items():
            m.w(meter,half_db<<22)
            draw_frame(m,view,bitmap);actual=screen(m,pixels)-fbox
            expected=with_bar(base,height)
            assert check_f()==min(half_db,63),'ascending: the peak is the current reduction'
            assert actual==expected,(values,half_db,actual^expected)
            if out and values[:5]==DEFAULT[:5] and half_db in (0,12,36,63):
                capture(screen(m,pixels),out/('comp-gr-%s.png'%{0:'0db',12:'6db',36:'18db',63:'full'}[half_db]))
        # Falls without a fresh framebuffer: the whole old bar is erased.
        m.w(meter,0);draw_frame(m,view,bitmap)
        assert screen(m,pixels)-fbox==base
        assert check_f()==63,'a fall shows the window peak once'
        draw_frame(m,view,bitmap);assert check_f()==0
    set_values(m,DEFAULT);m.w(meter,63<<22)
    draw_frame(m,view,bitmap);assert screen(m,pixels)&area==xored,'MIX 0 must show no attenuation'
    assert check_f()==0,'MIX 0 reads 0.0'
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
    assert screen(m,pixels)&area=={(x,y) for x in (122,123) for y in range(51-height,51)}^xored
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
    # 0.3: at most one meter repaint every 3 UI ticks (10 Hz), none for a
    # 1-pixel move except down to 0. Ticks while not visible do not count.
    def ticks(n):
        hits=[]
        for _ in range(n):
            m.invalidations.clear();m.call('comp_tick',ctrl);hits.append(bool(m.invalidations))
            if m.invalidations:draw_frame(m,view,bitmap)
        return hits
    m.w(view+0x90,1)
    assert ticks(2)==[False,True],'released bar: repaint on the third visible tick since the last'
    m.w(meter,63<<22)                                       # full scale: a big step
    assert ticks(3)==[False,False,True],'10 Hz at most'
    m.w(meter,62<<22)                                       # 50 -> 49 px: one pixel
    assert ticks(6)==[False]*6,'a 1-pixel move does not repaint'
    m.w(meter,2<<22)                                        # 49 -> 1 px, waited long enough
    assert ticks(1)==[True]
    m.w(meter,0)                                            # 1 -> 0 px: allowed, still 10 Hz
    assert ticks(3)==[False,False,True],'1 -> 0 px clears'
    assert screen(m,pixels)&area==xored
    # F's window: a short peak between two repaints is what F shows; the next
    # repaint starts from the reduction at that moment.
    draw_frame(m,view,bitmap)
    m.w(meter,40<<22);m.call('comp_tick',ctrl);m.w(meter,10<<22);m.call('comp_tick',ctrl)
    draw_frame(m,view,bitmap);assert check_f()==40,'the window peak'
    draw_frame(m,view,bitmap);assert check_f()==10,'then the current reduction'
    if out:capture(screen(m,pixels),out/'comp-gr-readout.png')
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
    print('PASS GR: native reference, 2x50 strip, F peak readout, 36 pixel-exact screens, no MUP/MIX/page-dot overlap, erase/bypass/audio/redraw/release/DRIVE, 10 Hz / 2-pixel repaint limit')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('core','bank','digitakt'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--out',type=Path);p.add_argument('--sram',action='store_true')
    a=p.parse_args();verify(a.core,a.bank,a.digitakt,a.out,a.sram)
