import copy,importlib.util,json,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
spec=importlib.util.spec_from_file_location('coverage_tool',ROOT/'tools/bc2_magazine_pipeline_coverage.py')
tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
import bc2_authored_magazine_geometry as geometry
import bc2_magazine_registry_header as registry
import bc2_magazine_descriptor_manifest as descriptor
def fixture_module(name):
 spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(name+'.py'))
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
geometry_fixture=fixture_module('test_bc2_authored_magazine_geometry')
descriptor_fixture=fixture_module('test_bc2_magazine_descriptor_manifest')
def signed(row,key):
 row.pop(key,None);row[key]=geometry.digest(row);return row
def document(schema,profiles):return {'schema':schema,'schema_version':1,'runtime_admission':False,'profiles':profiles}
def exact_fixture():
 jobs=descriptor.build(descriptor_fixture.catalog(*(descriptor_fixture.row(name='SyntheticRifle',variant=v) for v in ('A','B'))))
 row=geometry_fixture.profile()
 binding={'native_asset_name':row['native_asset_name'],'weapon':{'resource':'Objects/Weapons/A.dbx','sha256':'a'*64,'instance_guid':'00000000-0000-0000-0000-000000000001'},
  'configured_mesh_path':row['configured_mesh_path'],'rig_fingerprint':row['rig_fingerprint'],
  'skeleton_sha256':row['skeleton_sha256'],'animation_sha256':row['static_clip_sha256'],
  'meshes':[{'configured_mesh_path':row['configured_mesh_path'],'geometry':{'mesh_sha256':row['mesh_sha256'],
   'lods':[{'lod':0,'data_sha256':row['lod_sha256']}]}}]}
 signed(binding,'binding_digest');row['grip_binding_digest']=binding['binding_digest'];signed(row,'profile_digest')
 return jobs,document('fvr.bc2.authored_magazine_geometry',[row]),document('fvr.bc2.authored_reload_reference_bindings',[binding])
class ExactGeometryCoverage(unittest.TestCase):
 def test_name_only_geometry_does_not_enroll_variants(self):
  jobs,geo,_=exact_fixture();result=tool.audit(jobs,[geo],registry)
  self.assertEqual(result['summary']['fixture_rows'],0)
  self.assertTrue(all(not row['fixture_candidate'] for row in result['rows']))
 def test_exact_backlink_only_enrolls_its_configuration(self):
  jobs,geo,bindings=exact_fixture();original=copy.deepcopy(geo)
  result=tool.audit(jobs,[geo],registry,[bindings]);rows={r['path']:r for r in result['rows']}
  self.assertTrue(rows['Objects/Weapons/A']['fixture_candidate'])
  self.assertFalse(rows['Objects/Weapons/B']['fixture_candidate'])
  self.assertEqual(result['summary']['exact_geometry_paths'],1)
  self.assertEqual(result['summary']['new_runtime_admissions'],0);self.assertEqual(geo,original)
 def test_wrong_config_content_or_guid_cannot_borrow_geometry(self):
  for field,value in [('resource','Objects/Weapons/Other.dbx'),('sha256','f'*64),('instance_guid','F'*32)]:
   jobs,geo,bindings=exact_fixture();binding=bindings['profiles'][0];binding['weapon'][field]=value;signed(binding,'binding_digest')
   geo['profiles'][0]['grip_binding_digest']=binding['binding_digest'];signed(geo['profiles'][0],'profile_digest')
   self.assertEqual(tool.audit(jobs,[geo],registry,[bindings])['summary']['fixture_rows'],0)
 def test_changed_or_missing_backlinks_fail_closed(self):
  for fault in ('digest','absent','rig','mesh','lod','skeleton','pose','weapon','admitted'):
   jobs,geo,bindings=exact_fixture();row=geo['profiles'][0]
   if fault=='digest':row['mesh_sha256']='f'*64
   if fault=='absent':row['grip_binding_digest']='f'*64
   if fault=='rig':row['rig_fingerprint']='fnv1a64:'+'f'*16
   if fault=='mesh':row['mesh_sha256']='f'*64
   if fault=='lod':row['lod_sha256']='f'*64
   if fault=='skeleton':row['skeleton_sha256']='f'*64
   if fault=='pose':row['static_clip_sha256']='f'*64
   if fault=='weapon':row['weapon']={**bindings['profiles'][0]['weapon'],'resource':'Objects/Weapons/B.dbx'}
   if fault=='admitted':row['runtime_admitted']=True
   if fault!='digest':signed(row,'profile_digest')
   result=tool.audit(jobs,[geo],registry,[bindings]);self.assertEqual(result['summary']['fixture_rows'],0,fault)
   self.assertTrue(result['geometry_excluded'],fault)
 def test_distinct_profiles_for_same_configuration_are_ambiguous(self):
  jobs,geo,bindings=exact_fixture();other=copy.deepcopy(geo['profiles'][0]);other['geometry']['item_from_hand'][12]+=.001;signed(other,'profile_digest');geo['profiles'].append(other)
  result=tool.audit(jobs,[geo],registry,[bindings]);self.assertEqual(result['summary']['fixture_rows'],0)
  self.assertTrue(any('ambiguous_exact_configuration_geometry' in row['missing_admission'] for row in result['rows']))
 def test_distinct_exact_variant_can_join_without_name_cloning(self):
  jobs,geo,bindings=exact_fixture();binding=copy.deepcopy(bindings['profiles'][0]);binding['weapon']['resource']='Objects/Weapons/B.dbx';signed(binding,'binding_digest');bindings['profiles'].append(binding)
  row=copy.deepcopy(geo['profiles'][0]);row['grip_binding_digest']=binding['binding_digest'];signed(row,'profile_digest');geo['profiles'].append(row)
  self.assertEqual(tool.audit(jobs,[geo],registry,[bindings])['summary']['fixture_rows'],2)
 def test_prepare_regenerates_exact_path_and_disabled_registry(self):
  jobs,geo,bindings=exact_fixture();old=copy.deepcopy(geo)
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);stale=root/'old.h';stale.write_text('UNTRUSTED_STALE_HEADER')
   out=root/'generated';result=tool.prepare(jobs,[geo],[stale],registry,out,[bindings])
   text=(out/'MeasuredGeometry.h').read_text();disabled=(out/'DisabledRegistry.h').read_text()
   self.assertIn('g.configurationPath="Objects/Weapons/A";',text);self.assertNotIn('Objects/Weapons/B',text)
   self.assertNotIn('UNTRUSTED_STALE_HEADER',text);self.assertIn('ReloadDescriptorAdmission::Candidate',disabled)
   self.assertNotIn('ReviewedNative',disabled);self.assertNotIn('Objects/Weapons/B',disabled)
   self.assertEqual(result['fixture_generation']['production_enabled_rows'],0)
   self.assertEqual(result['fixture_generation']['measured_geometry_rows'],1)
   self.assertIn('NEVER ENABLE IN GAME',(out/'FixtureRegistry.h').read_text())
  self.assertEqual(geo,old)
  provenance=result['fixture_generation']['geometry_provenance'][0]
  self.assertEqual(provenance['source_profile_digest'],geo['profiles'][0]['profile_digest'])
  self.assertNotEqual(provenance['derived_profile_digest'],provenance['source_profile_digest'])
 def test_corrupt_or_admitted_source_binding_is_unavailable(self):
  for fault in ('digest','admitted','missing_pose'):
   jobs,geo,bindings=exact_fixture();binding=bindings['profiles'][0]
   if fault=='digest':binding['configured_mesh_path']='Objects/ChangedMesh'
   if fault=='admitted':binding['runtime_accepted']=True
   if fault=='missing_pose':binding.pop('animation_sha256')
   if fault!='digest':
    signed(binding,'binding_digest');geo['profiles'][0]['grip_binding_digest']=binding['binding_digest'];signed(geo['profiles'][0],'profile_digest')
   result=tool.audit(jobs,[geo],registry,[bindings])
   self.assertEqual(result['summary']['fixture_rows'],0,fault);self.assertTrue(result['geometry_excluded'],fault)
 def test_cli_uses_bindings_without_legacy_headers_and_preserves_inputs(self):
  jobs,geo,bindings=exact_fixture()
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp)
   for name,value in [('jobs',jobs),('geometry',geo),('bindings',bindings)]:
    (root/(name+'.json')).write_text(json.dumps(value),encoding='utf-8')
   command=[sys.executable,'-B',str(ROOT/'tools/bc2_magazine_pipeline_coverage.py'),'--source',str(ROOT),
    '--jobs',str(root/'jobs.json'),'--geometry',str(root/'geometry.json'),'--bindings',str(root/'bindings.json')]
   result=subprocess.run(command+['--private-output',str(root/'generated'),'--report',str(root/'report.json')],capture_output=True,text=True)
   self.assertEqual(result.returncode,0,result.stderr)
   report=json.loads((root/'report.json').read_text());self.assertEqual(report['summary']['fixture_rows'],1)
   self.assertIn(str(root/'bindings.json'),report['inputs'])
   before=(root/'geometry.json').read_bytes()
   result=subprocess.run(command+['--report',str(root/'geometry.json')],capture_output=True,text=True)
   self.assertNotEqual(result.returncode,0);self.assertIn('must not overwrite input evidence',result.stderr)
   self.assertEqual((root/'geometry.json').read_bytes(),before)
 def test_no_exact_geometry_cannot_emit_a_fixture(self):
  jobs,geo,_=exact_fixture()
  with tempfile.TemporaryDirectory() as temp:
   out=Path(temp)/'generated'
   with self.assertRaisesRegex(ValueError,'exact.configuration'):tool.prepare(jobs,[geo],[],registry,out)
   self.assertFalse(out.exists())
class ProbeEvidence(unittest.TestCase):
 def fixture(self):
  coverage={'rows':[{'asset':'A','path':'Objects/A','fixture_candidate':True},{'asset':'A','path':'Objects/B','fixture_candidate':True}]}
  rows=[{'asset':'A','path':p,'mode':'mock_native_measured_geometry','partial_reload':True,'empty_reload':True,'exact_identity_denials':True,'pass':True} for p in ['Objects/A','Objects/B']]
  return coverage,rows
 def test_exact_paths_are_independent(self):
  c,r=self.fixture();result=tool.reconcile(c,{'x86':r});self.assertEqual(result['probe_runs']['x86']['passed_rows'],2);self.assertFalse(result['probe_runs']['x86']['native_execution_proven'])
 def test_missing_or_duplicate_paths_are_rejected(self):
  for mutate in [lambda r:r[:1],lambda r:[r[0],r[0]],lambda r:r+[dict(r[0],path='Objects/C')]]:
   c,r=self.fixture()
   with self.assertRaises(ValueError):tool.reconcile(c,{'x86':mutate(r)})
 def test_stages_failure_or_mixed_modes_are_rejected(self):
  for field,value in [('empty_reload',False),('pass',False),('mode','production_disabled')]:
   c,r=self.fixture();r[1][field]=value
   with self.assertRaises(ValueError):tool.reconcile(c,{'x64':r})
 def test_disabled_is_not_native_acceptance(self):
  c,r=self.fixture();r=[dict(x,mode='production_disabled') for x in r]
  result=tool.reconcile(c,{'disabled':r});self.assertFalse(result['probe_runs']['disabled']['native_execution_proven'])
if __name__=='__main__':unittest.main()
