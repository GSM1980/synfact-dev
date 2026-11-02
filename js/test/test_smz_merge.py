import json,subprocess,sys,tempfile,unittest
from pathlib import Path

TOOL=Path(__file__).resolve().parents[1]/'tools'/'smz_merge.py'

class MergeTests(unittest.TestCase):
    def test_reuse_only_unchanged_permitted_objects(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp)
            base={'schema':1,'device':'syntakt','os':'1.41','modules':[
                {'id':'a','sha256':'a'*64},{'id':'b','sha256':'b'*64}], 'approved':[]}
            old={**base,'modules':[base['modules'][0],{'id':'b','sha256':'c'*64}],
                 'approved':[{'ids':['a'],'sha256':'1'*64},{'ids':['b'],'sha256':'2'*64}]}
            config={'modules':[{'id':'a'},{'id':'b'}]}
            for name,obj in [('new.json',base),('old.json',old),('config.json',config)]:
                (d/name).write_text(json.dumps(obj))
            subprocess.run([sys.executable,str(TOOL),'--catalog',str(d/'new.json'),'--config',str(d/'config.json'),
                            '--reuse',str(d/'old.json'),'--out',str(d/'merged.json')],check=True,capture_output=True)
            result=json.loads((d/'merged.json').read_text())
            self.assertEqual([x['ids'] for x in result['approved']],[['a']])
            incomplete=subprocess.run([sys.executable,str(TOOL),'--catalog',str(d/'new.json'),'--config',str(d/'config.json'),
                            '--reuse',str(d/'old.json'),'--out',str(d/'incomplete.json'),'--require-complete'],capture_output=True)
            self.assertNotEqual(incomplete.returncode,0)
            self.assertFalse((d/'incomplete.json').exists())
            config['modules'][0]['requires']=['b'];(d/'config.json').write_text(json.dumps(config))
            subprocess.run([sys.executable,str(TOOL),'--catalog',str(d/'new.json'),'--config',str(d/'config.json'),
                            '--reuse',str(d/'old.json'),'--out',str(d/'restricted.json')],check=True,capture_output=True)
            self.assertEqual(json.loads((d/'restricted.json').read_text())['approved'],[])
            config={'modules':[{'id':'a','always':True},{'id':'b'}]};(d/'config.json').write_text(json.dumps(config))
            old['modules'][1]['sha256']='b'*64
            old['approved']=[{'ids':['a'],'sha256':'1'*64},{'ids':['b'],'sha256':'2'*64},{'ids':['a','b'],'sha256':'3'*64}]
            (d/'old.json').write_text(json.dumps(old))
            subprocess.run([sys.executable,str(TOOL),'--catalog',str(d/'new.json'),'--config',str(d/'config.json'),
                            '--reuse',str(d/'old.json'),'--out',str(d/'always.json'),'--require-complete'],check=True,capture_output=True)
            self.assertEqual([x['ids'] for x in json.loads((d/'always.json').read_text())['approved']],[['a'],['a','b']])

if __name__=='__main__':unittest.main()
