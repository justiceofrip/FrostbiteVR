"""Batch extraction/blueprint tests use synthetic resources, never live memory."""
from pathlib import Path
import contextlib,copy,hashlib,json,shutil,struct,sys,unittest,uuid
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_weapon_asset_pipeline as p
from inspect_bc2_mesh_asset import Entry,bone_hash
def sample(stride=48,weights=(255,0,0,0),palette=(11,),influences=1):
 header=b'\x01\0\0\0\0\0\x01'+b'test\0'+struct.pack('<4I4BH',1,3,0,0,stride,3,influences,len(palette),len(palette))+struct.pack('<'+'H'*len(palette),*palette)
 vertices=[]
 for n in range(3):
  pos=struct.pack('<4e',float(n),.2,.3,1) if stride in (16,48) else struct.pack('<3f',float(n),.2,.3)
  vertices.append((pos+bytes((0,1,0,0))+bytes(weights)).ljust(stride,b'\0'))
 data=header+struct.pack('<3I',stride*3,6,0)+b''.join(vertices)+struct.pack('<3H',0,1,2)
 lod={'section_count':1,'buffer_bytes':stride*3+6+12,'palette':{11:bone_hash('jntWpn_1'),12:bone_hash('LeftHand')}}
 known={bone_hash('jntWpn_1'):'jntWpn_1',bone_hash('LeftHand'):'LeftHand'}
 return data,lod,known

def dictionary(strings):
 data=b'';offsets=[]
 for s in strings:offsets.append(len(data));data+=s.encode()+b'\0'
 end=24+4*len(strings)+len(data)
 return b'{binary}'+struct.pack('>4I',end,0,end-24,len(strings))+struct.pack('>'+'I'*len(strings),*offsets)+data+b'opaque-value-tree'

@contextlib.contextmanager
def scratch():
 root=Path(__file__).resolve().parents[1]/'test-temp';root.mkdir(exist_ok=True)
 path=root/('asset-'+uuid.uuid4().hex);path.mkdir()
 try:yield path
 finally:
  assert path.resolve().is_relative_to(root.resolve())
  shutil.rmtree(path)

def profile(name):
 return dict(resource='Objects/Weapons/Handheld/'+name,geometry_variants=[],sources=[],runtime_features={'aim_alignment':'unverified','support_grip':'unverified','translated_muzzle':'unverified'})

class BatchTests(unittest.TestCase):
 def test_configuration_dictionary_retains_exact_keys(self):
  for name in ('PrimaryFire','Reload','Firing','Ammo','MagazineCapacity','ReloadTime'):
   self.assertTrue(p.configuration_field(name),name)
  for name in ('MyReload','Weapons/Ammo','GameSharedResources.WeaponFiringData'):
   self.assertFalse(p.configuration_field(name),name)
 def test_deleted_and_wrong_type_not_treated_as_mesh(self):
  entries=[Entry('gone.res','SkinnedMeshSet',0,0,1,1),Entry('wrong.res','MeshData',65536,0,1,1),Entry('real.res','SkinnedMeshSet',65536,0,1,1),Entry('Reload.res','GrannyAnimation',65536,0,1,1)]
  meshes,animations=p.inventory_entries(entries);self.assertEqual([e.name for e in meshes],['real.res']);self.assertEqual(animations[0]['name'],'Reload.res')
 def test_all_skin_formats_bounds_without_vertex_export(self):
  for stride in (16,48,32,64,68):
   data,lod,known=sample(stride);parts=p.part_bounds(data,lod,p.decode_geometry(data,lod,known),known)
   self.assertEqual(len(parts),1);self.assertEqual(parts[0]['minimum'][0],0);self.assertEqual(parts[0]['maximum'][0],2)
   self.assertEqual(parts[0]['role'],'unassigned_weighted_part');self.assertTrue(parts[0]['all_vertices_single_weight']);self.assertNotIn('vertices',parts[0])
 def test_mixed_skin_not_reported_as_rigid_magazine(self):
  data,lod,known=sample(weights=(204,51,0,0),palette=(11,12),influences=2)
  parts=p.part_bounds(data,lod,p.decode_geometry(data,lod,known),known)
  self.assertEqual(len(parts),2);self.assertTrue(all(not x['all_vertices_single_weight'] for x in parts));self.assertTrue(all(x['role']=='unassigned_weighted_part' for x in parts))
 def test_unknown_bone_hash_retained_without_inventing_role(self):
  data,lod,known=sample();parts=p.part_bounds(data,lod,p.decode_geometry(data,lod,{}),{})
  self.assertIsNone(parts[0]['candidate_name']);self.assertEqual(parts[0]['bone_hash'],f'{bone_hash("jntWpn_1"):08x}')
 def test_family_plan_does_not_enable_runtime_or_underbarrel(self):
  plan={'schema':'fvr.weapon_family_work_plan','schema_version':1,'items':[{'label':'test','exact_mesh_names':['Gun.res'],'family_proposal':'detachable_magazine'}]}
  out=p.build_blueprints([profile('Gun.res')],plan);q=out['profiles'][0]
  self.assertFalse(q['runtime_enabled']);self.assertIsNone(q['underbarrel']['eligible']);self.assertEqual(q['family_proposal'],'detachable_magazine');self.assertEqual(q['features']['translated_muzzle'],'unverified')
  self.assertIsNone(out['capture_jobs'][0]['selector']['exact_native_asset_name']);self.assertFalse(out['capture_jobs'][0]['execute_automatically'])
 def test_no_family_inheritance_by_substring(self):
  plan={'schema':'fvr.weapon_family_work_plan','schema_version':1,'items':[{'label':'test','exact_mesh_names':['Gun.res'],'family_proposal':'detachable_magazine'}]}
  out=p.build_blueprints([profile('GunLauncher.res')],plan);self.assertEqual(out['profiles'][0]['family_proposal'],'unassigned');self.assertEqual(len(out['roster_gaps']),1)
 def test_reject_ambiguous_and_unknown_family_work_plan(self):
  rule={'label':'test','exact_mesh_names':['Gun.res'],'family_proposal':'detachable_magazine'};plan={'schema':'fvr.weapon_family_work_plan','schema_version':1,'items':[rule,copy.deepcopy(rule)]}
  with self.assertRaises(ValueError):p.build_blueprints([],plan)
  plan['items']=[dict(rule,family_proposal='all_guns')]
  with self.assertRaises(ValueError):p.build_blueprints([],plan)
 def test_curated_mechanism_exceptions_stay_distinct(self):
  path=Path(__file__).resolve().parents[1]/'profiles/weapon-family-work-plan.json';plan=json.loads(path.read_text());rules={r['label']:r for r in plan['items']}
  for label,family in [('Garand','en_bloc_clip'),('MP412','revolver_cylinder'),('Saiga','detachable_magazine'),('USAS','detachable_magazine'),('SPAS','tube_pump'),('MG36','detachable_magazine'),('XM8 LMG','detachable_magazine'),('M249','belt_feed'),('M24','bolt_magazine')]:self.assertEqual(rules[label]['family_proposal'],family)
 def test_resource_variants_preserve_different_hashes_and_gaps(self):
  rows=[]
  for n in range(3):
   mesh=dict(resource='Gun.res',geometry_status='derived',sha256=str(n),data_sha256='same',lods=[])
   if n==2:mesh=dict(resource='Gun.res',geometry_status='unsupported_or_incomplete',reason='unsupported')
   rows.append(dict(archive=f'{n}.fbrb',index_sha256=str(n),meshes=[mesh]))
  out=p.combine(rows);self.assertEqual(len(out),1);self.assertEqual(len(out[0]['geometry_variants']),2);self.assertEqual(len(out[0]['sources']),3);self.assertEqual(out[0]['sources'][2]['reason'],'unsupported')
 def test_missing_install_is_explicit(self):
  with scratch() as d:
   with self.assertRaises(ValueError):p.run(Path(d))
 def test_binary_dbx_dictionary_does_not_invent_typed_values(self):
  names=['','name','ReloadTime','GameSharedResources.WeaponFiringData']
  self.assertEqual(p.dbx_dictionary(dictionary(names)),names)
 def test_dbx_bad_bounds_offsets_and_terminators_reject(self):
  raw=dictionary(['','name','ReloadTime'])
  edits=[raw[:23],b'garbage!'+raw[8:]]
  for offset,value in [(8,999999),(12,1),(24,1),(28,0)]:
   bad=bytearray(raw);struct.pack_into('>I',bad,offset,value);edits.append(bytes(bad))
  bad=bytearray(raw);bad[struct.unpack_from('>I',raw,8)[0]-1]=65;edits.append(bytes(bad))
  for bad in edits:
   with self.assertRaises(ValueError):p.dbx_dictionary(bad)
 def test_runtime_scope_join_requires_exact_resource_and_source_hash(self):
  with scratch() as d:
   root=Path(d);(root/'source.cpp').write_text('verified source')
   snap={'schema':'fvr.bc2.runtime_weapon_scope_snapshot','schema_version':1,'sources':[{'path':'source.cpp','sha256':hashlib.sha256((root/'source.cpp').read_bytes()).hexdigest()}],'scopes':[{'native_asset':'ExactVariant','mesh_resource':'Gun.res','scope':'exact only'}]}
   out={'profiles':[{'resource':'Gun.res'},{'resource':'Other.res'}],'limits':[]};p.join_runtime(out,snap,root)
   self.assertEqual(len(out['profiles'][0]['existing_runtime_scopes']),1);self.assertEqual(out['profiles'][1]['existing_runtime_scopes'],[])
   (root/'source.cpp').write_text('changed source')
   with self.assertRaises(ValueError):p.join_runtime(out,snap,root)

if __name__=='__main__':unittest.main()
