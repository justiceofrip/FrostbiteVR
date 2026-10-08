import unittest, hashlib, json, copy, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from bc2_mechanism_coverage import coverage
class Tests(unittest.TestCase):
 def test_forged_identity_and_probe_are_rejected(self):
  i=copy.deepcopy(self.i);i['rows'][0]['identity']['configuration_resource']='objects/other.dbx'
  self.assertRaises(ValueError,coverage,i,self.p,self.bind('pump'))
  i=copy.deepcopy(self.i);i['rows'][0]['package_id']='p'
  self.assertRaises(ValueError,coverage,i,self.p,self.bind('pump'))
  p=copy.deepcopy(self.p);p['runtime_enabled']=True
  self.assertRaises(ValueError,coverage,self.i,p,self.bind('pump'))
 def test_same_label_variants_never_borrow_binding(self):
  i=copy.deepcopy(self.i);row=copy.deepcopy(i['rows'][0]);row['identity']['configuration_resource']='objects/variant.dbx'
  row['package_id']=hashlib.sha256(json.dumps(row['identity'],sort_keys=True,separators=(',',':')).encode()).hexdigest();i['rows'].append(row)
  rows=coverage(i,self.p,self.bind('pump'))['rows']
  self.assertEqual(rows[0]['feed_descriptor_status'],'explicit_data_only');self.assertEqual(rows[1]['feed_descriptor_status'],'missing')
  self.assertEqual(rows[0]['physical_feed_runtime_status'],'not_provided');self.assertFalse(rows[0]['fully_playable_manual'])
 def test_feed_action_composition_has_no_category_dispatch(self):
  for feed in ('detachable_magazine','internal_tube','belt','clip','single'):
   b=self.bind('bolt');b['rows'][0]['feed']=feed;r=coverage(self.i,self.p,b)['rows'][0]
   self.assertEqual(r['feed'],feed);self.assertEqual(r['purposes']['after_shot']['action'],'bolt');self.assertFalse(r['runtime_enabled'])
 def setUp(self):identity={'configuration_resource':'objects/gun.dbx','configuration_sha256':'b'*64,'instance_guid':'C'*32,'rig':'a7f219a1426216ab','meshes':[['objects/gun_mesh/GUID','d'*64,'e'*64]]}; self.pid=hashlib.sha256(json.dumps(identity,sort_keys=True,separators=(',',':')).encode()).hexdigest(); self.i={'schema':'fvr.bc2.weapon-package-index.prototype.v1','native_admission_granted':False,'rows':[{'package_id':self.pid,'asset':'unknown-label','identity':identity,'components':{},'missing_components':[],'runtime_enabled':False}]}; self.p={'schema':'fvr.bc2.compiled-capabilities.v1','runtime_enabled':False,'mechanisms':{'chamber_known':False}}
 def bind(self,action):return {'rows':[{'package_id':self.pid,'feed':'internal_tube','after_empty_feed':'charging_handle','after_shot':action,'native_admitted':False}]}
 def test_unknown(self):
  r=coverage(self.i,self.p,{'rows':[]})['rows'][0];self.assertIsNone(r['feed']);self.assertIn('exact_package_action_descriptor',r['purposes']['after_shot']['missing_native_evidence'])
 def test_separate_action(self):
  r=coverage(self.i,self.p,self.bind('pump'))['rows'][0];self.assertEqual(r['purposes']['after_empty_feed']['action'],'charging_handle');self.assertEqual(r['purposes']['after_shot']['action'],'pump');self.assertIn('native_shot_receipt',r['purposes']['after_shot']['missing_native_evidence'])
 def test_all_actions_fail_closed(self):
  for a in ('automatic','pump','bolt','charging_handle','slide'):
   r=coverage(self.i,{**self.p,'mechanisms':{'chamber_known':True}},self.bind(a));self.assertFalse(r['runtime_enabled']);self.assertIn('native_chamber_observation',r['rows'][0]['purposes']['after_shot']['missing_native_evidence'])
 def test_invalid(self):
  for change in ({'native_admitted':True},{'package_id':'other'},{'after_shot':'shotgun'},{'feed':'AR'}):
   b=self.bind('pump');b['rows'][0].update(change);self.assertRaises(ValueError,coverage,self.i,self.p,b)
  b=self.bind('pump');b['rows']*=2;self.assertRaises(ValueError,coverage,self.i,self.p,b)
if __name__=='__main__':unittest.main()
