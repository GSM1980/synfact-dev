"""Check the shared event routes in a real linked SYX; no CPU emulation."""
import json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader import syx
from elekloader.sdk.syntakt.audio import audit
from elekloader.isa import coldfire as cf

def verify(folder):
    folder=Path(folder);r=json.loads((folder/'verification.json').read_text())
    image=(folder/'main.bin').read_bytes();sym=r['symbols'];layout=r['link']
    def address(name):return sym['core:'+name]
    def read(at,n):
        if layout['ddr'][0]<=at<layout['ddr'][1]:at=layout['run_load']+at-layout['ddr'][0]
        return image[at-0x40000400:at-0x40000400+n]
    def jump(name,op=0x4ef9):return struct.pack('>HI',op,address(name))
    def body(start,end):
        at=address(start);b=read(at,address(end)-at);assert audit(b,at)>0
        return b,list(cf.disassemble_lengths(b,at,at,at+len(b)))
    def calls(ins,target):return [(pc,i) for pc,i in ins if i.flow in (cf.F_BSR,cf.F_JSR) and i.target==address(target)]
    # The parallel encoder patch is gone; the native editor calls the one
    # shared metadata entry, with its original policy call and return PC.
    assert read(0x4003b96c,6)==bytes.fromhex('4eb94007b9b2')
    assert read(0x4007b9b2,6)==jump('core_machine_cached_parameter')
    for name in ('core_machine_encoder_policy','core_machine_custom_encoder','core_machine_custom_key','profiles','st_pool_ui_idle'):
        assert 'core:'+name not in sym
    b,ins=body('core_machine_cached_parameter','core_stock_cached_parameter')
    assert len(calls(ins,'core_event_view'))==0 # metadata no longer dispatches input
    assert bytes.fromhex('72100c804003b972') in b and bytes.fromhex('72172f01') in b
    assert read(address('core_stock_cached_parameter'),16)==bytes.fromhex('222f00040c81000001f94ef94007b9bc')
    b,ins=body('core_machine_page_nav_37d9e','core_stock_page_nav_37d9e')
    assert len(calls(ins,'core_event_view'))==0 # base resolver is metadata only
    b,ins=body('core_machine_editor_resolve','core_machine_page_op_385dc')
    assert read(0x4003b8c6,6)==jump('core_machine_editor_resolve',0x4eb9)
    assert b.startswith(bytes.fromhex('206800bc2f2f00082f2f00084e90508f')) # actual virtual resolver, unchanged args
    assert len(calls(ins,'core_event_view'))==0
    assert len(calls(ins,'core_machine_editor_event'))==1
    assert bytes.fromhex('2f002f002f042f0a') in b
    assert bytes.fromhex('4cee3cfcffb84e5e70014e75') in b
    # SourcePageView bypasses the base resolver. Its two native layout queries
    # now receive the selected view's SAME layout service, not page one.
    b,ins=body('core_machine_source_layout','core_stock_source_layout')
    assert bytes.fromhex('0c8040044cc6') in b and bytes.fromhex('0c8040044ce6') in b
    assert len(calls(ins,'core_machine_view_layout'))==1
    assert read(0x40044cc4,4)==bytes.fromhex('4e934fef') and read(0x40044ce4,4)==bytes.fromhex('4e934fef')
    assert read(0x400c3294,2) # existing firmware selector predicate, no patch
    assert address('fw_parameter_selecting')==0x400c3294
    # One collection each. Global dispatch skips its view-context entries;
    # global handlers keep their old signatures and do not run a second time.
    for name in ('core_draw_table','core_enc_table','core_key_table'):
        at=address(name);raw=read(at,24)
        if name=='core_draw_table':assert raw[6:8]==bytes.fromhex('201a') and raw[10]==0x6b
    dispatch=address('dispatch');assert read(dispatch,6)[4]==0x6b
    def table(name):
        at=struct.unpack('>I',read(address(name)+2,4))[0];out=[]
        while (word:=struct.unpack('>I',read(at,4))[0]):out.append(word);at+=4
        return out
    assert table('core_draw_table')==[address('core_machine_draw')|0x80000000]
    assert not any(k.startswith('system-info-st:') for k in sym) # UI moved to SMZ
    assert address('core_machine_tick') in table('core_tick_table')
    assert 'core:core_pool_tick' not in sym  # optional SMZ owns UI ticks
    assert address('st_pool_preview_mix') in table('core_main_mix_table')
    assert table('core_enc_table')==[] # FM joins this very table once at boot
    # Drawing remains at the native view layer, and key notifications remain
    # after native handling. Both feed their EXISTING event collections.
    for start,end,event,native in (
        ('core_machine_page_op_3a244','core_stock_page_op_3a244',1,'core_stock_page_op_3a244'),
        ('core_machine_page_op_3bc74','core_stock_page_op_3bc74',2,'core_stock_page_op_3bc74')):
        b,ins=body(start,end);route=calls(ins,'core_event_view');original=calls(ins,native)
        if event==1:assert not route and len(original)==1 # complete-view emission replaces base emission
        else:assert len(route)==2 and len(original)==1 and route[0][0]<original[0][0]<route[1][0]
        if event==2:assert struct.pack('>HH',0x4878,event) in b
    b=read(address('core_view_draw'),64)
    assert read(0x40100830,6)==jump('core_view_draw')
    assert struct.pack('>HI',0x4eb9,address('core_event_view')) in b
    assert bytes.fromhex('2f0c4e9526534ef940100836') in b
    manifest=json.loads(Path('mods/core-st/mod.json').read_text())
    # Extend the EXISTING slot lookup only inside stock's machine-default loop.
    b,ins=body('core_machine_slot_to_engine','core_stock_slot_to_engine')
    assert bytes.fromhex('202f000c0c8040024e04') in b
    assert bytes.fromhex('202f00100c800000001a') in b
    assert bytes.fromhex('2f052f0a2f2f001c') in b
    assert len(calls(ins,'core_machine_defaults'))==1
    # 0.12.18: the lock preview's selection walk, inside 40039160 after its
    # trig test (LFO3 owns the entry); back to the native tail 4003921e.
    assert read(0x40039160,6)==bytes.fromhex('4fefffd870ff')
    assert read(0x400391a8,8)==jump('core_preview_selected')+bytes.fromhex('4e71')
    b,ins=body('core_preview_selected','core_preview_selected_end')
    assert b.endswith(bytes.fromhex('4ef94003921e')) and not calls(ins,'core_preview_selected')
    assert not any(i.flow==cf.F_JSR and i.target in (0x40103788,0x40103794) for pc,i in ins)
    assert read(0x400a423e,6)==jump('core_native_note_accept')
    # 1.2: the mixer's FX-send events and the FX record codec
    assert read(0x4008f3a0,8)==jump('core_track_send')+bytes.fromhex('4e71')
    assert read(0x4008f828,6)==jump('core_delay_in',0x4eb9)
    assert read(0x4008f952,6)==jump('core_reverb_in',0x4eb9)
    b,ins=body('core_track_send','core_delay_in')
    assert b.startswith(bytes.fromhex('ae000900ae810900')) and b.count(bytes.fromhex('4ef94008f3a8'))==2
    assert read(0x400a6d96,8)==jump('core_fx_encode')+bytes.fromhex('4e71')
    assert read(0x400a6d2e,8)==jump('core_fx_decode')+bytes.fromhex('4e71')
    # 1.4 (randomize, list item 22): parameter list, SYN view randomize, Kit owner, Kit setter
    for at,name in ((0x400146bc,'core_machine_parameter_list'),(0x40044972,'core_machine_syn_randomize'),
                    (0x400a61fa,'core_machine_kit_owner'),(0x40013516,'core_native_kit_set')):
        assert read(at,6)==jump(name),name
    assert len(manifest['sites'])==106 and len(manifest['collections'])==16   # core_mod_info: 1.0; mixer events: 1.2
    assert len({s['addr'] for s in manifest['sites']})==106
    # Safe boot (1.7.1): every eMMC accessor table the core links (machine-boot.c
    # `storage`, files.c `io`) has read, write and sectors, each a code address.
    # Core 1.7's boot table had a null write: the first trial boot jumped to 0.
    for name in ('storage','io'):
        words=struct.unpack('>3I',read(address(name),12))
        assert all(0x40000400<=w<0x40400000 or layout['ddr'][0]<=w<layout['ddr'][1] for w in words),(name,[hex(w) for w in words])
        assert words[0]==address('fw_mmc_read') and words[1]==address('fw_mmc_write'),(name,[hex(w) for w in words])
    assert '-O3' in r['cflags'] and r['resident_ram']['free']>=0
    actual=syx.Syx(next(folder.glob('*.syx')).read_bytes());assert actual.section(3)==image
    print('PASS: shared input/draw/preview, note acceptance, mixer events, FX codec, randomize; eMMC accessor tables complete; 106 sites, 16 collections; SYX and -O3')

if __name__=='__main__':verify(sys.argv[1])
