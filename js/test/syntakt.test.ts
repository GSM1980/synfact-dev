// SPDX-License-Identifier: GPL-3.0-or-later
import {test} from 'node:test'
import assert from 'node:assert/strict'
import {SYNTAKT, identify} from '../src/devices.ts'
import {Syx, packMain, write, verify} from '../src/syx.ts'
import {be32, concat, fromHex, putU32} from '../src/bytes.ts'
import {contentChecksum, encodeSyx} from '../src/codec/transport.ts'

test('Syntakt profile identifies pinned stock and reserves only its 64 KiB area', () => {
  const [d,r] = identify('8e2488f462c4a5656396a895f113bcd415e9900fa8709340dccf45d4cb9ed19e')
  assert.equal(d,SYNTAKT)
  assert.equal(r.version,'1.41')
  assert.deepEqual(d.ddr,[0x46000000,0x46010000])
  assert.equal(d.flashLimit-d.flashAt,2028880)
  assert.equal(d.versionOptional,true)
  assert.deepEqual(d.secondary[7][3],[[0x4000094a,0x40000950],[0x40000b04,0x40000b0a],
    [0x40001016,0x40001020],[0x400043b2,0x400043b6],[0x400043fe,0x40004400],
    [0x40004404,0x40004406],[0x40004408,0x4000440c],[0x40004460,0x40004462],
    [0x400044ac,0x400044b0],[0x40040860,0x40041050]])
})

test('Syntakt terminal packet: exact boundary, partial boundary and invalid padding', () => {
  const body = new TextEncoder().encode('synthetic UI image '.repeat(40))
  const packed = packMain(body)
  const start = concat([fromHex('f000203c16007f010500017200000000f7')!.subarray(0,15),Uint8Array.of(0xf7)])
  const end = start.slice(); end[7]=2
  for(const extra of [0,16]) {
    const cont = new Uint8Array(800+extra)
    cont.set(new TextEncoder().encode('ELE3'))
    cont.set(new TextEncoder().encode('1.00'),0x14)
    putU32(cont,0x1c,2)
    const meta = cont.length-0x80-Math.ceil(packed.length/16)*16
    for(const [row,id,off,len,dest] of [[0,5,0x80,meta,0],[1,3,0x80+meta,packed.length,SYNTAKT.mainLoad]]) {
      cont.set(be32(id,off,len,dest),0x20+row*16)
    }
    cont.fill(0xaa,0x80,0x80+meta); cont.set(packed,0x80+meta)
    const stream=concat([be32(cont.length,contentChecksum(cont)),cont])
    const tail=new Uint8Array(101-stream.length%101)
    const raw=encodeSyx(concat([stream,tail]),0x16,start,end), stock=new Syx(raw)
    assert.deepEqual(write(stock,packed,SYNTAKT),raw)
    verify(raw,stock,body,SYNTAKT)
    const nonzero=tail.slice(); nonzero[0]=1
    for(const bad of [nonzero,concat([tail,new Uint8Array(101)])])
      assert.throws(()=>verify(encodeSyx(concat([stream,bad]),0x16,start,end),stock,body,SYNTAKT),/after the container/)
    if(extra===0) {
      const missing=encodeSyx(stream,0x16,start,end)
      assert.throws(()=>verify(missing,stock,body,SYNTAKT),/after the container/)
      assert.deepEqual(write(stock,packed,{...SYNTAKT,terminalZeroPacket:false}),missing)
    }
    assert.throws(()=>verify(raw,stock,body,{...SYNTAKT,flashLimit:SYNTAKT.flashAt+cont.length-1}),/ends at flash/)
  }
})
