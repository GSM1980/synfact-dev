# SPDX-License-Identifier: AGPL-3.0-or-later
"""Pinned De-Pattern FM+ interpreter and OS reader, without its unrelated MD build.

Load complete upstream functions by AST, verifying every source hash first.
Only MD's multiplier/saturation helpers and ROM's NRV decoder are needed here;
its module-level construction of the unrelated MD tables is intentionally absent.
"""
import ast
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import types

HERE=Path(__file__).resolve().parent


def load(os_file):
    manifest=json.loads((HERE/'provenance.json').read_text())
    for name,digest in manifest['sha256'].items():
        assert hashlib.sha256((HERE/'upstream'/name).read_bytes()).hexdigest()==digest,name
    prefix='_dm_depattern'
    for name in (prefix,prefix+'.mnm',prefix+'.md',prefix+'.perc'):
        m=types.ModuleType(name);m.__path__=[];sys.modules[name]=m
    def module(name,file,names=None,values=None):
        full=prefix+'.'+name;m=types.ModuleType(full);m.__file__=str(HERE/'upstream'/file)
        m.__package__=full.rsplit('.',1)[0];sys.modules[full]=m
        if values:m.__dict__.update(values)
        source=(HERE/'upstream'/file).read_text()
        if names is None:exec(compile(source,m.__file__,'exec'),m.__dict__)
        else:
            tree=ast.parse(source)
            tree.body=[n for n in tree.body if isinstance(n,(ast.FunctionDef,ast.ClassDef)) and n.name in names]
            exec(compile(tree,m.__file__,'exec'),m.__dict__)
        return m
    dsl=module('perc.dsl','dsl.py')
    decoder=module('md.rom','md_rom.py',{'nrv2b','RomError'})
    import functools,os,pathlib,struct
    rom=module('mnm.rom','mnm_rom.py',
        {'RomError','path','available','flash','blocks','_w','ext','words','s24','fm_sine','fm_words'},
        dict(functools=functools,os=os,pathlib=pathlib,struct=struct,hashlib=hashlib,
             nrv2b=decoder.nrv2b,DEFAULT=Path(os_file or 'unused.syx'),CHAIN=0x4000,FLASH=1<<20,
             SHA256='fe13c09f661cdba5fe57813240fa1574de1ee0a3ddd7047fc39d0318cba1a710',
             FM_SINE_CODE=0x100077,FM_FLIST=(0x141a80,24),FM_TONE=(0x144ac7,128),FM_ENVT=(0x141880,129)))
    # Pin the caller's explicit file, independent of shell environment defaults.
    rom.path=lambda:Path(os_file or 'unused.syx')
    K=module('md.core','md_core.py',{'sat','mpy_floor','mpyf_any','w','s','ss'},
             dict(TOP=1-2**-23,Q23=2**-23,OUT0=195,S0=64,W0=0,Prog=dsl.Prog))
    C=module('mnm.core','mnm_core.py')
    fm=module('mnm.fm','fm.py')
    exact=module('mnm.exact','exact.py',None)
    return rom,dsl,K,C,fm,exact


class Voice:
    """Actual upstream DSL, unmodified FM init/trig/half and float32 semantics."""
    def __init__(self,os_file,kind):
        from types import SimpleNamespace
        rom,dsl,K,C,fm,X=load(os_file)
        self.dsl,self.K,self.C,self.X=dsl,K,C,X
        self.M=(fm.Stat,fm.Par,fm.Dyn)[kind-9]
        self.os_file=os_file
        md=SimpleNamespace(state=0x100000,C={'mdb':0x80000})
        self.L=C.Layout(SimpleNamespace(md=md),0x200000,0x400000)
        values={}
        if os_file:
            words=rom.fm_words()
        else:
            import re
            text=(HERE/'tables.h').read_text()
            assert hashlib.sha256(text.encode()).hexdigest()==json.loads((HERE/'tables.json').read_text())['tables_sha256']
            def array(name):
                return [int(x) for x in re.search(r'dmf_'+name+r'\[\d+\]=\{(.*?)\};',text,re.S)[1].replace('\n','').split(',') if x.strip()]
            self.increments=array('inc')
            words=array('sine')+array('ratios')+array('tone')+[-v for v in array('envt')]
        at=0
        for name,n in C.FM_TABLES:
            for i in range(n):values[self.L.FM[name]+4*i]=dsl.bits(words[at+i]*C.Q23)
            at+=n
        self.mem=dsl.Mem(lambda a:values.get(a,0));self.first=True

    def block(self,pitch,knobs,trig=False):
        dsl,K,C=self.dsl,self.K,self.C
        p=dsl.Prog('run',mem=self.mem);p.i[3]=self.L.md.state
        self.mem.put(self.L.md.C['mdb'],self.L.md.state)
        for k,x in enumerate(knobs):p.fconst(0,x/32768);p.sti(C.P0+k,3,0)
        # Same native pitch quantization as import_tables.py.
        if self.os_file:
            F=min(0x5800,max(0,pitch*8//3+341));A=self.X.increment(F)
            increment=self.M.exact(A,F,knobs)['i1']
        else:increment=self.increments[min(140*64,max(0,pitch))]
        p.iconst(0,increment);C.vws(p,'i1',0)
        if self.first:self.M.init(p,self.L);self.first=False
        if trig:self.M.trig(p,self.L)
        out=self.L.md.state+4*K.OUT0;p.iconst(0,out);p.seti(4,0)
        self.M.half(p,self.L)
        return [dsl.flt(self.mem.get(out+4*k)) for k in range(16)]
