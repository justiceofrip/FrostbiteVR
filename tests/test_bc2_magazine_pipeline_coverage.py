import copy,importlib.util,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('coverage_tool',ROOT/'tools/bc2_magazine_pipeline_coverage.py')
tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
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
