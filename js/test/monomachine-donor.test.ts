import {test} from 'node:test'
import assert from 'node:assert/strict'
import {readFileSync} from 'node:fs'
import {resolve,dirname} from 'node:path'
import {sha} from '../src/bytes.ts'
import {monomachineTables} from '../src/devices/monomachine.ts'
import {checkDonorSpec,donorRegions,inspectDonor} from '../src/smz-donor.ts'
import {buildSmzSelection} from '../src/smz-catalog.ts'
import {createService} from '../src/kit/serve.ts'

const root=resolve(import.meta.dirname,'../..')
const recipe=JSON.parse(readFileSync(resolve(root,'mods/digimono-st/monomachine.json'),'utf8'))
const encode=(x:unknown)=>new TextEncoder().encode(JSON.stringify(x))
const buffer=(x:Uint8Array)=>x.buffer.slice(x.byteOffset,x.byteOffset+x.byteLength) as ArrayBuffer

test('unknown and unsupported donor transforms are refused',async()=>{
 assert.throws(()=>monomachineTables(Uint8Array.of(1,2,3)),/official Monomachine/)
 for(const invalid of [{...recipe,schema:1},{...recipe,device:'digitone-mk1'},
   {...recipe,regions:[{...recipe.regions[0],transform:'unknown'}]},
   {...recipe,regions:[{...recipe.regions[0],length:0}]}])
   assert.throws(()=>checkDonorSpec(invalid),/Invalid SMZ donor/)
 await assert.rejects(donorRegions(recipe,[]),/Monomachine OS 1.32B/)
})

test('TypeScript reconstructs every pinned Monomachine table from the owner file',
 {skip:!process.env.MONOMACHINE_OS},async()=>{
 const raw=readFileSync(process.env.MONOMACHINE_OS!)
 const tables=monomachineTables(raw)
 assert.equal(tables.size,9)
 for(const region of recipe.regions){
   const data=tables.get(region.name)!
   assert.equal(data.length,region.length)
   assert.equal(sha(data),region.sha256,region.name)
 }
 assert.equal((await inspectDonor(raw)).dev.key,'monomachine')
 const corrupt=Uint8Array.from(raw);corrupt[100]^=1
 await assert.rejects(donorRegions(recipe,corrupt),/official Monomachine/)
 const bad=structuredClone(recipe);bad.regions[0].sha256='0'.repeat(64)
 await assert.rejects(donorRegions(bad,raw),/table hash mismatch/)
 const version=structuredClone(recipe);version.versions=['1.31']
 await assert.rejects(donorRegions(version,raw),/Monomachine OS 1.31/)
})

test('blanked full bank needs all three donor OS files, in any order; worker protocol matches',
 {skip:!process.env.SYNFACT_SMZ_CATALOG||!process.env.MONOMACHINE_OS||!process.env.DIGITAKT_OS||!process.env.DIGITONE_OS},async()=>{
 const path=process.env.SYNFACT_SMZ_CATALOG!,raw=readFileSync(path),catalog=JSON.parse(raw.toString())
 const selected=catalog.modules.map((m:any)=>m.id)
 const files=[process.env.MONOMACHINE_OS!,process.env.DIGITAKT_OS!,process.env.DIGITONE_OS!].map(f=>readFileSync(f))
 const get=async(name:string)=>readFileSync(resolve(dirname(path),name))
 for(let i=0;i<files.length;i++)
   await assert.rejects(buildSmzSelection(raw,sha(raw),selected,get,files.filter((_,j)=>j!==i)),/needs the/)
 const result=await buildSmzSelection(raw,sha(raw),selected,get,files)
 assert.equal(result.sha256,catalog.approved.find((p:any)=>p.ids.length===selected.length).sha256)
 for(const slug of ['digimono-st','comp-st']){
   const pin=catalog.modules.find((m:any)=>m.id===slug),item=JSON.parse((await get(pin.file)).toString())
   const names=slug==='digimono-st'?recipe.symbol_copies:JSON.parse(readFileSync(resolve(root,'mods/comp-st/digitakt.json'),'utf8')).symbol_copies
   const seen=new Set<string>()
   for(const object of [...item.main,...item.audio])for(const symbol of object.symbols){
     if(!(symbol.name in names))continue
     const data=object.sections[symbol.section].data
     assert.match(data.slice(2*symbol.value,2*(symbol.value+symbol.size)),/^0+$/,symbol.name)
     seen.add(symbol.name)
   }
   assert.equal(seen.size,Object.keys(names).length)
 }
 const base='https://local.invalid/artifacts/'
 const service=createService(async url=>url===base+'catalog.json'?encode({schema:1,kind:'elekloader-catalog',revision:'0'.repeat(40),cores:[],mods:[]}):url===base+'smz.json'?raw:get(url.slice(base.length)))
 const ready=await service({call:'init',args:{base}}) as {protocol:number}
 assert.equal(ready.protocol,5)
 await assert.rejects(service({call:'build_smz',args:{catalog:'smz.json',sha256:sha(raw),selected,donors:['bad']}}),/Invalid SMZ donor files/)
 await assert.rejects(service({call:'inspect_donor'}),/needs OS file bytes/)
 const info=await service({call:'inspect_donor',data:buffer(files[0])}) as {dev:{key:string}}
 assert.equal(info.dev.key,'monomachine')
 const built=await service({call:'build_smz',args:{catalog:'smz.json',sha256:sha(raw),selected,donors:files.map(buffer)}}) as {sha256:string}
 assert.equal(built.sha256,result.sha256)
})
