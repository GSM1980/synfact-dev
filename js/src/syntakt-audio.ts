// Experimental port of elekloader/sdk/syntakt/audio.py's patch stage.
// GPL-2.0-or-later. Requires precompiled, audited core AUDIO code and symbols.
import {sha} from './bytes.ts'
const BASE=0x40000400, CAVE=0x40040860, TABLES=[0x40040e00,0x40040e50,0x40040ec0]
const BW_TABLE=0x40040060, BW_OPEN=508, BW_SITES=[0x40002364,0x400023e8]
const fromHex=s=>Uint8Array.from(s.match(/../g)??[],b=>parseInt(b,16))
const concat=(...parts)=>{const out=new Uint8Array(parts.reduce((n,p)=>n+p.length,0));let at=0;for(const p of parts){out.set(p,at);at+=p.length}return out}
const words=(...values)=>{const out=new Uint8Array(values.length*4),v=new DataView(out.buffer);values.forEach((n,i)=>v.setUint32(4*i,n));return out}
const digest=sha

export async function buildAudio(original,{code,upper,symbols,meter,codeSha256,upperSha256}) {
  if(await digest(original)!=='daf6451cf9587c0b628e901b7bb6b25f4e2633d448c534c0c181dd35ec783bc2'||original.length!==383760)
    throw Error('Unsupported original AUDIO image')
  if(await digest(code)!==codeSha256||await digest(upper)!==upperSha256)throw Error('AUDIO artifact hash mismatch')
  if(CAVE+code.length>TABLES[0]||0x40040f60+upper.length>0x40041050)throw Error('AUDIO artifact exceeds reserved space')
  const image=Uint8Array.from(original)
  const patch=(address,old,next)=>{const off=address-BASE
    if(old.length!==next.length||off<0||off+old.length>original.length||old.some((b,i)=>original[off+i]!==b))
      throw Error('AUDIO preimage differs at 0x'+address.toString(16))
    image.set(next,off)}
  const native=(at,n)=>original.slice(at-BASE,at-BASE+n)
  const symbol=name=>{const value=symbols[name];if(!Number.isInteger(value)||value<CAVE||value>=0x40041050)throw Error('Invalid AUDIO symbol '+name);return value}
  const jsr=name=>concat(Uint8Array.of(0x4e,0xb9),words(symbol(name)))
  patch(CAVE,new Uint8Array(code.length),code)
  if(upper.length)patch(0x40040f60,new Uint8Array(upper.length),upper)
  const silent=words(...Array(16).fill(symbol('audio_machine_silent')))
  const types=concat(native(0x40014950,46),new Uint8Array(18),Uint8Array.from({length:16},(_,i)=>i+12))
  const tables=[types,concat(native(0x40014920,48),silent),concat(native(0x400148f0,48),silent)]
  tables.forEach((data,i)=>patch(TABLES[i],new Uint8Array(data.length),data))
  for(const [at,old,next] of [[0x400043b2,'40014920','40040e50'],[0x400043fe,'702d','704f'],
    [0x40004404,'722d','724f'],[0x40004408,'40014950','40040e00'],[0x40004460,'720b','721b'],
    [0x400044ac,'400148f0','40040ec0']])patch(at,fromHex(old),fromHex(next))
  patch(0x40001016,fromHex('4ebafcee4eb940001860'),concat(jsr('audio_machine_boot'),fromHex('4e714e71')))
  patch(0x40000b04,fromHex('4ab940040050'),jsr('audio_machine_exit'))
  // Base-width filter (audio.py BW_*): the stock poles from index 508 on are
  // zero; both low-pass lookups call audio_bw_index, which bounds the index there.
  const pole=i=>original.slice(BW_TABLE+4*i-BASE,BW_TABLE+4*i+4-BASE)
  if(original.slice(BW_TABLE+4*BW_OPEN-BASE,BW_TABLE+4*1024-BASE).some(b=>b!==0)||
     Array.from({length:BW_OPEN},(_,i)=>pole(i).every(b=>b===0)).some(Boolean))
    throw Error('stock base-width pole table is not 508 poles and a zero tail')
  for(const at of BW_SITES)patch(at,fromHex('d081ec8041f00c00'),concat(jsr('audio_bw_index'),fromHex('4e71')))
  patch(0x40040f40,new Uint8Array(16),words(0x454c0001,symbol('audio_machine_params'),32,24))
  patch(0x40040f50,new Uint8Array(16),words(0x454c5001,symbol('audio_pool_sample'),128,0))
  if(meter)patch(0x4000094a,fromHex('4bf9400010e4'),jsr('audio_enter'))
  return image
}
