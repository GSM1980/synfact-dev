// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental ColdFire linker for audited, precompiled SMZ objects. GNU ld
// can choose different addresses for identical merged strings; a candidate must
// be independently compared and validated before inclusion in a release catalog.
export type SmzSection = {name:string; align:number; size:number; data:Uint8Array; flags?:number}
export type SmzSymbol = {name:string; value:number; size:number; bind:number; section:number}
export type SmzRelocation = {section:number; offset:number; type:number; symbol:number; addend:number}
export type SmzObject = {sections:SmzSection[]; symbols:SmzSymbol[]; relocations:SmzRelocation[]}
export type SmzImage = {image:Uint8Array; relocations:number[]; symbols:Map<string,number>; symbolSizes:Map<string,number>}

const align=(n:number,a:number)=>Math.ceil(n/a)*a
const U32=0x100000000

/** Link audited, precompiled objects into one image. MAIN is relocatable at base
 * zero and reserves its descriptor directory first. AUDIO is linked at its
 * actual device address. Reject unsupported ELF features rather than guessing. */
export function linkSmzImage(objects:SmzObject[], base:number, directoryBytes=0, audio=false):SmzImage {
  if (!Number.isSafeInteger(base)||base<0||base>=U32||!Number.isInteger(directoryBytes)||directoryBytes<0||directoryBytes%4)
    throw Error('Invalid SMZ image origin or directory')
  const placements=objects.map(()=>new Map<number,number>())
  const stringMaps=objects.map(()=>new Map<number,Map<number,{key:string;delta:number}>>())
  const strings=new Map<string,{bytes:Uint8Array;owner:number;section:number}>()
  // GNU ld folds SHF_MERGE|SHF_STRINGS, including suffixes, globally.
  for(let oi=0;oi<objects.length;oi++)for(let si=0;si<objects[oi].sections.length;si++){
    const s=objects[oi].sections[si];if(!s.name||(s.flags!&0x30)!==0x30)continue
    if(s.align!==1||!s.name.startsWith('.rodata')||s.data.length!==s.size)throw Error('Unsupported SMZ merge section')
    let start=0
    for(let i=0;i<s.size;i++)if(s.data[i]===0){const bytes=s.data.slice(start,i+1),key=Array.from(bytes,b=>String.fromCharCode(b)).join('')
      if(!strings.has(key))strings.set(key,{bytes,owner:oi,section:si});start=i+1}
    if(start!==s.size)throw Error('Unterminated SMZ string')
  }
  const canonical=new Map<string,string>()
  for(const key of strings.keys()){
    let best=key
    for(const candidate of strings.keys())if(candidate.length>key.length&&candidate.endsWith(key)&&
      ![...strings.keys()].some(other=>other.length>candidate.length&&other.endsWith(candidate))){best=candidate;break}
    canonical.set(key,best)
  }
  const stringOffsets=new Map<string,number>()
  const compacted=objects.map(()=>new Map<number,Uint8Array>())
  for(let oi=0;oi<objects.length;oi++)for(let si=0;si<objects[oi].sections.length;si++){
    const s=objects[oi].sections[si];if(!s.name||(s.flags!&0x30)!==0x30)continue
    const chunks:number[]=[], map=new Map<number,{key:string;delta:number}>();let start=0
    for(let i=0;i<s.size;i++)if(s.data[i]===0){const key=Array.from(s.data.subarray(start,i+1),b=>String.fromCharCode(b)).join(''), root=canonical.get(key)!
      for(let j=start;j<=i;j++)map.set(j,{key:root,delta:root.length-key.length+j-start})
      const entry=strings.get(key)!
      if(key===root&&entry.owner===oi&&entry.section===si&&!stringOffsets.has(key)){
        stringOffsets.set(key,-1);chunks.push(...entry.bytes)
      }
      start=i+1
    }
    stringMaps[oi].set(si,map);compacted[oi].set(si,Uint8Array.from(chunks))
  }
  stringOffsets.clear()
  const groups=audio?[/^\.text$/,/^\.rodata/,/^\.data/,/^\.bss/]:[/^\.run$/,/^\.text/,/^\.rodata/,/^\.data/,/^\.bss/]
  for(const obj of objects)for(const s of obj.sections)
    if(s.name && !groups.some(g=>g.test(s.name)))throw Error('Unsupported allocated SMZ section '+s.name)
  let size=directoryBytes
  for(let g=0;g<groups.length;g++) {
    if(g===groups.length-1)size=align(size+1,4) // BYTE(0), then . = ALIGN(4)
    for(let oi=0;oi<objects.length;oi++)for(let si=0;si<objects[oi].sections.length;si++){
      const s=objects[oi].sections[si]
      if(!groups[g].test(s.name))continue
      if (!Number.isInteger(s.size)||s.size<0||!Number.isInteger(s.align)||s.align<1||s.align>4096||
          (s.align&(s.align-1))!==0||(!(s.data instanceof Uint8Array))||
          (s.name.startsWith('.bss')?s.data.length!==0:s.data.length!==s.size))throw Error('Invalid SMZ section '+s.name)
      size=align(base+size,s.align)-base
      placements[oi].set(si,size)
      const folded=compacted[oi].get(si)
      if(folded){let at=0;while(at<folded.length){let end=at;while(folded[end]!==0)end++
        const key=Array.from(folded.subarray(at,end+1),b=>String.fromCharCode(b)).join('');stringOffsets.set(key,size+at);at=end+1}}
      size+=folded?folded.length:s.size
      if(size>0x200000)throw Error('SMZ image exceeds allocation')
    }
  }
  size=align(size,audio?16:4)
  if(base+size>U32)throw Error('SMZ address overflow')
  const image=new Uint8Array(size)
  for(let oi=0;oi<objects.length;oi++)for(const [si,at] of placements[oi])image.set(compacted[oi].get(si)??objects[oi].sections[si].data,at)
  const globals=new Map<string,number>(), owners=new Map<string,number>(),symbolSizes=new Map<string,number>()
  const address=(oi:number,sym:SmzSymbol):number|undefined=>{
    if(sym.section===0)return undefined
    if(sym.section===0xfff1)return sym.value
    const at=placements[oi].get(sym.section)
    if(at===undefined){if(sym.section===0xfff2)throw Error('SMZ common symbols are unsupported');return undefined}
    const s=objects[oi].sections[sym.section]
    if(!Number.isInteger(sym.value)||sym.value<0||sym.value>s.size||sym.size<0||sym.value+sym.size>s.size)
      throw Error('SMZ symbol outside section')
    return base+at+sym.value
  }
  for(let oi=0;oi<objects.length;oi++)for(const sym of objects[oi].symbols){
    if(!sym.name||sym.bind===0)continue
    const value=address(oi,sym);if(value===undefined)continue
    if(globals.has(sym.name)){
      if(sym.bind===2)continue
      if(owners.get(sym.name)!==2)throw Error('Duplicate SMZ symbol '+sym.name)
    }
    globals.set(sym.name,value);owners.set(sym.name,sym.bind);symbolSizes.set(sym.name,sym.size)
  }
  const runtime:number[]=[],used=new Set<number>(),view=new DataView(image.buffer)
  for(let oi=0;oi<objects.length;oi++)for(const r of objects[oi].relocations){
    const sectionAt=placements[oi].get(r.section)
    if(sectionAt===undefined)continue
    if(r.type===0)continue
    const width=r.type===1||r.type===4?4:r.type===5?2:0
    const section=objects[oi].sections[r.section], sym=objects[oi].symbols[r.symbol]
    if(!width||!sym||!Number.isInteger(r.offset)||r.offset<0||r.offset+width>section.size||!Number.isInteger(r.addend))
      throw Error('Unsupported or invalid SMZ relocation')
    const at=sectionAt+r.offset
    for(let i=0;i<width;i++){if(used.has(at+i))throw Error('Overlapping SMZ relocations');used.add(at+i)}
    let target=address(oi,sym), local=sym.section!==0 && sym.section!==0xfff1
    if(target===undefined && sym.name){target=globals.get(sym.name);local=target!==undefined && target>=base && target<base+size}
    if(target===undefined){if(sym.bind===2){target=0;local=false}else throw Error('Unresolved SMZ symbol '+sym.name)}
    let value=target+r.addend
    const strmap=stringMaps[oi].get(sym.section)
    if(strmap){const item=strmap.get(sym.value+r.addend),where=item&&stringOffsets.get(item.key)
      if(!item||where===undefined)throw Error('Invalid SMZ string reference')
      value=base+where+item.delta}
    if(r.type===4||r.type===5)value-=base+at
    if(r.type===5){if(value< -32768||value>32767)throw Error('SMZ PC16 relocation out of range');view.setInt16(at,value)}
    else if(r.type===4){if(value< -2147483648||value>2147483647)throw Error('SMZ PC32 relocation out of range');view.setInt32(at,value)}
    else {if(value<0||value>=U32)throw Error('SMZ absolute relocation out of range');view.setUint32(at,value);if(!audio&&local)runtime.push(at)}
  }
  runtime.sort((a,b)=>a-b)
  return {image,relocations:runtime,symbols:globals,symbolSizes}
}
