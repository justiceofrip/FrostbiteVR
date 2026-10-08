"""Evidence-bound candidate data for AutomaticFire2's inactive stock bolt fields.

Static dispatch proof is not runtime admission. No cycle request, native binding,
ammunition authority or physical charging-handle interaction is granted here.
"""
import copy,hashlib,json,math

EXPECTED_PROOF={
 'schema':'fvr.bc2.automatic_stock_bolt_static.v1',
 'exe_sha256':'3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258',
 'whole_function_fingerprints':{'Update':'4a6ecc3d95700d53','Step':'71118b4d657473d3','Timed':'7344b32b39a9bd85','Order':'3cae7f07de53d0f7','Transfer':'d83485215d90a16a'},
 'reflected_firelogic_bolt_offset':68,
 'reflected_bolt_fields':{'BoltActionTime':0,'BoltActionDelay':4,'HoldBoltActionUntilFireRelease':9,'HoldBoltActionUntilZoomRelease':8},
 'automatic_fire_logic':2,'ordinary_bolt_states':[7,8],'ordinary_reload_states':[10,11,12],'ordinary_reload_return_state':1,
 'type2_return_skips_hold':True,'type1_only_enters_bolt_hold':True,'type2_entry_and_repeat_state':9,'native_runtime_admission':False}
def digest(v):return hashlib.sha256(json.dumps(v,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()
PROOF_DIGEST=digest(EXPECTED_PROOF)
FIELDS=('boltDelay','boltTime','holdBoltUntilFireRelease','holdBoltUntilZoomRelease')
def stock_values(v):
 return any(v.get(k) for k in FIELDS)
def candidate_dispatch(row):
 v=row['configuration']['values']
 return row.get('automatic_stock_bolt_proof_digest')==PROOF_DIGEST and stock_values(v) and \
  [v.get(k) for k in ('fireLogicType','reloadType','fireInputAction','reloadInputAction')]==[2,1,8,29] and \
  all(type(v.get(k)) in (float,int) and math.isfinite(v[k]) and 0<=v[k]<=10 for k in FIELDS[:2]) and \
  all(type(v.get(k)) is bool for k in FIELDS[2:])
def apply(jobs,proof):
 # Serialized proof marker is deliberately explicit and build-specific. It only
 # removes this data-extraction deferral; existing runtime review stays separate.
 if proof!=EXPECTED_PROOF or digest(proof)!=PROOF_DIGEST:raise ValueError('Unreviewed AutomaticFire2 stock-bolt proof')
 if jobs.get('schema')!='fvr.bc2.magazine_descriptor_jobs' or jobs.get('schema_version')!=1:raise ValueError('Expected descriptor jobs schema1')
 if jobs.get('runtime_enabled') is not False or jobs.get('registry_change') is not False:raise ValueError('Expected disabled descriptor data only')
 out=copy.deepcopy(jobs);changed=0
 for row in out['descriptors']:
  if digest(row['configuration'])!=row['descriptor_digest']:raise ValueError('Changed exact configuration')
  if 'authored_bolt_fields_require_shared_dispatch_review' not in row['deferred_reasons']:continue
  row['automatic_stock_bolt_proof_digest']=PROOF_DIGEST
  if not candidate_dispatch(row):row.pop('automatic_stock_bolt_proof_digest');continue
  row['deferred_reasons'].remove('authored_bolt_fields_require_shared_dispatch_review')
  row['same_reviewed_dispatch_shape']=not row['deferred_reasons']
  if not row['deferred_reasons'] and 'resolve_shared_dispatch_or_data_gap' in row['jobs']:row['jobs'].remove('resolve_shared_dispatch_or_data_gap')
  changed+=1
 out['summary']['deferred']=sum(bool(r['deferred_reasons'])for r in out['descriptors'])
 out['automatic_stock_bolt_candidate_extension']={'proof_digest':PROOF_DIGEST,'rows_reclassified':changed,'runtime_enabled':False,
   'limits':['Ordinary AutomaticFire2 routes bypass bolt states7/8; exact authored fields remain unchanged.',
             'External/restored bolt states and virtual callbacks are not granted by static dispatch proof.',
             'Zero-bolt review documents cannot enable these candidates; native fixture admission remains separate.']}
 return out
