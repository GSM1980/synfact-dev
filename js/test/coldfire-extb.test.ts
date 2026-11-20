// SPDX-License-Identifier: GPL-3.0-or-later
import test from 'node:test'
import assert from 'node:assert/strict'
import { decodeColdFire, ILLEGAL } from '../src/isa/coldfire.ts'

test('EXTB.L D0..D7 precedes the overlapping LEA mask', () => {
  for (let reg = 0; reg < 8; reg++) {
    const decoded = decodeColdFire(() => 0x49c0 + reg)
    assert.equal(decoded.op, 'extb')
    assert.equal(decoded.length, 2)
    assert.equal(decoded.flags, 0)
  }
})

test('invalid LEA An remains refused beside EXTB.L', () => {
  assert.ok(decodeColdFire(() => 0x49c8).flags & ILLEGAL)
  assert.equal(decodeColdFire(() => 0x49d0).op, 'lea')
})
