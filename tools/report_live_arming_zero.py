"""Independent active-hook zero evidence; never complete native acceptance."""
import json,argparse
from pathlib import Path
from preflight_reload_probe import validate

def analyze(before,live,trace):
 f=trace.get('gameplay',{}).get('arming_empty_probe',{});r=trace.get('gameplay',{}).get('reload_flow',{})
 failures=[]
 if any(x.get('input_or_focus_changes') is not False for x in (before,live)):failures.append('capture_input_focus_provenance_missing')
 try:
  before_summary=validate(before,expected_asset='SPAS12_sp');live_summary=validate(live,expected_asset='SPAS12_sp')
  if before.get('summary')!=before_summary or live.get('summary')!=live_summary:failures.append('summary_not_validated_capture')
 except (ValueError,KeyError,TypeError,IndexError,OverflowError):failures.append('native_preflight_validation_failed')
 if trace.get('pid')!=before.get('pid') or type(trace.get('pid')) is not int or trace['pid']<=0:failures.append('trace_process_mismatch')
 if before.get('executable_sha256')!='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258':failures.append('executable_not_pinned')
 owner=f.get('owner',[]);cycle=f.get('cycle',0);start=f.get('start_ns',0);cancel=f.get('cancel_ns',0)
 if not isinstance(owner,list) or len(owner)!=7 or any(type(x) is not int or x<=0 for x in owner):failures.append('fixture_owner_unknown')
 if type(cycle) is not int or cycle<=0 or not 0<r.get('start_ns',0)<start<cancel<r.get('start_ns',0)+int(r.get('window_seconds',0)*1e9):failures.append('operation_window_unknown')
 if f.get('original_loaded')!=0 or f.get('original_reserve')!=before.get('summary',{}).get('reserve'):failures.append('fixture_counts_not_joined')
 events=trace.get('gameplay',{}).get('physical_reload',{}).get('events',[])
 if not any(e.get('event')==2 and e.get('cycle')==cycle and 0<=start-e.get('now_ns',0)<=100000000 for e in events):failures.append('actual_start_not_joined')
 if not any(e.get('event')==5 and e.get('cycle')==cycle and 0<=e.get('now_ns',0)-cancel<=100000000 for e in events):failures.append('actual_cancel_not_joined')
 if f.get('phase')!=4 or f.get('failure')!=0:failures.append('fixture_not_done')
 if not before.get('passed') or not live.get('passed'):failures.append('coherent_preflight_missing')
 if live.get('pid')!=before.get('pid') or live.get('executable_sha256')!=before.get('executable_sha256'):failures.append('process_pin_mismatch')
 a=before.get('summary',{});z=live.get('summary',{})
 if a.get('loaded')!=1 or z.get('loaded')!=0 or z.get('reserve')!=a.get('reserve') or z.get('capacity')!=a.get('capacity'):failures.append('counts_not_conserved')
 originals=before.get('samples',[]);samples=live.get('samples',[])
 if len(originals)!=2 or len(samples)!=2:failures.append('missing_samples')
 else:
  keys=('client_owner','server_player','server_soldier','server_item','server_firing','asset_name','asset_path','weapon_data','firing_data','ammo_address')
  for sample in originals+samples:
   o=sample.get('client_owner',{})
   if owner[:4]!=[o.get('player'),o.get('actor'),o.get('weak'),o.get('selected_weapon')]:failures.append('fixture_native_owner_mismatch')
  for sample in samples:
   if sample.get('state',{}).get('loaded')!=0 or sample.get('state',{}).get('reserve')!=a.get('reserve'):failures.append('server_sample_not_zero')
   if any(sample.get(k)!=originals[-1].get(k) for k in keys):failures.append('native_identity_changed')
   at=sample.get('monotonic_ns',0)
   if not f.get('cancel_ns',0)+2200000000<=at<r.get('start_ns',0)+int(r.get('window_seconds',0)*1e9):failures.append('not_post_cancel_inside_hook_window')
 return {'status':'independent_live_zero_observed' if not failures else 'inconclusive','native_verified':False,'failures':failures,'ordinary_reload_positive_control_required':True}
def analyze_attempts(before,attempts,trace):
 results=[];contradictions=[];verified=[];gaps=[]
 for n,live in enumerate(attempts):
  result=analyze(before,live,trace);results.append(result)
  if result['status']=='independent_live_zero_observed':verified.append(n);continue
  # A failed structural reader contributes no authority, but can be retried.
  # Every returned field is still checked; never discard an observed conflict.
  known_gap=live.get('rejected') in (['owner changed across all-three read'],['server/client ownership changed during read'])
  if live.get('passed') or not known_gap:
   contradictions.append({'attempt':n,'reason':'nonretryable_capture_failure','audit':result});continue
  if (live.get('pid')!=before.get('pid') or live.get('executable_sha256')!=before.get('executable_sha256') or
      any(live.get(k) is not v for k,v in [('read_only',True),('native_calls',False),('process_writes',False),('input_or_focus_changes',False)])):
   contradictions.append({'attempt':n,'reason':'invalid_capture_provenance'})
  gaps.append({'attempt':n,'rejected':live.get('rejected'),'retained_samples':len(live.get('samples',[]))})
  for sample in live.get('samples',[]):
   original=before.get('samples',[{}])[-1]
   if any(sample.get(k)!=original.get(k) for k in ('client_owner','server_player','server_soldier','server_item','server_firing','weapon_data','firing_data','ammo_address','asset_name','asset_path')):
    contradictions.append({'attempt':n,'reason':'observed_owner_conflict'})
   if any(x.get('capacity')!=before.get('summary',{}).get('capacity') for x in sample.get('branches',[])):contradictions.append({'attempt':n,'reason':'observed_capacity_conflict'})
   states=sample.get('branches',[])+[sample.get('state',{})]
   if any(x.get('loaded')!=0 or x.get('reserve')!=before.get('summary',{}).get('reserve') for x in states):
    contradictions.append({'attempt':n,'reason':'observed_count_conflict'})
 return {'status':'independent_live_zero_observed' if verified and not contradictions else 'inconclusive',
         'native_verified':False,'selected_attempt':verified[0] if verified and not contradictions else None,
         'attempt_audits':results,'contradictions':contradictions,'coherence_gaps':gaps,'observed_attempts':verified,'continuous_stability_verified':False,'ordinary_reload_positive_control_required':True}

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('before',type=Path);p.add_argument('live',type=Path);p.add_argument('trace',type=Path);p.add_argument('--attempts',type=Path);a=p.parse_args()
 load=lambda x:json.loads(x.read_text(encoding='utf-8-sig'))
 before,live,trace=map(load,(a.before,a.live,a.trace))
 result=analyze_attempts(before,[load(x) for x in sorted(a.attempts.glob('reload-live-attempt-*.json'))],trace) if a.attempts else analyze(before,live,trace)
 print(json.dumps(result,indent=2))
