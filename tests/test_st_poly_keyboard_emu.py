"""Keyboard/MIDI Sound fallback regression on stock and delivered MAIN.

Executes the real 40119684 event constructor up to the completed event, compiled
POLY immediate routing, and 400a45ca's native Sound selection/publication through
400a31a2/400a30c4. Stop before smoothing/voice electronics. The before-bank must
exhibit the wrong slave Sound; the after-bank must bind the master's Sound.
No device access and no claim to emulate the analog electronics.
"""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader import formats
from test_st_poly_cost_emu import Runtime,KIT,EVENT,WORK,STACK,UC_M68K_REG_A2,UC_M68K_REG_A3,UC_M68K_REG_D2

def scenario(bank,native,master,voices,origin,layout=None):
    r=Runtime(bank,native);r.topology([(master,voices)])
    if layout:
        lo,hi=layout['ddr'];at=layout['run_load']-0x40000400
        r.u.mem_map(lo,0x10000);r.u.mem_write(lo,native[at:at+hi-lo])
    r.sym.update(live_create=0x40119684,live_created=0x40119728,
                 event_sound=0x400a45ca,published=0x400976b4,same_sound=0x400a466c)
    for t in range(8,11):
        sound=KIT+0x2a+t*0x123
        r.u.mem_write(sound,bytes(0x123));r.u.mem_write(sound+0xa2,bytes([t]))
    r.w(0x800030b8,KIT);r.w(0x43bd7424,0);r.w(0x43bd73bc,0)
    expected=KIT+0x2a+master*0x123;observations=[]
    def event(note,on):
        r.u.mem_write(WORK,struct.pack('>15I',master,note,100,on,*([0]*11)))
        r.execute('live_create',[WORK],stops=['live_created'],preserve=False)
        node=r.u.reg_read(UC_M68K_REG_A3);raw=r.read(node,80)
        words=struct.unpack('>20I',raw)
        assert words[2]==master and words[3]==2 and words[6]==note and words[10]==0
        r.u.mem_write(EVENT,raw)
        # Real caller return-PC, examined by the production stack census.
        r.w(STACK+0x100,origin if on==1 else (0x4002d602 if origin==0x4002ed0a else 0x401052c4))
        r.execute('poly_preview_locked',[EVENT])
        return struct.unpack('>20I',r.read(EVENT,80))
    # Repeated same note rotates voices; ascending notes cover held chords.
    for notes in [[60]*(voices*3),list(range(60,60+voices))]:
        for i,note in enumerate(notes):
            e=event(note,1);target=e[2]
            assert master<=target<master+voices and e[6]==note
            r.w(0x800030c0+target*4,0) # force native publication, not a cached shortcut
            r.execute('event_sound',stops=['published','same_sound'],
                      regs={UC_M68K_REG_D2:target,UC_M68K_REG_A2:EVENT},preserve=False)
            chosen=struct.unpack('>I',r.read(0x800030c0+target*4,4))[0]
            typ=r.read(0x80002f50+target,1)[0]
            observations.append(dict(track=target+1,note=note,sound=chosen,type=typ,
                                     correct=chosen==expected and typ==master))
            if len(notes)>voices:event(note,2)
        if len(notes)<=voices:
            for note in notes:event(note,2)
    # Native source selection must retain each saved kit machine identity.
    assert [r.read(KIT+0x2a+t*0x123+0xa2,1)[0] for t in range(8,11)]==[8,9,10]
    return observations

def verify(before,after,stock,core,out):
    fw,_,_=formats.load(stock)
    assert fw.sha256=='8e2488f462c4a5656396a895f113bcd415e9900fa8709340dccf45d4cb9ed19e'
    results=[]
    for label,native in [('stock',fw.section(3)),('core',core.read_bytes())]:
        layout=json.loads((core.parent/'verification.json').read_text())['link'] if label=='core' else None
        for master,voices in [(8,2),(8,3),(9,2)]:
            for origin in [0x4002ed0a,0x40105408]:
                old=scenario(before,native,master,voices,origin,layout)
                new=scenario(after,native,master,voices,origin,layout)
                assert any(not x['correct'] for x in old),'negative control did not reproduce wrong Sound'
                assert all(x['correct'] for x in new),('wrong master Sound',label,master,voices,new)
                results.append(dict(main=label,master=master+1,voices=voices,origin=hex(origin),
                                    before_wrong=sum(not x['correct'] for x in old),after_wrong=0,notes=len(new)))
    out.write_text(json.dumps({'ok':True,'cases':results,'physical_audio_pending':True},indent=2)+'\n')
    print('PASS:',len(results),'stock/core keyboard/MIDI native-constructor + routing + publication cases; old bank fails each case; fixed bank always uses master Sound')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['before','after','stock','core','out']:p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();verify(a.before,a.after,a.stock,a.core,a.out)
