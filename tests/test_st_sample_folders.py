"""Portable folder catalogue / migration tests; no device or MIDI IO."""
import copy,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader.sdk.syntakt.sample_catalog import Index,enable_folders,MAX_DEPTH
from elekloader.sdk.syntakt.long_samples import problems

def sample(ident=1,name='Kick',folder=0):
    return dict(id=ident,first=0x601+ident,blocks=1,bytes=68,frames=2,crc=123,created=1,flags=7,name=name,folder=folder)

class Store:
    def __init__(self,index,fail=0):self.copies=[copy.deepcopy(index),copy.deepcopy(index)];self.writes=0;self.fail=fail
    def current(self):
        rows=[Index.unpack(c.pack()) for c in self.copies]
        return max(rows,key=lambda c:c.seq),rows
    def commit(self,index,copies):
        self.writes+=1
        if self.fail==self.writes:raise IOError('Interrupted publication')
        seqs=[c.seq for c in self.copies];index.seq=max(seqs)+1
        self.copies[seqs.index(min(seqs))]=Index.unpack(index.pack())

class Folders(unittest.TestCase):
    def test_v1_and_v2_roundtrip(self):
        old=Index(1,0x680,3,[sample(),sample(2,'Snare')]);raw=old.pack()
        self.assertEqual(raw[4:8],bytes([0,0,0,1]));self.assertEqual(Index.unpack(raw).pack(),raw)
        idx=Index(2,0x680,3,[sample(),sample(2,'Snare')],version=2)
        drum=idx.mkdir('Drums');acoustic=idx.mkdir('Acoustic',drum)
        idx.move_samples([1],acoustic);before=dict(idx.entries[0]);idx.change_folder(drum,name='Percussion')
        self.assertEqual(idx.sample_path(idx.entries[0]),'Percussion/Acoustic/Kick')
        self.assertEqual(idx.entries[0],before)
        out=Index.unpack(idx.pack());self.assertEqual(out.pack(),idx.pack());self.assertFalse(problems(out))
    def test_names_and_empty_folders(self):
        idx=Index(1,0x680,3,[sample(),sample(2)],version=2)
        d=idx.mkdir('Drums');idx.move_samples([2],d);self.assertFalse(problems(idx))
        with self.assertRaises(ValueError):idx.move_samples([2],0)
        with self.assertRaises(ValueError):idx.rmdir(d)
        idx.move_samples([2],idx.mkdir('Other'));idx.rmdir(d)
        for name in ('','..','.','a/b','a\\b','a\0b','é'*17):
            with self.assertRaises(ValueError):idx.mkdir(name)
        self.assertEqual(idx.mkdir('é'*16),idx.next_folder-1)
    def test_cycles_and_depth(self):
        idx=Index(1,0x680,version=2);root=idx.mkdir('A');child=idx.mkdir('B',root)
        with self.assertRaises(ValueError):idx.change_folder(root,parent=child)
        self.assertEqual(idx.folder(root)['parent'],0)
        for i in range(MAX_DEPTH-2):child=idx.mkdir(str(i),child)
        with self.assertRaises(ValueError):idx.mkdir('too deep',child)
        idx.entries=[sample(folder=900)]
        self.assertTrue(problems(idx))
    def test_crc_and_capacity(self):
        idx=Index(1,0x680,version=2);idx.mkdir('One');raw=bytearray(idx.pack());raw[-1]^=1
        self.assertIsNone(Index.unpack(raw))
        idx.next_folder=32767
        with self.assertRaises(ValueError):idx.mkdir('limit')
    def test_migration_backup_interrupt_resume(self):
        old=Index(1,0x680,2,[sample()]);old.entries[0].pop('folder');original=old.pack()
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)
            for fail in (0,1,2):
                store=Store(old,fail);backup=path/str(fail)
                if fail:
                    with self.assertRaises(IOError):enable_folders(store,backup,firmware_confirmed=True)
                    self.assertEqual((backup/'catalogue-0.hzfs').read_bytes(),original)
                    store.fail=0;enable_folders(store,path/(str(fail)+'-resume'),firmware_confirmed=True)
                else:enable_folders(store,backup,firmware_confirmed=True)
                for c in store.copies:
                    self.assertEqual(c.version,2)
                    self.assertEqual({k:v for k,v in c.entries[0].items() if k!='folder'},old.entries[0])
                count=store.writes;enable_folders(store,path/str(fail),firmware_confirmed=True)
                self.assertEqual(store.writes,count)
            with self.assertRaises(ValueError):enable_folders(Store(old),path/'no')

def native_probe(cc,out):
    import subprocess,struct,zlib
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    root=Path(__file__).resolve().parents[1]
    size=0x80000+130;body=bytearray(i&255 for i in range(size));body[:64]=bytes(64);body[:4]=b'SAMP';struct.pack_into('>I',body,8,(size-64)//2)
    e=dict(id=1,first=0x602,blocks=2,bytes=size,frames=(size-64)//2,crc=zlib.crc32(body,0xffffffff)&0xffffffff,created=1,flags=6,name='long',folder=2)  # flags without a meaning (bit 0 is stereo since core 1.8)
    idx=Index(20,0xe90,3,[e,{**e,'id':2,'first':0x604,'folder':0}],version=2,folders=[dict(id=1,parent=0,name='Drums'),dict(id=2,parent=1,name='Acoustic')],next_folder=3)
    golden=out/'folders-golden.hzfs';result=out/'folders-c-result.hzfs';golden.write_bytes(idx.pack())
    exe=out/'folders-native.exe'
    subprocess.run([cc,'-O2','-Wl,--large-address-aware','-I',str(root/'elekloader/sdk/include'),str(root/'tests/test_st_sample_folders_native.c'),str(root/'mods/core-st/high-storage.c'),str(root/'mods/core-st/crc.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(golden),str(result)],check=True)
    got=Index.unpack(result.read_bytes());assert got and not problems(got)
    assert got.folders==idx.folders and [e['id'] for e in got.entries]==[2,3]
    assert all(e['folder']==0 for e in got.entries)
    print('PASS: independent Python reader accepts C-mutated v2 catalogue with identical folders')

if __name__=='__main__':
    if '--cc' in sys.argv:
        import argparse
        p=argparse.ArgumentParser();p.add_argument('--cc',required=True);p.add_argument('--out',required=True)
        args=p.parse_args();native_probe(args.cc,args.out)
    else:unittest.main()
