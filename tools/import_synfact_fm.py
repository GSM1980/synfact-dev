"""One-time local source import from an existing Synfact build (no emulation).

The SMZ builder never calls this tool. Imported private assembly/data remain
ignored by Git. No SYX or binary file is an input to subsequent SMZ builds.
"""
import argparse,hashlib,importlib.util,json,math,re,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader.sdk import elf
from elekloader.isa import coldfire as cf


def main():
    p=argparse.ArgumentParser();p.add_argument('--synfact',type=Path,required=True)
    p.add_argument('--as',dest='assembler',required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[1];folder=root/'mods/fm-tone-st/imported'
    folder.mkdir(parents=True,exist_ok=True)
    work=a.synfact/'build/smz-1/work';out=work/'current/out'
    inputs={}
    def read(path):
        data=path.read_bytes();inputs[str(path)]=hashlib.sha256(data).hexdigest();return data
    report=json.loads(read(out/'safe-machines.json'));raw=read(out/'inner-zone.smz4')
    words=struct.unpack_from('>7I',raw,4);section=raw[words[5]:words[5]+words[6]]
    fm=report['audio'][0];base=fm['base'];audio_base=0x4204f750
    blob=section[base-audio_base:base-audio_base+fm['size']]
    assert len(blob)==fm['size']
    glue_source=read(out/'fm-indexed.s').decode()
    # Assemble the original symbolic adapter into a relocatable object. Its
    # relocation records identify the original external symbol operands exactly.
    temp=root/'out/fm-source-import';temp.mkdir(exist_ok=True)
    source=temp/'original.s';source.write_text(glue_source)
    obj=temp/'original.o'
    subprocess.run([a.assembler,'-mcpu=54415','-o',str(obj),str(source)],check=True)
    e=elf.Elf.load(obj);text=e.section('.text');gbase=fm['fm_update']
    external={}
    for offset,kind,symbol,addend in e.relocs.get(text.idx,[]):
        if symbol.shndx!=elf.SHN_UNDEF:continue
        assert kind in (elf.R_68K_32,elf.R_68K_16,elf.R_68K_8),(symbol.name,kind)
        size={elf.R_68K_32:4,elf.R_68K_16:2,elf.R_68K_8:1}[kind]
        value=int.from_bytes(blob[gbase-base+offset:gbase-base+offset+size],'big')-addend
        assert symbol.name not in external or external[symbol.name]==value,symbol.name
        external[symbol.name]=value
    symbols={s.name:gbase+s.value for s in e.symbols if s.shndx==text.idx and s.name}
    kernel_listing=read(out/'fm-indexed-kernels.lst').decode()
    rows=[]
    for line in kernel_listing.splitlines():
        m=re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{4} ?)+)\s+\t',line)
        if m:rows.append((int(m[1],16),bytes.fromhex(m[2])))
    kbase=rows[0][0];kend=rows[-1][0]+len(rows[-1][1])
    cbase=(kend+15)&~15;cbase+=(0x5c0-cbase)&0x7ff
    wbase=(cbase+0xba8+15)&~15;wbase+=(0x230-wbase)&0x7ff
    assert wbase+26728<=gbase
    # Last core routine is the source-built descriptor initializer, before the
    # explicit 512-byte guard preceding the optimized kernels.
    marker=bytes.fromhex('701522d8538066fa4e75')
    at=blob.find(marker,0,kbase-base);assert at>=0
    cend=base+at+len(marker)
    regions=[(base,cend,'fm_core'),(kbase,kend,'fm_kernels'),
             (cbase,cbase+0xba8,'fm_constants'),(wbase,wbase+26728,'fm_waves'),
             (gbase,gbase+text.size,'fm_adapter_old'),(fm['workspace'],base+len(blob),'fm_state')]
    def relocated(value):
        for lo,hi,name in regions:
            if lo<=value<hi:
                if name=='fm_adapter_old':
                    candidates=[(v,k) for k,v in symbols.items() if v<=value]
                    v,k=max(candidates);return f'{k}+0x{value-v:x}'
                return f'{name}+0x{value-lo:x}'
        return None
    def emit_code(lo,hi,name):
        # Preserve the tuned cache-set phase after linking alongside other
        # machines. The object alignment propagates through the generic linker.
        data=blob[lo-base:hi-base];lines=['.text','.balignw 2048,0x4e71',
            f'.rept {(lo&0x7ff)//2}', '.word 0x4e71', '.endr',f'.globl {name}',f'{name}:']
        for pc,ins in cf.disassemble_lengths(data,lo,lo,hi):
            n=ins.length;code=data[pc-lo:pc-lo+n]
            assert len(code)==n,(hex(pc),ins)
            if ins.flags&cf.ILLEGAL and code==b'\0\0':
                assert data[pc-lo-2:pc-lo]==b'\x4e\x75'
                lines.append('| Unreachable source alignment after RTS, assembled as NOP.')
                lines.append('.word 0x4e71');continue
            lines.append(f'| imported {pc:08x}: {ins.op}; {code.hex()}')
            # Branch displacement and PC-relative operand retain their source
            # instruction size but point at symbolic assembled dependencies.
            patches={}
            if ins.flow in (cf.F_BRA,cf.F_BSR,cf.F_BCC):
                target=relocated(ins.target);assert target,(hex(pc),hex(ins.target))
                if n==2:
                    lines.append(f'.byte 0x{code[0]:02x},({target})-(.+1)');continue
                patches[2]=(n-2,f'({target})-(.)')
            elif ins.flags&cf.PCREL:
                assert n>=4
                target=pc+2+struct.unpack_from('>h',code,2)[0]
                expression=relocated(target);assert expression,(hex(pc),hex(target))
                patches[2]=(2,f'({expression})-(.)')
            for i in range(2,n-3,2):
                if any(j<=i<j+size for j,(size,_) in patches.items()):continue
                v=struct.unpack_from('>I',code,i)[0];expression=relocated(v)
                if expression:patches[i]=(4,expression)
            i=0
            while i<n:
                if i in patches:
                    size,value=patches[i];lines.append(('.long ' if size==4 else '.word ')+value);i+=size
                else:lines.append('.word 0x'+code[i:i+2].hex());i+=2
        lines+= [f'.type {name},@function',f'.size {name},.-{name}']
        return '\n'.join(lines)+'\n'
    (folder/'dsp.s').write_text(emit_code(base,cend,'fm_core')+emit_code(kbase,kend,'fm_kernels'))
    lines=[]
    for lo,hi,name in regions[2:4]+regions[5:]:
        lines+=['.section '+('.data' if name=='fm_state' else '.rodata')+',"a'+('w' if name=='fm_state' else '')+'"',
                '.balign 2048',f'.space {lo&0x7ff}',f'.globl {name}',f'{name}:']
        data=blob[lo-base:hi-base];assert len(data)%4==0
        for i in range(0,len(data),4):
            v=struct.unpack_from('>I',data,i)[0];lines.append('.long '+(relocated(v) or hex(v)))
    for name in ('fm_ext','FM_OFFSETS'):
        value=external[name];lines += [f'.globl {name}',f'.set {name},{relocated(value)}']
    (folder/'tables.s').write_text('\n'.join(lines)+'\n')
    # Build a symbol map for the authored adapter; no hardcoded relocation base.
    mapped=[]
    for name,value in external.items():
        expression=relocated(value) or hex(value)
        if name=='DSP_DESCRIPTORS':expression=f'fm_core+0x{cend-base-22:x}'
        mapped+= [f'.globl {name}',f'.set {name},{expression}']
    (folder/'bindings.s').write_text('\n'.join(mapped)+'\n')
    (folder/'adapter-original.s').write_text(glue_source)
    # UI constants are imported ONCE from the local reference sections, not
    # extracted by the SMZ build. Exact Synfact drawing rules remain C source.
    main=read(work/'analysis/digitone-1.44-sections/section_3_MAIN_OS.bin')
    audio=read(work/'analysis/digitone-1.44-sections/section_7_blob.raw')
    original=0x40000400
    array=lambda v:'{'+','.join(map(str,v))+'}'
    nested=lambda v:'{'+','.join(array(x) for x in v)+'}'
    widths=list(main[0x402319dc-original:0x402319dc-original+128]);glyphs=[]
    for c,w in enumerate(widths):
        off=struct.unpack_from('>H',main,0x40231adc-original+2*c)[0];assert w<=5
        glyphs.append(list(main[0x40231cdc-original+off:0x40231cdc-original+off+w])+[0]*(5-w))
    spec=importlib.util.spec_from_file_location('fm_icons',a.synfact/'src/current/fm_algorithm_icons.py')
    icons=importlib.util.module_from_spec(spec);spec.loader.exec_module(icons)
    inputs[str(a.synfact/'src/current/fm_algorithm_icons.py')]=hashlib.sha256((a.synfact/'src/current/fm_algorithm_icons.py').read_bytes()).hexdigest()
    waves=[]
    for runtime in (0x80001200,*struct.unpack_from('>26I',audio,0x40003c34-original)):
        at=0x400166e0+runtime-0x80000000 if runtime<0x80008000 else 0x40017cf0+runtime-0x80008000
        samples=struct.unpack_from('>256i',audio,at-original)
        waves.append([max(1,min(15,8+samples[(x*16)%256]*6//(1<<31))) for x in range(17)])
    defs=dict(FM_ALGORITHM_WORDS=50,FM_ALGORITHM_WIDTH=25,FM_ALGORITHM_HEIGHT=51,
        FM_ALGORITHM_PIXELS=nested([icons.words(q) for q in icons.compact(main,original,(widths,glyphs))]),
        FM_KNOB_POINTER=nested([[round(6*math.cos(math.radians(225-270*k/32))),round(6*math.sin(math.radians(225-270*k/32)))] for k in range(33)]),
        FM_DIAMOND_PIXELS=nested([struct.unpack_from('>19I',main,0x40273954+76*i-original) for i in range(32)]),
        FM_RATIO_FRAME_C=array(struct.unpack_from('>19I',main,0x40274320-original)),
        FM_RATIO_FRAME_B=array(struct.unpack_from('>19I',main,0x402743b8-original)),
        FM_HARM_WAVES=nested(waves),FM_FONT_WIDTHS=array(widths),FM_FONT_GLYPHS=nested(glyphs))
    def strings(lo,hi,table,count):
        result=[]
        for dest in range(table,table+4*count,4):
            suffix=bytes.fromhex('4879')+struct.pack('>I',dest)+bytes.fromhex('4e92')
            positions=[i for i in range(lo-original,hi-original) if main[i:i+8]==suffix]
            assert len(positions)==1
            at=positions[0]-6
            if main[at:at+6].hex()=='45f94018afc8':at-=6
            assert main[at:at+2]==bytes.fromhex('4879')
            off=struct.unpack_from('>I',main,at+2)[0]-original
            result.append(main[off:main.index(b'\0',off)].decode())
        return '{'+','.join(json.dumps(s) for s in result)+'}'
    defs['FM_RATIO_C']=strings(0x400145da,0x4001476e,0x4052b048,19)
    defs['FM_RATIO_A']=strings(0x400142f4,0x400145ae,0x4052b094,36)
    (folder/'ui-data.h').write_text('/* Private imported Synfact/Digitone UI data; see provenance.json. */\n'+'\n'.join(f'#define {k} {v}' for k,v in defs.items())+'\n')
    controls=[]
    for i in range(27):controls.append(struct.unpack_from('>5I',main,0x4018ed68+i*60-original))
    meta={'inputs':inputs,'source_build_requires_digitone_firmware':False,'import_uses_emulator':False,
          'regions':regions,'external':external,'symbols':symbols,'controls':controls,
          'fm_blob_sha256':hashlib.sha256(blob).hexdigest(),'fm_source_build':report['sha256']}
    (folder/'provenance.json').write_text(json.dumps(meta,indent=2)+'\n')
    print('IMPORTED',folder,'regions',[(x[2],x[1]-x[0]) for x in regions])


if __name__=='__main__':main()
