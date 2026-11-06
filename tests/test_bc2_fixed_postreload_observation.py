"""Completed fixed-container reloads may authorize one observed post-reload shot.

Fixtures exercise shared ownership, native receipt identity and release boundaries.
No game execution or native/headset acceptance is inferred.
"""
import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import test_bc2_feed_postrelease_observation as feed_tests
import test_bc2_feed_driver_observation as transport_tests
import test_bc2_fixed_container_simulation_plan as fixed_tests

d = feed_tests.d
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fixed_postshot_geometry', ROOT / 'tools/bc2_reload_stimulus_geometry.py')
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)

NS, MS = 1_000_000_000, 1_000_000


def renew(snapshot, now, sequence):
    result = fixed_tests.refresh(snapshot, now, sequence)
    # Historical native transactions are immutable, unlike current sample leases.
    result['fixedContainer']['lastCompletion'] = copy.deepcopy(snapshot['fixedContainer']['lastCompletion'])
    return result


def fixed_fixture(alternate=False):
    s = fixed_tests.snapshot(3)
    x = s['fixedContainer']
    if alternate:
        s['selected']['configurationPath'] = x['profile']['configurationPath'] = 'synthetic/other-breech'
        s['equipment']['asset'] = 'synthetic-other-launcher'
    x['physicalFamily'] = dict(owner=copy.deepcopy(s['owner']), observedNs=NS-10*MS,
        deadlineNs=NS+70*MS, current=True, physicalItem=s['equipment']['persistence'],
        inventory=s['selected']['inventory'], selectedSecondary=True, targetWeapon=15, targetSlot=0,
        action=25, primaryAction=25, configurationPath=s['selected']['configurationPath'],
        asset=s['equipment']['asset'], persistentId='synthetic-xm8')
    x['tracking']['gun']['token']['item']['id'] = s['equipment']['persistence']
    x['tracking'].update(step=2, blocksFire=True)
    s['weapon_actions_blocked'] = True
    pickup = fixed_tests.p.plan(s, 'Pickup', NS, s['nonce'])
    held = fixed_tests.held_after_pickup(s, pickup)
    insert = fixed_tests.p.plan(held, 'Insert', NS+200*MS, held['nonce'], pickup_plan=pickup)
    loaded = fixed_tests.completed(held, insert)
    for ev in (loaded['input_evidence'], loaded['fixedContainer']['input'], loaded['fixedContainer']['raw']['input']):
        ev['released'][0] = True
    plan = g.build_stimulus('fixed', None, loaded, NS+500*MS, 'Close')
    acquired = fixed_tests.physical_acquired(loaded, NS+700*MS, 130)
    acquired['fixedContainer']['lastCompletion'] = copy.deepcopy(loaded['fixedContainer']['lastCompletion'])
    a = g.check_boundary(plan, acquired, 'fixed_acquired', NS+700*MS)
    complete = fixed_tests.physical_completed(acquired, 'Close', NS+1000*MS, 140)
    complete['fixedContainer']['lastCompletion'] = copy.deepcopy(loaded['fixedContainer']['lastCompletion'])
    c = g.check_boundary(plan, complete, 'fixed_step_complete', NS+1000*MS, prior={'fixed_acquired': a})
    closed = dict(schema='bc2vr.reload-stimulus-result.v1', status='ordinary_stimulus_completed',
        stimulusCompleted=True, boundaryChecksPassed=True, observerStopped=True,
        observationIncomplete=False, leftGripHeld=False, pendingCommand=False,
        plans=[dict(family='fixed', stage='Close')], boundaries=dict(fixed_acquired=a, fixed_step_complete=c))
    ready = renew(complete, NS+1200*MS, 150)
    for ev in (ready['input_evidence'], ready['fixedContainer']['input'], ready['fixedContainer']['raw']['input']):
        ev['released'][0] = True
    ready['input']['hands'][0]['squeeze'] = 0.
    ready['fixedContainer']['tracking']['grip'] = ready['fixedContainer']['raw']['grip'] = False
    return plan, closed, ready


class FixedPostreloadTests(unittest.TestCase):
    def ready(self):
        plan, result, snapshot = fixed_fixture()
        return snapshot, d.fixed_post_reload_closure(plan, result)

    def test_actual_fixed_schema_and_released_completion_admit_without_mutation(self):
        s, closure = self.ready()
        before = copy.deepcopy(s)
        d.post_reload_ready(None, s, NS+1200*MS, closure, neutral=True)
        self.assertEqual(s, before)
        self.assertFalse(s['fixedContainer']['raw']['admittedAtTick'])

    def test_profile_driven_adapter_has_no_xm8_identity_specialcase(self):
        plan, result, s = fixed_fixture(alternate=True)
        closure = d.fixed_post_reload_closure(plan, result)
        d.post_reload_ready(None, s, NS+1200*MS, closure, neutral=True)

    def test_schema_and_acquisition_mismatches_refuse_before_input(self):
        faults = (lambda p, r: p['geometryEvidence'].update(stage='Open'),
                  lambda p, r: p['mapping']['owner'].update(weapon=999),
                  lambda p, r: p['geometryEvidence']['selection']['profile'].update(kind=4),
                  lambda p, r: p['geometryEvidence']['selection'].update(physicalFamily=None),
                  lambda p, r: p['geometryEvidence']['baseline'].update(lastCompletedRequest=0),
                  lambda p, r: r['boundaries']['fixed_acquired']['gate']['completionPrerequisite'].update(planSourceNonce=999),
                  lambda p, r: r['boundaries']['fixed_step_complete']['gate'].update(observed=False))
        for fault in faults:
            plan, result, _ = fixed_fixture()
            fault(plan, result)
            with self.subTest(fault=fault), self.assertRaises(d.REFUSAL):
                d.fixed_post_reload_closure(plan, result)

    def test_current_owner_claim_receipt_and_original_lease_refusals(self):
        faults = (lambda s: s['fixedContainer']['tracking'].update(hand={}),
                  lambda s: s['fixedContainer']['tracking'].update(carried=True),
                  lambda s: s['fixedContainer']['tracking'].update(lifetime=999),
                  lambda s: s['fixedContainer']['tracking']['gun']['token'].update(id=999),
                  lambda s: s['fixedContainer']['physicalFamily'].update(selectedSecondary=False),
                  lambda s: s['fixedContainer']['physicalFamily'].update(deadlineNs=NS),
                  lambda s: s['fixedContainer']['view']['identity']['firing'].__setitem__(1, 999),
                  lambda s: s['fixedContainer']['view']['snapshot']['counts'].update(reserve=5),
                  lambda s: s['fixedContainer']['lastCompletion']['outcome']['receipt'].update(copiesVerified=False),
                  lambda s: s['fixedContainer']['lastCompletion']['outcome']['receipt'].update(authorityVerified=False),
                  lambda s: s['fixedContainer']['lastCompletion']['outcome'].update(nativeDispatched=False),
                  lambda s: s['fixedContainer']['lastCompletion'].update(acceptedNs=9*NS),
                  lambda s: s['fixedContainer']['lastCompletion']['postSnapshot']['context']['resource'].update(weapon=999),
                  lambda s: s['fixedContainer']['input'].update(sequence=999))
        for fault in faults:
            s, closure = self.ready()
            fault(s)
            with self.subTest(fault=fault), self.assertRaises((d.REFUSAL, KeyError)):
                d.post_reload_ready(None, s, NS+1200*MS, closure, neutral=True)

    def test_final_empty_observation_preserves_debt_and_cannot_admit_a_shot(self):
        s, closure = self.ready()
        x = s['fixedContainer']
        x['view']['snapshot']['counts']['loaded'] = 0
        x['tracking'].update(step=0, blocksFire=True)
        s['weapon_actions_blocked'] = True
        original = copy.deepcopy(s)
        d.post_reload_ready(None, s, NS+1200*MS, closure, neutral=True, after_shot=True)
        self.assertEqual(s, original)
        with self.assertRaises(d.REFUSAL):
            d.post_reload_ready(None, s, NS+1200*MS, closure, neutral=True)

    def native(self):
        flow, boundary = feed_tests.FeedPostreloadShotTests.native()
        _, closure = self.ready()
        baseline = closure['fixedPlan']['baseline']
        identity = baseline['nativeIdentity']
        boundary.update(schema='bc2vr.fixed-post-reload-pulse.v1', owner=identity['owner'], closure=closure,
                        beforeCounts=baseline['counts'], afterCounts=dict(baseline['counts'], loaded=0))
        for k in ('neutralWrittenNs', 'pressWrittenNs', 'releaseWrittenNs', 'releaseCommittedNs', 'observationEndNs'):
            boundary[k] += 2*NS
        for row in flow['records']:
            row['begin_ns'] += 2*NS
            row['end_ns'] += 2*NS
            for key in ('before', 'after'):
                r = row[key]
                r['loaded'] -= 199
                r['reserve'] = baseline['counts']['reserve']
                r['firing'] = identity['firing'][r['branch']]
                for field, name in (('player', 'player'), ('soldier', 'soldier'), ('weak', 'weak'), ('weapon', 'weapon'),
                                    ('actor_generation', 'actorGeneration'), ('equip_generation', 'equipGeneration'), ('space', 'space')):
                    r[field] = identity['owner'][name]
                if r['branch'] == 2:
                    for field, name in (('server_player', 'serverPlayer'), ('server_soldier', 'serverSoldier'), ('server_item', 'serverItem')):
                        r[field] = identity[name]
        return flow, boundary

    def test_fixed_native_fire_joins_exact_reloaded_three_copy_identity(self):
        flow, boundary = self.native()
        result = d.audit_post_reload_shot(flow, boundary)
        self.assertEqual((result['loadedBefore'], result['loadedAfter'], result['reserve'], result['roundsObserved']), (1, 0, 6, 1))
        self.assertFalse(result['completeWindow'])
        self.assertFalse(result['nativeAcceptance'])

    def test_fixed_native_wrong_firing_reserve_server_token_or_overrides_refuse(self):
        faults = (lambda f, b: b['closure']['fixedPlan']['baseline']['nativeIdentity']['firing'].__setitem__(1, 999),
                  lambda f, b: b['closure']['fixedPlan']['baseline']['nativeIdentity'].update(serverItem=999),
                  lambda f, b: b['afterCounts'].update(reserve=5),
                  lambda f, b: feed_tests.FeedPostreloadShotTests.event(f)['context_after'].update(raw_1c=88),
                  lambda f, b: feed_tests.FeedPostreloadShotTests.event(f)['fixed_input'].update(applied=1),
                  lambda f, b: f.update(records=[r for r in f['records'] if r['before']['loaded'] == r['after']['loaded']]))
        for fault in faults:
            flow, boundary = self.native()
            fault(flow, boundary)
            with self.subTest(fault=fault), self.assertRaises(d.REFUSAL):
                d.audit_post_reload_shot(flow, boundary)

    def test_one_fixed_pulse_rearms_releases_and_refuses_another_attempt(self):
        plan, result, snapshot = fixed_fixture()
        with tempfile.TemporaryDirectory() as temp:
            v = d.Driver.__new__(d.Driver)
            v.run, v.artifacts = Path(temp), transport_tests.MemoryArtifacts()
            path = v.run / 'close' / 'result.json'
            v.host, v.manifest = dict(pid=7), dict(source_sha256='source')
            v.pending_command = v.unsafe_owner = v.held_left = v.held_right = v.held_trigger = False
            v.held_buttons, v.reference = set(), None
            v.deadline = v.stage_deadline = 10*NS
            v.profiles = None
            clock, commands, generation = [NS+1200*MS], [], [150]
            sources = {v.run / 'live-driver-owner.json': dict(artifactDirectory=str(path.parent)),
                       path: result, path.parent / 'stimulus-01.json': plan}
            def fresh():
                clock[0] += 20*MS
                generation[0] += 1
                current = renew(snapshot, clock[0], generation[0])
                if getattr(v, 'post_reload_phase', None) == 'observe':
                    current['fixedContainer']['view']['snapshot']['counts']['loaded'] = 0
                    current['fixedContainer']['tracking'].update(step=0, blocksFire=True)
                    current['weapon_actions_blocked'] = True
                v.admit_snapshot(current, clock[0], current['nonce'])
                v.latest_snapshot = current
                return current, b'{}', clock[0]
            def send(lines, **kwargs):
                v.validate_publication(lines, None, False)
                clock[0] += 25*MS
                if 'source_deadline' in kwargs:
                    d.require(clock[0] < kwargs['source_deadline'], 'expired')
                v.last_write = clock[0]
                clock[0] += 5*MS
                v.last_commit = clock[0]
                v.remember_controls(lines)
                commands.append(lines)
            v.fresh, v.send = fresh, send
            v.pause_until = lambda until: clock.__setitem__(0, max(clock[0], until))
            with patch.object(d, 'child', side_effect=lambda root, p: p), \
                 patch.object(d, 'read_json', side_effect=lambda p, *a: (copy.deepcopy(sources[p]), b'{}')), \
                 patch.object(d.time, 'perf_counter_ns', side_effect=lambda: clock[0]):
                boundary = v.post_reload_shot(path)
                self.assertEqual(commands, [['grip l 0', 'trigger r 0'], ['trigger r 1'], ['trigger r 0']])
                self.assertEqual(boundary['schema'], 'bc2vr.fixed-post-reload-pulse.v1')
                self.assertEqual(boundary['afterCounts'], dict(loaded=0, reserve=6, capacity=1))
                self.assertFalse(v.held_trigger)
                with self.assertRaises(FileExistsError):
                    v.post_reload_shot(path)
                self.assertEqual(len(commands), 3)


if __name__ == '__main__':
    unittest.main()
