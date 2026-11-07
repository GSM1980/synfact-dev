import {test} from 'node:test'
import assert from 'node:assert/strict'
import {sha} from '../src/bytes.ts'
import {buildSmzCandidate} from '../src/smz-catalog.ts'
import {checkDonorSpec, donorRegions, donorUnfill} from '../src/smz-donor.ts'

const encode=(v:unknown)=>new TextEncoder().encode(JSON.stringify(v))
const region={name:'audio',section:7,start:0,length:16,sha256:'0'.repeat(64)}
const spec={title:'FM TONE',device:'digitone-mk1',versions:['1.44'],minimum:4,regions:[region]}

test('a donor copy puts back only blanked bytes, inside its section and region',()=>{
 const regions=new Map([['audio',Uint8Array.from([1,2,3,4,5,6,7,8])]])
 const blanked=Uint8Array.from([9,0,0,0,0,9])
 assert.deepEqual(donorUnfill(blanked,[[1,'audio',2,4]],regions),Uint8Array.from([9,3,4,5,6,9]))
 assert.deepEqual(blanked,Uint8Array.from([9,0,0,0,0,9]))
 for(const bad of [[[0,'audio',0,2]],[[3,'audio',0,4]],[[1,'audio',6,4]],[[1,'main',0,4]],[[1,'audio',0,0]],[[1,'audio',-1,2]],[[1,'audio',0]],'x'])
  assert.throws(()=>donorUnfill(blanked,bad,regions),/SMZ donor/)
})

test('a donor spec is checked, and asks for its own OS file',async()=>{
 assert.equal(checkDonorSpec(spec),spec)
 for(const bad of [null,{...spec,regions:[]},{...spec,versions:[1]},{...spec,regions:[{...region,sha256:'x'}]},
   {...spec,regions:[region,region]},{...spec,regions:[{...region,start:-1}]}])
  assert.throws(()=>checkDonorSpec(bad),/Invalid SMZ donor/)
 await assert.rejects(donorRegions(spec,undefined),/FM TONE needs the Digitone mk1 OS 1\.44 file$/)
 await assert.rejects(donorRegions(spec,Uint8Array.of(1,2,3)),/FM TONE needs the Digitone mk1 OS 1\.44 file; this file is not one/)
})

test('an SMZ artifact with donor bytes needs the donor file, and schema 2',async()=>{
 const artifact=encode({id:'fm',version:'1.0',samplePool:false,machines:[],modules:['entry'],audio:[],main:[{
  sections:[{name:'',size:0,align:1,data:''},{name:'.data',size:16,align:4,data:'00'.repeat(16)}],
  symbols:[{name:'entry',value:0,size:16,bind:1,section:1}],relocations:[]}],
  donor:{...spec,fills:[{side:'main',object:0,section:1,copies:[[0,'audio',0,4]]}]}})
 const catalog=(schema:number)=>encode({schema,device:'syntakt',os:'1.41',approved:[],
  modules:[{id:'fm',file:'fm.json',sha256:sha(artifact),donor:{title:'FM TONE',device:'digitone-mk1',versions:['1.44']}}]})
 const get=async()=>artifact
 const two=catalog(2),one=catalog(1)
 await assert.rejects(buildSmzCandidate(two,sha(two),['fm'],get),/FM TONE needs the Digitone mk1 OS 1\.44 file/)
 await assert.rejects(buildSmzCandidate(one,sha(one),['fm'],get,Uint8Array.of(1)),/Invalid SMZ donor/)
})
