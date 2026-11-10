# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline protocol/storage tests, never MIDI and never an instruction emulator."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import wave
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader.sdk.syntakt import data_rpc as R,long_samples as H


class Device:
    def __init__(self):
        self.sectors=0x3a4000+0x123
        self.windows={};self.jobs={};self.next_job=1;self.calls=[];self.writes=[]
        self.fail_close=False;self.fail_read=False
    def size(self,k):return max(0,min(R.WINDOW,self.sectors*512-k*R.WINDOW))
    def read(self,k):return self.windows.get(k,bytes(R.WINDOW))[:self.size(k)]
    def call(self,kind,payload=b'',timeout=None):
        self.calls.append(kind)
        def ok(body=b''):return bytes((0,0,0,0,kind|128,1))+body
        def fail():return bytes((0,0,0,0,kind|128,0))+b'test failure\0'
        if kind in (0x54,0x57):
            at=0 if kind==0x54 else 4;path=payload[at:payload.index(0,at)].decode()
            n=1 if path in ('/mods','/mods-stage','/mod-status') else R.sample_slot(path)
            k=(0x49 if path=='/mod-status' else 0x48 if path=='/mods' else 0x4a if path=='/mods-stage' else
               (n+7 if n<=64 else n-R.RAW_FIRST))
            job=self.next_job;self.next_job+=1
            if kind==0x54:
                if self.fail_read:return fail()
                block=R.u32(payload,payload.index(0)+1)
                obj=R.object_pack(n,self.read(k)[:64] if path=='/mod-status' else self.read(k))
                self.jobs[job]=dict(obj=obj,block=block,pos=0)
                return ok(R.be32(job)+R.be32(block)+b'\0')
            self.jobs[job]=dict(k=k,n=n,total=R.u32(payload,0),obj=bytearray(),parts=[])
            return ok(R.be32(job))
        job=R.u32(payload,0);j=self.jobs[job]
        if kind==0x55:
            seq=R.u32(payload,4)
            # First partial may be empty while the native stream starts.
            chunk=b'' if seq==0 else j['obj'][j['pos']:j['pos']+j['block']]
            j['pos']+=len(chunk)
            return ok(R.be32(job)+R.be32(seq)+R.be32(j['pos'])+bytes((int(j['pos']==len(j['obj'])),))+
                      R.be32(0)+R.be32(len(chunk))+chunk)
        if kind==0x56:
            self.jobs.pop(job)
            # OS 1.41: closing a read before its object is complete fails.
            if j['pos']<len(j['obj']):return bytes((0,0,0,0,kind|128,0))+b'Reader did not complete\0'
            return ok(R.be32(job)+R.be32(j['pos']))
        if kind==0x58:
            seq,check,length=R.u32(payload,4),R.u32(payload,8),R.u32(payload,12)
            chunk=payload[16:];assert len(chunk)==length and check==R.crc(chunk)
            assert seq==len(j['parts']);j['obj'].extend(chunk);j['parts'].append(length)
            return ok(R.be32(job)+R.be32(seq)+R.be32(len(j['obj'])))
        if kind==0x59:
            self.jobs.pop(job)
            if self.fail_close:return fail()
            assert j['parts'][-1]==12 and len(j['obj'])==j['total']==R.u32(payload,4)
            body=R.object_unpack(bytes(j['obj']),j['n']);k=j['k']
            assert k*R.WINDOW+len(body)<=self.sectors*512
            for off in range(0,len(body),R.WINDOW):
                key=k+off//R.WINDOW;data=body[off:off+R.WINDOW]
                old=bytearray(self.read(key));old[:len(data)]=data;self.windows[key]=bytes(old)
            self.writes.append((k,len(body)))
            return ok(R.be32(job)+R.be32(j['total']))
        raise AssertionError(f'unexpected request {kind:#x}')


class Storage(unittest.TestCase):
    def setUp(self):
        self.dev=Device();self.client=R.DataClient(self.dev,block=8192)
        self.store=H.Store(self.client,log=lambda *_:None)
    def test_status_transport_read_only(self):
        body=R.be32(0x454c5331)+bytes(60);self.dev.windows[0x49]=body
        self.assertEqual(self.client.read('/mod-status'),body)
        calls=list(self.dev.calls)
        with self.assertRaises(PermissionError):self.client.write('/mod-status',body)
        self.assertEqual(self.dev.calls,calls)
    def test_stock_slot64_crop_and_crc(self):
        pcm=b'\x12\x34'*(48000*9)
        body,_=H.samp_body(pcm,'long input')
        sentinel=b'SMZ4'+bytes(R.WINDOW-4);self.dev.windows[0x48]=sentinel
        sent=self.client.write('/samples/64',body)
        self.assertEqual(len(sent),480064);self.assertEqual(R.u32(sent,8),240000)
        self.assertEqual(sent[64:],pcm[:480000]);self.assertEqual(self.dev.windows[0x48],sentinel)
        self.assertEqual(self.dev.writes,[(0x47,480064)])
        # Exact boundary and shorter bodies retain every byte.
        self.assertEqual(R.crop_stock(sent),sent)
        small,_=H.samp_body(b'\0\0'*37,'small');self.assertEqual(R.crop_stock(small),small)
    def test_write_preflight_no_request(self):
        for path,body,capacity in [('/samples/65',b'x',None),('/samples/9721',b'x',None),
                                  ('/samples/9721',b'x',0x30000000),('/mods',bytes(16*1024*1024+1),None),
                                  ('/samples/64',b'SAMP',None),('/samples/064',b'',None),('/projects/1',b'',None),
                                  ('/mods-stage',b'',None),('/mods-stage',bytes(R.STAGE_CAP+1),None),
                                  ('/mods-stages',b'x',None)]:
            before=list(self.dev.calls)
            with self.assertRaises((ValueError,PermissionError)):self.client.write(path,body,card_bytes=capacity)
            self.assertEqual(self.dev.calls,before)
    def test_mods_stage(self):
        # The trial slot `mods put` writes (core 1.7): the whole body, read back.
        body=bytes(range(256))*20
        self.client.write('/mods-stage',body);self.assertEqual(self.dev.writes,[(0x4a,len(body))])
        self.assertEqual(self.client.read('/mods-stage')[:len(body)],body)
        self.assertEqual(R.checked_body('/mods-stage',bytes(R.STAGE_CAP))[0],1)
    def test_index_crc_matches_core(self):
        packed=H.Index(seq=1,windows=0xe90).pack()
        self.assertEqual(R.u32(packed,32),0x1b940a04)
        self.assertIsNotNone(H.Index.unpack(packed))
        self.assertIsNone(H.Index.unpack(b'HZFS'))
    def test_catalogue_and_long_sample(self):
        with tempfile.TemporaryDirectory() as tmp:
            wav=Path(tmp)/'long.wav';back=Path(tmp)/'back.wav'
            with wave.open(str(wav),'wb') as f:
                f.setparams((1,2,48000,0,'NONE','not compressed'));f.writeframes(b'\x34\x12'*(48000*6))
            e=H.cmd_put(self.store,wav)
            self.assertEqual(e['frames'],288000);self.assertEqual(e['blocks'],2)
            H.cmd_get(self.store,'long',back)
            self.assertEqual(wav.read_bytes(),back.read_bytes())
            self.assertEqual(H.cmd_fsck(self.store,True),[])
            H.cmd_rm(self.store,'long');self.assertEqual(self.store.current()[0].entries,[])
            second=H.cmd_put(self.store,wav,name='second')
            self.assertEqual(second['first'],H.DATA_FIRST_K+2)
            self.assertTrue(all(k>=0x600 for k,_ in self.dev.writes))
            self.assertTrue(set(self.dev.calls)<=set(range(0x54,0x5a)))
    def test_stereo_long_sample(self):
        # Core 1.8: a two-channel WAV is stored interleaved with flag STEREO;
        # frames still counts samples, so every size check is unchanged.
        with tempfile.TemporaryDirectory() as tmp:
            wav=Path(tmp)/'wide.wav';dual=Path(tmp)/'dual.wav';back=Path(tmp)/'back.wav'
            frames=b''.join(struct.pack('<hh',i%30000,-(i%20000)) for i in range(48000))
            with wave.open(str(wav),'wb') as f:
                f.setparams((2,2,48000,0,'NONE','not compressed'));f.writeframes(frames)
            with wave.open(str(dual),'wb') as f:
                f.setparams((2,2,48000,0,'NONE','not compressed'))
                f.writeframes(b''.join(struct.pack('<hh',i%30000,i%30000) for i in range(48000)))
            e=H.cmd_put(self.store,wav)
            self.assertEqual((e['flags'],e['frames'],e['bytes']),(H.STEREO,96000,64+192000))
            self.assertEqual((H.channels(e),H.seconds(e)),(2,1.0))
            H.cmd_get(self.store,'wide',back)
            with wave.open(str(back),'rb') as f:
                self.assertEqual((f.getnchannels(),f.getnframes()),(2,48000));self.assertEqual(f.readframes(48000),frames)
            self.assertEqual(H.cmd_fsck(self.store,True),[])
            same=H.cmd_put(self.store,dual)        # identical channels: stored mono
            self.assertEqual((same['flags'],same['frames']),(0,48000))
            forced=H.cmd_put(self.store,wav,name='forced',mono=True)
            self.assertEqual((forced['flags'],forced['frames']),(0,48000))
            index,_=self.store.current();index.entries[0]['frames']-=1;index.entries[0]['bytes']-=2
            self.assertIn('wide: stereo with an odd sample count',H.problems(index))
    def test_put_progress(self):
        # The CLI's live progress: each phase reaches 100 %; a 44.1 kHz stereo
        # file shows its conversion per channel. Without progress, put is silent.
        import io
        with tempfile.TemporaryDirectory() as tmp:
            wav=Path(tmp)/'wide.wav'
            with wave.open(str(wav),'wb') as f:
                f.setparams((2,2,44100,0,'NONE','not compressed'))
                f.writeframes(b''.join(struct.pack('<hh',i%3000,-(i%2000)) for i in range(100000)))
            out=io.StringIO();self.store.progress=H.Progress(out)
            H.cmd_put(self.store,wav)
            lines=out.getvalue().splitlines()
            for phase in ('Reading catalogue 1/2','Converting L to 48 kHz','Converting R to 48 kHz',
                          'Writing sample','Verifying sample','Saving catalogue','Checking catalogue'):
                self.assertTrue(any(l.startswith(phase+': 100.0%') for l in lines),phase)
            self.store.progress=None;out=io.StringIO()
            H.cmd_put(self.store,wav,name='quiet');self.assertEqual(out.getvalue(),'')
    def test_header_probe_tolerates_only_the_partial_close(self):
        # The capacity probe reads a window's header and closes early: OS 1.41
        # answers "Reader did not complete", which is expected there and only there.
        self.assertEqual(self.client.read('/samples/'+str(R.RAW_FIRST+0x600),header_only=True),R.WINDOW)
        real=self.dev.call
        def broken(kind,payload=b'',timeout=None):
            if kind==0x56:return bytes((0,0,0,0,kind|128,0))+b'card error\0'
            return real(kind,payload,timeout)
        self.dev.call=broken
        with self.assertRaises(RuntimeError):self.client.read('/samples/'+str(R.RAW_FIRST+0x600),header_only=True)
    def test_corrupt_not_formatted(self):
        corrupt=bytearray(R.WINDOW);corrupt[-1]=123;self.dev.windows[0x601]=bytes(corrupt)
        with self.assertRaises(RuntimeError):self.store.ensure()
        self.assertFalse(self.dev.writes)
        self.dev.fail_read=True
        with self.assertRaises(RuntimeError):self.store.ensure()
        self.assertFalse(self.dev.writes)
    def test_failed_close_not_published(self):
        self.store.ensure();before=[i.pack() for i in self.store.current()[1]]
        self.dev.fail_close=True
        with self.assertRaises(RuntimeError):self.store.write_at(0x602,b'SAMP'+bytes(60))
        self.dev.fail_close=False
        self.assertEqual([i.pack() for i in self.store.current()[1]],before)
    def test_union_allocation(self):
        def entry(first):return dict(id=1,first=first,blocks=3,bytes=3*R.WINDOW,frames=(3*R.WINDOW-64)//2,
                                    crc=0,created=0,flags=0,name='entry')
        a=H.Index(seq=2,windows=0xe90,next_id=2,entries=[entry(0x602)])
        b=H.Index(seq=3,windows=0xe90,next_id=2)
        self.assertEqual(H.allocate(2,[a,b],0xe90),0x605)
    def test_codec(self):
        raw=bytes(range(256))+b'abcdefg'
        self.assertEqual(R.decode87(R.encode87(raw)),raw)
        with self.assertRaises(PermissionError):self.client.call(0x50,b'')

if __name__=='__main__':unittest.main()
