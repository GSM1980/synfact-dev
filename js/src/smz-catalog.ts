// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental curated, SHA-pinned SMZ artifact selection.
import {sha, fromHex} from './bytes.ts'
import {buildSmzBank, type SmzBankSpec} from './smz-bank.ts'

async function build(catalogBytes:Uint8Array, catalogHash:string, selected:string[],
  fetch:(name:string)=>Promise<Uint8Array>, requireApproval:boolean) {
  if(!/^[0-9a-f]{64}$/.test(catalogHash)||sha(catalogBytes)!==catalogHash)throw Error('SMZ catalog hash mismatch')
  const c=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(catalogBytes))
  if(c.schema!==1||c.device!=='syntakt'||c.os!=='1.41'||!Array.isArray(c.modules)||c.modules.length>32||
    c.modules.some((p:any)=>!p||typeof p.id!=='string'||!/^[\w-]+$/.test(p.id)||typeof p.file!=='string'||
      !/^[\w.-]+\.json$/.test(p.file)||!/^[0-9a-f]{64}$/.test(p.sha256))||
    new Set(c.modules.map((p:any)=>p.id)).size!==c.modules.length||new Set(c.modules.map((p:any)=>p.file)).size!==c.modules.length||!Array.isArray(c.approved))
    throw Error('Invalid SMZ catalog')
  if(!Array.isArray(selected)||!selected.length||new Set(selected).size!==selected.length||
    selected.some(id=>typeof id!=='string'||!c.modules.some((p:any)=>p.id===id)))throw Error('Invalid SMZ selection')
  const ordered=c.modules.filter((p:any)=>selected.includes(p.id)).map((p:any)=>p.id)
  const approved=c.approved.filter((p:any)=>Array.isArray(p.ids)&&JSON.stringify(p.ids)===JSON.stringify(ordered)&&/^[0-9a-f]{64}$/.test(p.sha256))
  if(requireApproval&&approved.length!==1)throw Error('This SMZ combination has not been validated')
  const spec:SmzBankSpec={main:[],audio:[],machines:[],modules:[],samplePool:false}
  for(const pin of c.modules.filter((p:any)=>selected.includes(p.id))){
    const raw=await fetch(pin.file)
    if(sha(raw)!==pin.sha256)throw Error(`${pin.file}: SMZ artifact hash mismatch`)
    const item=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(raw))
    if(item.id!==pin.id||!Array.isArray(item.main)||!Array.isArray(item.audio)||!Array.isArray(item.machines)||
      !Array.isArray(item.modules)||typeof item.samplePool!=='boolean')throw Error('Invalid SMZ artifact')
    for(const obj of [...item.main,...item.audio]){
      if(!Array.isArray(obj.sections)||!Array.isArray(obj.symbols)||!Array.isArray(obj.relocations))throw Error('Invalid SMZ object')
      for(const s of obj.sections){if(typeof s.data!=='string'||!/^(?:[0-9a-f]{2})*$/.test(s.data))throw Error('Invalid SMZ section bytes');s.data=fromHex(s.data)}
    }
    spec.main.push(...item.main);spec.audio.push(...item.audio);spec.machines.push(...item.machines);spec.modules.push(...item.modules)
    spec.samplePool ||= item.samplePool
  }
  const data=buildSmzBank(spec)
  const digest=sha(data)
  if(requireApproval&&digest!==approved[0].sha256)throw Error('SMZ result differs from validated bank')
  return {data,sha256:digest,bytes:data.length,selected:ordered}
}

/** Production path: only a combination independently validated and pinned in the catalog can be returned. */
export function buildSmzSelection(catalogBytes:Uint8Array, catalogHash:string, selected:string[], fetch:(name:string)=>Promise<Uint8Array>) {
  return build(catalogBytes,catalogHash,selected,fetch,true)
}

/** Offline development only: compare and validate the result before adding it to an approved catalog. */
export function buildSmzCandidate(catalogBytes:Uint8Array, catalogHash:string, selected:string[], fetch:(name:string)=>Promise<Uint8Array>) {
  return build(catalogBytes,catalogHash,selected,fetch,false)
}
