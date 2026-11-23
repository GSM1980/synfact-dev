// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental curated, SHA-pinned SMZ artifact selection.
import {sha, fromHex} from './bytes.ts'
import {checkValidatorPin,validateSmz} from './smz-validator.ts'
import {buildSmzBank, type SmzBankSpec} from './smz-bank.ts'
import {checkDonorSpec, donorRegions, donorUnfill, type DonorFiles} from './smz-donor.ts'

export const CATALOG_SCHEMA=4
// Parsed/restored objects are private and immutable to the linker. Always recheck
// fetched bytes and donor content hashes before reusing one.
const artifactCache=new Map<string,any>()

async function build(catalogBytes:Uint8Array, catalogHash:string, selected:string[],
  fetch:(name:string)=>Promise<Uint8Array>, requireApproval:boolean, donor?:DonorFiles) {
  if(!/^[0-9a-f]{64}$/.test(catalogHash)||sha(catalogBytes)!==catalogHash)throw Error('SMZ catalog hash mismatch')
  const c=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(catalogBytes))
  if((c.schema!==1&&c.schema!==2&&c.schema!==3&&c.schema!==CATALOG_SCHEMA)||c.device!=='syntakt'||c.os!=='1.42'||!Array.isArray(c.modules)||c.modules.length>32||
    c.modules.some((p:any)=>!p||typeof p.id!=='string'||!/^[\w-]+$/.test(p.id)||typeof p.file!=='string'||
      !/^[\w.-]+\.json$/.test(p.file)||!/^[0-9a-f]{64}$/.test(p.sha256))||
    new Set(c.modules.map((p:any)=>p.id)).size!==c.modules.length||new Set(c.modules.map((p:any)=>p.file)).size!==c.modules.length||!Array.isArray(c.approved))
    throw Error('Invalid SMZ catalog')
  if(!Array.isArray(selected)||!selected.length||new Set(selected).size!==selected.length||
    selected.some(id=>typeof id!=='string'||!c.modules.some((p:any)=>p.id===id)))throw Error('Invalid SMZ selection')
  const ordered=c.modules.filter((p:any)=>selected.includes(p.id)).map((p:any)=>p.id)
  const approved=c.approved.filter((p:any)=>Array.isArray(p.ids)&&JSON.stringify(p.ids)===JSON.stringify(ordered)&&/^[0-9a-f]{64}$/.test(p.sha256))
  const runtime=c.schema===CATALOG_SCHEMA
  if(runtime){
    checkValidatorPin(c.validation)
    for(const module of c.modules){
      if(typeof module.always!=='boolean'||!Array.isArray(module.requires)||!Array.isArray(module.conflicts)||
        [...module.requires,...module.conflicts].some((id:unknown)=>typeof id!=='string'||!c.modules.some((p:any)=>p.id===id)))
        throw Error('Invalid SMZ module rules')
      if(module.always&&!ordered.includes(module.id))throw Error('The bank is missing an always-included module')
      if(ordered.includes(module.id)&&(module.requires.some((id:string)=>!ordered.includes(id))||module.conflicts.some((id:string)=>ordered.includes(id))))
        throw Error('The selected mods have unmet requirements or conflicts')
    }
  }else if(requireApproval&&approved.length!==1)throw Error('This SMZ combination has not been validated')
  const files=donor instanceof Uint8Array?[donor]:(donor??[])
  const donorKey=files.map(raw=>sha(raw)).sort().join(',')
  const spec:SmzBankSpec={main:[],audio:[],machines:[],modules:[],samplePool:false,modInfo:[]}
  for(const pin of c.modules.filter((p:any)=>selected.includes(p.id))){
    const raw=await fetch(pin.file)
    if(sha(raw)!==pin.sha256)throw Error(`${pin.file}: SMZ artifact hash mismatch`)
    const cacheKey=pin.sha256+':'+donorKey
    let item=artifactCache.get(cacheKey)
    if(!item){
      item=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(raw))
      if(item.id!==pin.id||typeof item.version!=='string'||!Array.isArray(item.main)||!Array.isArray(item.audio)||!Array.isArray(item.machines)||
        !Array.isArray(item.modules)||typeof item.samplePool!=='boolean')throw Error('Invalid SMZ artifact')
      for(const obj of [...item.main,...item.audio]){
        if(!Array.isArray(obj.sections)||!Array.isArray(obj.symbols)||!Array.isArray(obj.relocations))throw Error('Invalid SMZ object')
        for(const s of obj.sections){if(typeof s.data!=='string'||!/^(?:[0-9a-f]{2})*$/.test(s.data))throw Error('Invalid SMZ section bytes');s.data=fromHex(s.data)}
      }
      // Another instrument's bytes, blanked in the catalog: the owner's own OS file puts them back.
      if(item.donor!==undefined){
        if(c.schema<2||(item.donor.schema===2&&c.schema<3)||!Array.isArray(item.donor.fills))throw Error('Invalid SMZ donor')
        const regions=await donorRegions(checkDonorSpec(item.donor),donor)
        for(const f of item.donor.fills){
          const s=(f?.side==='main'?item.main:f?.side==='audio'?item.audio:[])[f?.object]?.sections?.[f?.section]
          if(!s||!(s.data instanceof Uint8Array))throw Error('Invalid SMZ donor fill')
          s.data=donorUnfill(s.data,f.copies,regions)
        }
      }
      if(artifactCache.size>=32)artifactCache.delete(artifactCache.keys().next().value!)
      artifactCache.set(cacheKey,item)
    }
    if(item.id!==pin.id)throw Error('Invalid SMZ artifact')
    if(item.donor!==undefined&&(c.schema<2||(item.donor.schema===2&&c.schema<3)))throw Error('Invalid SMZ donor')
    spec.main.push(...item.main);spec.audio.push(...item.audio);spec.machines.push(...item.machines);spec.modules.push(...item.modules.filter((m:string)=>!spec.modules.includes(m)))
    spec.modInfo!.push([item.id,item.version])
    spec.samplePool ||= item.samplePool
  }
  const data=buildSmzBank(spec)
  const digest=sha(data)
  if(runtime)await validateSmz(data,c.validation,fetch)
  if(requireApproval&&(!runtime||approved.length)&& (approved.length!==1||digest!==approved[0].sha256))throw Error('SMZ result differs from validated bank')
  return {data,sha256:digest,bytes:data.length,selected:ordered}
}

/** Production path: legacy catalogs require approved output pins; schema 4 requires the independent core parser for every output.
 * `donor` is the owner's OS file a selected module takes bytes from (FM TONE: the Digitone OS 1.44). */
export function buildSmzSelection(catalogBytes:Uint8Array, catalogHash:string, selected:string[], fetch:(name:string)=>Promise<Uint8Array>,
  donor?:DonorFiles) {
  return build(catalogBytes,catalogHash,selected,fetch,true,donor)
}

/** Offline development only: compare and validate the result before adding it to an approved catalog. */
export function buildSmzCandidate(catalogBytes:Uint8Array, catalogHash:string, selected:string[], fetch:(name:string)=>Promise<Uint8Array>,
  donor?:DonorFiles) {
  return build(catalogBytes,catalogHash,selected,fetch,false,donor)
}
