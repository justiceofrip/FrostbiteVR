import copy,unittest
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from bc2_weapon_package_index import build
H='a'*64
ROW={'asset':'same','authored_configuration':{'resource':'weapon.dbx','sha256':H,'instance_guid':'A'*32,'mesh_references':[{'reference':'m/'+'B'*32,'resource':'m.dbx','instance_guid':'B'*32,'sha256':H}]},'resources':[{'mesh':'m','archive':'archive.fbrb','index_sha256':H,'metadata_sha256':H,'lods':[{'lod':0,'resource':'m_lod0.res','sha256':H,'sections':[{'normalized':True}]}]}],'meshes':['m'],'rig_fingerprint':'123456789abcdef0'}
class Checks(unittest.TestCase):
 def test_exact_change(self):
  a=build({'schema':'fvr.bc2.visibility-descriptors.v1','rows':[ROW]})['rows'][0];b=copy.deepcopy(ROW);b['authored_configuration']['sha256']='b'*64
  self.assertNotEqual(a['package_id'],build({'schema':'fvr.bc2.visibility-descriptors.v1','rows':[b]})['rows'][0]['package_id'])
  with self.assertRaises(ValueError):build({'schema':'fvr.bc2.visibility-descriptors.v1','rows':[b]},[{'package_id':a['package_id'],'component':'grip','source_sha256':H}])
 def test_complete_no_runtime(self):
  d={'schema':'fvr.bc2.visibility-descriptors.v1','rows':[ROW]};key=build(d)['rows'][0]['package_id']
  from bc2_weapon_package_index import COMPONENTS
  out=build(d,[{'package_id':key,'component':n,'source_sha256':H,'data_ready':True,'native_admitted':True} for n in COMPONENTS if n!='visibility'])['rows'][0]
  self.assertEqual(out['missing_components'],[]);self.assertFalse(out['runtime_enabled']);self.assertTrue(all(not c['native_admitted'] for c in out['components'].values()))
 def test_extra_mesh(self):
  b=copy.deepcopy(ROW);b['meshes'].append('extra')
  self.assertEqual(len(build({'schema':'fvr.bc2.visibility-descriptors.v1','rows':[b]})['excluded']),1)
 def test_duplicate_component(self):
  d={'schema':'fvr.bc2.visibility-descriptors.v1','rows':[ROW]};key=build(d)['rows'][0]['package_id'];c={'package_id':key,'component':'grip','source_sha256':H}
  with self.assertRaises(ValueError):build(d,[c,c])
 def test_invalid_evidence_excluded(self):
  for mutate in (
   lambda r:r['authored_configuration'].__setitem__('sha256','not-a-digest'),
   lambda r:r['authored_configuration']['mesh_references'][0].__setitem__('instance_guid','wrong'),
   lambda r:r['authored_configuration']['mesh_references'][0].__setitem__('resource','other.dbx'),
   lambda r:r['resources'][0].__setitem__('metadata_sha256','f'*63),
   lambda r:r['resources'][0]['lods'][0].__setitem__('sha256','z'*64),
   lambda r:r['resources'][0]['lods'][0]['sections'][0].__setitem__('normalized',False),
   lambda r:r.__setitem__('rig_fingerprint','bad'),
   lambda r:r['authored_configuration'].__setitem__('resource','../weapon.dbx')):
   with self.subTest(mutate=mutate):
    r=copy.deepcopy(ROW);mutate(r);out=build({'schema':'fvr.bc2.visibility-descriptors.v1','rows':[r]})
    self.assertEqual(out['rows'],[]);self.assertEqual(len(out['excluded']),1)
 def test_duplicate_configuration_rejected(self):
  with self.assertRaises(ValueError):build({'schema':'fvr.bc2.visibility-descriptors.v1','rows':[ROW,copy.deepcopy(ROW)]})
 def test_missing_backlink_explicit(self):
  r=copy.deepcopy(ROW);del r['authored_configuration']
  self.assertEqual(build({'schema':'fvr.bc2.visibility-descriptors.v1','rows':[r]})['excluded'][0]['reason'],'missing exact authored configuration')
 def test_data_unready_stays_missing(self):
  d={'schema':'fvr.bc2.visibility-descriptors.v1','rows':[ROW]};key=build(d)['rows'][0]['package_id']
  out=build(d,[{'package_id':key,'component':'grip','source_sha256':H,'data_ready':False}])
  self.assertIn('grip',out['rows'][0]['missing_components'])
 def test_rig_and_geometry_change_identity(self):
  d=lambda r:{'schema':'fvr.bc2.visibility-descriptors.v1','rows':[r]}
  key=build(d(ROW))['rows'][0]['package_id']
  for field in ('rig','geometry'):
   r=copy.deepcopy(ROW)
   if field=='rig':r['rig_fingerprint']='abcdef0123456789'
   else:r['resources'][0]['lods'][0]['sha256']='b'*64
   self.assertNotEqual(build(d(r))['rows'][0]['package_id'],key)
 def test_resource_change_distinguishes_same_content(self):
  d=lambda r:{'schema':'fvr.bc2.visibility-descriptors.v1','rows':[r]}
  r=copy.deepcopy(ROW);r['authored_configuration']['resource']='different.dbx'
  self.assertNotEqual(build(d(r))['rows'][0]['package_id'],build(d(ROW))['rows'][0]['package_id'])
 def test_invalid_canonical_resource(self):
  from bc2_weapon_package_index import canonical
  for path in ('a:b','a\0b','a\nb','a//b','a/./b','a/../b','/a','a\\b'):
   with self.subTest(path=path),self.assertRaises(ValueError):canonical(path)
 def test_cli_cannot_overwrite_input(self):
  import subprocess
  script=Path(__file__).resolve().parents[1]/'tools/bc2_weapon_package_index.py'
  result=subprocess.run([sys.executable,str(script),'--visibility',str(script),'--output',str(script)],capture_output=True,text=True)
  self.assertNotEqual(result.returncode,0);self.assertIn('must not overwrite',result.stderr)
if __name__=='__main__':unittest.main()


