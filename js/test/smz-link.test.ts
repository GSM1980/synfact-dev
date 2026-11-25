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


test('AUDIO empty labels use GNU suffix order instead of source order',()=>{
 const strings=new TextEncoder().encode('RAMP\0CDEC\0\0')
 const obj:SmzObject={
   sections:[{name:'',align:1,size:0,data:new Uint8Array()},
     {name:'.text',align:4,size:4,data:new Uint8Array(4)},
     {name:'.rodata.str1.1',align:1,size:strings.length,data:strings,flags:0x32}],
   symbols:[{name:'',value:10,size:0,bind:0,section:2}],
   relocations:[{section:1,offset:0,type:1,symbol:0,addend:0}]
 }
 const base=0x4204f920, linked=linkSmzImage([obj],base,0,true)
 // CDEC precedes RAMP in reverse-byte order, so its terminator owns "".
 assert.equal(new DataView(linked.image.buffer).getUint32(0),base+13)
 assert.deepEqual([...linked.image.subarray(4,14)],[...strings.subarray(0,10)])
})

// Two machines that include <syntakt/subtype-picker.h> each define the shared registry
// st_subtype_instances weakly in their own .bss and refer to it through that symbol.
// GNU ld keeps one definition for every reference; each object's own copy would give
// each picker a registry the one SUBTYPE SELECTOR module never reads.
test('SMZ resolves every reference to a weak symbol to the definition the link keeps',()=>{
 const weak=(bind:number):SmzObject=>({sections:[{name:'',align:1,size:0,data:new Uint8Array()},
   {name:'.text',align:4,size:4,data:new Uint8Array(4)},{name:'.bss',align:4,size:16,data:new Uint8Array()}],
   symbols:[{name:'registry',value:0,size:16,bind,section:2}],
   relocations:[{section:1,offset:0,type:1,symbol:0,addend:4}]})
 const two=linkSmzImage([weak(2),weak(2)],0,0), v=new DataView(two.image.buffer), kept=two.symbols.get('registry')!
 assert.equal(kept,12)                                       // the first object's .bss
 assert.equal(v.getUint32(0),kept+4);assert.equal(v.getUint32(4),kept+4)
 assert.deepEqual(two.relocations,[0,4])
 const strong=linkSmzImage([weak(2),weak(1)],0,0), w=new DataView(strong.image.buffer)
 assert.equal(strong.symbols.get('registry'),28)             // the strong one, in the second .bss
 assert.equal(w.getUint32(0),32);assert.equal(w.getUint32(4),32)
})
