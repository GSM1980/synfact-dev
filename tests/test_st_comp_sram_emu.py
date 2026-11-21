"""COMP checked SRAM copies, refusal/fallback, neighbours and LFO init order.

Actual linked module code and core; native heap/mutex and other initializers
are explicit boundaries in the shared patcher fixture. No hardware timing.
"""
import argparse
from pathlib import Path
from test_st_comp_emu import fixture,set_values,block
from test_st_patchers_emu import Patchers

CODE,STATE,END=0x80007680,0x80007774,0x800077c0
CODE_BYTES,STATE_BYTES=STATE-CODE,END-STATE   # 244; 68 + 8 guard bytes


def verify(core,bank):
    # Occupied code/state are independent refusals and must not be changed.
    for code_busy,state_busy in ((True,True),(True,False),(False,True),(False,False)):
        m=fixture(core,bank)
        m.u.mem_write(CODE-16,b'\xa5'*16);m.u.mem_write(END,b'\x5a'*16)
        if code_busy:m.u.mem_write(CODE,b'\xc3'*CODE_BYTES)
        if state_busy:m.u.mem_write(STATE,b'\x3c'*STATE_BYTES)
        assert m.call('comp_place')==(0 if code_busy else 2)|(0 if state_busy else 1)
        assert m.read(CODE-16,16)==b'\xa5'*16 and m.read(END,16)==b'\x5a'*16
        if code_busy:assert m.read(CODE,CODE_BYTES)==b'\xc3'*CODE_BYTES
        if state_busy:assert m.read(STATE,STATE_BYTES)==b'\x3c'*STATE_BYTES
        assert m.word(m.sym['comp_hot'])==(m.sym['comp_ddr_runtime'] if state_busy else STATE)
    ddr,sram=fixture(core,bank),fixture(core,bank);assert sram.call('comp_place')==3
    assert sram.sym['comp_detector_end']-sram.sym['comp_detector_start']==CODE_BYTES
    for n in range(320):
        if n%11==0:
            values=[(n*331)%0x7f01,(n*113)%0x7f01,(n*73)%0x7f01,(n*31)%0x7f01,(n%8)<<8,(n*179)%0x7f01,(n//11%8)<<8]
            set_values(ddr,values);set_values(sram,values)
        left=[((-1)**i)*(2**29+i*991) for i in range(32)];right=list(reversed(left))
        assert block(ddr,left,right)==block(sram,left,right)
        a=ddr.word(ddr.sym['comp_hot']);b=sram.word(sram.sym['comp_hot'])
        assert ddr.read(a,68)==sram.read(b,68)
        if n==100:
            # Guard damage preserves the current history when switching data.
            sram.w(STATE+68,0);sram.call('comp_check_state')
            assert sram.word(sram.sym['comp_hot'])==sram.sym['comp_ddr_runtime']
        if n==200:
            # Corrupt an executable byte, run the task checker BEFORE audio.
            sram.u.mem_write(CODE,b'\0')
            for _ in range(60):sram.call('comp_check_state')
            for name in ('peak','hp','curve','scale'):
                assert sram.word(sram.sym['comp_'+name+'_entry'])==sram.sym['comp_'+name]
    # Run the real LFO3 copy installer on both sides of COMP placement. Its
    # first window must exclude COMP even when COMP is initialized first.
    for before in (True,False):
        m=Patchers(core.parent,bank)
        def call(name,*args):
            m.sym['core:comp_test_entry']=m.bs[name];return m.call('comp_test_entry',*args)
        call('comp_params_init')
        if before:assert call('comp_place')==3
        m.run('full')
        assert m.word(m.lfo['in_sram'])==1 and m.word(m.lfo['hooks_installed'])==1
        n=m.bs['lfo3_render_end']-m.bs['lfo3_render']
        assert n==822 and 0x80007340+n<=CODE
        lfo=bytes(m.u.mem_read(0x80007340,n))
        if not before:assert call('comp_place')==3
        assert call('chorus_place')==7,'Chorus must keep all its SRAM copies'
        assert bytes(m.u.mem_read(0x80007340,n))==lfo
        assert m.word(m.bs['comp_hot'])==STATE
        assert call('comp_place')==3,'repeat placement is idempotent'
    print('PASS COMP SRAM: 244-byte code + 68-byte state + guards (SCF loop and state included), four occupied-window cases, 320 exact blocks, data/code fallback, neighbours and both real LFO3 initialization orders; Chorus keeps all SRAM')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core',type=Path,required=True);p.add_argument('--bank',type=Path,required=True)
    a=p.parse_args();verify(a.core,a.bank)
