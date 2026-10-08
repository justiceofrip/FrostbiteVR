import importlib.util,sys,unittest
from pathlib import Path
TOOLS=Path(__file__).resolve().parents[1]/'tools'
sys.path.insert(0,str(TOOLS))
import bc2_visibility_descriptors as v
class Tests(unittest.TestCase):
 def row(self,config=None):return dict(asset='Generic',meshes=['Objects/Generic/Mesh'],weighted_names=['jntWpn_1'],rig_fingerprint='a7f219a1426216ab',authored_configuration=config)
 def test_exact_path_stays_unadmitted(self):
  h=v.header({'rows':[self.row({'resource':'Objects/Generic/Configuration.dbx'})]})
  self.assertIn('false,"Objects/Generic/Configuration",false,false',h)
 def test_missing_backlink_explicit(self):
  h=v.header({'rows':[self.row()]});self.assertIn('false,"",false,false',h)
 def test_invalid_path_rejected(self):
  for resource in ('/Objects/Config.dbx','Objects/../Config.dbx','Objects//Config.dbx','G:/Objects/Config.dbx','Objects/Config','Objects\\Config.dbx'):
   with self.subTest(resource=resource),self.assertRaises(ValueError):v.header({'rows':[self.row({'resource':resource})]})
if __name__=='__main__':unittest.main()
