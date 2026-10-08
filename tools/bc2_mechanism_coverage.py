"""Exact-package mechanism data gap report; no native admission or category inference."""
import argparse,json,hashlib
from pathlib import Path
from bc2_weapon_capability_report import report as validate_package_receipt
FEEDS=('detachable_magazine','internal_tube','belt','clip','single')
ACTIONS=('automatic','pump','bolt','charging_handle','slide')
COMMON=('exact_owner_configuration','original_fresh_observation','native_completion_consumer')
REQUIRED={
 'automatic':('native_automatic_action_semantics',),
 'pump':('part_rig_binding','closed_part_boundary','travel_axis_detents','mechanism_hand_claim','native_shot_receipt','native_cycle_pause_resume'),
 'bolt':('part_rig_binding','closed_part_boundary','travel_axis_detents','rotation_lift_detents','mechanism_hand_claim','native_shot_receipt','native_cycle_pause_resume'),
 'charging_handle':('part_rig_binding','closed_part_boundary','travel_axis_detents','mechanism_hand_claim','native_action_completion'),
 'slide':('part_rig_binding','closed_part_boundary','travel_axis_detents','slide_lock_release_semantics','mechanism_hand_claim','native_action_completion')}
def load(path):
 def pairs(items):
  d={}
  for k,v in items:
   if k in d:raise ValueError('duplicate JSON key '+k)
   d[k]=v
  return d
 return json.loads(Path(path).read_text(encoding='utf-8-sig'),object_pairs_hook=pairs)
def coverage(index,probe,bindings):
 # Reuse the real pipeline's immutable configuration/mesh/rig digest and
 # compiled receipt validation. A label or copied variant cannot bind action.
 validate_package_receipt(index,probe)
 ids=[r['package_id'] for r in index['rows']]
 if len(set(ids))!=len(ids):raise ValueError('duplicate package')
 joined={}
 for r in bindings.get('rows',[]):
  pid=r['package_id']
  if pid not in ids or pid in joined:raise ValueError('unknown or duplicate mechanism package')
  if r['feed'] not in FEEDS or any(r[k] not in ACTIONS for k in ('after_empty_feed','after_shot')):raise ValueError('unknown mechanism')
  # Explicit authored data binding; no class/weapon-name inference. This input
  # cannot carry runtime/native review grants, even if caller sets booleans.
  if r.get('native_admitted',False) is not False or r.get('runtime_enabled',False) is not False:raise ValueError('data cannot admit native cycle')
  joined[pid]=r
 chamber=(probe.get('mechanisms') or {}).get('chamber_known')
 if chamber not in (None,True,False) or (chamber is not None and type(chamber)is not bool):raise ValueError('invalid compiled chamber declaration')
 rows=[]
 for p in index['rows']:
  b=joined.get(p['package_id']);purposes={}
  for purpose in ('after_empty_feed','after_shot'):
   action=b[purpose] if b else None
   missing=list(COMMON)+['native_chamber_observation']
   if action is None:missing+=['exact_package_action_descriptor']
   else:missing+=list(REQUIRED[action])
   # Compiled chamberKnown=true alone is not owner-scoped live evidence.
   purposes[purpose]={'action':action,'missing_native_evidence':missing,'runtime_enabled':False}
  rows.append({'package_id':p['package_id'],'asset':p['asset'],'identity':p['identity'],
    'feed':b['feed'] if b else None,'feed_descriptor_status':'explicit_data_only' if b else 'missing',
    'feed_missing':[] if b else ['exact_package_feed_descriptor'],
    'physical_feed_runtime_status':'not_provided','purposes':purposes,'compiled_chamber_known':chamber,
    'live_chamber_authority':'not_provided','fully_playable_manual':False,'runtime_enabled':False})
 return {'schema':'fvr.bc2.mechanism-coverage.v1','rows':rows,'excluded':index.get('excluded',[]),'action_requirements':REQUIRED,'native_admission_granted':False,'runtime_enabled':False,'limits':['Feed/action binding is explicit package data; no category or loaded-count chamber inference.','Pose curves and gesture policies cannot prove native shot/chamber/action completion.','Compiled chamber declaration never grants current owner-scoped authority.']}
def main():
 p=argparse.ArgumentParser();p.add_argument('--index',type=Path,required=True);p.add_argument('--probe',type=Path,required=True);p.add_argument('--bindings',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists() or a.output.resolve() in {v.resolve() for v in (a.index,a.probe,a.bindings) if v}:p.error('new non-input output required')
 try:r=coverage(load(a.index),load(a.probe),load(a.bindings) if a.bindings else {'rows':[]})
 except (KeyError,TypeError,ValueError,OSError) as e:p.error(str(e))
 r['input_sha256']={str(v):hashlib.sha256(v.read_bytes()).hexdigest() for v in (a.index,a.probe,a.bindings) if v};a.output.write_text(json.dumps(r,indent=2)+'\n',encoding='utf8')
if __name__=='__main__':main()
