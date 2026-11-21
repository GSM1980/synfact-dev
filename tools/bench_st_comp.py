"""Count comp-st at the actual FX CV hook, including shared dispatch.

ColdFire guest instructions per 32 stereo frames, not cycles or CPU percent.
Requires a core 2.4 build and a bank containing comp-st. No device access.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from test_st_comp_emu import fixture,set_values,block,W
from unicorn import UC_HOOK_CODE


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('core','bank','out'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--sram',action='store_true',help='run the checked SRAM placement used at module init')
    a=p.parse_args();rows={}
    for signal in ('silence','sine','noise','full_scale'):
        for mode in ('off','wet','parallel','moving','scf'):
            m=fixture(a.core,a.bank)
            if a.sram:assert m.call('comp_place')==3
            m.w(0x444e2334,W+0x1c00)  # live UI dirty target, including meter publications
            m.w(W+0x1d00,m.sym['comp_audio']);m.w(W+0x1d04,0)
            m.w(m.cs['core:core_fx_cv_table']+2,W+0x1d00)
            counter=[0]
            def count(u,pc,size,user):counter[0]+=1
            hook=m.u.hook_add(UC_HOOK_CODE,count)
            measured=[];cold=0
            for n in range(64):
                values=[0x4000,0x1800,0x2000,0x2000,0x300,
                        {'off':0,'wet':0x7f00,'parallel':0x4000,'moving':0x1000+n*256,'scf':0x7f00}[mode],
                        0x2a00 if mode=='scf' else 0]   # SCF 150 Hz in 'scf'
                if mode=='moving':values[:3]=[0x1000+n*256,n*256,(n*3%128)*256]
                set_values(m,values)
                rng=random.Random(600+n)
                if signal=='silence':left=right=[0]*32
                elif signal=='full_scale':left=[-2**31]*32;right=[2**31-1]*32
                elif signal=='noise':
                    left=[rng.randrange(-2**30,2**30) for _ in range(32)]
                    right=[rng.randrange(-2**30,2**30) for _ in range(32)]
                else:
                    left=[int(2**29*math.sin((n*32+i)*.13)) for i in range(32)]
                    right=[int(2**28*math.sin((n*32+i)*.21)) for i in range(32)]
                counter[0]=0;block(m,left,right,through_core=True)
                if not n:cold=counter[0]
                if n>=16:measured.append(counter[0])
            m.w(W+0x1d00,0);counter[0]=0;block(m,[0]*32,[0]*32,through_core=True)
            empty=counter[0];m.u.hook_del(hook)
            # Count the exact displaced native window without the shared hook.
            m.u.mem_write(0x400905b6,bytes.fromhex('0c810000ffff'))
            m.u.ctl_remove_cache(0x400905b6,0x400905da)
            hook=m.u.hook_add(UC_HOOK_CODE,count);counter[0]=0
            block(m,[0]*32,[0]*32,through_core=True);native=counter[0]
            m.u.hook_del(hook)
            row=dict(minimum=min(measured)-native,maximum=max(measured)-native,
                     mean=sum(measured)/len(measured)-native,first=cold-native,
                     total_hook_window_minimum=min(measured),stock_window=native,
                     empty_core_overhead=empty-native)
            rows[signal+'/'+mode]=row
            print(signal,mode,row['minimum'],row['maximum'],'first',row['first'],flush=True)
    sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    a.out.write_text(json.dumps(dict(units='extra guest instructions per 32 stereo frames versus stock FX CV window; not cycles',
        includes='shared core dispatch, settings lookup, conversion, detector, compact kernel, meter, MIX and original VCA clamp/store',
        excludes='UI setters, cache stalls, real hardware and other mods',sram=a.sram,core_sha256=sha(a.core),bank_sha256=sha(a.bank),
        measured_blocks=16*48,cases=rows),indent=2)+'\n')
    print('PASS; no device access')

if __name__=='__main__':main()
