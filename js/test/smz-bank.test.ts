// SPDX-License-Identifier: GPL-3.0-or-later
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { modInfoDirectory } from '../src/smz-bank.ts'

test('the mod-info directory: id and version NUL-padded, a zero record last; refused when one does not fit (smz.py mod_info_directory)', () => {
  const d = modInfoDirectory([['arp-st', '1.1'], ['lfo3-st', '1.0']])
  assert.equal(d.length, 3 * 40)
  assert.deepEqual([...d.subarray(0, 6)], [...new TextEncoder().encode('arp-st')])
  assert.ok(d.subarray(6, 24).every(b => b === 0))
  assert.deepEqual([...d.subarray(24, 27)], [...new TextEncoder().encode('1.1')])
  assert.ok(d.subarray(80).every(b => b === 0))
  assert.equal(modInfoDirectory(Array(48).fill(['a', '1'])).length, 49 * 40)   // core 2.6.1: 16 machine + 32 module folders
  for (const bad of [[['x'.repeat(24), '1']], [['a', '']], [['a', 'vé']], Array(49).fill(['a', '1']), []] as [string, string][][])
    assert.throws(() => modInfoDirectory(bad), /bank|does not fit|lists/)
})
