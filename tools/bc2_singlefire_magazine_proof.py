"""Exact reflected SingleFire candidate data; never runtime/chamber admission."""
import copy,hashlib,json
import bc2_magazine_descriptor_manifest as descriptor

def digest(v):return hashlib.sha256(json.dumps(v,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()
EXPECTED_PROOF={'schema': 'fvr.bc2.singlefire_magazine_static.v1',
 'executable_sha256': '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258',
 'enum': {'enum': 'FireLogicType',
          'metadata_rva': 25147952,
          'table_rva': 25298032,
          'fields': [{'name': 'fltSingleFire', 'value': 0},
                     {'name': 'fltSingleFireWithBoltAction', 'value': 1},
                     {'name': 'fltAutomaticFire', 'value': 2},
                     {'name': 'fltHoldAndRelease', 'value': 3},
                     {'name': 'fltDetonatedFiring', 'value': 4}],
          'metadata_sha256': '65e4574abc7d2f053377345798732758fa03e4f37f74b07bfe3033f2d9f4a8c2',
          'table_sha256': '9e4b986196f18dc51dc02e17148a56e42e2590e9a9f973d53fd64104e1f470fa'},
 'functions': {'Update': {'rva': 3051520,
                          'size': 397,
                          'sha256': '0babdb25cdcc8f494a544d27cdf628c67f495215872b1ff5fa90f62fd5ca426e'},
               'Step': {'rva': 3039952,
                        'size': 1732,
                        'sha256': '122c98fefa4503c8c98688477f9cf5a1bfbbccb47e84ce6b303898c199ad908d'}},
 'instruction_evidence_sha256': '76c083751b0fb33bc2161b0158673ca1fa0d3d1d31f7296a31e5d4a389a90351',
 'fire_logic': 0,
 'reload_type': 1,
 'input_fire': 8,
 'input_reload': 29,
 'ordinary_reload_states': [11, 12, 1, 2],
 'scoped_empty_inhibition': True,
 'original_round_conservation': True,
 'chamber_knowledge': 'Unknown',
 'native_runtime_admission': False,
 'slide_ready_admission': False}
PROOF_DIGEST=digest(EXPECTED_PROOF)
DEFERRED='unreviewed_dispatch_symbol:FireLogic.FireLogicType:fltSingleFire'

def candidate_dispatch(row):
    v=row['configuration']['values']
    return row.get('singlefire_magazine_proof_digest')==PROOF_DIGEST and \
        [v.get(k) for k in ('fireLogicType','reloadType','fireInputAction','reloadInputAction')]==[0,1,8,29] and \
        all(v.get(k)==0 for k in ('boltDelay','boltTime','holdBoltUntilFireRelease','holdBoltUntilZoomRelease')) and \
        row.get('authored_symbols',{}).get('FireLogic.FireLogicType')=='fltSingleFire'

def apply(jobs,proof):
    if proof!=EXPECTED_PROOF or digest(proof)!=PROOF_DIGEST:raise ValueError('Unreviewed exact SingleFire proof')
    if jobs.get('schema')!='fvr.bc2.magazine_descriptor_jobs' or jobs.get('schema_version')!=1:raise ValueError('Expected descriptor jobs1')
    if jobs.get('runtime_enabled') is not False or jobs.get('registry_change') is not False:raise ValueError('Expected disabled data only')
    out=copy.deepcopy(jobs);changed=0
    for row in out['descriptors']:
        c=row['configuration'];v=c['values']
        if digest(c)!=row['descriptor_digest']:raise ValueError('Changed consumed descriptor')
        if DEFERRED not in row['deferred_reasons']:continue
        # Zero-bolt is a strict extraction boundary, never proof of a chamber.
        if any(v.get(k) for k in ('boltDelay','boltTime','holdBoltUntilFireRelease','holdBoltUntilZoomRelease')):continue
        if (v.get('reloadType'),v.get('fireInputAction'),v.get('reloadInputAction'))!=(1,8,29):continue
        if row.get('authored_symbols',{}).get('FireLogic.ReloadLogic')!='rlWeaponSwitchCancelsUnfinishedReload':continue
        if 'fireLogicType' in v or c.get('admission')!='Candidate':raise ValueError('SingleFire numeric word already substituted')
        row['prior_descriptor_digest']=row['descriptor_digest'];v['fireLogicType']=proof['fire_logic']
        row['singlefire_magazine_proof_digest']=PROOF_DIGEST
        if not candidate_dispatch(row):raise ValueError('Exact authored SingleFire symbols differ')
        words=[(0x10,int(descriptor.bits(v['reloadThreshold']),16)),(0x14,int(descriptor.bits(v['reloadDelay']),16)),
               (0x18,int(descriptor.bits(v['reloadTime']),16)),(0x20,1),(0x24,0),(0x2c,0)]
        c['timing']=[dict(offset=o,expected=w,expected_bits=f'{w:08x}',comparison='Bits') for o,w in words]
        row['deferred_reasons'].remove(DEFERRED);row['same_reviewed_dispatch_shape']=not row['deferred_reasons']
        row['descriptor_digest']=digest(c);row['chamber_knowledge']='Unknown';row['native_runtime_admission']=False
        row['jobs']=[job for job in row.get('jobs',[]) if job!='resolve_shared_dispatch_or_data_gap' or row['deferred_reasons']]
        row['jobs']+=['independent_singlefire_native_family_review','exact_magazine_hand_contact','slide_ready_and_chamber_boundary']
        changed+=1
    if 'summary' in out:out['summary']['deferred']=sum(bool(r['deferred_reasons']) for r in out['descriptors'])
    out['singlefire_candidate_extension']=dict(proof_digest=PROOF_DIGEST,rows_reclassified=changed,runtime_enabled=False,
        chamber_knowledge='Unknown',limits=['Isolated original instructions, synthetic objects and empty listeners only.',
        'No native owners, three-copy convergence, shot, chamber or slide readiness receipt.'])
    return out
