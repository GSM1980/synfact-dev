// SPDX-License-Identifier: GPL-3.0-or-later
// Ports elekloader/sdk/syntakt/wavetable.py: the canonical wavetable of core 2.8 (wavetable.h,
// docs/SP-WAVETBL.md section 4) and the converter that writes it. For the same channels and name
// it gives the same bytes and messages as the Python: every float operation is in the same order
// and uses only +, -, *, / and sqrt (Math.cos and Math.sin are not correctly rounded).

export const WAVETABLE = 2       // HZFS entry flag; with STEREO (1) also set, the entry is stereo
export const WAVE = 2048         // samples of a level 0 wave
export const LEVELS = 9          // 2048, 1024, ..., 8
export const FRAMES = 4088       // samples of one wave over all levels
export const MAX_WAVES = 64
export const POOL_KIND = 0x81    // the pool's per-slot kind byte for a mono wavetable
export const SIZES = [64, 128, 256, 512, 1024, 2048, 4096]
export const FOLDER = 'wavetables'
const SUFFIX = /_wt([0-9]+)(r?)$/i

/** Waves of a table with these entry flags and frames (16-bit samples), or 0. */
export function waves(flags: number, frames: number): number {
  if ((flags & 3) !== WAVETABLE || !frames || frames % FRAMES) return 0
  const w = Math.floor(frames / FRAMES)
  return w <= MAX_WAVES ? w : 0
}

/** First sample of a wave at a level, from the start of the PCM. */
export const offset = (count: number, level: number, wave: number) =>
  count * (2 * WAVE - ((2 * WAVE) >> level)) + wave * (WAVE >> level)

/** A file name without extension -> [stored name, wave size, raw]. */
export function parseName(stem: string): [string, number, boolean] {
  const m = SUFFIX.exec(stem)
  if (!m) return [stem, WAVE, false]
  const size = parseInt(m[1], 10)
  if (!SIZES.includes(size)) throw Error(`wave size ${size} in "${stem}": use one of ${SIZES.join(', ')}`)
  return [stem.slice(0, m.index) || stem, size, m[2] !== '']
}

export const hasSuffix = (stem: string) => SUFFIX.test(stem)

/** cos and sin of 2 pi / n for n = 2, 4, ..., 4096, by half angles. */
const ROOTS = new Map<number, [number, number]>([[2, [-1, 0]], [4, [0, 1]]])
{
  let c = 0, s = 1, n = 4
  while (n < 4096) {
    const c2 = Math.sqrt(0.5 + 0.5 * c)
    s = s / (2 * c2)
    c = c2; n = 2 * n
    ROOTS.set(n, [c, s])
  }
}

/** In-place radix-2 DFT, X[k] = sum x[n] e^(-2 pi i k n / N), N a power of two <= 4096. */
export function fft(re: Float64Array, im: Float64Array) {
  const n = re.length
  let j = 0
  for (let i = 1; i < n; i++) {
    let bit = n >> 1
    while (j & bit) { j ^= bit; bit >>= 1 }
    j |= bit
    if (i < j) {
      let t = re[i]; re[i] = re[j]; re[j] = t
      t = im[i]; im[i] = im[j]; im[j] = t
    }
  }
  for (let m = 2; m <= n; m <<= 1) {
    const [wc, s] = ROOTS.get(m)!
    const ws = -s
    const half = m >> 1
    for (let start = 0; start < n; start += m) {
      let ur = 1, ui = 0
      for (let k = 0; k < half; k++) {
        const a = start + k, b = start + k + half
        const tr = ur * re[b] - ui * im[b]
        const ti = ur * im[b] + ui * re[b]
        re[b] = re[a] - tr
        im[b] = im[a] - ti
        re[a] = re[a] + tr
        im[a] = im[a] + ti
        const nr = ur * wc - ui * ws
        ui = ur * ws + ui * wc
        ur = nr
      }
    }
  }
}

function levels(input: ArrayLike<number>, raw: boolean): Float64Array[] {
  let size = input.length
  let wave: ArrayLike<number> = input
  if (raw && size < WAVE) {
    const step = WAVE / size, up = new Float64Array(WAVE)
    for (let i = 0; i < WAVE; i++) up[i] = input[Math.floor(i / step)]
    wave = up; size = WAVE
  }
  const re = Float64Array.from(wave), im = new Float64Array(size)
  fft(re, im)
  const top = size / 2 - 1
  const out: Float64Array[] = []
  for (let level = 0; level < LEVELS; level++) {
    const n = WAVE >> level, h = Math.min(top, n / 2 - 1)
    const yr = new Float64Array(n), yi = new Float64Array(n)
    for (let k = 1; k <= h; k++) {
      yr[k] = re[k]; yi[k] = -im[k]
      yr[n - k] = re[k]; yi[n - k] = im[k]
    }
    fft(yr, yi)
    for (let i = 0; i < n; i++) yr[i] = yr[i] / size
    out.push(yr)
  }
  return out
}

/** Audio channels (floats in [-1, 1), as the Python's read_wav_channels gives them) and the file
 * name without extension -> the stored name, the PCM16BE body and notes. The rate is not used. */
export function convert(channels: ArrayLike<number>[], stem: string): { name: string, pcm: Uint8Array, notes: string[] } {
  const [name, size, raw] = parseName(stem)
  let mono: ArrayLike<number>
  if (channels.length === 1) mono = channels[0]
  else {
    const n = Math.min(...channels.map(c => c.length)), m = new Float64Array(n)
    for (let i = 0; i < n; i++) {
      let sum = 0
      for (const c of channels) sum += c[i]
      m[i] = sum / channels.length
    }
    mono = m
  }
  const count = Math.floor(mono.length / size), notes: string[] = []
  if (!count) throw Error(`${mono.length} samples: less than one wave of ${size}`)
  if (mono.length % size) notes.push(`${mono.length % size} samples after the last whole wave left out`)
  let picks = Array.from({ length: count }, (_, i) => i)
  if (count > MAX_WAVES) {
    picks = Array.from({ length: MAX_WAVES }, (_, i) => Math.floor((i * (count - 1) + 31) / 63))
    notes.push(`${count} waves: ${MAX_WAVES} evenly spaced kept`)
  }
  const slice = (p: number) => Array.prototype.slice.call(mono, p * size, (p + 1) * size) as number[]
  const tables = picks.map(p => levels(slice(p), raw))
  let peak = 0
  for (const t of tables) for (const level of t) for (const v of level) {
    const a = v < 0 ? -v : v
    if (a > peak) peak = a
  }
  const gain = peak > 0 ? 32767 / peak : 0
  const pcm = new Uint8Array(2 * FRAMES * tables.length), view = new DataView(pcm.buffer)
  let at = 0
  for (let level = 0; level < LEVELS; level++) for (const t of tables) for (const v of t[level]) {
    const x = Math.floor(v * gain + 0.5)
    view.setInt16(at, x < -32768 ? -32768 : x > 32767 ? 32767 : x); at += 2
  }
  return { name, pcm, notes }
}

/** The level 0 waves of a canonical body's PCM, one after the other. */
export const level0 = (pcm: Uint8Array, count: number) => pcm.subarray(0, 2 * WAVE * count)
