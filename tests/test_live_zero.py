import unittest,sys,copy,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from report_live_arming_zero import analyze,analyze_attempts
def fixture():
 # Minimal sanitized schema, no dependency on private monitor artifacts.
 owner={'player':101,'actor':102,'weak':103,'selected_weapon':104}
 state={'current':2,'next':2,'loaded':1,'reserve':24,'capacity':8,'finite_ammo':True}
 sample={'client_owner':owner,'server_player':201,'server_soldier':202,'server_item':203,'server_firing':204,
         'asset_name':'SPAS12_sp','asset_path':'test/SPAS12_sp','weapon_data':301,'firing_data':302,'ammo_address':303,
         'identity_coherent':True,'branches':[copy.deepcopy(state) for _ in range(3)],
         'state':{'current':2,'next':2,'loaded':1,'reserve':24},'monotonic_ns':100000000}
 b={'passed':True,'pid':123,'executable_sha256':'3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258',
    'read_only':True,'native_calls':False,'process_writes':False,'input_or_focus_changes':False,'rejected':[],
    'samples':[copy.deepcopy(sample),copy.deepcopy(sample)],
    'summary':{'asset':'SPAS12_sp','loaded':1,'reserve':24,'capacity':8,'prepare_shots':False,'authority_proven':False,'native_reload_verified':False}}
 b['samples'][1]['monotonic_ns']+=30000000
 t={'pid':123,'gameplay':{'arming_empty_probe':{'phase':4,'failure':0,'cycle':1,'start_ns':6000000000,'cancel_ns':6300000000,
       'owner':[101,102,103,104,1,1,1],'original_loaded':0,'original_reserve':24},
       'reload_flow':{'start_ns':200000000,'window_seconds':30},
       'physical_reload':{'events':[{'event':2,'cycle':1,'now_ns':5999000000},{'event':5,'cycle':1,'now_ns':6300001000}]}}}
 l=copy.deepcopy(b);l['summary']['loaded']=0
 for n,x in enumerate(l['samples']):
  x['monotonic_ns']=9300000000+n*30000000;x['state']['loaded']=0
  for branch in x['branches']:branch['loaded']=0
 return b,l,t
class Tests(unittest.TestCase):
 def test_sanitized_native_schema_model(self):
  r=analyze(*fixture());self.assertEqual(r['status'],'independent_live_zero_observed');self.assertFalse(r['native_verified'])
 def test_isolated_join_attacks(self):
  for fault in ('tracepid','fixtureowner','cycle','start','reserve','summaryonly','readonly','focus','coherent','beforefirst','early','after','serverstate'):
   b,l,t=fixture();f=t['gameplay']['arming_empty_probe']
   if fault=='tracepid':t['pid']+=1
   if fault=='fixtureowner':f['owner'][0]+=1
   if fault=='cycle':f['cycle']+=1
   if fault=='start':f['start_ns']=0
   if fault=='reserve':f['original_reserve']+=1
   if fault=='summaryonly':l['samples'][0]['branches'][0]['loaded']=8
   if fault=='readonly':l['read_only']=False
   if fault=='focus':l['input_or_focus_changes']=True
   if fault=='coherent':l['samples'][0]['identity_coherent']=False
   if fault=='beforefirst':b['samples'][0]['client_owner']['selected_weapon']+=4
   if fault=='early':l['samples'][0]['monotonic_ns']=f['cancel_ns']+1
   if fault=='after':l['samples'][0]['monotonic_ns']=t['gameplay']['reload_flow']['start_ns']+31000000000
   if fault=='serverstate':l['samples'][0]['state']['loaded']=8
   with self.subTest(fault=fault):self.assertEqual(analyze(b,l,t)['status'],'inconclusive')
class Attempts(unittest.TestCase):
 def test_retry_preserves_conflicts(self):
  b,l,t=fixture();missing={k:l[k] for k in ('pid','executable_sha256','read_only','native_calls','process_writes','input_or_focus_changes')}
  missing.update(passed=False,samples=[],rejected=['owner changed across all-three read'])
  self.assertEqual(analyze_attempts(b,[missing,l],t)['status'],'independent_live_zero_observed')
  for fault in ('counts','owner','different_error','passed_bad','provenance','asset','path','capacity'):
   bad=copy.deepcopy(missing)
   if fault=='counts':bad['samples']=[copy.deepcopy(l['samples'][0])];bad['samples'][0]['state']['loaded']=1
   if fault=='owner':bad['samples']=[copy.deepcopy(l['samples'][0])];bad['samples'][0]['client_owner']['player']+=1
   if fault=='different_error':bad['rejected']=['native ammunition changed during preflight']
   if fault=='passed_bad':bad=copy.deepcopy(l);bad['samples'][0]['branches'][0]['reserve']=23
   if fault=='provenance':bad['process_writes']=True
   if fault in ('asset','path','capacity'):
    bad['samples']=[copy.deepcopy(l['samples'][0])]
    if fault=='asset':bad['samples'][0]['asset_name']='OTHER'
    if fault=='path':bad['samples'][0]['asset_path']='OTHER'
    if fault=='capacity':bad['samples'][0]['branches'][0]['capacity']=9
   self.assertEqual(analyze_attempts(b,[bad,l],t)['status'],'inconclusive')
 def test_source_proved_server_client_gap(self):
  b,l,t=fixture();gap=copy.deepcopy(l);gap['passed']=False;gap['samples']=[];gap['rejected']=['server/client ownership changed during read']
  r=analyze_attempts(b,[l,gap,l],t);self.assertEqual(r['status'],'independent_live_zero_observed');self.assertEqual(r['observed_attempts'],[0,2]);self.assertEqual(len(r['coherence_gaps']),1);self.assertFalse(r['continuous_stability_verified'])
  bad=copy.deepcopy(gap);bad['samples']=[copy.deepcopy(l['samples'][0])];bad['samples'][0]['state']['loaded']=8
  self.assertEqual(analyze_attempts(b,[bad,l],t)['status'],'inconclusive')
 def test_actual_run04_is_inconclusive_without_sampler(self):
  b,l,t=fixture();missing=copy.deepcopy(l);missing['passed']=False;missing['samples']=[];missing['rejected']=['owner changed across all-three read']
  self.assertEqual(analyze_attempts(b,[missing],t)['status'],'inconclusive')
if __name__=='__main__':unittest.main()
