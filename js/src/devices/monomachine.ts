// SPDX-License-Identifier: AGPL-3.0-or-later
// Donor-only Monomachine OS facts and DigiPRO table build. Algorithm/provenance:
// mods/digimono-st/{fmplus,dpro}/upstream (De-Pattern); Python device counterpart.
import { sha } from '../bytes.ts'

export const MONOMACHINE = { key: 'monomachine', name: 'Monomachine', version: '1.32B',
  sha256: 'fe13c09f661cdba5fe57813240fa1574de1ee0a3ddd7047fc39d0318cba1a710' }
export const MNM_TRANSFORM = 'monomachine-digipro-v1'
const bad = (text: string): never => { throw Error(text) }

function nrv2b(src: Uint8Array): Uint8Array {
  let out = new Uint8Array(65536), size = 0, pos = 0, bb = 0, last = 1
  const byte = () => pos < src.length ? src[pos++] : bad('Truncated Monomachine block')
  const bit = () => { bb = bb & 127 ? bb * 2 : byte() * 2 + 1; return (bb >> 8) & 1 }
  const push = (v: number) => {
    if (size === 1 << 24) bad('Monomachine block exceeds bounds')
    if (size === out.length) { const next = new Uint8Array(out.length * 2); next.set(out); out = next }
    out[size++] = v
  }
  while (true) {
    while (bit()) push(byte())
    let offset = 1
    while (true) {
      offset = offset * 2 + bit()
      if (offset > 0x1000002) bad('Invalid Monomachine match offset')
      if (bit()) break
    }
    if (offset === 2) offset = last
    else {
      offset = (offset - 3) * 256 + byte()
      if (offset === 0xffffffff) break
      last = ++offset
    }
    let length = bit() * 2 + bit()
    if (!length) {
      length = 1
      while (true) {
        length = length * 2 + bit()
        if (length > 1 << 24) bad('Invalid Monomachine match length')
        if (bit()) break
      }
      length += 2
    }
    length += Number(offset > 0xd00) + 1
    if (offset > size || size + length > 1 << 24) bad('Invalid Monomachine match')
    for (let i = 0; i < length; i++) push(out[size - offset])
  }
  return out.slice(0, size)
}

export function monomachineBlocks(raw: Uint8Array): Uint8Array[] {
  if (sha(raw) !== MONOMACHINE.sha256) bad('Requires official Monomachine OS 1.32B')
  const flash = new Uint8Array(1 << 20).fill(255)
  let at = 0, messages = 0
  while (at < raw.length) {
    const first = raw.indexOf(0xf0, at)
    if (first < 0) break
    const end = raw.indexOf(0xf7, first)
    if (end < 0) bad('Truncated Monomachine SysEx')
    const m = raw.subarray(first, end + 1); at = end + 1
    if (m.length < 18 || [0, 0x20, 0x3c, 3, 0, 0x7e].some((v, i) => m[i + 1] !== v)) continue
    let addr = 0
    for (let k = 9; k < 15; k++) addr = addr * 16 + (m[k] & 15)
    for (let k = 15; k < m.length - 3; k += 3) {
      const value = (m[k] << 14) | (m[k + 1] << 7) | m[k + 2], offset = addr + 2 * ((k - 15) / 3)
      if (offset + 2 > flash.length) bad('Monomachine flash address exceeds bounds')
      flash[offset] = value >> 8; flash[offset + 1] = value & 255
    }
    messages++
  }
  if (messages < 8000) bad('Incomplete Monomachine flash')
  const result: Uint8Array[] = [], view = new DataView(flash.buffer)
  at = 0x4000
  for (let k = 0; k < 4; k++) {
    const length = view.getUint32(at), checksum = view.getUint32(at + 4), compressed = flash.subarray(at + 8, at + 8 + length)
    let sum = 0
    for (const v of compressed) sum = (sum + v) >>> 0
    if (compressed.length !== length || sum !== checksum) bad('Monomachine compressed block checksum mismatch')
    result.push(nrv2b(compressed)); at += 8 + length
  }
  return result
}

function records(data: Uint8Array): Map<number, number>[] {
  const word = (at: number) => {
    if (at + 3 > data.length) bad('Truncated Monomachine DSP record')
    return data[at] | (data[at + 1] << 8) | (data[at + 2] << 16)
  }
  const out = [new Map<number, number>(), new Map<number, number>(), new Map<number, number>()]
  let at = data[0] === 3 ? 6 : 0
  if (data[at] === 4) at += 6
  while (at < data.length) {
    const kind = word(at)
    if (kind > 2) break
    const addr = word(at + 3), count = word(at + 6)
    if (at + 9 + 3 * count > data.length) bad('Invalid Monomachine DSP record bounds')
    for (let k = 0; k < count; k++) out[kind].set(addr + k, word(at + 9 + 3 * k))
    at += 9 + 3 * count
  }
  return out
}

const s24 = (v: number) => (v << 8) >> 8
const f32 = Math.fround
const roundEven = (v: number) => { const q = Math.floor(v); return v - q === .5 ? q + (q & 1) : Math.round(v) }

export function monomachineTables(raw: Uint8Array): Map<string, Uint8Array> {
  const decoded = monomachineBlocks(raw), kernel = records(decoded[1]), external = records(decoded[3])
  const [X, Y] = [kernel[1], kernel[2]], ext = new Map([...external[0], ...external[1], ...external[2]])
  const get = (m: Map<number, number>, at: number) => m.get(at) ?? bad('Missing Monomachine DSP table word')
  const words = (at: number, count: number) => Array.from({ length: count }, (_, k) => get(ext, at + k))
  const exp = words(0x140000, 2048), wave = words(0x101d7b, 33 * 256).flatMap(w => [s24(w), s24(w << 12)])
  const interp: number[] = [], interp256: number[] = [], br: number[] = []
  for (let phase = 0; phase < 32; phase++) {
    const c = 0x334 + phase
    const B = [get(Y, c + 0x20), get(X, c + 0x60), get(Y, c + 0x80), get(Y, c + 0xc0), get(Y, c + 0xe0)]
    const A = [get(Y, c), get(Y, c + 0x40), get(Y, c + 0x60), get(Y, c + 0xa0), get(X, c + 0xc0)]
    for (let j = 0; j < 5; j++) interp.push(s24(B[j]), 2 * s24(A[j]))
  }
  for (let q = 0; q < 256; q++) {
    const phase = q >> 3, residual = ((q & 7) + .5) / 8
    for (let j = 0; j < 5; j++) interp256.push(roundEven(interp[10 * phase + 2 * j] + interp[10 * phase + 2 * j + 1] * residual) * 256)
    interp256.push(0, 0, 0)
  }
  for (let k = 0; k < 128; k++) {
    const n = (192 * k) >> 11, frac = (192 * k) & 2047
    br.push(Math.floor(s24(exp[frac]) * 2 ** n / 8192) & 0xffffff,
      Math.floor(s24(exp[2047 - frac]) / 2 ** (8 + n)) & 0xffffff)
  }
  const bank = new Int32Array(32 * 2048), taps = [-1 / 32, 0, 9 / 32, 1 / 2, 9 / 32, 0, -1 / 32]
  for (let w = 0; w < 32; w++) {
    const base = w * 2048
    for (let level = 0; level < 10; level++) {
      const size = 1024 >> Math.max(0, level - 1)
      for (let j = 0; j < size; j++) {
        let total = 0
        for (let tap = 0; tap < 7; tap++) {
          const k = (j + tap - 3) & (size - 1)
          const value = !level ? (!(k & 1) ? f32(2 * f32(wave[w * 512 + (k >> 1)] / 8388608)) : 0)
            : s24(bank[base + 2048 - 2 * size + k] & 0xffff00) / 8388608
          total = f32(total + f32(value * taps[tap]))
        }
        const sample = Math.trunc(Math.max(-32768, Math.min(32767, f32(total * 32768)))), word = (sample & 65535) << 8
        if (!level) bank[base + j] = word
        else if (level < 9 && !(j & 1)) bank[base + 2048 - size + (j >> 1)] = word
        if (level >= 2) {
          const at = base + 2048 - 4 * size + 2 * j
          bank[at] = (bank[at] & 0xffff00) | ((sample >> 8) & 255)
          bank[at + 1] = (bank[at + 1] & 0xffff00) | (sample & 255)
        }
      }
    }
  }
  const partner = new Int32Array(bank.length)
  for (let w = 0; w < 32; w++) for (let shift = 1; shift < 9; shift++) {
    const base = w * 2048, size = 1024 >> shift
    for (let j = 0; j < size; j++) {
      const at = base + 2048 - 4 * size + 2 * j, pair = ((bank[at] & 255) << 8) | (bank[at + 1] & 255)
      partner[base + 2048 - 2 * size + j] = (pair < 32768 ? pair : pair - 65536) * 2048
    }
  }
  const tables: Record<string, ArrayLike<number>> = { exp, wave: [...wave.map(v => v * 8), ...Array(64).fill(0)],
    interp: interp.map(v => v * 256), interp256, pch: words(0x101afb, 128),
    wid: [...words(0x101bfb, 128), ...words(0x101cfb, 128)], br,
    digibank: Array.from(bank, v => s24(v) * 8), partner }
  return new Map(Object.entries(tables).map(([name, values]) => {
    const data = new Uint8Array(values.length * 4), view = new DataView(data.buffer)
    for (let k = 0; k < values.length; k++) view.setUint32(4 * k, values[k] >>> 0)
    return [name, data]
  }))
}
