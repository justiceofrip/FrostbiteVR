import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from bc2_campaign_vehicle_coverage import REQUIRED, SCHEMA, audit_capture, report, validate_catalog


def catalog():
    case = dict(id='truck', level='Levels/SP_02', asset='objects/vehicles/truck.dbx',
                authored_forced_seat=3, human_trigger_guids=['11111111-1111-1111-1111-111111111111'],
                runtime_slot_verified=False, adapter='attached_soldier_pending',
                restrictions=['native mission permission'], missing_native_evidence=['callback owner'],
                required_checks=list(REQUIRED), campaign_accepted=False, headset_accepted=False)
    return dict(schema=SCHEMA, complete_campaign_inventory=False, native_admission_granted=False,
                sources=[dict(path='evidence.json', sha256='a'*64)], human_force_entry_count=1, cases=[case])


def capture(admitted=False):
    route = dict(status=0, actor=0x10000, controlled=0x20000, entry=0x30000, router=0x40000,
                 cache=0x50000, player=0x60000, weak=0x70000, slot=3, fingerprint=99,
                 permissions=0x160, routes=[[5, 7], [6, 8], [8, 9]])
    detail = dict(complete=True, collection_valid=True, input_authority=False, atomic=False,
                  attached_soldier=dict(input_authority=False, production_enabled=False, ownership_joined=True))
    rows = [dict(index=i, error='', owner_stable=True, route_stable=True, seat_details_identity_stable=True,
                 before=copy.deepcopy(route), after=copy.deepcopy(route), profile_revalidated=admitted,
                 profile={} if admitted else None, start_ms=100+i*50, end_ms=120+i*50,
                 read_begin=i*100, read_end=(i+1)*100,
                 seat_details_before=copy.deepcopy(detail), seat_details_after=copy.deepcopy(detail)) for i in range(3)]
    return dict(schema='fvr.private.attached_seat_readonly398.v1', read_only=True,
                game_writes=0, native_calls=0, os_input=0, input_authority=False, vehicle_motion_tested=False,
                complete=True, snapshot_count=3, coherent_snapshots=3, route_stable_across_snapshots=True,
                input_verified=True, campaign_consumer_verified=True, budget_exhausted=False, failed_reads=0,
                read_operations=300, read_bytes=4096, elapsed_ms=140, pid=12, process_creation=1234,
                installed_sha256='a'*64, snapshots=rows, admitted_snapshots=3 if admitted else 0,
                profile_admitted_three_times=admitted,
                native_input_proofs=[dict(rva=i+1, equal=True, disk_sha256='b'*64, live_sha256='b'*64) for i in range(13)])


class VehicleCoverageTests(unittest.TestCase):
    def test_rejection_is_useful_evidence_not_failed_collection(self):
        r = audit_capture(capture(), 3)
        self.assertTrue(r['read_only_joins_verified'])
        self.assertTrue(r['attached_ownership_joined'])
        self.assertFalse(r['generic_profile_admitted'])
        self.assertFalse(r['controls_tested'])

    def test_even_admitted_native_layout_is_not_playable_or_exact_vehicle(self):
        r = report(catalog(), captures={'truck': capture(True)})
        self.assertEqual(r['campaign_accepted_count'], 0)
        self.assertFalse(r['cases'][0]['read_only_observation']['exact_blueprint_verified'])
        self.assertEqual(r['cases'][0]['checks_remaining'], list(REQUIRED))

    def test_boat_capture_cannot_cover_passenger_or_other_named_case(self):
        c = capture(); c['snapshots'][0]['before']['slot'] = 0
        c['snapshots'][0]['after']['slot'] = 0
        self.assertRaises(ValueError, report, catalog(), False, {'truck': c})
        self.assertRaises(ValueError, report, catalog(), False, {'boat': capture()})

    def test_owner_replacement_or_one_unstable_bracket_is_not_ignored(self):
        for field in ('actor', 'controlled', 'entry', 'router', 'cache', 'slot', 'fingerprint'):
            c = capture()
            for side in ('before', 'after'): c['snapshots'][1][side][field] += 1
            self.assertRaises(ValueError, audit_capture, c)
        for field in ('owner_stable', 'route_stable', 'seat_details_identity_stable'):
            c = capture(); c['snapshots'][2][field] = False
            self.assertRaises(ValueError, audit_capture, c)

    def test_failures_bounds_and_aggregate_flags_cannot_hide_partial_capture(self):
        for field, value in (('elapsed_ms', 10001), ('read_bytes', 8*1024*1024+1),
                             ('read_operations', 100001), ('budget_exhausted', True),
                             ('failed_reads', 1), ('coherent_snapshots', 2),
                             ('profile_admitted_three_times', True), ('admitted_snapshots', 1)):
            c = capture(); c[field] = value
            self.assertRaises(ValueError, audit_capture, c)
        c = capture(); c['native_input_proofs'][2]['live_sha256'] = 'c'*64
        self.assertRaises(ValueError, audit_capture, c)

    def test_catalog_never_upgrades_authored_seat_or_omits_required_steps(self):
        for field in ('runtime_slot_verified', 'campaign_accepted', 'headset_accepted'):
            c = catalog(); c['cases'][0][field] = True
            self.assertRaises(ValueError, validate_catalog, c)
        c = catalog(); c['cases'][0]['required_checks'].remove('death_checkpoint_return')
        self.assertRaises(ValueError, validate_catalog, c)
        c = catalog(); c['cases'] *= 2
        self.assertRaises(ValueError, validate_catalog, c)

    def test_committed_inventory_keeps_uav_and_two_attached_routes_separate(self):
        root = Path(__file__).resolve().parents[1]
        c = json.loads((root/'data/bc2/campaign-vehicle-coverage.json').read_text())
        validate_catalog(c)
        self.assertEqual(c['human_force_entry_count'], 14)
        self.assertEqual(len(c['cases']), 11)
        self.assertEqual(sum(r['adapter'] == 'attached_soldier_pending' for r in c['cases']), 2)
        self.assertEqual(sum(r['adapter'] == 'uav_pending' for r in c['cases']), 1)
        self.assertTrue(any(r['asset'].endswith('rowing_boat.dbx') and r['adapter'] != 'pbl_exact' for r in c['cases']))


if __name__ == '__main__':
    unittest.main()
