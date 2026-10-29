# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Syntakt Data RPC client. Ports are opened only by explicit CLI calls.

Only DataRead*/DataWrite* (0x54..0x59) are allowed. Every write is checked
before DataWriteOpen. Stock samples are cropped and their SAMP size/CRC rebuilt.
"""
import struct
import time
from .mods_object import crc

HDR=bytes.fromhex('f000203c1000')
WINDOW=0x80000
RAW_FIRST=8185
STOCK_FRAMES=48000*5
STOCK_BODY=64+2*STOCK_FRAMES

def be32(n):return struct.pack('>I',n)
def u32(b,o):
    if o+4>len(b):raise ValueError('truncated Data RPC integer')
    return struct.unpack_from('>I',b,o)[0]

def encode87(raw):
    out=bytearray()
    for i in range(0,len(raw),7):
        group=raw[i:i+7]
        out.append(sum((0x40>>j) for j,b in enumerate(group) if b&128))
        out.extend(b&127 for b in group)
    return bytes(out)

def decode87(raw):
    if any(b&128 for b in raw):raise ValueError('invalid SysEx encoding')
    out=bytearray()
    for i in range(0,len(raw),8):
        if len(raw[i:])==1:raise ValueError('truncated SysEx group')
        out.extend(b|(128 if raw[i]&(0x40>>j) else 0) for j,b in enumerate(raw[i+1:i+8]))
    return bytes(out)

def ok(reply,kind):
    if reply is None:raise RuntimeError('Data RPC timeout')
    if len(reply)<6 or reply[4]!=(kind|128):raise RuntimeError('invalid Data RPC response')
    if not reply[5]:raise RuntimeError(reply[6:].split(b'\0')[0].decode('cp1252','replace'))
    return reply

def object_pack(n,body):
    return (struct.pack('>IBHH4sIIII',0xac11d303,2,8,13,b'0094',8,0xffffffff,n,len(body))
            +b'\0\x0c'+body+struct.pack('>III',crc(body),len(body),0xaaa1daaa))

def object_unpack(obj,n):
    if len(obj)<43:raise ValueError('truncated Data object')
    h,body,f=obj[:31],obj[31:-12],obj[-12:]
    if (h[:5]!=bytes.fromhex('ac11d30302') or h[5:9]!=bytes.fromhex('0008000d') or
        u32(h,13)!=8 or u32(h,21)!=n or u32(h,25)!=len(body) or h[29:]!=b'\0\x0c'):
        raise ValueError('invalid Data object header')
    if f!=struct.pack('>III',crc(body),len(body),0xaaa1daaa):raise ValueError('invalid Data object checksum/footer')
    return body

def sample_slot(path):
    if not path.startswith('/samples/'):return None
    value=path[9:]
    if not value.isascii() or not value.isdecimal() or value.startswith('0'):raise ValueError('invalid sample path')
    n=int(value)
    if path!=f'/samples/{n}':raise ValueError('noncanonical sample path')
    return n

def crop_stock(body):
    if len(body)<64 or body[:4]!=b'SAMP' or len(body)!=64+2*u32(body,8):
        raise ValueError('invalid stock SAMP body')
    if len(body)<=STOCK_BODY:return body
    header=bytearray(body[:64]);struct.pack_into('>I',header,8,STOCK_FRAMES)
    return bytes(header)+body[64:STOCK_BODY]

def checked_body(path,body,card_bytes=None):
    """No request may be sent until this returns. No bypass/oversize option."""
    n=sample_slot(path)
    if n is not None:
        if 1<=n<=64:return n,crop_stock(body)
        k=n-RAW_FIRST
        if not 0x600<=k<0x2000:raise PermissionError('not a stock slot or high-zone window')
        if card_bytes is None:raise ValueError('high-zone writes require verified card capacity')
        if not body or k*WINDOW+len(body)>min(card_bytes,1<<32):raise ValueError('write passes card boundary')
        return n,body
    if path=='/mods':
        if len(body)>16*1024*1024:raise ValueError('/mods exceeds 16 MiB')
        return 1,body
    raise PermissionError('unsupported write destination')

class MidiTransport:
    """Optional python-rtmidi transport; constructing it explicitly opens ports."""
    def __init__(self,port='Syntakt',timeout=10):
        import rtmidi
        self.mi,self.mo=rtmidi.MidiIn(),rtmidi.MidiOut()
        self.timeout,self.seq,self.pending=timeout,1,None
        def choose(names):
            exact=[i for i,n in enumerate(names) if n==port]
            matches=exact or [i for i,n in enumerate(names) if port.lower() in n.lower()]
            if len(matches)!=1:raise ValueError(f'expected one MIDI port matching {port!r}: {names}')
            return matches[0]
        pi,po=choose(self.mi.get_ports()),choose(self.mo.get_ports())
        if hasattr(self.mi,'set_buffer_size'):self.mi.set_buffer_size(16384,16)
        try:
            self.mi.open_port(pi);self.mo.open_port(po)
            self.mi.ignore_types(sysex=False,timing=True,active_sense=True)
        except BaseException:
            self.close();raise
    def close(self):self.mi.close_port();self.mo.close_port()
    def call(self,kind,payload=b'',timeout=None):
        if kind not in range(0x54,0x5a):raise PermissionError('only Data read/write RPC is allowed')
        seq=self.seq;self.seq=(seq+1)&0x3fff or 1
        while self.mi.get_message():pass
        self.pending=None
        self.mo.send_message(list(HDR+encode87(struct.pack('>HHB',seq,0,kind)+payload)+b'\xf7'))
        until=time.monotonic()+(self.timeout if timeout is None else timeout)
        while time.monotonic()<until:
            message=self.mi.get_message()
            if not message:time.sleep(.002);continue
            chunk=bytes(message[0])
            if not chunk:continue
            if chunk[0]==0xf0:self.pending=bytearray(chunk)
            elif self.pending is not None and (chunk[0]<128 or chunk[0]==0xf7):self.pending.extend(chunk)
            else:continue
            if len(self.pending)>32768:self.pending=None;raise ValueError('oversize MIDI response')
            if self.pending[-1]!=0xf7:continue
            frame=bytes(self.pending);self.pending=None
            if not frame.startswith(HDR):continue
            rx=decode87(frame[len(HDR):-1])
            if len(rx)>=6 and rx[2:4]==struct.pack('>H',seq) and rx[4]==(kind|128):return rx
        raise TimeoutError('Data RPC timed out; no request was retried')

class DataClient:
    def __init__(self,transport,block=2048,rest=0):
        if not 64<=block<=8192 or rest<0:raise ValueError('invalid Data chunk/pause')
        self.transport,self.block,self.rest=transport,block,rest
    def call(self,kind,payload):
        if kind not in range(0x54,0x5a):raise PermissionError('only Data read/write RPC is allowed')
        if self.rest:time.sleep(self.rest)
        return ok(self.transport.call(kind,payload,timeout=15),kind)
    def read(self,path,*,header_only=False):
        n=1 if path in ('/mods','/mod-status') else sample_slot(path)
        if n is None or not (1<=n<=64 or 0x600<=n-RAW_FIRST<0x2000):
            if path not in ('/mods','/mod-status'):raise PermissionError('unsupported read destination')
        rx=self.call(0x54,path.encode('ascii')+b'\0'+be32(self.block)+b'\0');job=u32(rx,6)
        out=bytearray();seq=empty=0;expected=None
        try:
            while expected is None or len(out)<expected:
                rx=self.call(0x55,be32(job)+be32(seq));seq+=1
                if len(rx)<27 or u32(rx,6)!=job:raise RuntimeError('invalid read response/job')
                size=u32(rx,23);chunk=rx[27:]
                if len(chunk)!=size or size>self.block:raise RuntimeError('invalid read chunk size')
                out.extend(chunk);empty=empty+1 if not size else 0
                if empty>64:raise RuntimeError('read made no progress')
                if len(out)>=31 and expected is None:
                    body_len=u32(out,25)
                    limit=128 if path=='/mod-status' else 16*1024*1024 if path=='/mods' else WINDOW
                    if body_len>limit:raise ValueError('read object exceeds destination size')
                    expected=43+body_len
                    if header_only:
                        if out[:5]!=bytes.fromhex('ac11d30302') or u32(out,21)!=n:raise ValueError('invalid probe header')
                        return body_len
                if rx[18] and (expected is None or len(out)<expected):raise RuntimeError('truncated Data object')
            if len(out)!=expected:raise RuntimeError('extra Data object bytes')
            return object_unpack(bytes(out),n)
        finally:self.call(0x56,be32(job))
    def write(self,path,body,*,card_bytes=None):
        n,body=checked_body(path,body,card_bytes)
        obj=object_pack(n,body);head,footer=obj[:-12],obj[-12:]
        rx=self.call(0x57,be32(len(obj))+path.encode('ascii')+b'\0');job=u32(rx,6)
        sent=seq=0
        try:
            # Footer is always a separate partial, including exact block multiples.
            for part in (head,footer):
                for off in range(0,len(part),self.block):
                    chunk=part[off:off+self.block]
                    rx=self.call(0x58,be32(job)+be32(seq)+be32(crc(chunk))+be32(len(chunk))+chunk)
                    sent+=len(chunk)
                    if u32(rx,6)!=job or u32(rx,10)!=seq or u32(rx,14)!=sent:
                        raise RuntimeError('write acknowledgment differs')
                    seq+=1
        except BaseException:
            # Close an incomplete job, but never publish metadata on failure.
            try:self.call(0x59,be32(job)+be32(len(obj)))
            except Exception:pass
            raise
        rx=self.call(0x59,be32(job)+be32(len(obj)))
        if u32(rx,6)!=job or u32(rx,10)!=len(obj):raise RuntimeError('write close was not confirmed')
        return body
