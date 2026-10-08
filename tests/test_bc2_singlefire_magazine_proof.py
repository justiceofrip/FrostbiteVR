from pathlib import Path
import copy,hashlib,json,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import bc2_singlefire_magazine_proof as proof
import bc2_magazine_registry_header as registry
import bc2_magazine_descriptor_manifest as descriptor
FOLDER=ROOT/'profiles/singlefire238'
def jobs():return json.loads((FOLDER/'original-descriptor-jobs.json').read_text())
def extend(j=None):return proof.apply(j or jobs(),proof.EXPECTED_PROOF)

class SingleFire(unittest.TestCase):
 def test_explicit_cli_extension(self):
  with tempfile.TemporaryDirectory(dir=ROOT) as temp:
   output=Path(temp)/'jobs.json'
   command=[sys.executable,'-B',str(ROOT/'tools/bc2_magazine_descriptor_manifest.py'),'--catalog',str(FOLDER/'catalog.json'),'--output',str(output)]
   subprocess.run(command,check=True,capture_output=True)
   self.assertEqual(registry.build(json.loads(output.read_text()))[1]['rows'],0)
   subprocess.run(command+['--singlefire-magazine-proof',str(FOLDER/'static-proof.json')],check=True,capture_output=True)
   j=json.loads(output.read_text());summary=registry.build(j)[1]
   self.assertEqual((summary['rows'],summary['enabled']),(2,0))
   self.assertEqual(j['input_sha256']['singlefire_magazine_proof'],hashlib.sha256((FOLDER/'static-proof.json').read_bytes()).hexdigest())
 def test_unknown_symbol_is_unmapped_without_explicit_proof(self):
  j=jobs();self.assertEqual(registry.build(j)[1]['rows'],0)
  for row in j['descriptors']:self.assertNotIn('fireLogicType',row['configuration']['values'])
 def test_exact_two_candidates_reproduce_disabled_header(self):
  j=jobs();before=copy.deepcopy(j);out=extend(j);header,summary=registry.build(out)
  self.assertEqual(j,before);self.assertEqual(summary['rows'],2);self.assertEqual(summary['enabled'],0)
  self.assertEqual(header,(FOLDER/'DisabledRegistry.h').read_text())
  self.assertTrue(all(r['chamber_knowledge']=='Unknown' and not r['native_runtime_admission'] and r['configuration']['values']['fireLogicType']==0 for r in out['descriptors']))
 def test_automatic_review_cannot_enable_singlefire(self):
  j=extend()
  for row in j['descriptors']:
   review=dict(key=row['key'],descriptor_digest=row['descriptor_digest'],family_proof=registry.FAMILY,evidence_sha256='e'*64)
   with self.assertRaisesRegex(ValueError,'SingleFire'):registry.build(j,[review])
 def test_corrupt_review_proof_never_materializes_enum(self):
  for key in proof.EXPECTED_PROOF:
   bad=copy.deepcopy(proof.EXPECTED_PROOF);bad.pop(key)
   with self.subTest(key=key),self.assertRaises(ValueError):proof.apply(jobs(),bad)
  bad=copy.deepcopy(proof.EXPECTED_PROOF);bad['enum']['fields'][0]['value']=2
  with self.assertRaises(ValueError):proof.apply(jobs(),bad)
 def test_nonzero_bolt_or_unreviewed_feed_stays_deferred(self):
  for key,value in [('boltTime',.1),('boltDelay',.1),('holdBoltUntilFireRelease',True),('holdBoltUntilZoomRelease',True),('reloadType',0)]:
   j=jobs()
   for row in j['descriptors']:
    row['configuration']['values'][key]=value;row['descriptor_digest']=descriptor.digest(row['configuration'])
   out=extend(j);self.assertEqual(registry.build(out)[1]['rows'],0)
   self.assertEqual(out['singlefire_candidate_extension']['rows_reclassified'],0)
 def test_wrong_enum_timing_or_descriptor_cannot_be_enabled_by_marker(self):
  for fault in range(5):
   j=extend();row=j['descriptors'][0]
   if fault==0:row['configuration']['values']['fireLogicType']=2
   elif fault==1:row['configuration']['timing'][4]['expected']=2
   elif fault==2:row['singlefire_magazine_proof_digest']='0'*64
   elif fault==3:row['authored_symbols']['FireLogic.FireLogicType']='fltAutomaticFire'
   else:row['configuration']['values']['baseCapacity']=99
   if fault in (1,2,3):row['descriptor_digest']=descriptor.digest(row['configuration']);self.assertEqual(registry.build(j)[1]['rows'],1)
   else:
    with self.assertRaises(ValueError):registry.build(j)
 def test_asserted_chamber_or_runtime_admission_is_not_proof(self):
  for key,value in [('chamber_knowledge','Occupied'),('native_runtime_admission',True),('slide_ready_admission',True)]:
   p=copy.deepcopy(proof.EXPECTED_PROOF);p[key]=value
   with self.assertRaises(ValueError):proof.apply(jobs(),p)
 def test_loaded_count_and_listenerless_state_do_not_prove_a_shot(self):
  evidence=json.loads((FOLDER/'instruction-evidence.json').read_text())
  self.assertEqual(len(evidence['cases']),22);self.assertFalse(evidence['chamber_knowledge_proved'])
  self.assertFalse(evidence['actual_shot_verified']);self.assertFalse(evidence['game_process_opened'])
  for r in evidence['shot_negative_controls']:
   self.assertFalse(r['shot_receipt']);self.assertEqual(r['chamber_knowledge'],'Unknown')
   self.assertEqual(r['before']['loaded'],r['after']['loaded'])
 def test_instruction_pin_and_independent_original_return_conservation(self):
  raw=(FOLDER/'instruction-evidence.json').read_bytes()
  self.assertEqual(hashlib.sha256(raw).hexdigest(),proof.EXPECTED_PROOF['instruction_evidence_sha256'])
  cases=json.loads(raw)['cases'];seen=set()
  for c in cases:
   self.assertEqual(c['patches'],c['restores']);self.assertTrue(c['original_context_restored'])
   if c.get('scenario')!='remove_return_original':continue
   rounds,reserve=c['requested_rounds'],c['initial_reserve']
   self.assertEqual((c['final_loaded'],c['final_reserve']),(rounds,reserve))
   self.assertEqual(c['adjustments'],[dict(frame=-1,delta=-rounds,loaded=0,reserve=reserve),dict(frame=180,delta=rounds,loaded=rounds,reserve=reserve)])
   for t in c['transitions']:
    self.assertEqual((t['loaded'],t['reserve']),((0,reserve) if t['frame']<180 else (rounds,reserve)))
   seen.add((c['asset'],rounds,reserve))
  self.assertEqual(seen,{(name,rounds,reserve) for name in ('MP443','MP443_sp') for rounds in (8,17) for reserve in (0,51)})

if __name__=='__main__':unittest.main()
