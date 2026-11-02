// SPDX-License-Identifier: GPL-3.0-or-later
// Bytes an SMZ module takes from another instrument's OS file (elekloader/sdk/syntakt/donor.py). FM TONE's objects
// come from the catalog with the Digitone's bytes blanked to zero; the visitor's own Digitone OS file puts them back,
// before linking. The regions read are pinned by sha256, and the bank's own pin checks the result.
import {sha} from './bytes.ts'
import {devices} from './devices.ts'
import {load} from './formats.ts'

export type DonorRegion = {name:string; section:number; start:number; length:number; sha256:string}
export type DonorSpec = {title:string; device:string; versions:string[]; minimum:number; regions:DonorRegion[]}
/** [offset in the section, region, offset in the region, length] */
export type DonorCopy = [number, string, number, number]
export type DonorFill = {side:'main'|'audio'; object:number; section:number; copies:DonorCopy[]}

const int=(v:unknown)=>Number.isSafeInteger(v)&&(v as number)>=0

/** Refuse a spec this engine cannot follow exactly. */
export function checkDonorSpec(spec:any):DonorSpec {
  if(!spec||typeof spec.title!=='string'||typeof spec.device!=='string'||!Array.isArray(spec.versions)||
    spec.versions.some((v:unknown)=>typeof v!=='string')||!Array.isArray(spec.regions)||!spec.regions.length||
    spec.regions.some((r:any)=>!r||typeof r.name!=='string'||!int(r.section)||!int(r.start)||!int(r.length)||
      !/^[0-9a-f]{64}$/.test(r.sha256))||new Set(spec.regions.map((r:DonorRegion)=>r.name)).size!==spec.regions.length)
    throw Error('Invalid SMZ donor')
  return spec as DonorSpec
}

/** 'the <device> OS <versions> file' a spec reads. */
export function donorWanted(spec:DonorSpec):string {
  const name=devices().find(d=>d.key===spec.device)?.name??spec.device
  return `the ${name} OS ${spec.versions.join(' or ')} file`
}

/** The spec's regions, read from the owner's OS file and checked. */
export async function donorRegions(spec:DonorSpec, raw:Uint8Array|undefined):Promise<Map<string,Uint8Array>> {
  if(!raw)throw Error(`${spec.title} needs ${donorWanted(spec)}`)
  let parsed, dev, rel
  try{[parsed,dev,rel]=await load(raw)}
  catch(e){throw Error(`${spec.title} needs ${donorWanted(spec)}; this file is not one (${e instanceof Error?e.message:e})`)}
  if(dev.key!==spec.device||!spec.versions.includes(rel.version))
    throw Error(`${spec.title} needs ${donorWanted(spec)}; this file is ${dev.name} ${rel.version}`)
  const out=new Map<string,Uint8Array>()
  for(const r of spec.regions){
    const data=parsed.section(r.section).subarray(r.start,r.start+r.length)
    if(data.length!==r.length||sha(data)!==r.sha256)
      throw Error(`${dev.name} ${rel.version}: section ${r.section} is not the one ${spec.title} was made from`)
    out.set(r.name,data)
  }
  return out
}

/** A blanked section's bytes and its copies -> the section's bytes. */
export function donorUnfill(data:Uint8Array, copies:unknown, regions:Map<string,Uint8Array>):Uint8Array {
  if(!Array.isArray(copies))throw Error('Invalid SMZ donor copies')
  const out=data.slice()
  for(const c of copies){
    const [at,name,offset,n]=Array.isArray(c)&&c.length===4?c:[]
    const src=typeof name==='string'?regions.get(name):undefined
    if(!src||!int(at)||!int(offset)||!int(n)||n<=0||at+n>out.length||offset+n>src.length||out.subarray(at,at+n).some(b=>b))
      throw Error('SMZ donor copy outside its section or region')
    out.set(src.subarray(offset,offset+n),at)
  }
  return out
}
