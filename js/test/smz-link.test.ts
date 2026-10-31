// Synthetic objects exercise relocation behavior without distributing firmware.
import {test} from 'node:test'
import assert from 'node:assert/strict'
import {linkSmzImage, type SmzObject} from '../src/smz-link.ts'

const object=():SmzObject=>({sections:[{name:'',align:1,size:0,data:new Uint8Array()}, {name:'.text',align:4,size:12,data:new Uint8Array(12)}],
  symbols:[{name:'target',value:8,size:0,bind:1,section:1}],
  relocations:[{section:1,offset:0,type:1,symbol:0,addend:0},{section:1,offset:4,type:5,symbol:0,addend:0}]})

test('SMZ links absolute and PC-relative references with runtime relocations',()=>{
 const main=linkSmzImage([object()],0,16)
 const view=new DataView(main.image.buffer)
 assert.equal(view.getUint32(16),24);assert.equal(view.getInt16(20),4)
 assert.deepEqual(main.relocations,[16]);assert.equal(main.symbols.get('target'),24)
 const audio=linkSmzImage([object()],0x4204f920,0,true)
 assert.equal(new DataView(audio.image.buffer).getUint32(0),0x4204f928)
 assert.deepEqual(audio.relocations,[])
})

test('SMZ rejects unresolved and overlapping relocations',()=>{
 const a=object();a.symbols[0].section=0;a.symbols[0].name=''
 assert.throws(()=>linkSmzImage([a],0,0),/unresolved/i)
 const b=object();b.relocations[1].offset=2
 assert.throws(()=>linkSmzImage([b],0,0),/overlapping/i)
 const c=object();c.sections.push({name:'.unexpected',align:4,size:4,data:new Uint8Array(4)})
 assert.throws(()=>linkSmzImage([c],0,0),/Unsupported allocated SMZ section/)
})
