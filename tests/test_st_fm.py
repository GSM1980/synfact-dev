"""FM source/relocation/cache/UI regressions, without CPU emulation or hardware.

python tests/test_st_fm.py --bank out/.../synfact-machines.smz4 --synfact "$SYNFACT_SRC" --cc "$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe"
The reference build is a test input only; normal SMZ compilation needs no donor.
"""
import argparse,ast,hashlib,json,re,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader.sdk import elf
from elekloader.isa import coldfire as cf
from elekloader.sdk.syntakt.smz import source_inputs
from st_bank_modules import check_bank_modules

ROOT=Path(__file__).resolve().parents[1]

def verify(bank,synfact,cc):
    folder=ROOT/'mods/fm-tone-st';meta=json.loads((folder/'imported/provenance.json').read_text())
    report=json.loads(Path(str(bank)+'.json').read_text());work=bank.parent/(bank.stem+'.work')
    ae=elf.Elf.load(work/'audio.elf');section=ae.section('.image')
    shoff=struct.unpack_from('>I',ae.raw,0x20)[0];shsize=struct.unpack_from('>H',ae.raw,0x2e)[0]
    section_base=struct.unpack_from('>I',ae.raw,shoff+section.idx*shsize+12)[0]
    syms={s.name:s.value for s in ae.symbols if s.name}
    assert report['machines']==[66,67,68,69,65] and report['features']==15   # bit 8: the bank's mod-info directory (core 1.1)
    check_bank_modules(report,bank.read_bytes(),(
        ['fm_tone_module'],['fm_tone_module','recorder_module'],
        ['fm_tone_module','recorder_module','poly_module','system_info_module'],
        ['fm_tone_module','recorder_module','lfo3_module','poly_module','system_info_module'],
        ['fm_tone_module','recorder_module','lfo3_module','arp_module','poly_module','system_info_module']))
    assert not report['firmware_required']
    assert all(flag in report['cflags'] for flag in ('-O3','-mcpu=54415','-mtune=cfv4'))
    for path,digest in report['source_sha256'].items():
        assert Path(path).is_relative_to(ROOT) and hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest
    for name in ('imported/ui-data.h','bindings.s','imported/dsp.text.bin','imported/tables.rodata.bin','imported/tables.data.bin'):
        assert str((folder/name).resolve()) in report['source_sha256'],name
    # A change in either nested include must be covered by the build's source pin.
    assert str((folder/'events.h').resolve()) in report['source_sha256']
    raw=(synfact/'build/smz-1/work/current/out/inner-zone.smz4').read_bytes()
    old_report=json.loads((synfact/'build/smz-1/work/current/out/safe-machines.json').read_text())
    fm=old_report['audio'][0];words=struct.unpack_from('>7I',raw,4)
    old_audio=raw[words[5]:words[5]+words[6]]
    old=old_audio[fm['base']-0x4204f750:fm['base']-0x4204f750+fm['size']]
    assert hashlib.sha256(old).hexdigest()==meta['fm_blob_sha256']
    regions=meta['regions'];count=0
    def mapped(value):
        for lo,hi,name in regions:
            if lo<=value<hi:
                if name=='fm_adapter_old':
                    at,symbol=max((v,k) for k,v in meta['symbols'].items() if v<=value)
                    return syms[symbol]+value-at
                return syms[name]+value-lo
        return None
    def linked(address,size):
        at=address-section_base;assert 0<=at<=len(section.data)-size
        return section.data[at:at+size]
    for lo,hi,name in regions:
        if name=='fm_adapter_old':continue
        base=syms[name];assert (base&0x7ff)==(lo&0x7ff),(name,hex(base),hex(lo))
        expected=bytearray(old[lo-fm['base']:hi-fm['base']])
        if name in ('fm_core','fm_kernels'):
            for pc,ins in cf.disassemble_lengths(bytes(expected),lo,lo,hi):
                offset=pc-lo;n=ins.length;code=bytes(expected[offset:offset+n]);patches=[]
                if ins.flags&cf.ILLEGAL:
                    assert code==b'\0\0' and expected[offset-2:offset]==b'\x4e\x75'
                    expected[offset:offset+2]=b'\x4e\x71';continue
                if ins.flow in (cf.F_BRA,cf.F_BSR,cf.F_BCC):
                    target=mapped(ins.target);assert target is not None
                    pos,size=(1,1) if n==2 else (2,n-2)
                    delta=target-(base+offset+2)
                    expected[offset+pos:offset+pos+size]=(delta&((1<<(8*size))-1)).to_bytes(size,'big')
                    patches.append((pos,size))
                elif ins.flags&cf.PCREL:
                    target=mapped(pc+2+struct.unpack_from('>h',code,2)[0]);assert target is not None
                    delta=target-(base+offset+2)
                    expected[offset+2:offset+4]=(delta&0xffff).to_bytes(2,'big');patches.append((2,2))
                for i in range(2,n-3,2):
                    if any(at<=i<at+size for at,size in patches):continue
                    target=mapped(struct.unpack_from('>I',code,i)[0])
                    if target is not None:struct.pack_into('>I',expected,offset+i,target);patches.append((i,4))
                count+=1
        else:
            for i in range(0,len(expected),4):
                target=mapped(struct.unpack_from('>I',expected,i)[0])
                if target is not None:struct.pack_into('>I',expected,i,target)
        actual=linked(base,len(expected))
        assert actual==expected,(name,next((hex(i) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b),'length'))
    # Pin the ECO default/latches, 16 bake slots, no-context-copy adapter and
    # the optimized bake assembly, except the intentional feedback guard.
    eco=mapped(meta['external']['FM_ECO']);assert linked(eco,16)==bytes(16)
    authored=(folder/'audio.s').read_text();original=(folder/'imported/adapter-original.s').read_text()
    bake=lambda s:s[s.index('fm_bake_mod:'):s.index('        .balignw 4,0x4e71',s.index('fm_bake_mod:'))].replace('.globl fm_bake_car\n','')
    old_guard='''        | Algorithms 6/7 with feedback form a chaotic loop that amplifies the
        | 1-3 LSB rounding difference (t5_bake_error.py): keep the morph kernel.
        lea     fm_params,%a0
        movea.l 0(%a0,%d2.l*4),%a0
        move.w  0x40(%a0),%d0               | FDBK
        beq.s   fm_bake_feedback_ok
        moveq   #0,%d1
        move.w  0x34(%a0),%d1               | ALGO, Q8
        cmpi.l  #0x600,%d1
        bcc.w   fm_bake_fallback
fm_bake_feedback_ok:
'''
    new_guard='''        | Feedback can amplify table-bake rounding on every algorithm.
        | Keep the original morph kernel whenever FDBK is nonzero; bake
        | remains available for steady HARM without feedback.
        lea     fm_params,%a0
        movea.l 0(%a0,%d2.l*4),%a0
        move.w  0x40(%a0),%d0               | FDBK
        bne.w   fm_bake_fallback
'''
    assert bake(original).count(old_guard)==1
    assert bake(authored)==bake(original).replace(old_guard,new_guard)
    assert 'move.l 40(%sp),-(%sp)' in authored and 'FM_RECORD' not in authored
    assert 'Native per-voice state; no context or parameter swaps.' in authored
    print('PASS:',count,'imported instructions, every table/state word, symbolic relocation, five tuned cache phases, ECO defaults and optimized bake source')

    output=ROOT/'out/fm-tests';output.mkdir(exist_ok=True)
    # Independent defaults/ranges oracle: read authored Synfact declarations
    # without importing legacy builders, plus its SHA-pinned descriptor input.
    donor=synfact/'src/machines/fm/legacy'
    tree=ast.parse((donor/'machine_fm_tone.py').read_text())
    base=next(n.value for n in tree.body if isinstance(n,ast.Assign) and
              any(isinstance(t,ast.Name) and t.id=='FM_TONE' for t in n.targets))
    params=next(k.value for k in base.keywords if k.arg=='params')
    contract=[tuple(ast.literal_eval(x) for x in p.args[2:5]) for p in params.elts]
    tree=ast.parse((donor/'machine_fm_tone_audio.py').read_text())
    ext=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and
             any(isinstance(t,ast.Name) and t.id=='EXTENDED' for t in n.targets))
    descriptors=(synfact/'build/smz-1/work/analysis/digitone-1.44-sections/section_3_MAIN_OS.bin').read_bytes()
    assert hashlib.sha256(descriptors).hexdigest()=='fce648a97c6c5d93b961732e8f8db6b02e0820c6d344c7e2131fa05a4b3168e4'
    for _,_,index,slot in ext:
        owner,local,lo,hi,initial=struct.unpack_from('>5I',descriptors,0x4018ed68-0x40000400+60*index)
        assert owner==0 and local==slot
        contract.append((lo,hi,initial))
    contract += [(0,0x1200,0x300)]+[(0,0x7ffe,0x4000)]*4
    assert len(contract)==28
    (output/'reference-controls.h').write_text('/* Independent Synfact control oracle. */\n'+''.join(
        'static const unsigned short reference_'+name+'[28]={'+','.join(hex(p[i]) for p in contract)+'};\n'
        for i,name in enumerate(('min','max','initial'))))
    source=(synfact/'src/current/fm_digitone_ui.c').read_text();production=(folder/'ui.c').read_text()
    # Compile the donor's actual ratio algorithm as an independent oracle.
    # Only native call boundaries/IDs are replaced with explicit host doubles.
    ratio=source[source.index('int fm_ui_ratio_encoder('):source.index('/* . PHRT and ECO')]
    ratio=ratio.replace('fm_ui_ratio_encoder','reference_ratio_encoder').replace('fm_ui_supported','reference_supported')
    ratio=ratio.replace('ratio_dispatch_depth','reference_depth').replace('fm_ui_dispatch_delta','reference_dispatch')
    for address,signature,name in [
        ('4007b9b2','u32 (*)(u32)','FM_POLICY'),('400f66a2','s32 (*)(void *,u32,u32)','FM_EVENT_DELTA'),
        ('400f66c0','u32 (*)(void *)','FM_EVENT_PRESSED'),('400f68d2','s32 (*)(void *,u32,s32,void *,u32)','FM_INCREMENT'),
        ('400c2d2a','u32 (*)(void *)','FM_TRIG_HELD'),('40102d7c','u32 (*)(u32)','FM_KEY_HELD')]:
        call='(('+signature+')0x'+address+'u)';assert call in ratio;ratio=ratio.replace(call,name)
    assert not re.search(r'\)0x40[0-9a-f]+u\)',ratio)
    (output/'reference-ratio.h').write_text('static u32 reference_depth;\n'
        'static int reference_supported(u8 *v){return *(u32 *)(v+0x88)==0x14 && TYPE(v)==65;}\n'+ratio)
    start=source.index('void fm_ui_text(');end=source.index('/* Enter the unchanged native encoder dispatch')
    drawing=source[start:end]
    # The production drawing is optimized (one native GET per control and
    # frame, set pixels only inside the area it cleared): its contract is
    # the donor's pixels, compared frame by frame below, not its source.
    native='((void (*)(void *,void *,u32,u32,u32))0x4003a1c2u)(view,ctx,4,page,7);'
    assert native in drawing and 'FM_PAGE_INDICATOR(view,ctx,4,page,7);' in production
    pre=source[:source.index('u32 fm_ui_action_begin(')]
    for name in ('GET','TYPE','PIX','FILL','RECT','TEXT','TOGGLE'):
        pre=re.sub(r'^(#define '+name+r' .*)$',r'#ifndef '+name+'\n'+r'\1'+'\n#endif',pre,flags=re.M)
    pre=pre.replace('#define DATA __attribute__((section(".data")))','#define DATA')
    defines=production[production.index('#define FM_PAGE_IDS'):production.index('/* Digitone SYN')]
    (output/'reference-ui.c').write_text('#include "../../mods/fm-tone-st/imported/ui-data.h"\n'+defines+pre+drawing.replace(native,'FM_PAGE_INDICATOR(view,ctx,4,page,7);'))
    calls={}
    for label,flags in (('ported',[]),('reference',['-DFM_REFERENCE'])):
        exe=output/(label+'.exe');frames=output/(label+'.frames')
        subprocess.run([str(cc),'-O2','-I',str(ROOT/'elekloader/sdk/include'),*flags,str(ROOT/'tests/test_st_fm.c'),'-o',str(exe)],check=True)
        run=subprocess.run([str(exe),str(frames)],check=True,capture_output=True,text=True)
        print(''.join(l+'\n' for l in run.stdout.splitlines() if not l.startswith('CALLS ')),end='')
        calls[label]={int(m[0]):(int(m[1]),int(m[2])) for m in
                      re.findall(r'^CALLS (\d) GET (\d+) PIX (\d+)$',run.stdout,re.M)}
        assert sorted(calls[label])==[0,1,2,3]
    assert (output/'ported.frames').read_bytes()==(output/'reference.frames').read_bytes()
    print('PASS: all 256 frames byte-identical to Synfact drawing C over a patterned screen; header/rail retained; paired locks and all 361 ratio combinations')
    # One native GET per control the page shows: ALGO, the grid and, on the
    # envelope page, the delays and trig modes. 64 frames per page.
    for page,gets in enumerate((8,5,13,8)):
        (pg,pp),(rg,rp)=calls['ported'][page],calls['reference'][page]
        assert pg==64*gets and pp<rp,(page,pg,pp,rg,rp)
        print(f'PASS: page {page+1} per frame: native GET {rg/64:.0f} -> {pg/64:.0f}, PIX calls {rp/64:.0f} -> {pp/64:.0f}')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bank',type=Path,required=True)
    p.add_argument('--synfact',type=Path,required=True);p.add_argument('--cc',type=Path,required=True)
    a=p.parse_args();verify(a.bank.resolve(),a.synfact.resolve(),a.cc.resolve())
