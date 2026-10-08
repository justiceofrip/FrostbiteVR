from pathlib import Path
import copy,json,sys,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import bc2_resource_enrollment as enrollment
import bc2_authored_magazine_geometry as geometry
import bc2_magazine_assembly_cache as cache
from unittest.mock import patch
def inputs():
 folder=ROOT/'profiles/resource-enrollment240-f2000'
 doc={key:json.loads((folder/name).read_text()) for key,name in [('jobs','selected-jobs.json'),('reviews','exact-reviews.json'),('geometry','exact-geometry.json'),('visibility','visibility.json')]}
 doc['catalog']=json.loads((ROOT/'config/body-ammo-assets.json').read_text());doc['catalog']['profiles']=[r for r in doc['catalog']['profiles'] if r['asset']=='F2000_sp'];return doc
class F2000Enrollment(unittest.TestCase):
 def test_exact_registry_and_assembly_header_reproduce(self):
  x=inputs();doc,header=enrollment.join(**x);folder=ROOT/'profiles/resource-enrollment240-f2000'
  self.assertEqual(header,(folder/'F2000Registry.h').read_text());self.assertEqual(len(doc['profiles']),1)
  self.assertEqual(geometry.cpp_header(x['geometry'],{'F2000_sp'}),(folder/'F2000Geometry.h').read_text())
  g=x['geometry']['profiles'][0];self.assertEqual(len(g['assembly']['members']),3)
  self.assertEqual(g['geometry']['design']['paired_reload_grasp']['window']['time_seconds'],.9)
  self.assertFalse(doc['native_constructor_permission']);self.assertFalse(doc['profiles'][0]['native_per_variant_tested'])
 def test_leaf_substitution_or_mismatched_assembly_source_cannot_enroll(self):
  for fault in range(14):
   x=inputs();g=x['geometry']['profiles'][0];mag=next(r for r in x['catalog']['profiles'] if r.get('rigid_assembly'))
   if fault==0:mag.pop('rigid_assembly')
   elif fault==1:mag['display_only']=True
   elif fault==2:mag['assembly_digest']='0'*64
   elif fault==3:mag['geometry_profile_digest']='0'*64
   elif fault==4:mag['assembly_source']['profile']['weapon']['sha256']='0'*64
   elif fault==5:mag['mesh_sha256']='0'*64
   elif fault==6:mag['lod_sha256']='0'*64
   elif fault==7:mag['skeleton_sha256']='0'*64
   elif fault==8:mag['static_clip_sha256']='0'*64
   elif fault==9:mag['reload_clip_sha256']='0'*64
   elif fault==10:g['assembly']['members'].pop()
   elif fault==11:mag['rig_fingerprint']+=1
   elif fault==12:x['catalog']['profiles'].remove(mag)
   else:x['catalog']['profiles'].append(copy.deepcopy(mag))
   with self.subTest(fault=fault),self.assertRaises(ValueError):enrollment.join(**x)
 def test_rebuild_rejects_incomplete_sources_before_reading_assets(self):
  mag=next(r for r in inputs()['catalog']['profiles'] if r.get('rigid_assembly'))
  for label in ('mesh','lod','skeleton','static','reload','profile'):
   broken=copy.deepcopy(mag);broken['assembly_source'].pop(label)
   with self.subTest(label=label),self.assertRaises(ValueError):cache.derive_reviewed_cache(Path('.'),lambda _:self.fail('must not read'),broken)
 def test_rebuild_rejects_changed_archive_or_resource_hash(self):
  mag=next(r for r in inputs()['catalog']['profiles'] if r.get('rigid_assembly'))
  class Archive:
   index_sha256='0'*64
   def read_selected(self,names):return {n:b'wrong exact resource' for n in names}
  with self.assertRaisesRegex(ValueError,'archive changed'):cache.derive_reviewed_cache(Path('.'),lambda _:Archive(),mag)
  Archive.index_sha256=mag['assembly_source']['mesh']['archive_index_sha256']
  with self.assertRaisesRegex(ValueError,'resource changed'):cache.derive_reviewed_cache(Path('.'),lambda _:Archive(),mag)
 def test_magazine_cache_is_not_a_whole_weapon_body_registration(self):
  rows=inputs()['catalog']['profiles'];self.assertEqual(len(rows),3) # preserves the earlier closed display-only prop
  bodies=[r for r in rows if r.get('display_only') and r.get('configuration_path')];self.assertEqual(len(bodies),1)
  self.assertTrue(bodies[0]['configuration_path']);mag=next(r for r in rows if r.get('rigid_assembly'))
  self.assertTrue(mag['render_only']);self.assertFalse(mag['native_admission']);self.assertNotIn('configuration_path',mag)
if __name__=='__main__':unittest.main()
