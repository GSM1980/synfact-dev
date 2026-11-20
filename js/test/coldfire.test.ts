// SPDX-License-Identifier: GPL-3.0-or-later
// The ColdFire decoder (src/isa/coldfire.ts) as isa/coldfire.py decodes it.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { decodeColdFire, ILLEGAL } from '../src/isa/coldfire.ts'

const one = (...words: number[]) => decodeColdFire((o) => words[o >> 1] ?? 0, 0)

test('extb.l Dn is not lea Dn,A4 (tests/test_units.py test_the_decoder_tells_extb_from_lea)', () => {
  for (let n = 0; n < 8; n++) {
    const i = one(0x49c0 + n)
    assert.deepEqual([i.op, i.length, i.flags & ILLEGAL], ['extb', 2, 0], String(n))
  }
  assert.equal(one(0x49d0).op, 'lea')            // lea (a0),a4
  assert.equal(one(0x41d0).op, 'lea')            // lea (a0),a0
  for (const w of [0x41c0, 0x43c1, 0x4bc2, 0x49c8]) assert.ok(one(w).flags & ILLEGAL, w.toString(16))
})
