"""Generate native C-parser verdicts for valid and deliberately damaged banks.

Only patch descriptions are saved. The original private bank remains an input.
Checksums are repaired for structural mutations so checksum refusal cannot
mask bounds, ABI, directory, relocation or control validation failures.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import zlib

def mutate(original,case):
 data=bytearray(original)
 for at,value in case.get('words',[]):struct.pack_into('>I',data,at,value)
 for at,value in case.get('bytes',[]):data[at]=value
 u=lambda at:struct.unpack_from('>I',original,at)[0]
 put=lambda at,v:struct.pack_into('>I',data,at,v&0xffffffff)
 if case.get('repair'):
  start=80+4*u(56);end=start+u(52);audio=48+u(72);size=u(76)
  if case.get('audio'):put(audio+12,zlib.adler32(data[audio+16:audio+size]))
  put(68,sum(struct.unpack('>'+str((end-80)//4)+'I',data[80:end])))
  put(16,zlib.adler32(data[20:]))
 return bytes(data[:case['truncate']]) if 'truncate' in case else bytes(data)

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--bank',type=Path,required=True);p.add_argument('--native-parser',type=Path,required=True)
 p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.bank.read_bytes();u=lambda at:struct.unpack_from('>I',raw,at)[0]
 start=80+4*u(56);audio=48+u(72);desc=start+u(40);ui=start+u(28);module=start+u(32)
 params=start+u(desc+20);layout=start+u(ui+8);events=start+u(module+8)
 cases=[{'name':'original','valid':True}]
 def add(name,words=None,byte_values=None,repair=True,audio=False):
  cases.append(dict(name=name,valid=False,words=words or [],bytes=byte_values or [],repair=repair,audio=audio))
 for size in [0,1,16,79,len(raw)-1]:cases.append(dict(name='truncated-'+str(size),valid=False,truncate=size))
 for offset,value,label in [(0,0,'magic'),(4,3,'version'),(8,0,'ABI'),(12,len(raw)+16,'length'),(20,0,'header-size'),
  (24,0x8000001f,'unknown-features'),(36,33,'module-count'),(40,1,'descriptor-alignment'),(44,17,'machine-count'),
  (48,0,'SMZ-magic'),(52,0xfffffffc,'MAIN-overflow'),(56,0xffffffff,'relocation-count'),(60,1,'metadata-alignment'),
  (64,0x53594605,'old-contract'),(72,0xffffffff,'AUDIO-offset'),(76,0x400001,'AUDIO-size')]:add(label,[(offset,value)])
 add('envelope-checksum',[(16,0)],repair=False)
 add('MAIN-checksum',[(68,0)],repair=False)
 for at,value,label in [(desc,63,'machine-type'),(desc+4,29,'parameter-count'),(desc+20,0xfffffffc,'parameter-table'),
  (desc+28,43,'sound-stride'),(desc+32,0,'category'),(desc+36,128,'rank'),
  (params+8,0xffff,'parameter-minimum'),(params+12,0x10000,'parameter-maximum'),(params+20,0xfffffffe,'configuration-alias'),
  (ui+4,0,'UI-pages'),(layout,32,'UI-control-index'),(module+12,17,'event-count'),(events,0x10b,'event-type')]:add(label,[(at,value)])
 add('relocation-odd',[(80,1)])
 add('relocation-overlap',[(84,u(80))])
 add('relocation-outside',[(80,u(52))])
 add('relocation-target-outside',[(start+u(80),u(52))])
 add('AUDIO-header',[(audio,0)],audio=True)
 add('AUDIO-type',[(audio+16,63)],audio=True)
 add('AUDIO-entry',[(audio+20,0x4204f750)],audio=True)
 add('metadata-terminator',byte_values=[(start+u(60),0)],repair=True)
 # The parser checks file structure, not DSP semantics. Input hashes and
 # native differential tests are the separate protection for compiled code.
 cases.append(dict(name='structurally-valid-code-byte-change',valid=True,
                   bytes=[[audio+u(76)-32,raw[audio+u(76)-32]^1]],repair=True,audio=True))
 parser=ctypes.CDLL(str(a.native_parser.resolve()));parser.synfact_smz_validate.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
 for case in cases:
  data=mutate(raw,case);buf=ctypes.create_string_buffer(data)
  verdict=bool(parser.synfact_smz_validate(buf,len(data)))
  if verdict!=case['valid']:raise AssertionError((case['name'],verdict,parser.synfact_smz_failure()))
 report={'bankSha256':hashlib.sha256(raw).hexdigest(),'cases':cases,'nativeParserAgrees':True}
 with a.out.open('x',encoding='utf-8') as f:f.write(json.dumps(report,indent=2)+'\n')
 print('Native parser corpus:',len(cases),'cases, including checksum-repaired structural failures')

if __name__=='__main__':main()
