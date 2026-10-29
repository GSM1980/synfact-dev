# SPDX-License-Identifier: GPL-2.0-or-later
"""Long samples in the core's HZFS v1 high-zone storage.

python -m elekloader.sdk.syntakt.long_samples ls
python -m elekloader.sdk.syntakt.long_samples put pad.wav --yes-write-device
python -m elekloader.sdk.syntakt.long_samples get pad --out pad-back.wav
python -m elekloader.sdk.syntakt.long_samples rm pad --yes-write-device
python -m elekloader.sdk.syntakt.long_samples stock short.wav --slot 64 --yes-write-device

Requires python-rtmidi for live commands. Importing this module never opens MIDI.
"""
import argparse
import array
from pathlib import Path
import struct
import sys
import time
from .data_rpc import DataClient, MidiTransport, WINDOW, RAW_FIRST as RAW_FIRST_N, be32, u32
from .mods_object import crc

HZ_FIRST_K=0x600
INDEX_K=(0x600,0x601)
DATA_FIRST_K=0x602
MAGIC,VERSION=b'HZFS',1
HEAD,MAP_SLOTS,ENTRY=0x40,64,0x40
MAP_OFF=HEAD
CRC_OFF=0x20
ENTRIES_OFF=0x140
MAX_ENTRIES=(WINDOW-ENTRIES_OFF)//ENTRY
RATE=48000
SAMP_HEADER=64

# ------------------------------------------------------------------ index
from .sample_catalog import Index, logical_length, enable_folders


def problems(index):
    """Consistency checks of one index -> list of messages (empty = consistent)."""
    out, spans, names, ids = index.metadata_problems(), [], set(), set()
    if not (0<index.seq<=0xffffffff and DATA_FIRST_K<index.windows<=0x2000 and 0<index.next_id<=0xffffffff):
        out.append('invalid catalogue sequence/capacity/next id')
    for e in index.entries:
        lo, hi = e['first'], e['first'] + e['blocks']
        if not (DATA_FIRST_K <= lo < hi <= index.windows):
            out.append(f"{e['name']}: blocks {lo:#x}..{hi:#x} outside the data area")
        if e['blocks'] != -(-e['bytes'] // WINDOW) or e['bytes'] != SAMP_HEADER + 2 * e['frames']:
            out.append(f"{e['name']}: size fields inconsistent")
        if (e.get('folder',0),e['name']) in names or e['id'] in ids:
            out.append(f"{e['name']}: duplicate name or id")
        names.add((e.get('folder',0),e['name']))
        ids.add(e['id'])
        if not e['name'] or not e['id'] or e['id'] >= index.next_id:
            out.append(f"{e['name']}: id {e['id']} >= next_id {index.next_id}")
        spans.append((lo, hi, e['name']))
    spans.sort()
    for (a_lo, a_hi, a), (b_lo, b_hi, b) in zip(spans, spans[1:]):
        if b_lo < a_hi:
            out.append(f'{a} and {b} overlap')
    for slot, eid in enumerate(index.slot_map):
        if eid and eid not in ids:
            out.append(f'slot {65 + slot} -> missing id {eid}')
    return out


def allocate(blocks, indexes, windows):
    """First fit over blocks free in EVERY given index copy."""
    used = sorted({span for index in indexes if index for span in index.used()})
    k = DATA_FIRST_K
    for lo, hi in used:
        if lo - k >= blocks:
            return k
        k = max(k, hi)
    if windows - k >= blocks:
        return k
    raise ValueError(f'no {blocks} contiguous free blocks ({blocks * WINDOW / 2**20:.1f} MiB); '
                     f'free total {free_blocks(indexes[0], windows) * WINDOW / 2**20:.1f} MiB')


def free_blocks(index, windows):
    return windows - DATA_FIRST_K - sum(hi - lo for lo, hi in index.used())


def largest_hole(index, windows):
    k, best = DATA_FIRST_K, 0
    for lo, hi in sorted(index.used()):
        best, k = max(best, lo - k), max(k, hi)
    return max(best, windows - k)


# ------------------------------------------------------------------ audio
def read_wav(path):
    """-> (rate, mono samples as floats in [-1, 1)). PCM 8/16/24/32 bit, float 32, extensible."""
    data = Path(path).read_bytes()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError(f'{path}: not a RIFF/WAVE file')
    fmt = pcm = None
    pos = 12
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], int.from_bytes(data[pos + 4:pos + 8], 'little')
        if pos+8+size>len(data):raise ValueError('truncated WAV chunk')
        body = data[pos + 8:pos + 8 + size]
        if cid == b'fmt ':
            fmt = body
        elif cid == b'data':
            pcm = body
        pos += 8 + size + (size & 1)
    if fmt is None or pcm is None or len(fmt)<16:
        raise ValueError(f'{path}: missing fmt or data chunk')
    tag, channels, rate = struct.unpack_from('<HHI', fmt, 0)
    bits = struct.unpack_from('<H', fmt, 14)[0]
    if tag == 0xFFFE:
        if len(fmt)<40:raise ValueError('truncated extensible WAV format')
        tag = struct.unpack_from('<H', fmt, 24)[0]
    if not 1<=channels<=32 or not 1<=rate<=768000 or bits not in (8,16,24,32):
        raise ValueError('invalid WAV rate/channels/width')
    width = bits // 8
    if len(pcm)%(width*channels):raise ValueError('partial WAV audio frame')
    usable = len(pcm)
    pcm = pcm[:usable]
    if tag == 1 and bits == 16:
        values, scale = array.array('h', pcm), 32768.0
    elif tag == 1 and bits == 32:
        values, scale = array.array('i', pcm), 2147483648.0
    elif tag == 3 and bits == 32:
        values, scale = array.array('f', pcm), 1.0
    elif tag == 1 and bits == 8:
        values, scale = [b - 128 for b in pcm], 128.0
    elif tag == 1 and bits == 24:
        values = [int.from_bytes(pcm[i:i + 3], 'little', signed=True) for i in range(0, len(pcm), 3)]
        scale = 8388608.0
    else:
        raise ValueError(f'{path}: unsupported WAV format tag {tag} / {bits} bit')
    if sys.byteorder != 'little' and isinstance(values, array.array):
        values.byteswap()
    if channels == 1:
        mono = [v / scale for v in values]
    else:
        mono = [sum(values[i:i + channels]) / (channels * scale) for i in range(0, len(values), channels)]
    return rate, mono


def resample(samples, rate):
    """4-point Hermite interpolation to 48 kHz (no anti-alias filter: downsampling from
    above 48 kHz can alias; prepare such files at 48 kHz for best quality)."""
    if rate == RATE or not samples:
        return samples
    step, n = rate / RATE, len(samples)
    out_len = int(n * RATE / rate)
    s = samples
    out = []
    for i in range(out_len):
        x = i * step
        j = int(x)
        t = x - j
        y0, y1 = s[j - 1] if j else s[0], s[j]
        y2 = s[j + 1] if j + 1 < n else s[-1]
        y3 = s[j + 2] if j + 2 < n else s[-1]
        c1 = 0.5 * (y2 - y0)
        c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3
        c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2)
        out.append(((c3 * t + c2) * t + c1) * t + y1)
    return out


def to_pcm16be(samples):
    values = array.array('h', (max(-32768, min(32767, round(v * 32768.0))) for v in samples))
    if sys.byteorder == 'little':
        values.byteswap()
    return values.tobytes()


def samp_body(pcm_be, name):
    if len(pcm_be)&1:raise ValueError('partial PCM16 frame')
    frames = len(pcm_be) // 2
    header = bytearray(SAMP_HEADER)
    header[0:4] = b'SAMP'
    struct.pack_into('>I', header, 8, frames)
    header[12:28] = name.encode('cp1252', 'replace')[:16].ljust(16, b'\0')
    return bytes(header) + pcm_be, frames


def write_wav(path, pcm_be):
    values = array.array('h', pcm_be)
    if sys.byteorder == 'little':
        values.byteswap()
    data = values.tobytes()
    head = (b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE' + b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, RATE,
            RATE * 2, 2, 16) + b'data' + struct.pack('<I', len(data)))
    Path(path).write_bytes(head + data)


class Store:
    """HZFS catalogue transactions over bounded native Data windows.

    No persistent cache: every command rereads both indices. No stock slots,
    projects or /mods are used by this class. Files are published after readback.
    """
    def __init__(self,client,log=print):
        self.client,self.log=client,log
        self.windows=self.card_bytes=None
    def window(self,k):
        if not HZ_FIRST_K<=k<0x2000:raise PermissionError('not a high-zone window')
        return self.client.read(f'/samples/{RAW_FIRST_N+k}')
    def capacity(self):
        if self.windows is None:
            def size(k):return self.client.read(f'/samples/{RAW_FIRST_N+k}',header_only=True)
            if size(HZ_FIRST_K)!=WINDOW:raise RuntimeError('high-zone core/card unavailable')
            lo,hi=HZ_FIRST_K,0x2000
            while hi-lo>1:
                mid=(lo+hi)//2
                if size(mid):lo=mid
                else:hi=mid
            last=size(lo)
            if not 0<last<=WINDOW or last%512:raise RuntimeError('invalid card geometry')
            self.card_bytes=lo*WINDOW+last
            # HZFS deliberately uses complete windows only.
            self.windows=self.card_bytes//WINDOW
            if self.windows<=DATA_FIRST_K:raise RuntimeError('no high-zone sample space')
        return self.windows
    def read_blocks(self,first,count,length):
        self.capacity()
        if first<DATA_FIRST_K or first+count>self.windows or not 0<length<=count*WINDOW:
            raise ValueError('file read outside high-zone data')
        out=bytearray()
        for k in range(first,first+count):
            chunk=self.window(k)
            if len(chunk)!=WINDOW:raise RuntimeError('short storage window')
            out.extend(chunk[:min(WINDOW,length-len(out))])
        return bytes(out)
    def write_at(self,k,body):
        self.capacity()
        if k<HZ_FIRST_K or not body or k*WINDOW+len(body)>self.windows*WINDOW:
            raise PermissionError('write outside complete high-zone windows')
        self.client.write(f'/samples/{RAW_FIRST_N+k}',body,card_bytes=self.card_bytes)
    def read_index_copies(self):
        capacity=self.capacity();copies=[];raws=[]
        for k in INDEX_K:
            raw=self.window(k)
            if len(raw)!=WINDOW:raise RuntimeError('short catalogue read')
            index=Index.unpack(raw)
            if index is None and raw[:4]==MAGIC:
                length=u32(raw,36)
                if ENTRIES_OFF<=length<=WINDOW and crc(raw[:CRC_OFF]+bytes(4)+raw[CRC_OFF+4:length])==u32(raw,CRC_OFF):
                    raise RuntimeError('CRC-valid catalogue has an unsupported or inconsistent header')
            if index and (problems(index) or index.windows>capacity):
                raise RuntimeError('CRC-valid catalogue contains inconsistent entries')
            copies.append(index);raws.append(raw)
        if all(copies) and copies[0].seq==copies[1].seq and copies[0].pack()!=copies[1].pack():
            raise RuntimeError('conflicting catalogues with equal sequence')
        return copies,raws
    def ensure(self):
        copies,raws=self.read_index_copies()
        if any(copies):return
        if any(any(b not in (0,255) for b in raw) for raw in raws):
            raise RuntimeError('unknown/corrupt catalogue: automatic formatting refused')
        index=Index(seq=1,windows=self.capacity());body=index.pack()
        for k in INDEX_K:
            self.write_at(k,body)
            if self.window(k)[:len(body)]!=body:raise RuntimeError('initial catalogue readback differs')
        self.log('HZFS created in empty high-zone storage')
    def current(self):
        copies,_=self.read_index_copies();valid=[i for i in copies if i]
        if not valid:raise RuntimeError('no valid HZFS catalogue')
        return max(valid,key=lambda c:c.seq),copies
    def commit(self,index,copies):
        seqs=[c.seq if c else 0 for c in copies]
        if max(seqs)==0xffffffff:raise ValueError('catalogue sequence exhausted')
        target=0 if seqs[0]<=seqs[1] else 1
        index.seq=max(seqs)+1
        if problems(index):raise ValueError(f'inconsistent catalogue: {problems(index)}')
        body=index.pack();self.write_at(INDEX_K[target],body)
        back=self.window(INDEX_K[target])
        if back[:len(body)]!=body or Index.unpack(back) is None:raise RuntimeError('catalogue commit readback differs')


def cmd_ls(store):
    index,copies=store.current()
    store.log(f'HZFS sequence {index.seq}: {len(index.entries)} samples')
    for e in index.entries:store.log(f"{e['id']:5}  {e['name']:<32} {e['frames']/RATE:9.3f} s  {e['bytes']/2**20:8.2f} MiB")
    # Safe allocation includes extents still referenced by the other catalogue.
    spans=sorted({s for c in copies if c for s in c.used()});at=DATA_FIRST_K;free=largest=0
    for lo,hi in spans:
        hole=max(0,lo-at);free+=hole;largest=max(largest,hole);at=max(at,hi)
    hole=max(0,index.windows-at);free+=hole;largest=max(largest,hole)
    store.log(f'Allocatable {free*WINDOW/2**20:.1f} MiB; largest hole {largest*WINDOW/2**20:.1f} MiB')
    return index

def wav_body(path,name):
    if not name or '\0' in name or len(name.encode('utf-8'))>32:raise ValueError('name must be 1..32 UTF-8 bytes')
    rate,mono=read_wav(path)
    return samp_body(to_pcm16be(resample(mono,rate)),name)[0]

def cmd_put(store,wav,name=None,folder=0):
    name=name or Path(wav).stem;body=wav_body(wav,name)
    store.ensure();index,copies=store.current()
    if len(index.entries)>=MAX_ENTRIES or index.next_id==0xffffffff or index.seq==0xffffffff:
        raise ValueError('catalogue full/exhausted')
    index.folder(folder)
    if folder and index.version!=2:raise ValueError('Enable folders first')
    logical_length(index.version,len(index.entries)+1,len(index.folders))
    if any(e['name']==name and e.get('folder',0)==folder for e in index.entries):raise ValueError('sample name already exists')
    blocks=(len(body)+WINDOW-1)//WINDOW;first=allocate(blocks,copies,index.windows)
    store.write_at(first,body)
    if store.read_blocks(first,blocks,len(body))!=body:raise RuntimeError('sample readback differs; catalogue unchanged')
    e=dict(id=index.next_id,first=first,blocks=blocks,bytes=len(body),frames=u32(body,8),crc=crc(body),
           created=int(time.time()),flags=0,name=name)
    if index.version==2:e['folder']=folder
    index.entries.append(e);index.next_id+=1;store.commit(index,copies)
    store.log(f"Stored {name}: {e['frames']/RATE:.3f} s, id {e['id']}")
    return e

def sample_read(store,e):
    body=store.read_blocks(e['first'],e['blocks'],e['bytes'])
    if body[:4]!=b'SAMP' or u32(body,8)!=e['frames'] or crc(body)!=e['crc']:
        raise RuntimeError('sample CRC/header differs from catalogue')
    return body

def cmd_get(store,key,out,raw=False):
    if Path(out).exists():raise ValueError('output already exists')
    index,_=store.current();body=sample_read(store,index.find(key))
    if raw:Path(out).write_bytes(body)
    else:write_wav(out,body[SAMP_HEADER:])
    return body

def cmd_rm(store,key):
    index,copies=store.current();e=index.find(key);index.entries.remove(e)
    index.slot_map=[0 if i==e['id'] else i for i in index.slot_map];store.commit(index,copies)
    store.log(f"Removed {e['name']}; PCM is left in place until its blocks can be reused")

def cmd_fsck(store,deep=False):
    index,copies=store.current();issues=[]
    for i,c in enumerate(copies):
        if c is None:issues.append(f'Catalogue {"AB"[i]} invalid; using the other copy')
    if deep:
        for e in index.entries:
            try:sample_read(store,e)
            except RuntimeError as exc:issues.append(f"{e['name']}: {exc}")
    for message in issues or ['HZFS consistent']:store.log(message)
    return issues

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',default='Syntakt');p.add_argument('--block',type=int,default=2048)
    sub=p.add_subparsers(dest='cmd',required=True)
    sub.add_parser('ls');sub.add_parser('init')
    check=sub.add_parser('fsck');check.add_argument('--deep',action='store_true')
    get=sub.add_parser('get');get.add_argument('key');get.add_argument('--out',required=True);get.add_argument('--raw',action='store_true')
    put=sub.add_parser('put');put.add_argument('wav');put.add_argument('--name');put.add_argument('--folder',type=int,default=0)
    rm=sub.add_parser('rm');rm.add_argument('key')
    stock=sub.add_parser('stock');stock.add_argument('wav');stock.add_argument('--slot',type=int,required=True,choices=range(1,65));stock.add_argument('--name')
    for command in (put,rm,stock,sub.choices['init']):command.add_argument('--yes-write-device',action='store_true')
    migrate=sub.add_parser('folders-enable');migrate.add_argument('--backup',required=True);migrate.add_argument('--confirm-firmware',action='store_true')
    mkdir=sub.add_parser('mkdir');mkdir.add_argument('name');mkdir.add_argument('--parent',type=int,default=0)
    move=sub.add_parser('move');move.add_argument('ids',nargs='+',type=int);move.add_argument('--folder',type=int,required=True)
    rmdir=sub.add_parser('rmdir');rmdir.add_argument('id',type=int)
    for command in (migrate,mkdir,move,rmdir):command.add_argument('--yes-write-device',action='store_true')
    args=p.parse_args(argv)
    if args.cmd in ('put','rm','stock','init','folders-enable','mkdir','move','rmdir') and not args.yes_write_device:
        p.error('this command writes eMMC; pass --yes-write-device')
    stock_body=None
    if args.cmd=='stock':
        from .data_rpc import crop_stock
        original=wav_body(args.wav,args.name or Path(args.wav).stem);stock_body=crop_stock(original)
        print(f'Stock slot {args.slot}: {u32(stock_body,8)/RATE:.3f} s'+(' (cropped to 5 s)' if len(original)!=len(stock_body) else ''))
    transport=MidiTransport(args.port)
    try:
        client=DataClient(transport,block=args.block);store=Store(client)
        if args.cmd=='stock':
            path=f'/samples/{args.slot}';client.write(path,stock_body)
            if client.read(path)!=stock_body:raise RuntimeError('stock sample readback differs')
        elif args.cmd=='ls':cmd_ls(store)
        elif args.cmd=='init':store.ensure()
        elif args.cmd=='put':cmd_put(store,args.wav,args.name,args.folder)
        elif args.cmd=='folders-enable':enable_folders(store,args.backup,firmware_confirmed=args.confirm_firmware)
        elif args.cmd in ('mkdir','move','rmdir'):
            index,copies=store.current()
            if args.cmd=='mkdir':print(index.mkdir(args.name,args.parent))
            elif args.cmd=='move':index.move_samples(args.ids,args.folder)
            else:index.rmdir(args.id)
            store.commit(index,copies)
        elif args.cmd=='get':cmd_get(store,args.key,args.out,args.raw)
        elif args.cmd=='rm':cmd_rm(store,args.key)
        elif args.cmd=='fsck':return int(bool(cmd_fsck(store,args.deep)))
        return 0
    finally:transport.close()

if __name__=='__main__':sys.exit(main())


