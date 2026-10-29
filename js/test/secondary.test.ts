// SPDX-License-Identifier: GPL-3.0-or-later
import {test} from 'node:test'
import assert from 'node:assert/strict'
import {SYNTAKT} from '../src/devices.ts'
import {Syx,packMain,write,verify} from '../src/syx.ts'
import {be32,concat,fromHex,putU32,sha} from '../src/bytes.ts'
import {contentChecksum,encodeSyx} from '../src/codec/transport.ts'

test('explicit secondary image: pins, size, windows, default denial and protected sections',()=>{
  const main=new TextEncoder().encode('synthetic main '.repeat(30)),packed=packMain(main),audio=new Uint8Array(64).fill(0xa5)
  const head=new Uint8Array(0x80);head.set(new TextEncoder().encode('ELE3'));head.set(new TextEncoder().encode('1.00'),0x14)
  putU32(head,0x1c,3)
  const parts=[head];let off=head.length
  for(const [row,sid,data,dest] of [[0,2,new TextEncoder().encode('protected boot!!'),0x4000000],
    [1,3,packed,SYNTAKT.mainLoad],[2,7,audio,0]] as [number,number,Uint8Array,number][]) {
    head.set(be32(sid,off,data.length,dest),0x20+16*row);parts.push(data);off+=data.length
    const pad=new Uint8Array((16-off%16)%16);parts.push(pad);off+=pad.length
  }
  const cont=concat(parts),frame=concat([fromHex('f000203c16007f010500017200000000f7')!.subarray(0,15),Uint8Array.of(0xf7)]),end=frame.slice();end[7]=2
  const stock=new Syx(encodeSyx(concat([be32(cont.length,contentChecksum(cont)),cont]),0x16,frame,end))
  const dev={...SYNTAKT,terminalZeroPacket:false,secondary:{7:[sha(audio),64,0x40000400,[[0x40000410,0x40000420]]] as [string,number,number,[number,number][]]}}
  const changed=audio.slice();changed[16]=1
  const out=write(stock,packed,dev,null,new Map([[7,changed]]))
  const facts=verify(out,stock,main,dev,null,new Map([[7,changed]]))
  assert.equal(facts.sections.get('2')!.stock,true);assert.equal(facts.sections.get('7')!.stock,false)
  assert.throws(()=>verify(out,stock,main,dev),/section 7 is not stock/)
  const wrong=changed.slice();wrong[17]=2
  assert.throws(()=>verify(out,stock,main,dev,null,new Map([[7,wrong]])),/requested secondary/)
  const outside=changed.slice();outside[32]=1
  assert.throws(()=>write(stock,packed,dev,null,new Map([[7,outside]])),/outside secondary patch ranges/)
  for(const data of [changed.subarray(1),concat([changed,new Uint8Array(1)])])
    assert.throws(()=>write(stock,packed,dev,null,new Map([[7,data]])),/length or stock hash/)
  const badPin={...dev,secondary:{7:['0'.repeat(64),64,0x40000400,[]] as [string,number,number,[number,number][]]}}
  assert.throws(()=>write(stock,packed,badPin,null,new Map([[7,changed]])),/length or stock hash/)
  for(const sid of [2,4,6,3,8])assert.throws(()=>write(stock,packed,dev,null,new Map([[sid,audio]])),/not an allowed secondary/)
})
