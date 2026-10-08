import copy,sys,importlib.util,unittest
from pathlib import Path
r=Path(__file__).resolve().parents[1];base=r
sys.path.insert(0,str(r/'tools'))
import bc2_magazine_descriptor_manifest as d
import bc2_magazine_registry_header as h
import bc2_automatic_stock_bolt_proof as p
spec=importlib.util.spec_from_file_location('config_fixture',base/'tests/test_bc2_magazine_descriptor_manifest.py');f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)
def jobs():
 row=f.row();fields=row['fields'];fields['FireLogic.BoltAction.BoltActionTime']['value']=1
 fields['FireLogic.BoltAction.BoltActionDelay']['value']=.5;fields['FireLogic.BoltAction.HoldBoltActionUntilFireRelease']['value']=True
 return d.build(f.catalog(row))
class StockBolt(unittest.TestCase):
 def test_disabled_extension_preserves_exact_fields(self):
  original=jobs();out=p.apply(original,p.EXPECTED_PROOF);a=original['descriptors'][0];b=out['descriptors'][0]
  self.assertEqual(a['configuration'],b['configuration']);self.assertEqual(a['descriptor_digest'],b['descriptor_digest']);self.assertTrue(h.eligible(b))
  header,summary=h.build(out);self.assertEqual(summary,dict(rows=1,enabled=0,omitted_unreviewed_or_builtin=0));self.assertIn('ReloadDescriptorAdmission::Candidate',header)
  self.assertIn('authored_bolt_fields_require_shared_dispatch_review',a['deferred_reasons']);self.assertNotIn('authored_bolt_fields_require_shared_dispatch_review',b['deferred_reasons'])
 def test_default_is_unchanged(self):
  self.assertEqual(h.build(jobs())[1]['rows'],0)
 def test_wrong_build_layout_branch_proofs_reject(self):
  for key,value in [('exe_sha256','f'*64),('automatic_fire_logic',1),('type1_only_enters_bolt_hold',False),('native_runtime_admission',True)]:
   proof=copy.deepcopy(p.EXPECTED_PROOF);proof[key]=value
   with self.assertRaises(ValueError):p.apply(jobs(),proof)
  proof=copy.deepcopy(p.EXPECTED_PROOF);proof['whole_function_fingerprints']['Step']='0'*16
  with self.assertRaises(ValueError):p.apply(jobs(),proof)
 def test_existing_zero_bolt_review_cannot_enable(self):
  out=p.apply(jobs(),p.EXPECTED_PROOF);row=out['descriptors'][0]
  review=dict(key=row['key'],descriptor_digest=row['descriptor_digest'],family_proof=h.FAMILY,evidence_sha256='e'*64)
  with self.assertRaisesRegex(ValueError,'cannot be enabled'):h.build(out,[review])
 def test_unknown_dispatch_and_bounds_remain_blocked(self):
  for key,value in [('fireLogicType',1),('reloadType',0),('boltTime',11),('holdBoltUntilFireRelease',1)]:
   out=jobs();row=out['descriptors'][0];row['configuration']['values'][key]=value;row['descriptor_digest']=d.digest(row['configuration'])
   updated=p.apply(out,p.EXPECTED_PROOF);self.assertFalse(h.eligible(updated['descriptors'][0]))
 def test_descriptor_mutation_and_forged_marker_reject(self):
  out=jobs();out['descriptors'][0]['configuration']['values']['boltTime']=0
  with self.assertRaises(ValueError):p.apply(out,p.EXPECTED_PROOF)
  out=p.apply(jobs(),p.EXPECTED_PROOF);out['descriptors'][0]['automatic_stock_bolt_proof_digest']='f'*64
  self.assertFalse(h.eligible(out['descriptors'][0]))
 def test_input_cannot_claim_enabled_runtime(self):
  for key in ['runtime_enabled','registry_change']:
   out=jobs();out[key]=True
   with self.assertRaises(ValueError):p.apply(out,p.EXPECTED_PROOF)
if __name__=='__main__':unittest.main()
