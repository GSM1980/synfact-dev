import {test} from 'node:test'
import assert from 'node:assert/strict'
import {sha} from '../src/bytes.ts'
import {buildSmzCandidate,buildSmzSelection} from '../src/smz-catalog.ts'
const encode=(v:unknown)=>new TextEncoder().encode(JSON.stringify(v))
const artifact=encode({id:'module',samplePool:false,machines:[],modules:['entry'],audio:[],main:[{
 sections:[{name:'',size:0,align:1,data:''},{name:'.data',size:16,align:4,data:'00000000000000000000000000000000'}],
 symbols:[{name:'entry',value:0,size:16,bind:1,section:1}],relocations:[]}]})
const cat=()=>({schema:1,device:'syntakt',os:'1.41',modules:[{id:'module',file:'module.json',sha256:sha(artifact)}],approved:[] as any[]})
const get=async()=>artifact

test('SMZ selection requires a pinned, approved result and rejects changes',async()=>{
 const c=cat(),raw=encode(c),candidate=await buildSmzCandidate(raw,sha(raw),['module'],get)
 await assert.rejects(buildSmzSelection(raw,sha(raw),['module'],get),/not been validated/)
 c.approved.push({ids:['module'],sha256:candidate.sha256})
 const pinned=encode(c),result=await buildSmzSelection(pinned,sha(pinned),['module'],get)
 assert.deepEqual(result.data,candidate.data)
 await assert.rejects(buildSmzSelection(pinned,'0'.repeat(64),['module'],get),/catalog hash mismatch/)
 await assert.rejects(buildSmzSelection(pinned,sha(pinned),['other'],get),/Invalid SMZ selection/)
 await assert.rejects(buildSmzSelection(pinned,sha(pinned),[],get),/Invalid SMZ selection/)
 await assert.rejects(buildSmzSelection(pinned,sha(pinned),['module'],async()=>Uint8Array.of(42)),/artifact hash mismatch/)
 c.approved[0].sha256='0'.repeat(64);const bad=encode(c)
 await assert.rejects(buildSmzSelection(bad,sha(bad),['module'],get),/differs from validated bank/)
})
