import { test } from 'node:test'
import assert from 'node:assert/strict'
import { createService } from '../src/kit/serve.ts'
import { convert } from '../src/wavetable.ts'

test('worker wavetable conversion matches the SDK converter without catalog or OS access', async () => {
  const handle = createService(async () => { throw Error('must not fetch') })
  const channels = [Float64Array.from({ length: 256 }, (_, i) => ((i * 37) % 127) / 128 - .5)]
  const expected = convert(channels, 'Pad_wt128r')
  const result = await handle({ call: 'convert_wavetable', args: { stem: 'Pad_wt128r', channels: channels.map(c => c.buffer) } }) as { name: string; data: ArrayBuffer; waves: number; notes: string[] }
  assert.equal(result.name, 'Pad')
  assert.equal(result.waves, 2)
  assert.deepEqual(new Uint8Array(result.data), expected.pcm)
  assert.deepEqual(result.notes, expected.notes)
})

test('worker refuses malformed wavetable channels and unsupported wave lengths', async () => {
  const handle = createService(async () => { throw Error('must not fetch') })
  for (const channels of [[], [new ArrayBuffer(7)], [Float64Array.of(NaN).buffer], [new ArrayBuffer(64), new ArrayBuffer(128)]])
    await assert.rejects(handle({ call: 'convert_wavetable', args: { stem: 'Pad', channels } }))
  await assert.rejects(handle({ call: 'convert_wavetable', args: { stem: 'Pad_wt32', channels: [new Float64Array(64).buffer] } }), /wave size 32/)
})
