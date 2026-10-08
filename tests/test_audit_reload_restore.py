import copy
import struct
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from audit_reload_restore import audit, FAILURE_COUNTERS

def boundary(branch=1, loaded=7, reserve=20):
    return dict(player=0x10000, soldier=0x11000, weak=0x12000, weapon=0x13000, actor_generation=1,
                equip_generation=2, space=3, firing=0x14000+branch*0x1000, branch=branch,
                wrapper_offset=0x40 if branch else 0x3c, current=11, previous=10, next=12,
                timer=.72, loaded=loaded, reserve=reserve)

def record(number, kind, start, before, after):
    return dict(id=number, kind=kind, parent=0, update=0, depth=1, thread=42, caller=0,
                context=0, begin_ns=start, end_ns=start+10, before=copy.deepcopy(before), after=copy.deepcopy(after),
                finished=True, identity_retained=True)

def restore(number=2, start=120, before=None, after=None):
    before = before or boundary()
    after = after or boundary(loaded=8, reserve=19)
    after = copy.deepcopy(after)
    after['previous'] = before['current']
    r = record(number, 3, start, before, after)
    raw = bytearray(64)
    struct.pack_into('<IIf', raw, 0, after['current'], after['next'], after['timer'])
    struct.pack_into('<ii', raw, 0x18, after['loaded'], after['reserve'])
    r.update(caller=0x8bb438, context=0x20000, snapshot_before={'bytes': list(raw)},
             snapshot_after={'bytes': list(raw)}, restore_fields_matched=True)
    return r

def trace(rows=None):
    rows = rows or [record(1, 0, 100, boundary(), boundary()), restore(),
                    record(3, 0, 140, boundary(loaded=8, reserve=19), boundary(loaded=8, reserve=19))]
    matched = [sum(r['kind'] == n for r in rows) for n in range(4)]
    flow = dict(installed=True, started=True, drained=True, in_flight=0, observation_only=True,
                native_state_writes=False, native_gate_enabled=False, authority_proven=False,
                records=rows, matched=matched, **dict.fromkeys(FAILURE_COUNTERS, 0))
    return {'hooks_disabled': True, 'gameplay': {'reload_flow': flow}}

class ReloadRestoreAuditTests(unittest.TestCase):
    def test_missing_fifth_transfer_explained_by_restore(self):
        r = audit(trace(), require_restore=True, require_branch40_restore=True)
        self.assertTrue(r['valid_observation'], r['errors'])
        self.assertEqual(r['branch40_decode_restore_ammo_changes'], 1)
        self.assertEqual(r['between_observed_calls_ammo_changes'], [])
        self.assertFalse(r['authority_proven'])
    def test_legacy_capture_keeps_unexplained_gap(self):
        t = trace();t['gameplay']['reload_flow']['records'].pop(1)
        t['gameplay']['reload_flow']['records'][1]['id'] = 2
        t['gameplay']['reload_flow']['matched'] = [2, 0, 0]
        r = audit(t)
        self.assertTrue(r['valid_observation'], r['errors'])
        self.assertEqual(len(r['between_observed_calls_ammo_changes']), 1)
        self.assertFalse(audit(t, require_restore=True)['valid_observation'])
    def test_restore_correction_does_not_double_count_ammo(self):
        b, a = boundary(loaded=8, reserve=19), boundary(loaded=7, reserve=20)
        r = audit(trace([restore(1, 100, b, a)]))
        self.assertTrue(r['valid_observation'], r['errors'])
        self.assertEqual(len(r['restore_round_reverts']), 1)
        self.assertEqual(r['transfers'], [])
    def test_changed_source_and_false_runtime_claim_rejected(self):
        t = trace();t['gameplay']['reload_flow']['records'][1]['snapshot_after']['bytes'][63] ^= 1
        self.assertFalse(audit(t)['valid_observation'])
        t = trace();t['gameplay']['reload_flow']['records'][1]['restore_fields_matched'] = False
        self.assertFalse(audit(t)['valid_observation'])
    def test_source_size_values_and_output_mutations_rejected(self):
        for mutation in ('size', 'nan', 'ammo', 'previous'):
            t = trace();r = t['gameplay']['reload_flow']['records'][1]
            if mutation == 'size':r['snapshot_before']['bytes'].pop()
            if mutation == 'nan':r['snapshot_before']['bytes'][8:12] = list(struct.pack('<f', float('nan')))
            if mutation == 'ammo':r['after']['loaded'] = 9
            if mutation == 'previous':r['after']['previous'] = 0
            self.assertFalse(audit(t)['valid_observation'], mutation)
    def test_wrong_route_branch_and_owner_rejected(self):
        t = trace();r = t['gameplay']['reload_flow']['records'][1]
        r['caller'] = 0x89216b
        self.assertFalse(audit(t)['valid_observation'])
        t = trace();t['gameplay']['reload_flow']['records'][1]['after']['space'] += 1
        self.assertFalse(audit(t)['valid_observation'])
    def test_new_callers_explicitly_unclassified(self):
        t = trace();t['gameplay']['reload_flow']['records'][1]['caller'] = 0x123456
        r = audit(t)
        self.assertTrue(r['valid_observation'], r['errors'])
        self.assertEqual(r['unclassified_restore_callers'], [0x123456])
        self.assertFalse(audit(t, require_branch40_restore=True)['valid_observation'])
    def test_observer_failure_and_id_corruption_rejected(self):
        for field in FAILURE_COUNTERS:
            t = trace();t['gameplay']['reload_flow'][field] = 1
            self.assertFalse(audit(t)['valid_observation'], field)
        t = trace();t['gameplay']['reload_flow']['records'][1]['id'] = 1
        self.assertFalse(audit(t)['valid_observation'])
    def test_nested_restore_and_transfer_provenance(self):
        b, a = boundary(), boundary(loaded=8, reserve=19)
        parent = record(1, 0, 100, b, a);parent['end_ns'] = 200
        child = restore(2, 120, b, a);child.update(parent=1, update=1, depth=2)
        t = trace([parent, child]);r = audit(t)
        self.assertTrue(r['valid_observation'], r['errors'])
        child['thread'] = 99
        self.assertFalse(audit(t)['valid_observation'])
    def test_explicit_server_transfer_branch(self):
        b, a = boundary(loaded=3, reserve=24), boundary(loaded=4, reserve=23)
        for x in (b, a):
            x.update(branch=2, wrapper_offset=0x10, server_player=0x30000, server_soldier=0x31000, server_item=0x32000)
        row = record(1, 2, 100, b, a);row.update(caller=0x6e6831, transfer_path=0)
        t = trace([row]);flow=t['gameplay']['reload_flow'];flow.update(server_binding_verified=True, server_matched=[0,0,1,0], server_read_misses=0)
        r=audit(t, require_server_transfer=True)
        self.assertTrue(r['valid_observation'],r['errors']);self.assertEqual(len(r['server_transfers']),1)
        row['after']['server_item']+=4
        self.assertFalse(audit(t)['valid_observation'])
        row['after']['server_item']-=4;flow['server_read_misses']=1
        self.assertFalse(audit(t)['valid_observation'])
        self.assertFalse(audit(trace(),require_server_transfer=True)['valid_observation'])
    def test_generations_and_simultaneous_calls_are_separate(self):
        a = record(1, 0, 100, boundary(), boundary())
        b = record(2, 0, 105, boundary(loaded=8, reserve=19), boundary(loaded=8, reserve=19))
        r = audit(trace([a, b]));self.assertEqual(r['between_observed_calls_ammo_changes'], [])
        self.assertEqual(r['overlapping_top_level_calls'], [[1, 2]])
        b['before']['space'] = b['after']['space'] = 4
        r = audit(trace([a, b]));self.assertEqual(r['overlapping_top_level_calls'], [])

if __name__ == '__main__':
    unittest.main()
