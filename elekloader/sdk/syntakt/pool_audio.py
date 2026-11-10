# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the generic core AUDIO pool service from C; no machine or donor input."""
from pathlib import Path
import os
import struct
import zlib
from .. import build as sdk,elf
from .audio import audit,audit_functions
from ...devices.syntakt import DEVICE

BASE=0x42100750
TABLE=BASE+0xc00
# Core 1.8: one channel-count byte per slot, below the descriptor table, and
# its header record {454c4801, address} at +40 (machines.h).
CHANNELS=BASE+0xb80

def build(out):
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    source=Path(__file__).resolve().parents[3]/'mods/core-st/audio-pool.c'
    prefix=os.environ.get('ELEKLOADER_CROSS',DEVICE.toolchain['prefix'])
    obj=out/'pool.o'
    sdk.run([prefix+'gcc',*DEVICE.toolchain['cflags'],'-fno-jump-tables','-c',str(source),'-o',str(obj)])
    e=elf.Elf.load(obj);audit(e.section('.text').data)
    script=out/'pool.ld'
    script.write_text('SECTIONS { .image 0x%x : { *(.text*) *(.rodata*) *(.data*) '
        'BYTE(0) . = ALIGN(4); *(.bss*) *(COMMON) . = ALIGN(4); } '
        '/DISCARD/ : { *(.comment) *(.note*) *(.gnu.attributes) } }\n'%(BASE+64))
    exe=out/'pool.elf';binary=out/'pool.bin'
    sdk.run([prefix+'ld','-T',str(script),'-e','audio_pool_receive','-o',str(exe),str(obj)])
    sdk.run([prefix+'objcopy','-O','binary','-j','.image',str(exe),str(binary)])
    e=elf.Elf.load(exe);symbols={s.name:s.value for s in e.symbols if s.name}
    linked_audits=audit_functions(e,'.image',BASE+64)
    code=binary.read_bytes()
    if len(code)>CHANNELS-BASE-64:raise ValueError('generic AUDIO pool core exceeds its owned code page')
    image=bytearray(4096)
    struct.pack_into('>8I',image,0,0x454c5031,4096,0,symbols['audio_pool_receive'],
                     0x454c5001,128,BASE+4096,0x4404f750)
    struct.pack_into('>2I',image,32,0x454c4301,symbols['audio_pool_cursor'])
    struct.pack_into('>2I',image,40,0x454c4801,CHANNELS)
    image[CHANNELS-BASE:CHANNELS-BASE+128]=bytes([1])*128
    image[64:64+len(code)]=code
    struct.pack_into('>I',image,8,zlib.adler32(image[12:])&0xffffffff)
    raw=out/'core-pool.bin';raw.write_bytes(image)
    assembly=out/'core-pool.s'
    assembly.write_text('.section .run,"a"\n.balign 16\n.globl core_pool_image\ncore_pool_image:\n'
                        '.incbin "'+raw.resolve().as_posix()+'"\n')
    return assembly,dict(bytes=len(code),resident_bytes=len(image),symbols=symbols,
                        linked_instructions=linked_audits,
                        source=str(source),source_sha256=__import__('hashlib').sha256(source.read_bytes()).hexdigest())
