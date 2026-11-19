"""Linked ColdFire subtype UI, real native Bitmap fill, fixture native UI reads.

No full boot or native selector lifecycle: native view/model/get/edit, selection,
track and record queries are explicit doubles. The bank's module initialization,
key/encoder/idle/draw, state, timing, layout and glyph code execute unmodified.
"""
import argparse,json,struct
from pathlib import Path
from test_st_digimono_destinations_emu import Destination,ENGINE,DATA
from test_st_safe_boot_emu import RET
from unicorn import UC_HOOK_CODE


def verify(folder,out):
    m=Destination(folder);u=m.u;s=m.bank_symbols
    view,model,track_model,pages,event,ctrl,bitmap,pixels,mask,ui=[DATA+0x5000+0x800*i for i in range(10)]
    assert ui+64<DATA+0x10000
    state=dict(track=3,machine=71,selecting=0,held=0,recording=0,value=4<<8,edits=0)
    edit=RET+0x80;u.mem_write(edit,b'\x4e\x75')
    def hook(uc,pc,n,data):
        if pc==0x40039160:
            raw,id,locked=m.args(3);assert raw==view and id==ENGINE+8
            uc.mem_write(locked,b'\0');m.ret(state['value'])
        elif pc==0x40030622:m.ret(state['machine'])
        elif pc==0x4001ff74:m.ret(state['track'])
        elif pc==0x400c3340:m.ret(state['selecting'])
        elif pc==0x400c2dd6:m.ret(state['held'])
        elif pc==0x400c3582:m.ret(state['recording'])
        elif pc==edit:
            raw,id,delta,knob=m.args(4)
            assert raw==view and id==ENGINE+8 and knob==8
            assert not state['held'] and not state['recording'] and not state['selecting']
            state['value']=(state['value']+delta)&0xffff;state['edits']+=1;m.ret(0)
    u.hook_add(UC_HOOK_CODE,hook)
    # Real native parameter-view geometry; the model and track getters are doubles.
    m.w(view,0x401ca37c);m.w(view+0x88,0x14);m.w(view+0x6c,model);m.w(view+0x74,track_model)
    m.w(view+0x7c,pages);m.w(pages,6)
    u.mem_write(ui,bytes(u.mem_read(m.sym['core:core_machine_ui_api'],48)))
    m.w(ui+24,edit);m.w(s['stp_api'],ui)
    u.mem_write(mask,b'\xff'*1024)
    m.invoke(0x400f72be,bitmap,128,64,pixels,mask)
    def call(name,*args):return m.invoke(s[name],*args)
    def key(k,flags):
        u.mem_write(event,struct.pack('>5I',0,0,0,k,flags));return call('stp_key_global',0,event)
    def turn(delta):
        u.mem_write(event,struct.pack('>5I',0,0,0,5,delta&0xffffffff));return call('stp_enc_global',0,event)
    def idle():return call('dm_idle',ui,s['dm_monomachine'],view,0)
    def draw():return call('dm_draw',ui,s['dm_monomachine'],view,bitmap,0)
    def tick():return call('stp_tick',ctrl)
    def clear():u.mem_write(pixels,bytes(1024))
    def pbm(name):
        raw=struct.unpack('>256I',u.mem_read(pixels,1024))
        # Native bitmap y=63 is the top row on the display.
        data=bytearray()
        for y in range(63,-1,-1):
            for x in range(0,128,8):
                byte=0
                for k in range(8):byte|=((raw[2*(x+k)+y//32]>>(31-y%32))&1)<<(7-k)
                data.append(byte)
        (out/(name+'.pbm')).write_bytes(b'P4\n128 64\n'+data)
    out.mkdir(parents=True,exist_ok=True)
    assert m.word(s['dm_ui']+4)==1
    assert m.invoke(0x4007ba2a,ENGINE+8)==m.invoke(0x4007ba2a,0x13),'configuration lost its native scalar editor'
    layout=m.word(s['dm_ui']+8)
    assert all(m.word(layout+4*i)!=8 for i in range(8))
    state['selecting']=1;call('stp_view_draw',view,bitmap,0)
    state['machine']=66;assert not key(12,1);state['machine']=71
    assert key(12,1)==1 and key(12,0)==1
    assert m.word(s['stp_sel'])==1
    clear();call('stp_frame_draw',bitmap,ctrl);pbm('selector-ens')
    selector=bytes(u.mem_read(pixels,1024));assert any(selector)
    assert turn(1)==1 and m.word(s['stp_sel']+8)==5
    assert key(13,1)==1 and key(13,0)==1 and not key(13,1)
    assert state['edits']==0 and state['selecting']
    assert key(12,1)==1 and key(12,0)==1
    for _ in range(4):assert turn(1)==1
    assert m.word(s['stp_sel']+8)==8
    state['held']=1;assert key(12,1)==1 and not state['edits'];state['held']=0
    assert key(12,1)==0 and key(12,0)==0
    state['selecting']=0;state['machine']=66;idle();assert not state['edits']
    state['machine']=71;assert idle() and state['value']==8<<8 and state['edits']==1
    clear();draw();pbm('name-sid-6581');assert any(u.mem_read(pixels,1024))
    for _ in range(30):tick();idle()
    clear();draw();assert not any(u.mem_read(pixels,1024))
    state['track']=4;assert idle();clear();draw();assert any(u.mem_read(pixels,1024))
    seen=set()
    for kind in range(12):
        state['value']=kind<<8;idle();clear();draw();seen.add(bytes(u.mem_read(pixels,1024)))
        pbm(f'name-{kind:02}')
    assert len(seen)==12,'missing glyphs or indistinguishable type boxes'
    state['machine']=66;clear();draw();assert not any(u.mem_read(pixels,1024))
    assert not turn(1) and not key(12,1)
    # Execute the linked MAIN sender, with conflicting saved/effective TYPEs.
    # A legacy LFO/Sound Setup route changes the working projection only.
    for base,size in ((0x80000000,0x10000),(0x10000000,0x10000),(0xfc070000,0x1000)):
        u.mem_map(base,size)
    m.w(0x43bdd824,1)
    u.mem_write(0x80002f50,bytes([71])*8)
    for track in range(8):
        u.mem_write(0x800021cc+track*0x8e+0x36*2,struct.pack('>H',3<<8))
        u.mem_write(0x80006364+track*0x8e+0x36*2,struct.pack('>H',7<<8))
    def snapshot():
        m.invoke(m.sym['core:core_machine_snapshot'],0xff)
        tag=u.mem_read(0x800021a9,1)[0]
        bank=0x10000c00+(tag&1)*512
        assert m.word(bank)==0x4d503333 and m.word(bank+8)==0xff
        for track in range(8):
            row=bank+16+60*track
            assert m.word(row)==(1<<16)|71
            assert m.word(row+4)==(3<<24)
        return tag
    first=snapshot()
    for track in range(8):
        u.mem_write(0x80006364+track*0x8e+0x36*2,struct.pack('>H',11<<8))
    assert snapshot()==first,'legacy modulation changed saved configuration'
    u.mem_write(0x800021cc+0x36*2,struct.pack('>H',4<<8))
    m.invoke(m.sym['core:core_machine_snapshot'],0xff)
    tag=u.mem_read(0x800021a9,1)[0]
    assert tag!=first and m.word(0x10000c00+(tag&1)*512+20)==4<<24
    result=dict(ok=True,types=12,native_bitmap=True,bank_callbacks=True,
                configuration_sender=True,
                scope='Linked bank with native Bitmap fill; explicit UI/model/get/edit doubles, no device/full native selector boot')
    (out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: linked ColdFire TYPE selector, YES/NO/track/record guards, native 128x64 bitmap, 12 distinct subtype boxes and 30-tick expiry')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();verify(a.folder,a.out)
