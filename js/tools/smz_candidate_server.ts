// Private offline matrix worker; not shipped to the website.
import {readFileSync,writeFileSync} from 'node:fs'
import {dirname,join} from 'node:path'
import {createInterface} from 'node:readline'
import {sha} from '../src/bytes.ts'
import {buildSmzCandidate} from '../src/smz-catalog.ts'

const [,,catalogPath,...args]=process.argv
if(!catalogPath)throw Error('A pinned catalog is required')
const donors:Uint8Array[]=[]
for(let i=0;i<args.length;i++){
  if(args[i]!=='--donor'||!args[i+1])throw Error('Expected --donor <OS file>')
  donors.push(readFileSync(args[++i]))
}
const catalog=readFileSync(catalogPath),pin=sha(catalog),files=new Map<string,Uint8Array>()
const fetch=async(name:string)=>{
  let bytes=files.get(name)
  if(!bytes){bytes=readFileSync(join(dirname(catalogPath),name));files.set(name,bytes)}
  return bytes
}
console.log('READY')
for await(const line of createInterface({input:process.stdin,crlfDelay:Infinity})){
  try{
    const request=JSON.parse(line)
    const result=await buildSmzCandidate(catalog,pin,request.ids,fetch,donors)
    writeFileSync(request.out,result.data,{flag:'wx'})
    console.log(JSON.stringify({ok:true,sha256:result.sha256}))
  }catch(error){console.log(JSON.stringify({ok:false,error:error instanceof Error?error.stack:String(error)}))}
}
