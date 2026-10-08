import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_visibility_descriptors as v
class Tests(unittest.TestCase):
 def row(self):
  return dict(asset='Generic',meshes=['Objects/Generic/Mesh'],weighted_names=['jntWpn_1'],rig_fingerprint='a7f219a1426216ab',authored_configuration={'resource':'Objects/Generic/Configuration.dbx'})
 def test_missing_extraction_proof_remains_ineligible(self):
  self.assertIn(',false,false,false},',v.header({'rows':[self.row()]}))
 def test_extraction_proof_does_not_grant_native_flags(self):
  r=self.row();r['weighted_section_data_verified']=True
  self.assertIn('ull,false,"Objects/Generic/Configuration",false,false,true},',v.header({'rows':[r]}))
 def test_truthy_values_cannot_substitute_boolean_proof(self):
  for value in (1,'true',[],None):
   r=self.row();r['weighted_section_data_verified']=value
   with self.subTest(value=value):self.assertIn(',false,false,false},',v.header({'rows':[r]}))
if __name__=='__main__':unittest.main()
