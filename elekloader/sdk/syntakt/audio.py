# SPDX-License-Identifier: GPL-2.0-or-later
"""Source-built CPU2 services; explicit, pinned companion to the MAIN core."""
import hashlib
import os
from pathlib import Path
import struct
from elekloader import syx
from elekloader.sdk import build as sdk, elf
from elekloader.isa import coldfire as cf

HERE=Path(__file__).resolve().parents[3]/'mods/core-st'
BASE=0x40000400
CAVE=0x40040860
TABLES=(0x40040e00,0x40040e50,0x40040ec0)
# Remainder of the declared runtime window (devices/syntakt.py, the same
# [40040860,40041050) companion cave Synfact's AUDIO code ran in), after the
# scheduler tables and the two 16-byte ABI directories at 40040f40/40040f50.
UPPER=(0x40040f60,0x40041050)
# The window is also the zero tail of the stock base-width filter's pole table
# (40040060, 4-byte poles): stock 40002320 reads T[(BASE+WDTH)>>6] up to index
# 1023. Indices 508..1023 are zero on the stock OS (an open low-pass), so both
# low-pass lookups call audio_bw_index (audio-bw-filter.s), which bounds the
# index to 508. Index 508 is below the window; the output is the stock one.
BW_TABLE,BW_OPEN=0x40040060,508
BW_SITES=(0x40002364,0x400023e8)
BW_STOCK=bytes.fromhex('d081ec8041f00c00')   # add.l d1,d0; asr.l #6,d0; lea (a0,d0.l*4),a0
# Chain end (core 1.8, <syntakt/stereo.h>): the IRQ site after the AMP stage
# and the 16-byte directory between the render table (ends 40040f30) and the
# ABI directory at 40040f40. Word 1 is the hook audio_chain_end calls.
CHAIN_END,CHAIN_DIRECTORY=0x40000ac6,0x40040f30

def audit(code,base=0):
    """Check an explicit code range, including its complete final instruction."""
    if base&1 or len(code)&1:
        raise ValueError('ColdFire code range is not word aligned')
    count=0
    for address,ins in cf.disassemble_lengths(code,base,base,base+len(code)):
        if address+ins.length>base+len(code):
            raise ValueError('truncated ColdFire instruction at %#x'%address)
        if ins.flags&(cf.ILLEGAL|cf.FPU|cf.LINEF):
            raise ValueError('unsupported ColdFire instruction at %#x (%s)'%(address,ins.op))
        count+=1
    return count

def audit_functions(image,section_name,base):
    """Audit linked C function ranges; never decode interleaved data as code."""
    section=image.section(section_name);result={}
    if not section:raise ValueError('missing linked code section')
    for symbol in image.symbols:
        if symbol.shndx!=section.idx or symbol.type!=elf.STT_FUNC or not symbol.size:continue
        at=symbol.value-base;end=at+symbol.size
        if at<0 or end>len(section.data):raise ValueError('linked function exceeds code image')
        result[symbol.name]={'bytes':symbol.size,'instructions':audit(section.data[at:end],symbol.value)}
    return result

def build(stock,dev,out,meter=False):
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    prefix=os.environ.get('ELEKLOADER_CROSS',dev.toolchain['prefix'])
    objects=[];audits={}
    sources=['audio-machines.s','audio-machines.c','audio-bw-filter.s']+(['audio-meter.s'] if meter else [])
    for name in sources:
        obj=out/(name+'.o');src=HERE/name
        if src.suffix=='.c':sdk.run([prefix+'gcc',*dev.toolchain['cflags'],'-fno-jump-tables','-c',str(src),'-o',str(obj)])
        else:sdk.run([prefix+'as',*dev.toolchain['asflags'],
            *(['--defsym','WITH_METER=1'] if meter else []),'-o',str(obj),str(src)])
        e=elf.Elf.load(obj)
        code=e.section('.text').data
        if name=='audio-meter.s':
            end=next(s.value for s in e.symbols if s.name=='audio_code_end');code=code[:end]
        audits[name]={'bytes':len(code),'instructions':audit(code)}
        if e.section('.text.upper'):
            code=e.section('.text.upper').data
            audits[name+':upper']={'bytes':len(code),'instructions':audit(code)}
        objects.append(str(obj))
    script=out/'audio.ld'
    script.write_text('SECTIONS { .text 0x40040860 : { *(.text) *(.rodata*) *(.data*) '
        'BYTE(0) . = ALIGN(4); *(.bss*) *(COMMON) } '
        '.upper 0x%x : { *(.text.upper) } '
        '/DISCARD/ : { *(.comment) *(.note*) *(.gnu.attributes) } }\n'%UPPER[0])
    exe=out/'audio.elf';raw=out/'audio-code.bin';raw_upper=out/'audio-upper.bin'
    sdk.run([prefix+'ld','-T',str(script),'-e','audio_machine_boot',
        '--defsym=audio_machine_types=0x40040e00','--defsym=audio_machine_updates=0x40040e50',
        '--defsym=audio_machine_renders=0x40040ec0','-o',str(exe),*objects])
    e=elf.Elf.load(exe)
    audits['linked_functions']=audit_functions(e,'.text',CAVE)
    if e.section('.upper'):audits['linked_functions'].update(audit_functions(e,'.upper',UPPER[0]))
    if e.section('.bss') and e.section('.bss').size:raise ValueError('AUDIO runtime cannot use native BSS')
    sdk.run([prefix+'objcopy','-O','binary','-j','.text',str(exe),str(raw)])
    code=raw.read_bytes()
    if CAVE+len(code)>TABLES[0]:raise ValueError('AUDIO services exceed their fixed code reserve')
    upper=b''
    if e.section('.upper'):
        sdk.run([prefix+'objcopy','-O','binary','-j','.upper',str(exe),str(raw_upper)])
        upper=raw_upper.read_bytes()
    if UPPER[0]+len(upper)>UPPER[1]:raise ValueError('AUDIO services exceed the upper code reserve')
    symbols={s.name:s.value for s in e.symbols if s.name}
    original=stock.section(7);image=bytearray(original);patches=[]
    def patch(at,old,new):
        i=at-BASE
        if len(old)!=len(new) or original[i:i+len(old)]!=old:
            raise ValueError('AUDIO preimage differs at %#x'%at)
        image[i:i+len(new)]=new
        patches.append({'address':hex(at),'bytes':len(new),'before_sha256':hashlib.sha256(old).hexdigest(),
                        'after_sha256':hashlib.sha256(new).hexdigest()})
    def jsr(symbol):return b'\x4e\xb9'+struct.pack('>I',symbols[symbol])
    patch(CAVE,bytes(len(code)),code)
    if upper:patch(UPPER[0],bytes(len(upper)),upper)
    def native(at,n):return original[at-BASE:at-BASE+n]
    # Stock 0..45 and backends 0..11 remain byte-identical. Extra backends are
    # silent until the complete, checksummed SMA3 section has passed validation.
    silent=struct.pack('>I',symbols['audio_machine_silent'])*16
    types=native(0x40014950,46)+bytes(18)+bytes(range(12,28))
    for at,data in zip(TABLES,[types,native(0x40014920,48)+silent,native(0x400148f0,48)+silent]):
        patch(at,bytes(len(data)),data)
    scheduler=[(0x400043b2,'40014920',struct.pack('>I',TABLES[1])),
        (0x400043fe,'702d',bytes.fromhex('704f')),(0x40004404,'722d',bytes.fromhex('724f')),
        (0x40004408,'40014950',struct.pack('>I',TABLES[0])),
        (0x40004460,'720b',bytes.fromhex('721b')),(0x400044ac,'400148f0',struct.pack('>I',TABLES[2]))]
    for at,old,new in scheduler:patch(at,bytes.fromhex(old),new)
    patch(0x40001016,bytes.fromhex('4ebafcee4eb940001860'),jsr('audio_machine_boot')+b'\x4e\x71'*2)
    patch(0x40000b04,bytes.fromhex('4ab940040050'),jsr('audio_machine_exit'))
    # Chain end (core 1.8): after AMP, before the track DMA (move.l 4004f6e0,d0).
    patch(CHAIN_END,bytes.fromhex('20394004f6e0'),jsr('audio_chain_end'))
    # The bound is exact only while the stock poles from index 508 on are zero.
    tail=original[BW_TABLE+4*BW_OPEN-BASE:BW_TABLE+4*1024-BASE]
    if any(tail) or not all(original[BW_TABLE+4*i-BASE:BW_TABLE+4*i+4-BASE]!=bytes(4) for i in range(BW_OPEN)):
        raise ValueError('stock base-width pole table is not 508 poles and a zero tail')
    for at in BW_SITES:patch(at,BW_STOCK,jsr('audio_bw_index')+b'\x4e\x71')
    # Fixed ABI directory, separate from scheduler tables; only core services.
    api=struct.pack('>4I',0x454c0001,symbols['audio_machine_params'],32,24)
    patch(0x40040f40,bytes(16),api)
    patch(0x40040f50,bytes(16),struct.pack('>4I',0x454c5001,symbols['audio_pool_sample'],128,0))
    # Chain-end directory, after the render table: {magic, hook, version, 0}.
    # A bank writes its hook into word 1; until then it is the silent rts.
    patch(CHAIN_DIRECTORY,bytes(16),struct.pack('>4I',0x454c5301,symbols['audio_machine_silent'],1,0))
    if meter:
        patch(0x4000094a,bytes.fromhex('4bf9400010e4'),jsr('audio_enter'))
    syx._check_secondary(stock,dev,{7:bytes(image)})
    return bytes(image),{'bytes':len(code),'upper_bytes':len(upper),'symbols':symbols,'patches':patches,'instructions':audits}
