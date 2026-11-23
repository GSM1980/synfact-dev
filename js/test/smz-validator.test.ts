import {test} from 'node:test'
import assert from 'node:assert/strict'
import {readFileSync} from 'node:fs'
import {resolve,dirname} from 'node:path'
import {sha} from '../src/bytes.ts'
import {checkValidatorPin,validateSmz} from '../src/smz-validator.ts'
import {buildSmzSelection} from '../src/smz-catalog.ts'

const enabled=!!process.env.SYNFACT_VALIDATOR&&!!process.env.SYNFACT_SMZ_REFERENCE
const encode=(x:unknown)=>new TextEncoder().encode(JSON.stringify(x))

test('parser pins reject unknown API, invalid contracts and external file paths',()=>{
 const pin={kind:'core-parser-wasm',api:1,file:'parser.wasm',sha256:'0'.repeat(64),contract:0x53594606,coreVersion:'2.6.1'}
 assert.equal(checkValidatorPin(pin),pin)
 for(const p of [{...pin,api:2},{...pin,file:'../parser.wasm'},{...pin,contract:0},{...pin,sha256:'x'}])
  assert.throws(()=>checkValidatorPin(p),/Invalid core parser pin/)
})

test('WASM and native C parser agree on damaged banks, beyond just checksums',
 {skip:!enabled||!process.env.SYNFACT_SMZ_CORPUS},async()=>{
 const path=process.env.SYNFACT_VALIDATOR!,pin=JSON.parse(readFileSync(path,'utf8'))
 const original=readFileSync(process.env.SYNFACT_SMZ_REFERENCE!),corpus=JSON.parse(readFileSync(process.env.SYNFACT_SMZ_CORPUS!,'utf8'))
 assert.equal(sha(original),corpus.bankSha256)
 const get=async(name:string)=>readFileSync(resolve(dirname(path),name))
 const u=(at:number)=>original.readUInt32BE(at)
 const adler=(b:Uint8Array)=>{let a=1,s=0;for(const x of b){a=(a+x)%65521;s=(s+a)%65521}return (s*65536+a)>>>0}
 for(const entry of corpus.cases){
  let data=Uint8Array.from(original);const view=new DataView(data.buffer)
  for(const [at,value] of entry.words??[])view.setUint32(at,value)
  for(const [at,value] of entry.bytes??[])data[at]=value
  if(entry.repair){
   const end=80+4*u(56)+u(52),audio=48+u(72),size=u(76)
   if(entry.audio)view.setUint32(audio+12,adler(data.subarray(audio+16,audio+size)))
   let sum=0;for(let at=80;at<end;at+=4)sum=(sum+view.getUint32(at))>>>0
   view.setUint32(68,sum);view.setUint32(16,adler(data.subarray(20)))
  }
  if(entry.truncate!==undefined)data=data.subarray(0,entry.truncate)
  if(entry.valid)await validateSmz(data,pin,get)
  else await assert.rejects(validateSmz(data,pin,get),/core rejected|memory bounds/,entry.name)
 }
})

test('the independent parser is pinned, isolated, and checked on every bank', {skip:!enabled},async()=>{
 const path=process.env.SYNFACT_VALIDATOR!,pin=JSON.parse(readFileSync(path,'utf8'))
 const bank=readFileSync(process.env.SYNFACT_SMZ_REFERENCE!),wasm=readFileSync(resolve(dirname(path),pin.file))
 const module=await WebAssembly.compile(wasm)
 assert.deepEqual(WebAssembly.Module.imports(module),[])
 assert.equal(wasm.includes(Buffer.from('C:\\Users')),false)
 await validateSmz(bank,pin,async()=>wasm)
 const wrong=Uint8Array.from(wasm);wrong[wrong.length-1]^=1
 await assert.rejects(validateSmz(bank,pin,async()=>wrong),/parser hash mismatch/)
 await assert.rejects(validateSmz(bank,{...pin,contract:0x53594605},async()=>wasm),/different compatibility contracts/)
 const empty=Uint8Array.of(0,97,115,109,1,0,0,0)
 await assert.rejects(validateSmz(bank,{...pin,sha256:sha(empty)},async()=>empty),/parser exports/)
 const imports=Uint8Array.of(...empty,2,13,1,3,101,110,118,4,104,111,115,116,3,127,0)
 await assert.rejects(validateSmz(bank,{...pin,sha256:sha(imports)},async()=>imports),/must have no imports/)
})

test('runtime catalogs need rules and the actual pinned parser even without output approval pins',
 {skip:!process.env.SYNFACT_RUNTIME_CATALOG||!process.env.DIGITONE_OS||!process.env.DIGITAKT_OS||!process.env.MONOMACHINE_OS},async()=>{
 const path=process.env.SYNFACT_RUNTIME_CATALOG!,c=JSON.parse(readFileSync(path,'utf8'))
 const donors=[process.env.DIGITONE_OS!,process.env.DIGITAKT_OS!,process.env.MONOMACHINE_OS!].map(f=>readFileSync(f))
 const get=async(name:string)=>readFileSync(resolve(dirname(path),name))
 const ids=c.modules.map((p:any)=>p.id),golden=c.approved.find((p:any)=>p.ids.length===ids.length).sha256
 c.approved=[]
 const run=(value:any,selected=ids,fetch=get)=>{const raw=encode(value);return buildSmzSelection(raw,sha(raw),selected,fetch,donors)}
 assert.equal((await run(c)).sha256,golden)
 await assert.rejects(run(c,ids.filter((id:string)=>id!=='mix-fast-st')),/always-included/)
 await assert.rejects(run(c,ids.filter((id:string)=>id!=='lfo3-st')),/requirements or conflicts/)
 const bad=structuredClone(c);bad.modules[0].requires=['missing-mod']
 await assert.rejects(run(bad),/Invalid SMZ module rules/)
 const parser=structuredClone(c);parser.validation.sha256='0'.repeat(64)
 await assert.rejects(run(parser),/parser hash mismatch/)
 await assert.rejects(run(c,ids,async name=>name===c.modules[0].file?Uint8Array.of(42):get(name)),/artifact hash mismatch/)
 const legacy=structuredClone(c);legacy.schema=3
 await assert.rejects(run(legacy),/not been validated/)
 const mismatched=structuredClone(c);mismatched.approved=[{ids,sha256:'0'.repeat(64)}]
 await assert.rejects(run(mismatched),/differs from validated bank/)
})
