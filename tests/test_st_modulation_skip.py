"""modulation-skip-st: host logic test and, with banks, their composition.

--bank (repeatable) with --reference (the normal bank from the same sources):
each bank holds the reference's modules plus modulation_skip_module (and
irq_timer_module last if present), the same machines and identical AUDIO.
--core <main.bin>: the patched site 40097b04 is the stock bytes there too.
"""
import argparse,hashlib,json,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader import syx

SITES=((0x40097b04,bytes.fromhex('246f0040260a')),(0x400a30c4,bytes.fromhex('4feffff448d70c04')))


def audio(bank):
    b=bank.read_bytes();at,n=struct.unpack_from('>II',b,72);return b[48+at:48+at+n]


def check_bank(bank,reference):
    report=json.loads(Path(str(bank)+'.json').read_text())
    ref=json.loads(Path(str(reference)+'.json').read_text())
    tail=['modulation_skip_module']+(['irq_timer_module'] if 'irq_timer_module' in report['modules'] else [])
    assert report['modules']==ref['modules']+tail,(report['modules'],ref['modules'])
    assert report['machines']==ref['machines'] and not report['firmware_required']
    for path,digest in report['source_sha256'].items():
        assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest,path
    assert audio(bank)==audio(reference)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',type=Path,required=True)
    p.add_argument('--bank',type=Path,action='append',default=[]);p.add_argument('--reference',type=Path)
    p.add_argument('--stock',type=Path);p.add_argument('--core',type=Path)
    p.add_argument('--work',type=Path,default=Path('out/modulation-skip-tests'))
    a=p.parse_args()
    a.work.mkdir(parents=True,exist_ok=True)
    exe=a.work/'modulation-skip.exe'
    subprocess.run([str(a.cc),'-O2','-Wall','-Werror','-I','elekloader/sdk/include',
                    'tests/test_st_modulation_skip.c','-o',str(exe)],check=True)
    subprocess.run([str(exe.resolve())],check=True)
    images=([syx.Syx(a.stock.read_bytes()).section(3)] if a.stock else [])+([a.core.read_bytes()] if a.core else [])
    for image in images:
        for site,stock in SITES:
            assert image[site-0x40000400:site-0x40000400+len(stock)]==stock,hex(site)
    for bank in a.bank:
        check_bank(bank,a.reference)
    if a.bank: print('PASS: %d banks: reference modules plus modulation_skip_module, same machines, AUDIO identical'%len(a.bank))
    if a.stock or a.core: print('PASS: sites 40097b04 and 400a30c4 hold the stock bytes in the given MAIN images')


if __name__=='__main__':
    main()
