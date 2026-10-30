"""Bounded data/export checks; no compiler, runtime, game or asset extraction."""
from pathlib import Path
import copy
import hashlib
import json
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools'))
import bc2_shotgun_reload_registry as shotgun
import bc2_magazine_registry_header as magazine

HERE=Path(__file__).resolve().parent
batch=json.loads((HERE/'batch.json').read_text(encoding='utf-8'))
generated=shotgun.header(batch)
assert generated.encode()==(ROOT/'src/games/bc2/Bc2ShotgunReloadRegistry.h').read_bytes()
assert len(batch['tube_descriptors'])==25
assert len({(r['asset'],r['resource']) for r in batch['inventory']})==39
assert all(not g['gaps'] for g in batch['geometry_groups'].values())
_,magazine_summary=magazine.build(batch['magazine_jobs'])
assert magazine_summary['rows']==14 and magazine_summary['enabled']==0
row=next(r for r in batch['tube_descriptors'] if r['configuration']['assetName']=='T194_sp')
review={'key':row['key'],'descriptor_digest':row['descriptor_digest'],
        'native_family':shotgun.FAMILY,'instruction_proof_digest':shotgun.PROOF_DIGEST,
        'reviewer':'synthetic-validation-only','evidence_sha256':'0'*64}
# Synthetic reviews are used only in-memory to exercise exact join rejection.
# No reviewed header or admission document is saved by this checker.
for field,value in [('key','bc2:shotgun-descriptor:'+'0'*64),
                    ('descriptor_digest','0'*64),('instruction_proof_digest','0'*64),
                    ('native_family','bc2.automatic-magazine-zero-bolt.v1')]:
    changed=dict(review);changed[field]=value
    try:shotgun.header(batch,[changed])
    except ValueError:pass
    else:raise AssertionError('Foreign review accepted: '+field)
bad=copy.deepcopy(batch);bad['tube_descriptors'].append(copy.deepcopy(row))
try:shotgun.header(bad)
except ValueError:pass
else:raise AssertionError('Duplicate exact native selector accepted')
bad=copy.deepcopy(batch);bad['tube_descriptors'][0]['configuration']['values']['reloadTime']=.123
try:shotgun.header(bad)
except ValueError:pass
else:raise AssertionError('Changed consumed scalar accepted')
receipt={'schema':'fvr.bc2.shotgun_batch_data_checks.v1','data_checks_passed':True,
         'header_sha256':hashlib.sha256(generated.encode()).hexdigest(),
         'summary':batch['summary'],'common_magazine_export':magazine_summary,
         'negative_checks':['foreign_key','foreign_descriptor_digest','foreign_proof',
                            'foreign_family','duplicate_selector','changed_scalar'],
         'compiled':False,'actual_game':False,'headset':False,
         'default_new_native_admissions':0,'default_existing_SPAS_admission_preserved':True}
(HERE/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print(json.dumps(receipt,sort_keys=True))
