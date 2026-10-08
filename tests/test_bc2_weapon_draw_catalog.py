from pathlib import Path
import copy,json,struct,sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_weapon_draw_catalog as c
from test_bc2_weapon_asset_pipeline import sample

def rows(stride=48):
 data,lod,_=sample(stride);lod['lod']=0
 out=c.derive_sections('Test.res',b'mesh-metadata','Test_lod0_data.res',data,lod)
 for row in out:row['sources']=[dict(archive='a.fbrb',index_sha256='abc')]
 return out

class CatalogTests(unittest.TestCase):
 def test_indexed_not_unindexed_and_format_bounds(self):
  for stride in (16,48,32,64,68):
   result=rows(stride)[0];self.assertEqual(result['index_count'],3);self.assertEqual(result['position_format'],'Half4' if stride in (16,48) else 'Float3');self.assertFalse(result['native_association_verified'])
   self.assertNotIn('vertices',result);self.assertNotIn('indices',result)
 def test_index_order_changes_signature(self):
  data,lod,_=sample();lod['lod']=0
  one=c.derive_sections('T.res',b'm','D.res',data,lod)[0]
  data=data[:-6]+struct.pack('<3H',2,1,0)
  two=c.derive_sections('T.res',b'm','D.res',data,lod)[0]
  self.assertNotEqual(one['vertex_skin_fnv1a64'],two['vertex_skin_fnv1a64'])
 def test_invalid_indices_or_skin_rejected(self):
  data,lod,_=sample();lod['lod']=0
  with self.assertRaises(ValueError):c.derive_sections('T.res',b'm','D.res',data[:-6]+struct.pack('<3H',0,1,9),lod)
  data,lod,_=sample(weights=(1,0,0,0));lod['lod']=0
  with self.assertRaises(ValueError):c.derive_sections('T.res',b'm','D.res',data,lod)
 def test_duplicate_sources_join_but_variants_stay_distinct(self):
  a=rows();b=copy.deepcopy(a);b[0]['sources'][0]['archive']='b.fbrb'
  result=c.combine([a,b]);self.assertEqual(len(result['sections']),1);self.assertEqual(len(result['sections'][0]['sources']),2)
  b[0]['variant']='f'*64;self.assertEqual(len(c.combine([a,b])['sections']),2)
 def test_conflicting_identity_refused(self):
  a=rows();b=copy.deepcopy(a);b[0]['index_count']=6
  with self.assertRaises(ValueError):c.combine([a,b])
 def test_cpp_header_metadata_only_and_escaped(self):
  a=rows();a[0]['resource']='quote"name.res';out=c.header(c.combine([a]));self.assertIn('quote\\"name.res',out);self.assertIn('GeneratedWeaponDrawCatalog',out)
  self.assertNotIn('vertices',out);self.assertNotIn('indices',out)
 def test_malformed_header_input_rejected(self):
  for key,value in [('position_format','hack'),('resource','bad\npath'),('vertex_skin_fnv1a64','0x123'),('palette_size',0),('index_count',999999)]:
   a=rows();a[0][key]=value
   with self.assertRaises(ValueError):c.header(c.combine([a]))
 def test_explicit_install_paths_only(self):
  with self.assertRaises(ValueError):c.run(Path(__file__).parent,['../../missing.fbrb'])
 def test_ambiguity_is_not_filtered_by_selected_label(self):
  a=rows();b=copy.deepcopy(a);b[0]['resource']='Different.res';out=c.combine([a,b]);self.assertEqual(len(out['sections']),2);self.assertFalse(out['native_association_verified']);self.assertEqual(a[0]['vertex_skin_fnv1a64'],b[0]['vertex_skin_fnv1a64'])

if __name__=='__main__':unittest.main()
