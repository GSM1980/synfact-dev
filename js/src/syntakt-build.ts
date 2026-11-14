// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit Syntakt core build. All artifacts are pinned by the caller's pinned manifest.
import {sha} from './bytes.ts'
import {load} from './formats.ts'
import {loadAny} from './patch.ts'
import {link, Mod2} from './link.ts'
import {Syx, write, verify} from './syx.ts'
import {compact} from './codec/compact.ts'
import {buildAudio} from './syntakt-audio.ts'

export type SyntaktFetcher = (file: string) => Promise<Uint8Array>

// Application startup follows native BSS clearing. Appended boot/load storage
// no longer contains executable code then; match the desktop builder's guard.
export function assertSyntaktRuntimeResident(symbols: ReadonlyMap<string, number>, ddr: readonly number[]) {
  for (const name of ['core_modules_bind','core_machine_load','core_native_initialize']) {
    const address=symbols.get(`core:${name}`)
    if (address===undefined || !Number.isInteger(address) || address<ddr[0] || address>=ddr[1])
      throw new Error(`runtime startup entry ${name} is outside resident core RAM`)
  }
}

export async function buildSyntakt(stockBytes: Uint8Array, manifestBytes: Uint8Array, manifestSha256: string,
  fetchFile: SyntaktFetcher, progress: (line: string) => void = () => {}) {
  if (!/^[0-9a-f]{64}$/.test(manifestSha256) || sha(manifestBytes) !== manifestSha256)
    throw new Error('Syntakt manifest hash mismatch')
  const m = JSON.parse(new TextDecoder('utf-8', {fatal:true}).decode(manifestBytes))
  if (m.schema !== 1 || m.device !== 'syntakt' || m.os !== '1.41' || typeof m.meter !== 'boolean' ||
      !Array.isArray(m.mods) || m.mods.length !== 2 || !m.symbols || typeof m.symbols !== 'object' ||
      [m.mainSha256,m.audioSha256,m.outputSha256].some(x=>typeof x!=='string'||!/^[0-9a-f]{64}$/.test(x)))
    throw new Error('Invalid Syntakt manifest')
  const pins = [...m.mods, m.audio, m.upper]
  if (pins.some(p => !p || typeof p.file !== 'string' || !/^[a-zA-Z0-9_.-]+$/.test(p.file) || ['.','..'].includes(p.file) ||
      !/^[0-9a-f]{64}$/.test(p.sha256)) || new Set(pins.map(p=>p.file)).size !== pins.length)
    throw new Error('Invalid Syntakt artifact pins')
  const [stock, dev, rel] = await load(stockBytes)
  if (dev.key !== 'syntakt' || rel.version !== '1.41' || !(stock instanceof Syx)) throw new Error('Requires official Syntakt OS 1.41')
  const raw = await Promise.all(pins.map(async p=>{
    const data=await fetchFile(p.file)
    if (sha(data)!==p.sha256) throw new Error(`${p.file}: artifact hash mismatch`)
    return data
  }))
  const mods=raw.slice(0,2).map((data,i)=>loadAny({path:m.mods[i].file,data}))
  if (mods.some(x=>!(x instanceof Mod2)||x.dev.key!==dev.key||x.rel!==rel) ||
      mods[0].id!=='core' || mods[1].id!=='machines-st' || mods[0].version!==m.coreVersion)
    throw new Error('Incorrect Syntakt core artifacts')
  progress('Linking Syntakt MAIN')
  const linked=link(mods as Mod2[], stock.section(3)), layout=linked.layout
  assertSyntaktRuntimeResident(linked.map,layout.ddr)
  if (sha(linked.image)!==m.mainSha256) throw new Error('Syntakt MAIN does not match reference')
  // Match the explicit builder's check of every native event-table operand.
  for(const name of ['tick','draw','key','enc','settings','render_in','render_out','fs_route','fs_write','fs_written','main_mix']){
    const addr=linked.map.get(`core:core_${name}_table`), target=linked.map.get(`ev_${name}`)
    if (!Number.isInteger(addr) || !Number.isInteger(target) || addr<layout.ddr[0] || addr>=layout.ddr[1]-5)
      throw new Error(`Invalid Syntakt event ${name}`)
    const at=layout.run_load-dev.mainLoad+addr-layout.ddr[0], v=new DataView(linked.image.buffer,linked.image.byteOffset,linked.image.byteLength)
    if(at<0 || at+6>linked.image.length || (v.getUint16(at)&0xf1ff)!==0x41f9 || v.getUint32(at+2)!==target)
      throw new Error(`Invalid Syntakt event operand ${name}`)
  }
  progress('Building Syntakt AUDIO')
  const audio=await buildAudio(stock.section(7),{code:raw[2],upper:raw[3],symbols:m.symbols,meter:m.meter,
    codeSha256:m.audio.sha256,upperSha256:m.upper.sha256})
  if (sha(audio)!==m.audioSha256) throw new Error('Syntakt AUDIO does not match reference')
  progress('Compressing and verifying firmware')
  const secondary=new Map([[7,audio]]), packed=compact(linked.image,1)
  const result=write(stock,packed,dev,null,secondary), facts=verify(result,stock,linked.image,dev,null,secondary)
  if(facts.main.inplace_min_gap<4096)throw new Error('MAIN decompression margin is below 4096 bytes')
  if(sha(result)!==m.outputSha256)throw new Error('Syntakt firmware does not match reference')
  const built=new Syx(result)
  if (sha(built.header)!==sha(stock.header)) throw new Error('Syntakt header changed')
  return {data:result, sha256:sha(result), bytes:result.length, facts, coreVersion:m.coreVersion}
}
