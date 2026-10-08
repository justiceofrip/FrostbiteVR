import copy,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from report_arming_empty import analyze

def fixture():
 owner=[65536,131072,196608,262144,1,2,3]
 rows=[]
 for n in range(3):
  state={"firing":327680+n*65536,"current":2,"next":2,"loaded":0,"reserve":24,"timer":0.,"flags_a8":0}
  rows.append({"stage":9,"now_ns":110,"input_sequence":1,"owner_revision":3,"observed_ns":100,"deadline_ns":200,
   "branch":n,"profile":0,"phase":2,"family_known":True,"family":0,"owner":owner,"requested":True,"applied":True,
   "restored":True,"owner_retained":True,"before":state,"after":copy.deepcopy(state),
   "context":{"delta":.005,"reload_multiplier":1.,"input_flags":0,"flags_24_28":[1,0,0,0,0]}})
 return {"gameplay":{"arming_empty_probe":{"phase":4,"failure":0,"withheld_reload_samples":3,"arming_policy_samples":4,"owner":owner,"start_ns":100,"cancel_ns":150,"original_reserve":24},
  "reload_flow":{"empty_step_diagnostic":{"drained":True,"rows":rows}}}}
class Tests(unittest.TestCase):
 def test_coverage_never_native_acceptance(self):
  r=analyze(fixture());self.assertEqual(r["status"],"paired_arming_step_coverage_observed");self.assertFalse(r["native_verified"])
 def test_adversarial(self):
  for key,value in [("phase",1),("restored",False),("owner_revision",9),("family_known",False)]:
   d=fixture();d["gameplay"]["reload_flow"]["empty_step_diagnostic"]["rows"][1][key]=value
   self.assertEqual(analyze(d)["status"],"inconclusive")
  for field in ["loaded","reserve"]:
   d=fixture();d["gameplay"]["reload_flow"]["empty_step_diagnostic"]["rows"][1]["after"][field]+=1
   self.assertEqual(analyze(d)["status"],"inconclusive")
 def test_wrong_consistent_binding(self):
  for family,profile in [(1,0),(0,1),(1,1),(2,4)]:
   d=fixture()
   for row in d["gameplay"]["reload_flow"]["empty_step_diagnostic"]["rows"]:
    row["family"]=family;row["profile"]=profile
   result=analyze(d);self.assertEqual(result["status"],"inconclusive");self.assertFalse(result["native_verified"]);self.assertEqual(result["groups"],[])
 def test_explicit_is_not_unsolicited(self):
  d=fixture();r=d["gameplay"]["reload_flow"]["empty_step_diagnostic"]["rows"][1];r["context"]["input_flags"]=4;r["requested"]=r["applied"]=False
  self.assertEqual(analyze(d)["status"],"inconclusive")
 def test_valid_branch_specific_context(self):
  d=fixture();d["gameplay"]["reload_flow"]["empty_step_diagnostic"]["rows"][1]["context"]["flags_24_28"]=[1,1,0,0,0]
  self.assertEqual(analyze(d)["status"],"paired_arming_step_coverage_observed")
 def test_completed_fixture_without_arming_rows_not_fabricated(self):
  d=fixture()
  for row in d['gameplay']['reload_flow']['empty_step_diagnostic']['rows']:row['phase']=6
  self.assertEqual(d['gameplay']['arming_empty_probe']['phase'],4)
  self.assertEqual(analyze(d)['status'],'inconclusive')
 def test_incomplete(self):
  d=fixture();d["gameplay"]["arming_empty_probe"]["phase"]=3;self.assertEqual(analyze(d)["status"],"inconclusive")
if __name__=="__main__":unittest.main()
