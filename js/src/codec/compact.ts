// SPDX-License-Identifier: MIT
// Experimental JavaScript port of ap_pack from elektron-firmware-tool/aplib.c.
// Copyright (c) 2026 Marcel Bierling. MIT; see LICENSE-kompressor.txt.
// No firmware bytes are embedded in this source.
export function compact(data, level = 1) {
  if (!(data instanceof Uint8Array) || !Number.isInteger(level) || level < 0 || level > 3 || !data.length)
    throw Error('Expected non-empty bytes and compression level 0..3')
  const n=data.length, head=new Int32Array(131072).fill(-1), prev=new Int32Array(n),
    cost=new Uint32Array(n+1).fill(0xffffffff), last=new Uint32Array(n+1),
    from=new Int32Array(n+1), offset=new Uint32Array(n+1), length=new Uint32Array(n+1)
  cost[0]=0;last[0]=1;from[0]=-1
  const gamma=v=>2*(31-Math.clz32(v))
  const matchCost=(o,l,lo)=>{const b=l-1-(o>3328?1:0);return 1+(o===lo?2:gamma((o+767)>>>8)+8)+(b<=3?2:2+gamma(b-2))}
  const relax=(pos,c,lo,src,o,l)=>{if(c<cost[pos]){cost[pos]=c;last[pos]=lo;from[pos]=src;offset[pos]=o;length[pos]=l}}
  const run=(a,b,cap)=>{let l=0;while(l<cap&&data[a+l]===data[b+l])l++;return l}
  const chains=[16,64,256,2048]
  for(let i=0;i<n;i++){
    const bi=cost[i],lo=last[i],cap=Math.min(n-i,2048)
    relax(i+1,bi+9,lo,i,0,0)
    if(i+1<n){
      if(i>=lo){const rl=run(i-lo,i,cap),min=lo>3328?3:2
        for(let l=min;l<=rl;l++)relax(i+l,bi+matchCost(lo,l,lo),lo,i,lo,l)}
      const hash=(data[i]<<8)^data[i+1];let j=head[hash],chain=chains[level],pm=1
      while(j>=0&&chain--){const off=i-j;if(off>1048576)break;if(pm>=cap)break
        if(data[j+pm]!==data[i+pm]){j=prev[j];continue}
        const l=run(j,i,cap)
        if(l>pm){const min=off>3328?3:2
          for(let k=Math.max(pm+1,min);k<=l;k++)relax(i+k,bi+matchCost(off,k,lo),off,i,off,k)
          pm=l;if(l>=cap)break}
        j=prev[j]
      }
      prev[i]=head[hash];head[hash]=i
    }
  }
  const path=[];for(let p=n;p>0;p=from[p])path.push(p);path.reverse()
  let out=new Uint8Array(Math.ceil(n*1.2)+64),p=8,tag=0,bits=0
  const byte=v=>{if(p>=out.length){const x=new Uint8Array(out.length*2);x.set(out);out=x}out[p++]=v}
  const bit=v=>{if(!bits){tag=p;byte(0);bits=8}if(v)out[tag]|=1<<(bits-1);bits--}
  const emitGamma=v=>{for(let b=30-Math.clz32(v);b>=0;b--){bit((v>>>b)&1);bit(b===0)}}
  let lo=1
  for(const pos of path){const len=length[pos],off=offset[pos]
    if(!len){bit(1);byte(data[from[pos]]);continue}
    bit(0);if(off===lo)emitGamma(2);else{const raw=off+767;emitGamma(raw>>>8);byte(raw&255);lo=off}
    const base=len-1-(off>3328?1:0)
    if(base<=3){bit(base>>>1);bit(base&1)}else{bit(0);bit(0);emitGamma(base-2)}
  }
  bit(0);emitGamma(0x1000002);byte(255)
  let sum=0;for(let i=8;i<p;i++)sum=(sum+out[i])>>>0
  const view=new DataView(out.buffer);view.setUint32(0,p-8);view.setUint32(4,sum)
  return out.slice(0,p)
}
