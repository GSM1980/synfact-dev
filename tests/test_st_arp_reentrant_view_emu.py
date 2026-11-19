# SPDX-License-Identifier: GPL-2.0-or-later
"""Native push -> visibility -> synchronous compose -> actual core_tick.

Includes real base View construction/destruction, manager/key/shared ownership
and LED frame. The compose timer/auto-popup state is idle; the raster/transport
part after core_tick is a boundary. No synchronous compose or tick is mocked.
"""
import argparse
import json
from pathlib import Path

from test_st_arp_retained_view_emu import RetainedMachine, verify as retained_cases
from test_st_arp_native_stack_emu import EX, verify as stack_cases, mute_first_cases
from test_st_arp_view_emu import APP, PARAM
from unicorn.m68k_const import UC_M68K_REG_PC


class ReentrantMachine(RetainedMachine):
    def __init__(self,core,bank):
        self.compose=False;self.compositions=[]
        super().__init__(core,bank)
        # Native controller idle state: no timed auto-popup. Keep the real
        # compose prologue, actual core_tick call, result test and epilogue.
        self.w(APP+0xe4,2);self.w(APP+0xe0,1)
        self.w(EX+0x640,self.sym['arp_view_tick']);self.w(EX+0x644,0)
        self.w(self.cs['core:events'],EX+0x640)
        self.w(self.cs['core:core_tick_table']+2,EX+0x640)
        self.compose=True

    def code(self,u,pc,n,ctx):
        if self.native and self.compose:
            if pc==0x4000a0e6:
                view=self.word(self.sym['arp_view'])
                self.compositions.append(dict(index=self.index(),page=self.word(self.sym['arp_page']),
                                              view=view,attached=bool(view and self.word(view+0x2c))))
                return  # Execute real synchronous composition, not the old stub.
            if pc==0x4000a22c:
                u.reg_write(UC_M68K_REG_PC,0x4000a2a0);return  # LCD work after tick.
            if pc in (0x400ffd6a,0x400ffcb8):
                return  # Actual View construction and transparency setter.
            if pc==0x400fffc0:
                self.disposals+=1;return  # Actual base destructor, then native free.
        return super().code(u,pc,n,ctx)


def normal_cases(core,bank):
    cases=[]
    for track,poly in ((0,False),(0,True),(8,False),(9,True),(11,True)):
        m=ReentrantMachine(core,bank);m.track=track;first=2+int(poly)
        m.pages([1 if track>=8 else 2,3]+([3] if poly else [])+[3,3])
        m.w(PARAM+0x90,0)
        for _ in range(first):m.pulse()
        assert m.index()==first and m.word(m.sym['arp_view']) and m.closed==0
        assert 'MODE' in m.texts() and len(m.led_frame())==16
        opening=[c for c in m.compositions if c['view'] and not c['attached']]
        assert opening and all(c['index']==first and c['page']==0 for c in opening),opening
        m.pulse();assert 'NOTE' in m.texts() and not m.led_frame()
        m.pulse();assert m.index()==0 and not m.word(m.sym['arp_view'])
        for _ in range(first):m.pulse()
        assert 'MODE' in m.texts() and len(m.led_frame())==16
        cases.append(dict(track=track+1,poly=poly,cold_open_chord_wrap_reopen=True))
        for page in (0,1):
            q=ReentrantMachine(core,bank);q.track=track
            q.pages([1 if track>=8 else 2,3]+([3] if poly else [])+[3,3])
            q.w(PARAM+0x90,first+page);q.key(0x12,1,0);q.call('arp_view_tick',APP)
            assert q.index()==first+page and q.closed==0
            assert ('MODE' if not page else 'NOTE') in q.texts()
            assert all(c['index']==first+page and c['page']==page
                       for c in q.compositions if c['view'] and not c['attached'])
    return cases


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('core','bank','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--before',type=Path)
    a=p.parse_args()
    result=dict(ok=True,normal=normal_cases(a.core,a.bank),
                stack=stack_cases(a.core,a.bank,ReentrantMachine),
                mute_first=mute_first_cases(a.core,a.bank,ReentrantMachine),
                retained=retained_cases(a.core,a.bank,ReentrantMachine),device_access=False)
    if a.before:
        m=ReentrantMachine(a.core,a.before);m.w(PARAM+0x90,1)
        assert m.key(0x13,1,0)==1
        assert m.index()==1 and not m.word(m.sym['arp_view']) and m.closed==1
        assert not m.texts() and not m.led_frame()
        assert any(c['index']==1 and c['view'] and not c['attached'] for c in m.compositions)
        result['previous_normal_open_blank_reproduced']=True
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
