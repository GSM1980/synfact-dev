// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental SMZ bank construction. The independent core parser must verify
// output before any hardware use. Inputs must originate from audited sources.
import {linkSmzImage, type SmzObject} from './smz-link.ts'

export type SmzMachine = {descriptor:string; ui:string; update:string; render:string}
/** modInfo: [id, version] of each mod folder, for the mod-info directory (feature bit 3, SMZ4.md). */
export type SmzBankSpec = {main:SmzObject[]; audio:SmzObject[]; machines:SmzMachine[]; modules:string[]; samplePool:boolean; modInfo?:[string,string][]}
const align=(n:number,a:number)=>Math.ceil(n/a)*a
const AUDIO=0x4204f750, AUDIO_MAX=0x42100000-AUDIO
export const SMZ_CORE_CONTRACT=0x53594601 // mods/core-st/smz.h
const word=(a:Uint8Array,n:number)=>new DataView(a.buffer,a.byteOffset,a.byteLength).getUint32(n)
const put=(a:Uint8Array,n:number,v:number)=>new DataView(a.buffer,a.byteOffset,a.byteLength).setUint32(n,v)
const adler=(data:Uint8Array)=>{let a=1,b=0;for(const x of data){a=(a+x)%65521;b=(b+a)%65521}return (b*65536+a)>>>0}

const MOD_INFO_MAX=16
/** The bank's mod-info directory: one 40-byte record per mod (id in 24 bytes, version in 16, NUL-padded), then a zero record. Same bytes as smz.py mod_info_directory. */
export function modInfoDirectory(records:[string,string][]):Uint8Array {
 if(!records.length||records.length>MOD_INFO_MAX) throw Error(`a bank lists 1..${MOD_INFO_MAX} mods`)
 const out=new Uint8Array(40*(records.length+1))
 records.forEach(([id,version],i)=>{
  for(const [text,size,at] of [[id,24,0],[version,16,24]] as [string,number,number][]){
   if(!text.length||text.length>=size||![...text].every(c=>c>=' '&&c<'\x7f')) throw Error(`mod ${JSON.stringify(text)} does not fit the bank directory`)
   for(let k=0;k<text.length;k++) out[40*i+at+k]=text.charCodeAt(k)
  }
 })
 return out
}

export function buildSmzBank(spec:SmzBankSpec):Uint8Array {
 const n=spec.machines.length, numModules=spec.modules.length
 if(n>16||numModules>16||(!n&&!numModules)||spec.machines.some(m=>!m.descriptor||!m.ui||!m.update||!m.render))
   throw Error('Invalid SMZ machine/module selection')
 const moduleAt=72*n, main=linkSmzImage(spec.main,0,moduleAt+16*numModules), image=main.image
 const rels=main.relocations.slice(), types:number[]=[]
 function descriptor(name:string,dst:number,length:number){
   const at=main.symbols.get(name)
   if(at===undefined||main.symbolSizes.get(name)!==length||at+length>image.length)throw Error('Invalid SMZ descriptor '+name)
   image.copyWithin(dst,at,at+length)
   for(const r of main.relocations)if(r>=at&&r<at+length)rels.push(dst+r-at)
 }
 spec.machines.forEach((m,i)=>{
   descriptor(m.descriptor,40*i,40)
   const type=word(image,40*i),params=word(image,40*i+4)
   if(type<64||type>=80||types.includes(type)||params<1||params>28)throw Error('Invalid SMZ machine type or parameter count')
   types.push(type)
 })
 spec.machines.forEach((m,i)=>descriptor(m.ui,40*n+32*i,32))
 spec.modules.forEach((name,i)=>descriptor(name,moduleAt+16*i,16))
 const start=AUDIO+400+16*n, audio=spec.audio.length?linkSmzImage(spec.audio,start,0,true):
   {image:new Uint8Array(),symbols:new Map<string,number>()}
 const audioSize=400+16*n+audio.image.length
 if(audioSize>AUDIO_MAX)throw Error('SMZ AUDIO exceeds reserve')
 const section=new Uint8Array(audioSize);section.set([0x53,0x4d,0x41,0x33]);put(section,4,n);put(section,8,audioSize)
 spec.machines.forEach((m,i)=>{
   const update=audio.symbols.get(m.update),render=audio.symbols.get(m.render)
   if(update===undefined||render===undefined||update%2||render%2||update<start||render<start||update>=start+audio.image.length||render>=start+audio.image.length)
     throw Error('Invalid SMZ AUDIO entry')
   const at=16+16*i;put(section,at,types[i]);put(section,at+4,update);put(section,at+8,render)
 })
 section.set(audio.image,400+16*n);put(section,12,adler(section.subarray(16)))
 rels.sort((a,b)=>a-b)
 for(let i=0;i<rels.length;i++){
   const r=rels[i]
   if(r%2||r+4>image.length||(i>0&&r<rels[i-1]+4)||word(image,r)>=image.length)
     throw Error(`Invalid SMZ runtime relocation at ${r.toString(16)} target ${r+4<=image.length?word(image,r).toString(16):'outside'}`)
 }
 const pad=spec.modInfo?(4-image.length%4)%4:0, directory=spec.modInfo?modInfoDirectory(spec.modInfo):new Uint8Array(0)
 const infoAt=image.length+pad, body=new Uint8Array(infoAt+directory.length)
 body.set(image);body.set(directory,infoAt)
 const bodySize=4*rels.length+body.length, audioAt=align(80+bodySize,16)
 if(80+bodySize>0x200000)throw Error('SMZ MAIN exceeds reserve')
 const result=new Uint8Array(audioAt+align(section.length,2048))
 result.set([0x53,0x41,0x46,0x45]);put(result,4,2);put(result,8,0x454c0001);put(result,12,result.length);put(result,20,48)
 put(result,24,(spec.samplePool?1:0)|(n?2:0)|(numModules?4:0)|(spec.modInfo?8:0)|16);put(result,28,n?40*n:0);put(result,32,numModules?moduleAt:0)
 put(result,36,numModules);put(result,44,n);result.set([0x53,0x4d,0x5a,0x34],48)
 put(result,52,body.length);put(result,56,rels.length);if(spec.modInfo)put(result,60,infoAt);put(result,64,SMZ_CORE_CONTRACT);put(result,72,audioAt-48);put(result,76,section.length)
 rels.forEach((r,i)=>put(result,80+4*i,r));result.set(body,80+4*rels.length)
 let sum=0;for(let i=80;i<80+bodySize;i+=4)sum=(sum+word(result,i))>>>0
 put(result,68,sum);result.set(section,audioAt);put(result,16,adler(result.subarray(20)))
 return result
}
