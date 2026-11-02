// Local development only: create a candidate for smz_approve.py.
// This file is not suitable for the browser or for publishing without review.
import {readFileSync,writeFileSync} from 'node:fs'
import {dirname,join} from 'node:path'
import {sha} from '../src/bytes.ts'
import {buildSmzCandidate} from '../src/smz-catalog.ts'
const [,,catalogPath,out,...rest]=process.argv
// --donor <file>: the OS file a selected module takes bytes from (FM TONE: the Digitone OS 1.44)
const at=rest.indexOf('--donor'),donor=at<0?undefined:readFileSync(rest[at+1])
const ids=at<0?rest:[...rest.slice(0,at),...rest.slice(at+2)]
if(!catalogPath||!out||!ids.length||(at>=0&&!rest[at+1]))throw Error('Usage: node smz_candidate.ts catalog.json output.smz4 [--donor os-file] id...')
const raw=readFileSync(catalogPath)
const result=await buildSmzCandidate(raw,sha(raw),ids,async name=>readFileSync(join(dirname(catalogPath),name)),donor)
writeFileSync(out,result.data,{flag:'wx'})
console.log('Candidate:',result.sha256,'selection:',result.selected.join(', '))
