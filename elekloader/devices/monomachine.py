# SPDX-License-Identifier: AGPL-3.0-or-later
"""Monomachine donor-only reader and DigiPRO table reconstruction.

OS facts and the float32 halfband build follow the pinned De-Pattern sources
in mods/digimono-st/{fmplus,dpro}/upstream. No firmware bytes are stored here.
This profile supplies donor data only; it does not enable firmware patching.
"""
import hashlib
import struct

KEY = 'monomachine'
NAME = 'Monomachine'
VERSION = '1.32B'
SHA256 = 'fe13c09f661cdba5fe57813240fa1574de1ee0a3ddd7047fc39d0318cba1a710'
TRANSFORM = 'monomachine-digipro-v1'


def nrv2b(src):
    out = bytearray(); pos = 0; bb = 0; last = 1
    def byte():
        nonlocal pos
        if pos >= len(src): raise ValueError('Truncated Monomachine block')
        value = src[pos]; pos += 1
        return value
    def bit():
        nonlocal bb
        bb = bb * 2 if bb & 0x7f else byte() * 2 + 1
        return (bb >> 8) & 1
    while True:
        while bit():
            out.append(byte())
            if len(out) > 1 << 24: raise ValueError('Monomachine block exceeds bounds')
        off = 1
        while True:
            off = off * 2 + bit()
            if off > 0x1000002: raise ValueError('Invalid Monomachine match offset')
            if bit(): break
        if off == 2: off = last
        else:
            off = (off - 3) * 256 + byte()
            if off == 0xffffffff: break
            off += 1; last = off
        length = bit() * 2 + bit()
        if not length:
            length = 1
            while True:
                length = length * 2 + bit()
                if length > 1 << 24: raise ValueError('Invalid Monomachine match length')
                if bit(): break
            length += 2
        length += int(off > 0xd00) + 1
        if off > len(out) or len(out) + length > 1 << 24: raise ValueError('Invalid Monomachine match')
        for _ in range(length): out.append(out[-off])
    return bytes(out)


def blocks(raw):
    if hashlib.sha256(raw).hexdigest() != SHA256:
        raise ValueError('Requires official Monomachine OS 1.32B')
    flash = bytearray(b'\xff' * (1 << 20)); at = 0; messages = 0
    while at < len(raw):
        first = raw.find(b'\xf0', at)
        if first < 0: break
        end = raw.find(b'\xf7', first)
        if end < 0: raise ValueError('Truncated Monomachine SysEx')
        m = raw[first:end+1]; at = end + 1
        if len(m) < 18 or m[1:7] != b'\x00\x20\x3c\x03\x00\x7e': continue
        addr = 0
        for value in m[9:15]: addr = (addr << 4) | (value & 15)
        for k in range(15, len(m)-3, 3):
            value = (m[k] << 14) | (m[k+1] << 7) | m[k+2]
            offset = addr + 2*((k-15)//3)
            if offset + 2 > len(flash): raise ValueError('Monomachine flash address exceeds bounds')
            flash[offset:offset+2] = value.to_bytes(2, 'big')
        messages += 1
    if messages < 8000: raise ValueError('Incomplete Monomachine flash')
    result = []; at = 0x4000
    for _ in range(4):
        length, checksum = struct.unpack_from('>II', flash, at)
        compressed = flash[at+8:at+8+length]
        if len(compressed) != length or sum(compressed) & 0xffffffff != checksum:
            raise ValueError('Monomachine compressed block checksum mismatch')
        result.append(nrv2b(compressed)); at += 8 + length
    return result


def records(data):
    def word(at):
        if at + 3 > len(data): raise ValueError('Truncated Monomachine DSP record')
        return int.from_bytes(data[at:at+3], 'little')
    out = [{}, {}, {}]; at = 6 if data[0] == 3 else 0
    if data[at] == 4: at += 6
    while at < len(data):
        kind = word(at)
        if kind > 2: break
        addr, count = word(at+3), word(at+6)
        if at + 9 + 3*count > len(data): raise ValueError('Invalid Monomachine DSP record bounds')
        for k in range(count): out[kind][addr+k] = word(at+9+3*k)
        at += 9 + 3*count
    return out


def s24(value):
    value &= 0xffffff
    return value - (1 << 24) if value & 0x800000 else value


def f32(value): return struct.unpack('<f', struct.pack('<f', value))[0]


def digipro_tables(raw):
    decoded = blocks(raw); kernel = records(decoded[1]); external = records(decoded[3])
    X, Y = kernel[1], kernel[2]
    ext = {**external[0], **external[1], **external[2]}
    def words(addr, count): return [ext[addr+k] for k in range(count)]
    exp = words(0x140000, 2048)
    wave = [v for w in words(0x101d7b, 33*256) for v in (s24(w), s24(w << 12))]
    interp = []
    for phase in range(32):
        c = 0x334 + phase
        B = (Y[c+0x20], X[c+0x60], Y[c+0x80], Y[c+0xc0], Y[c+0xe0])
        A = (Y[c], Y[c+0x40], Y[c+0x60], Y[c+0xa0], X[c+0xc0])
        interp.extend(v for b, a in zip(B, A) for v in (s24(b), 2*s24(a)))
    interp256 = []
    for q in range(256):
        phase = q >> 3; residual = ((q & 7) + .5) / 8
        interp256.extend([round(interp[10*phase+2*j] + interp[10*phase+2*j+1]*residual)*256 for j in range(5)] + [0]*3)
    br = []
    for k in range(128):
        n, frac = divmod(192*k, 2048)
        br.extend([((s24(exp[frac]) << n) >> 13) & 0xffffff,
                   (s24(exp[2047-frac]) >> (8+n)) & 0xffffff])
    bank = [0] * (32*2048)
    taps = [-1/32, 0, 9/32, 1/2, 9/32, 0, -1/32]
    for w in range(32):
        base = w*2048
        for level in range(10):
            size = 1024 >> max(0, level-1)
            for j in range(size):
                total = 0.0
                for tap, coef in enumerate(taps):
                    k = (j+tap-3) & (size-1)
                    value = (f32(2*f32(wave[w*512+k//2]/(1 << 23))) if not k & 1 else 0.0) if not level else s24(bank[base+2048-2*size+k] & 0xffff00)/(1 << 23)
                    total = f32(total + f32(value*coef))
                sample = int(max(-32768, min(32767, f32(total*32768))))
                word = (sample & 0xffff) << 8
                if not level: bank[base+j] = word
                elif level < 9 and not j & 1: bank[base+2048-size+j//2] = word
                if level >= 2:
                    at = base+2048-4*size+2*j
                    bank[at] = (bank[at] & 0xffff00) | ((sample >> 8) & 255)
                    bank[at+1] = (bank[at+1] & 0xffff00) | (sample & 255)
    partner = [0] * len(bank)
    for w in range(32):
        base = w*2048
        for shift in range(1, 9):
            size = 1024 >> shift
            for j in range(size):
                at = base+2048-4*size+2*j
                pair = ((bank[at] & 255) << 8) | (bank[at+1] & 255)
                partner[base+2048-2*size+j] = (pair if pair < 32768 else pair-65536)*2048
    tables = dict(exp=exp, wave=[v*8 for v in wave]+[0]*64,
                  interp=[v*256 for v in interp], interp256=interp256,
                  pch=words(0x101afb,128), wid=words(0x101bfb,128)+words(0x101cfb,128),
                  br=br, digibank=[s24(v)*8 for v in bank], partner=partner)
    return {key: b''.join((v & 0xffffffff).to_bytes(4,'big') for v in values) for key,values in tables.items()}
