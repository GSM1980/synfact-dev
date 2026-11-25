// SPDX-License-Identifier: GPL-3.0-or-later
// src/wavetable.ts against what the Python converter gave (test/wavetable-vectors.json,
// tools/wavetable_vectors.py): the same bytes, names, notes and messages.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { sha } from '../src/bytes.ts'
import { FRAMES, LEVELS, WAVE, convert, fft, level0, offset, parseName, waves } from '../src/wavetable.ts'
import { lcg } from './helpers.ts'

const V = JSON.parse(readFileSync(new URL('./wavetable-vectors.json', import.meta.url), 'utf8'))

function samples(seed: number, n: number): number[] {
  const raw = lcg(seed, 2 * n), out: number[] = []
  for (let i = 0; i < n; i++) out.push(((raw[2 * i] << 24 >> 16) | raw[2 * i + 1]) / 32768)
  return out
}

test('wavetable converter: the Python bytes, names, notes and messages', () => {
  assert.ok(V.cases.length >= 8)
  for (const c of V.cases) {
    const channels = Array.from({ length: c.channels }, (_, k) => samples(c.seed + 100 * k, c.n))
    if (c.error) {
      assert.throws(() => convert(channels, c.stem), (e: Error) => e.message === c.error, c.stem)
      continue
    }
    const r = convert(channels, c.stem)
    assert.equal(r.name, c.name, c.stem)
    assert.deepEqual(r.notes, c.notes, c.stem)
    assert.equal(r.pcm.length, c.bytes, c.stem)
    assert.equal(sha(r.pcm), c.sha256, c.stem)
    assert.equal(waves(2, r.pcm.length / 2), r.pcm.length / 2 / FRAMES, c.stem)
  }
})

test('wavetable layout and helpers', () => {
  assert.equal(Array.from({ length: LEVELS }, (_, k) => WAVE >> k).reduce((a, b) => a + b), FRAMES)
  assert.deepEqual([waves(2, 4088), waves(2, 4088 * 64), waves(2, 4088 * 65), waves(3, 4088), waves(0, 4088)], [1, 64, 0, 0, 0])
  assert.equal(offset(64, 8, 63) + 8, 64 * FRAMES)
  assert.deepEqual(parseName('pad_wt1024'), ['pad', 1024, false])
  assert.deepEqual(parseName('strings_WT128R'), ['strings', 128, true])
  assert.deepEqual(parseName('plain'), ['plain', 2048, false])
  assert.equal(level0(new Uint8Array(10000), 2).length, 2 * 2 * WAVE)
  // The FFT of a unit impulse is flat.
  const re = new Float64Array(16), im = new Float64Array(16); re[0] = 1; fft(re, im)
  assert.ok(re.every(v => v === 1) && im.every(v => v === 0))
})
