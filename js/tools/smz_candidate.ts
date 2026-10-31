// Local development only: create a candidate for smz_approve.py.
// This file is not suitable for the browser or for publishing without review.
import {readFileSync,writeFileSync} from 'node:fs'
import {dirname,join} from 'node:path'
import {sha} from '../src/bytes.ts'
import {buildSmzCandidate} from '../src/smz-catalog.ts'
const [,,catalogPath,out,...ids]=process.argv
if(!catalogPath||!out||!ids.length)throw Error('Usage: node smz_candidate.ts catalog.json output.smz4 id...')
const raw=readFileSync(catalogPath)
const result=await buildSmzCandidate(raw,sha(raw),ids,async name=>readFileSync(join(dirname(catalogPath),name)))
writeFileSync(out,result.data,{flag:'wx'})
console.log('Candidate:',result.sha256,'selection:',result.selected.join(', '))
