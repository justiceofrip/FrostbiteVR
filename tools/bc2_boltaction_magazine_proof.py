"""Exact static magazine dispatch for bolt-action and inactive SingleFire fields.

This establishes descriptor extraction and the ordinary reload 11/12 path only.
It grants no hand custody, owed-bolt handoff, native cycle, chamber or live owner.
"""
import copy
import math
import bc2_magazine_descriptor_manifest as descriptor
from bc2_singlefire_magazine_proof import EXPECTED_PROOF as SINGLEFIRE_PROOF

BOLT_FAMILY = 'bc2.bolt-action-magazine-reload.v1'
SINGLE_STOCK_FAMILY = 'bc2.singlefire-magazine-inactive-stock-bolt.v1'
EXPECTED_PROOF = {
    'schema': 'fvr.bc2.boltaction_magazine_static.v1',
    'executable_sha256': SINGLEFIRE_PROOF['executable_sha256'],
    'enum': SINGLEFIRE_PROOF['enum'],
    'functions': {
        'Step': {'va': 0x6e62d0, 'size': 1732, 'sha256': '122c98fefa4503c8c98688477f9cf5a1bfbbccb47e84ce6b303898c199ad908d'},
        'Transfer': {'va': 0x6d6f40, 'size': 254, 'sha256': '1fa3fc0036f39fcdf7a6db01e916d079810fd3ae0b5ac79fdd054b8c84b3966a'},
    },
    'cached_evidence_sha256': {
        'decoded-branch-proof.json': '49b24ba7ce41bb5dbb3109bd38a39a2472f301ac84f254e77763e3ca93e9f67f',
        'Step-disassembly.txt': '9e4206a9ce26138df14486979b0976376966914fc5c5f84be3c62a0a4f1375ea',
        'Transfer-disassembly.txt': '93395daae78136ac4275527fa86ba5a2a545e6c74eb824a7c4c344cb803e0474',
        'functions.json': 'd3706cceb7c8ec47f7467e51de204ae5a95262d3a7ebf5279e9890b0d1f90321',
    },
    'dispatch': {
        'type1_only_enters_bolt_hold_va': 0x6e6625,
        'state7_delay_and_release_flags_va': 0x6e672d,
        'state8_bolt_time_va': 0x6e67a9,
        'reload_state10_va': 0x6e67bd,
        'reload_state11_va': 0x6e67fa,
        'reload_state12_transfer_va': 0x6e682c,
        'magazine_return1_va': 0x6e685a,
        'transfer_reload_type_va': 0x6d6fac,
        'ordinary_reload_states': [10, 11, 12, 1],
        'reload_type': 1, 'input_fire': 8, 'input_reload': 29,
        'reload11_12_do_not_read_bolt_fields': True,
        'singlefire0_ordinary_shot_does_not_enter_bolt7_8': True,
    },
    'native_runtime_admission': False,
    'owed_bolt_reload_handoff_admission': False,
    'chamber_knowledge': 'Unknown',
}
PROOF_DIGEST = descriptor.digest(EXPECTED_PROOF)
BOLT_DEFERRED = 'authored_bolt_fields_require_shared_dispatch_review'


def candidate_dispatch(row):
    v = row['configuration']['values']
    logic = v.get('fireLogicType')
    if row.get('boltaction_magazine_proof_digest') != PROOF_DIGEST or logic not in (0, 1):
        return False
    if [v.get(k) for k in ('reloadType', 'fireInputAction', 'reloadInputAction')] != [1, 8, 29]:
        return False
    if any(type(v.get(k)) not in (float, int) or not math.isfinite(v[k]) or not 0 <= v[k] <= 10 for k in ('boltDelay', 'boltTime')):
        return False
    if any(type(v.get(k)) is not bool for k in ('holdBoltUntilFireRelease', 'holdBoltUntilZoomRelease')):
        return False
    if not any(v[k] for k in ('boltDelay', 'boltTime', 'holdBoltUntilFireRelease', 'holdBoltUntilZoomRelease')):
        return False
    expected = 'fltSingleFireWithBoltAction' if logic == 1 else 'fltSingleFire'
    symbols = row.get('authored_symbols', {})
    return symbols.get('FireLogic.FireLogicType') == expected and symbols.get('FireLogic.ReloadLogic') == 'rlWeaponSwitchCancelsUnfinishedReload'


def family_for(row):
    if not candidate_dispatch(row):
        return None
    return BOLT_FAMILY if row['configuration']['values']['fireLogicType'] == 1 else SINGLE_STOCK_FAMILY


def apply(jobs, proof):
    if proof != EXPECTED_PROOF or descriptor.digest(proof) != PROOF_DIGEST:
        raise ValueError('Unreviewed exact bolt-action magazine instruction proof')
    if jobs.get('schema') != 'fvr.bc2.magazine_descriptor_jobs' or jobs.get('schema_version') != 1:
        raise ValueError('Expected descriptor jobs schema1')
    if jobs.get('runtime_enabled') is not False or jobs.get('registry_change') is not False:
        raise ValueError('Expected disabled data only')
    out = copy.deepcopy(jobs)
    changed = 0
    for row in out['descriptors']:
        c = row['configuration']; v = c['values']
        if descriptor.digest(c) != row['descriptor_digest']:
            raise ValueError('Changed consumed descriptor')
        symbol = row.get('authored_symbols', {}).get('FireLogic.FireLogicType')
        if symbol not in ('fltSingleFire', 'fltSingleFireWithBoltAction'):
            continue
        deferred = 'unreviewed_dispatch_symbol:FireLogic.FireLogicType:' + symbol
        if deferred not in row['deferred_reasons'] or BOLT_DEFERRED not in row['deferred_reasons']:
            continue
        if 'fireLogicType' in v or c.get('admission') != 'Candidate':
            raise ValueError('Numeric dispatch already substituted')
        if [v.get(k) for k in ('reloadType', 'fireInputAction', 'reloadInputAction')] != [1, 8, 29]:
            continue
        if row.get('authored_symbols', {}).get('FireLogic.ReloadLogic') != 'rlWeaponSwitchCancelsUnfinishedReload':
            continue
        row['prior_descriptor_digest'] = row['descriptor_digest']
        v['fireLogicType'] = 1 if symbol == 'fltSingleFireWithBoltAction' else 0
        row['boltaction_magazine_proof_digest'] = PROOF_DIGEST
        if not candidate_dispatch(row):
            raise ValueError('Unsupported exact bolt-action magazine fields')
        words = [(0x10, int(descriptor.bits(v['reloadThreshold']), 16)), (0x14, int(descriptor.bits(v['reloadDelay']), 16)),
                 (0x18, int(descriptor.bits(v['reloadTime']), 16)), (0x20, 1), (0x24, v['fireLogicType']), (0x2c, 0)]
        c['timing'] = [dict(offset=o, expected=w, expected_bits=f'{w:08x}', comparison='Bits') for o, w in words]
        row['deferred_reasons'] = [r for r in row['deferred_reasons'] if r not in (deferred, BOLT_DEFERRED)]
        row['same_reviewed_dispatch_shape'] = not row['deferred_reasons']
        row['descriptor_digest'] = descriptor.digest(c)
        row['chamber_knowledge'] = 'Unknown'
        row['native_runtime_admission'] = False
        row['owed_bolt_reload_handoff_admission'] = False
        row['jobs'] = [j for j in row.get('jobs', []) if j != 'resolve_shared_dispatch_or_data_gap' or row['deferred_reasons']]
        row['jobs'] += ['independent_exact_magazine_native_family_review', 'exact_magazine_hand_contact']
        if v['fireLogicType'] == 1:
            row['jobs'] += ['typed_owed_bolt_reload_pause_and_ammo_receipt_rebase']
        changed += 1
    if 'summary' in out:
        out['summary']['deferred'] = sum(bool(r['deferred_reasons']) for r in out['descriptors'])
    out['boltaction_magazine_candidate_extension'] = {
        'proof_digest': PROOF_DIGEST, 'rows_reclassified': changed, 'runtime_enabled': False,
        'limits': ['Static ordinary reload dispatch only; no new original-instruction execution or actual-game result.',
                   'Owed bolt, original magazine ownership, native three-copy convergence and physical custody require their own receipts.',
                   'No firing/chamber grant and no direct native state, timer, ammo or animation writes.']}
    return out
