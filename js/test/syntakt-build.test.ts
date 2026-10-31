import {test} from 'node:test'
import assert from 'node:assert/strict'
import {compact} from '../src/codec/compact.ts'
import {depackSection} from '../src/codec/elz.ts'
import {buildSyntakt} from '../src/syntakt-build.ts'
import {sha} from '../src/bytes.ts'
import {readFileSync} from 'node:fs'
import {join} from 'node:path'

test('compact streams round-trip different data and compression levels', () => {
  let seed=123456789
  const random=new Uint8Array(9000)
  for(let i=0;i<random.length;i++){seed=(Math.imul(seed,1664525)+1013904223)>>>0;random[i]=seed>>>24}
  const far=new Uint8Array(18000);far.set(random);far.set(random,9000)
  for(const data of [Uint8Array.of(42),new Uint8Array(4096),random,far,new TextEncoder().encode('abcdefabcdef123456'.repeat(180))])
    for(let level=0;level<=3;level++)assert.deepEqual(depackSection(compact(data,level)),data)
})

test('Syntakt builder rejects unpinned and malformed manifests before fetching', async () => {
  const data=new TextEncoder().encode('{}'), fetch=async()=>{throw Error('must not fetch')}
  await assert.rejects(buildSyntakt(new Uint8Array(),data,'0'.repeat(64),fetch),/manifest hash mismatch/)
  await assert.rejects(buildSyntakt(new Uint8Array(),data,sha(data),fetch),/Invalid Syntakt manifest/)
})

test('Syntakt core matches an independent desktop reference',
  {skip: !process.env.SYNTAKT_STOCK || !process.env.SYNTAKT_ARTIFACTS || !process.env.SYNTAKT_REFERENCE}, async () => {
    const stock=readFileSync(process.env.SYNTAKT_STOCK!)
    const dir=process.env.SYNTAKT_ARTIFACTS!, manifest=readFileSync(join(dir,'syntakt.json'))
    const result=await buildSyntakt(stock,manifest,sha(manifest),async file=>readFileSync(join(dir,file)))
    assert.deepEqual(result.data,new Uint8Array(readFileSync(process.env.SYNTAKT_REFERENCE!)))
  })
