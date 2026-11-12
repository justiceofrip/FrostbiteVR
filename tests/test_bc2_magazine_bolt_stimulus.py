"""Narrow synthetic regressions for the previously missing normal input route.

Invented observation values test contracts only; no native/headset claim.
"""
import copy
import importlib.util
from pathlib import Path
import unittest
import tempfile
from types import SimpleNamespace
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name+".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


d = load("drive_bc2_magazine_bolt_stimulus")
g, f = d.geometry, d.frame
NOW = 1_000_000_000


def fixture():
    owner = dict(player=1, soldier=2, weak=3, weapon=4, actorGeneration=5, equipGeneration=6, space=7)
    physical = 8
    hand_owner = dict(actor=(3 << 32)|2, actorGeneration=5, equipGeneration=physical, space=7)
    inp = dict(owner=hand_owner, sequence=21, observedNs=NOW-10_000_000,
               deadlineNs=NOW+80_000_000, nowNs=NOW-1, focused=True, tracked=[True, True], released=[True, False])
    pose = dict(position=[0., 0., 0.], orientation=[0., 0., 0., 1.])
    hand = dict(grip=pose, aim=pose, gripTracked=True, aimTracked=True, squeeze=0., trigger=0.,
                active=6, held=0, touchActive=0, touched=0, stickX=0., stickY=0.)
    weapon = dict(id=4, generation=physical)
    token = dict(id=91, owner=hand_owner, hand=1, kind=1, item=weapon,
                 contact=dict(id=1, generation=physical), prerequisiteClaim=0)
    claim = dict(token=token, inputSequence=21, deadlineNs=NOW+70_000_000)
    identity = dict(owner=owner, firing=[101, 102, 103], serverPlayer=104, serverSoldier=105, serverItem=106)
    context = dict(resource=dict(actor=105, actorGeneration=5, weapon=106, weaponGeneration=9), equipGeneration=6, space=7)
    binding = dict(owner=owner, context=context, inventory=107, switching=108, data=109, persistence=110,
                   observedNs=NOW-10_000_000, deadlineNs=NOW+80_000_000)
    counts = dict(loaded=3, reserve=30, capacity=7)
    entry, attached = f.identity(), f.identity()
    attached[3][2] = .12
    insertion = dict(id=111, revision=2, orientation=0, approach=0, itemFromHand=f.identity(),
        itemFromInsertion=f.identity(), weaponFromEntry=entry, travelDirection=[0, 0, 1], travelMeters=.12,
        captureDistanceMeters=.03, releaseDistanceMeters=.06, captureAngleRadians=.2, releaseAngleRadians=.3,
        seatToleranceMeters=.01, postCaptureTravelMeters=0, maxStepMeters=.04, maxStepRadians=.2,
        alignmentNs=20_000_000, seatDwellNs=20_000_000, maxSampleGapNs=100_000_000,
        maxGuidedNs=3_000_000_000, pathCount=0, path=[])
    profile = dict(nativeId=1, configurationPath="synthetic/variant", mesh="synthetic_mesh", rigFingerprint=112,
                   geometryVerified=True, experimentalGeometry=False, attachedItem=attached,
                   grabRadiusMeters=.09, pullMeters=.06, pullReleaseMeters=.15, maxPullStepMeters=.04, insertion=insertion)
    raw = dict(valid=True, current=True, owner=owner, rigFingerprint=112, rigMatches=True, input=inp,
               originalHandEvidence=inp, rawWristWorldMeters=f.identity(), weaponWorldMeters=f.identity())
    magazine = dict(observationOnly=True, available=True, retainedEventsAreHistorical=True, owner=owner,
        input=inp, capturedNs=NOW-1, weapon=weapon, resourceBackend=True, cancel=False, phase=0,
        blocksWeaponActions=False, requestPending=False, leftClaim=None, rightClaim=claim, profile=profile,
        raw=raw, family=dict(owner=owner, weapon=weapon, inventory=107, verified=True, current=True,
                            observedNs=NOW-10_000_000, deadlineNs=NOW+80_000_000),
        resource=dict(current=True, binding=binding, identity=identity, snapshot=dict(context=context, sequence=31,
            observedNs=NOW-10_000_000, deadlineNs=NOW+80_000_000, coherentIdleVerified=True, counts=counts),
            phase=1, wellEmpty=False, originalState=0, original=None, request=10, requestState=0,
            receipt=None, receiptJoined=False, terminal=None),
        carriedReplacement=None, reservation=None, seatedRequest=0, pouch=dict(centerMeters=[-.23, -.55, .02], radiusMeters=.18))
    calibration = dict(nativeJoined=True, authoredReviewed=True, configurationPath="synthetic/variant",
        mesh="synthetic_mesh", rigFingerprint=112, gunHand=0, mechanismHand=1, profile=113, revision=3,
        axis=[1., 0., 0.], closedContact=f.identity(), wristFromPart=f.identity(), strokeMeters=.09,
        unlockRadians=.4, rotationTolerance=.05, maxStepMeters=.04, endpointDwellNs=20_000_000,
        maximumCycleNs=4_000_000_000)
    bolt = dict(observationOnly=True, available=True, owner=owner, input=inp, capturedNs=NOW-1, cancel=False,
        blocksFire=False, leftClaim=None, rightClaim=claim, gunClaim=None, mechanismClaim=None,
        custodyCurrent=False, custody=0, mechanismPhase=0, calibration=calibration,
        held=None, recovery=None, reloadPause=None, reloadMechanicalPose=None, settled=None, recovered=None, completion=None,
        raw=dict(raw, mappingValid=True, rawWristWorldMeters=[f.identity(), f.identity()],
                 wristToGrip=[f.identity(), f.identity()], bodyWorldMeters=f.identity()),
        originalGripReferences=dict(geometryOnly=True, sourceInputClockAvailable=False,
                                    wristInWeapon=[f.identity(), f.identity()]),
        native=dict(identity=identity, held=None, ready=None, cycle=0, shot=0, counts=counts,
                    countsFreshnessAvailable=False))
    s = dict(schema="fvr.bc2.live-mechanism-snapshot.v1", valid=True, nonce=123, observed_ns=NOW-1,
        valid_until_ns=NOW+60_000_000, native_deadline_ns=NOW+80_000_000, owner=owner,
        equipment=dict(weapon=4, data=109, persistence=110, asset="synthetic"),
        rig=dict(soldier=2, weak=3, count=20, skeleton=115, pose=116, fingerprint=112),
        physical_equipment_generation=physical, input_evidence=inp, shot_input_generation=21,
        weapon_actions_blocked=False, feed=None,
        selected=dict(configurationPath="synthetic/variant", configurationPathVerified=True, sequence=21,
            observedNs=NOW-10_000_000, deadlineNs=NOW+80_000_000, inventory=107, selectedSlot=0,
            matchedMeshPath="synthetic_mesh", meshes=[dict(path="synthetic_mesh")]),
        input=dict(generation=21, spaceGeneration=7, trackingBasisGeneration=1, focused=True, headValid=True,
            predictedNs=NOW, worldUnitsPerMeter=1, floorRelative=False, referenceHead=pose, head=pose,
            hands=[hand, dict(hand, squeeze=1.)]),
        mapping=dict(trackingBodyWorldMeters=f.identity(), anatomicalWristToGrip=f.identity()),
        magazine=magazine, bolt=bolt)
    return copy.deepcopy(s)


def removal_receipt(s, base):
    r = s["magazine"]["resource"]
    after = dict(base["counts"], loaded=0)
    command = dict(context=r["snapshot"]["context"], id=33, sourceSequence=31, requestedNs=NOW+1,
        admissionDeadlineNs=NOW+50_000_000, deadlineNs=NOW+2_000_000_000, operation=0,
        mutationRequired=True, before=base["counts"], after=after, original=None)
    receipt = dict(command=command, authorityInvocation=77, beganNs=NOW+2, completedNs=NOW+3,
                   authorityVerified=True, copiesVerified=True, before=base["counts"], after=after)
    request = dict(binding=r["binding"], discard=False,
        intent=dict(context=command["context"], id=15, operation=0, original=None,
                    observedNs=NOW+1, deadlineNs=NOW+50_000_000),
        sourceProof=dict(inputSequence=21, snapshotSequence=31, constructedNs=NOW+1,
                         snapshotObservedNs=NOW-10_000_000, snapshotDeadlineNs=NOW+80_000_000))
    original = dict(owner=command["context"]["resource"], id=33, rounds=3, capacity=7)
    r.update(receipt=receipt, receiptJoined=True, request=15, requestState=3, original=original,
             wellEmpty=True, originalState=1,
             terminal=dict(request=request, state=3, resolvedNs=NOW+4, nativeDispatched=True, receipt=copy.deepcopy(receipt)))
    r["snapshot"].update(counts=after, observedNs=NOW+3)
    return after


def owed(s):
    b = s["bolt"]
    held = dict(owner=b["input"]["owner"], item=s["magazine"]["weapon"], mechanism=dict(id=113, generation=8),
        cycle=41, shot=42, sequence=43, observedNs=NOW-10_000_000, deadlineNs=NOW+80_000_000,
        held=True, advancing=dict(guardEpoch=44, updates=[45, 46, 47], counts=dict(loaded=2, reserve=30, capacity=7)))
    b.update(held=copy.deepcopy(held), blocksFire=True)
    b["native"].update(held=held, cycle=41, shot=42, counts=dict(loaded=2, reserve=30, capacity=7))
    return g.owed(s, NOW)


class MagazineBoltTests(unittest.TestCase):
    def test_complete_snapshot_reader_bound_and_total_retention_are_distinct(self):
        self.assertEqual(d.transport.planner.SNAPSHOT_LIMIT, 65536)
        self.assertEqual(d.ordinary.MAX_BYTES, 1024 * 1024)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "snapshot.json"
            for size in (32769, 65536):
                path.write_bytes(b'{"payload":"' + b'x' * (size - 14) + b'"}')
                value, _ = d.transport.read_json(path, d.transport.planner.SNAPSHOT_LIMIT)
                self.assertEqual(len(value["payload"]), size - 14)
            path.write_bytes(b'{"payload":"' + b'x' * (65537 - 14) + b'"}')
            with self.assertRaises(d.transport.REFUSAL):
                d.transport.read_json(path, d.transport.planner.SNAPSHOT_LIMIT)

    def test_failure_cleanup_preserves_unknown_original_replacement_and_gun_custody(self):
        for family, context in (("magazine", "initial_unknown"), ("magazine", "removed_before_admission"),
                                ("magazine", "held_replacement"), ("bolt", "left_gun")):
            with self.subTest(context=context):
                driver = d.MagazineBoltDriver.__new__(d.MagazineBoltDriver)
                driver.pending_command = driver.unsafe_owner = False
                driver.held_left = driver.held_right = driver.held_trigger = True
                driver.family = family
                driver.command = mock.Mock()
                message = driver.cleanup_owned()
                driver.command.assert_called_once_with(control="trigger r", value=0, cleanup=True)
                self.assertIn("custody", message)
                self.assertTrue(driver.held_left and driver.held_right)

    def test_unsettled_cleanup_publishes_no_release(self):
        for field in ("pending_command", "unsafe_owner"):
            driver = d.MagazineBoltDriver.__new__(d.MagazineBoltDriver)
            driver.pending_command = driver.unsafe_owner = False
            setattr(driver, field, True)
            driver.command = mock.Mock()
            driver.cleanup_owned()
            driver.command.assert_not_called()

    def test_completed_bolt_neutral_handoff_enters_shared_holster(self):
        driver, clock, sent, current = handoff_driver()
        holster = load("bc2_holster_core")
        before, _ = current()
        self.assertFalse(holster.neutral(before["input"]))
        with mock.patch.object(d.time, "perf_counter_ns", side_effect=lambda: clock[0]):
            driver.run_neutral_handoff()
        self.assertEqual(sent, [["grip l 0"], ["grip r 0"]])
        self.assertTrue(driver.neutral_handoff_completed)
        self.assertFalse(driver.held_left or driver.held_right or driver.pending_command)
        self.assertEqual(driver.deadline, NOW + 9_700_000_000)
        after, now = current()
        holster.holster_proof(after, now)
        consumer = holster.OrdinaryHolsterDriver.__new__(holster.OrdinaryHolsterDriver)
        consumer.current = lambda: (after, now)
        consumer.args = SimpleNamespace(stow_asset=after["equipment"]["asset"],
            stow_config=after["selected"]["configurationPath"], stow_slot=1,
            target_asset="other", target_config="other/config", target_slot=2)
        # Run the real consumer through every initial admission predicate to
        # the first ordinary stow reach. No fake success or native draw follows.
        consumer.reach = mock.Mock(side_effect=StopIteration("admitted ordinary stow reach"))
        with self.assertRaisesRegex(StopIteration, "admitted ordinary stow reach"):
            consumer.run_holster()
        consumer.reach.assert_called_once_with(slot=1)
        self.assertEqual(consumer.phase, "stow_reach")

    def test_neutral_handoff_refuses_lost_custody_without_speculative_cleanup(self):
        for failure, expected in (("owner", []), ("expired", []),
                                  ("expired_at_publication", []), ("writer", []),
                                  ("right_after_left", [["grip l 0"]]),
                                  ("foreign_left_after_left", [["grip l 0"]]),
                                  ("pending_ack", [["grip l 0"]])):
            with self.subTest(failure=failure):
                driver, clock, sent, _ = handoff_driver(failure)
                with mock.patch.object(d.time, "perf_counter_ns", side_effect=lambda: clock[0]):
                    with self.assertRaises(g.Refusal):
                        driver.run_neutral_handoff()
                    before_cleanup = copy.deepcopy(sent)
                    driver.cleanup_owned()
                self.assertEqual(sent, expected)
                self.assertEqual(sent, before_cleanup)
                self.assertFalse(driver.neutral_handoff_completed)
                self.assertTrue(driver.held_right)
                self.assertIsNone(driver.handoff_phase)
                self.assertIsNotNone(driver.debt["readyReceipt"])
                self.assertTrue(driver.bolt_cycle_completed)

    def test_every_motion_sample_has_an_original_deadline_and_expiry_reacquires(self):
        driver, clock, published, renewed = motion_driver()
        original = fixture()
        original_copy = copy.deepcopy(original)
        target = f.identity()
        target[3][0] = .18
        with mock.patch.object(d.time, "perf_counter_ns", side_effect=lambda: clock[0]):
            driver.move_control(original, 0, target, 0, .04, .2)
        self.assertEqual(len(published), 7)
        self.assertGreaterEqual(len(renewed), 2)
        self.assertTrue(all(isinstance(deadline, int) and now < deadline for now, deadline in published))
        self.assertEqual(original, original_copy)
        self.assertIsNone(driver.motion_reference)
        self.assertEqual(driver.deadline, NOW+10*g.NS)

    def test_motion_renewal_refuses_changed_claim_before_further_publication(self):
        driver, clock, published, renewed = motion_driver(change_claim=True)
        target = f.identity()
        target[3][0] = .18
        with mock.patch.object(d.time, "perf_counter_ns", side_effect=lambda: clock[0]):
            with self.assertRaisesRegex(g.Refusal, "custody token changed"):
                driver.move_control(fixture(), 0, target, 0, .04, .2)
        self.assertEqual(len(published), 3)
        self.assertEqual(len(renewed), 1)
        self.assertIsNone(driver.motion_reference)

    def test_publication_revalidates_original_input_even_with_nonnull_snapshot(self):
        s = fixture()
        driver = d.MagazineBoltDriver.__new__(d.MagazineBoltDriver)
        driver.family, driver.anchor, driver.motion_reference = "magazine", None, None
        driver.latest_snapshot = s
        driver.state = dict(head={}, handL={}, handR={}, controls={})
        driver.read_state = lambda: copy.deepcopy(driver.state)
        driver.ownership = lambda state: None
        target = dict(commands=["grip l 1"])
        with mock.patch.object(d.time, "perf_counter_ns", return_value=NOW+80_000_000):
            with self.assertRaises(g.Refusal):
                driver.validate_publication(target["commands"], target, False)

    def test_motion_checks_native_identity_mapping_and_contact_deadlines(self):
        for mutation in (lambda s: s["magazine"]["resource"]["identity"].update(serverItem=999),
                         lambda s: s["magazine"]["raw"]["weaponWorldMeters"][3].__setitem__(0, .01)):
            s = fixture()
            mutation(s)
            with self.assertRaises(g.Refusal):
                g.movement_continuation(fixture(), s, NOW, "magazine")
        s = fixture()
        s["bolt"]["gunClaim"] = dict(s["bolt"]["rightClaim"], deadlineNs=NOW+8)
        s["bolt"]["currentContact"] = dict(joined=True, deadlineNs=NOW+4)
        self.assertEqual(g.source_deadline(s, "bolt"), NOW+4)

    def test_acknowledged_completion_survives_missed_tick_and_expired_receipt_clock(self):
        for kind in ("settled", "recovered"):
            s, debt, event = completion_fixture(kind)
            if kind == "recovered":
                # Recovery authority has its own sequence domain; exact debt
                # identity and its own release/ready order still must match.
                event["release"]["cycle"]["sequence"], event["sequence"] = 1, 2
            original = copy.deepcopy(event)
            self.assertEqual(g.ready(s, NOW+20, debt), event)
            self.assertEqual(event, original)
            self.assertEqual(g.source_deadline(s, "bolt"), NOW+60_000_000)
            self.assertIsNone(s["bolt"]["settled"])
            self.assertIsNone(s["bolt"]["native"]["ready"])

    def test_completion_cannot_create_owed_authority_or_renew_current_input(self):
        s, debt, _ = completion_fixture()
        with self.assertRaises(g.Pending):
            g.owed(s, NOW+20)
        with self.assertRaises(g.Refusal):
            g.ready(s, NOW+80_000_000, debt)

    def test_completion_requires_ack_exact_native_identity_and_one_receipt(self):
        mutations = (lambda b: b["completion"].update(acknowledged=False),
                     lambda b: b["completion"]["native"].update(serverItem=999),
                     lambda b: b["completion"].update(recovered=copy.deepcopy(b["completion"]["settled"])),
                     lambda b: b["completion"].update(settled=None),
                     lambda b: b["native"].update(cycle=999))
        for mutation in mutations:
            s, debt, _ = completion_fixture()
            mutation(s["bolt"])
            with self.assertRaises(g.Refusal):
                g.ready(s, NOW+20, debt)

    def test_completion_keeps_original_request_sequence_and_clock_order(self):
        mutations = (lambda z: z["release"].update(request=0),
                     lambda z: z["release"].update(inputSequence=20),
                     lambda z: z.update(sequence=z["release"]["cycle"]["sequence"]),
                     lambda z: z["release"].update(observedNs=NOW-1),
                     lambda z: z["release"].update(deadlineNs=NOW),
                     lambda z: z.update(deadlineNs=z["observedNs"]))
        for mutation in mutations:
            s, debt, event = completion_fixture()
            mutation(event)
            with self.assertRaisesRegex(g.Refusal, "does not settle"):
                g.ready(s, NOW+20, debt)

    def test_missing_normal_route_is_admitted_with_exact_typed_sections(self):
        s = fixture()
        for family in ("magazine", "bolt"):
            g.admit(s, NOW, 123, family)
        self.assertEqual(g.baseline(s, NOW)["counts"]["capacity"], 7)
        s.pop("magazine")
        with self.assertRaisesRegex(g.Refusal, "lacks magazine"):
            g.admit(s, NOW, 123, "magazine")

    def test_owner_or_raw_original_clock_cannot_be_borrowed(self):
        for change in (lambda s: s["magazine"].update(owner=dict(s["owner"], weapon=999)),
                       lambda s: s["magazine"]["raw"].update(originalHandEvidence=dict(s["input_evidence"], sequence=20))):
            s = fixture()
            change(s)
            with self.assertRaises(g.Refusal):
                g.baseline(s, NOW)

    def test_settled_magazine_admission_cannot_clear_old_bolt(self):
        s = fixture()
        owed(s)
        with self.assertRaisesRegex(g.Refusal, "owed bolt"):
            g.baseline(s, NOW)

    def test_remove_receipt_joins_global_request_and_three_copies(self):
        s = fixture()
        base = g.baseline(s, NOW)
        after = removal_receipt(s, base)
        z = g.receipt(s, NOW+10, base, 0, base["counts"], after)
        self.assertEqual((z["request"], z["receipt"]["command"]["id"]), (15, 33))
        self.assertEqual(z["original"]["rounds"], 3)

    def test_final_counts_without_authority_copy_or_request_join_refused(self):
        mutations = [lambda r: r["receipt"].update(copiesVerified=False),
                     lambda r: r["receipt"].update(authorityInvocation=0),
                     lambda r: r["terminal"]["request"]["intent"].update(id=99),
                     lambda r: r["identity"].update(firing=[101, 102, 999]),
                     lambda r: r["terminal"]["request"].update(sourceProof=None)]
        for mutation in mutations:
            s = fixture()
            base = g.baseline(s, NOW)
            after = removal_receipt(s, base)
            mutation(s["magazine"]["resource"])
            with self.assertRaises(g.Refusal):
                g.receipt(s, NOW+10, base, 0, base["counts"], after)

    def test_receipt_cannot_extend_original_native_completion_deadline(self):
        s = fixture()
        base = g.baseline(s, NOW)
        after = removal_receipt(s, base)
        r = s["magazine"]["resource"]
        r["receipt"]["command"]["deadlineNs"] = NOW+3
        r["terminal"]["receipt"] = copy.deepcopy(r["receipt"])
        with self.assertRaisesRegex(g.Refusal, "original clocks"):
            g.receipt(s, NOW+10, base, 0, base["counts"], after)

    def test_refill_receipt_requires_a_distinct_matching_operation(self):
        s = fixture()
        base = g.baseline(s, NOW)
        empty = removal_receipt(s, base)
        with self.assertRaisesRegex(g.Refusal, "original request"):
            g.receipt(s, NOW+10, base, 2, empty, dict(empty, loaded=7, reserve=23))

    def test_exact_current_root_and_old_debt_survive_two_hand_continuation(self):
        s = fixture()
        debt = owed(s)
        b = s["bolt"]
        b["input"]["released"][0] = False
        token = dict(b["rightClaim"]["token"], id=201, hand=0)
        b["leftClaim"] = dict(token=token, inputSequence=21, deadlineNs=NOW+70_000_000)
        b.update(custodyCurrent=True, gunClaim=copy.deepcopy(b["leftClaim"]), mechanismPhase=2)
        b["controllerCustody"] = copy.deepcopy(token)
        b["currentContact"] = dict(joined=True, gun=copy.deepcopy(token), inputSequence=21,
                                   observedNs=NOW-10_000_000, deadlineNs=NOW+70_000_000)
        self.assertEqual(g.custody(s, NOW, debt, 0, token), token)
        expected = copy.deepcopy(token)
        b["leftClaim"]["token"]["id"] = 202
        b["gunClaim"]["token"]["id"] = 202
        b["controllerCustody"]["id"] = 202
        b["currentContact"]["gun"]["id"] = 202
        with self.assertRaisesRegex(g.Refusal, "exact gun custody"):
            g.custody(s, NOW, debt, 0, expected)

    def test_explicit_discard_keeps_original_rounds_and_no_reserve_credit(self):
        s = fixture()
        base = g.baseline(s, NOW)
        after = removal_receipt(s, base)
        removed = g.receipt(s, NOW+10, base, 0, base["counts"], after)
        r = s["magazine"]["resource"]
        r.update(originalState=2, request=16)
        t = r["terminal"]
        t.update(nativeDispatched=False, receipt=None)
        t["request"].update(discard=True)
        t["request"]["intent"].update(id=16, original=copy.deepcopy(r["original"]))
        self.assertTrue(g.discarded(s, NOW+10, base, removed)["request"]["discard"])
        r["snapshot"]["counts"]["reserve"] += 3
        with self.assertRaisesRegex(g.Refusal, "reserve credit"):
            g.discarded(s, NOW+10, base, removed)

    def test_actual_replacement_continuation_cannot_change_claim(self):
        s = fixture()
        base = g.baseline(s, NOW)
        x = s["magazine"]
        x["input"]["released"][0] = False
        token = dict(x["rightClaim"]["token"], hand=0, kind=5, id=301,
                     prerequisiteClaim=91, item=dict(id=302, generation=1))
        x["leftClaim"] = dict(token=token, inputSequence=21, deadlineNs=NOW+70_000_000)
        x["resource"].update(wellEmpty=True, originalState=2)
        x["carriedReplacement"] = dict(current=True, claim=copy.deepcopy(x["leftClaim"]),
            item=token["item"], units=7, family=1,
            identity=dict(owner=token["owner"], weapon=x["weapon"], trackingEpoch=7))
        held = g.replacement(s, NOW, base)
        self.assertEqual(g.replacement(s, NOW, base, held), held)
        x["leftClaim"]["token"]["id"] = 303
        x["carriedReplacement"]["claim"]["token"]["id"] = 303
        with self.assertRaisesRegex(g.Refusal, "changed during continuation"):
            g.replacement(s, NOW, base, held)

    def test_short_original_raw_lease_is_not_replaced_by_snapshot_deadline(self):
        s = fixture()
        s["magazine"]["raw"]["input"] = dict(s["input_evidence"], deadlineNs=NOW+10)
        self.assertEqual(g.source_deadline(s, "magazine"), NOW+10)

    def test_observed_ready_survives_return_without_renewing_its_retired_lease(self):
        s = fixture()
        debt = owed(s)
        b = s["bolt"]
        ready = dict(release=dict(cycle=copy.deepcopy(b["held"]), request=51, inputSequence=21,
                                 observedNs=NOW+1, deadlineNs=NOW+50_000_000),
                     sequence=52, observedNs=NOW+2, deadlineNs=NOW+50_000_000,
                     nativeReady=True, unchangedAmmunition=True)
        b["settled"] = ready
        debt["readyReceipt"] = g.ready(s, NOW+10, debt)
        original_deadline = debt["readyReceipt"]["release"]["cycle"]["deadlineNs"]
        b.update(held=None, settled=None, blocksFire=False, custody=3)
        b["native"].update(held=None, ready=None, phase=5)
        self.assertEqual(g.ready(s, NOW+20, debt), ready)
        self.assertEqual(debt["readyReceipt"]["release"]["cycle"]["deadlineNs"], original_deadline)
        b["native"]["cycle"] += 1
        with self.assertRaisesRegex(g.Refusal, "debt changed"):
            g.ready(s, NOW+20, debt)

    def test_receipt_pending_is_observation_only_not_a_new_action(self):
        s = fixture()
        x = s["magazine"]
        x["requestPending"] = True
        x["resource"].update(phase=2, requestState=1)
        with self.assertRaises(g.Pending):
            g.magazine(s, NOW, idle=True)
        x["resource"]["requestState"] = 5
        with self.assertRaisesRegex(g.Refusal, "uncertain"):
            g.magazine(s, NOW, idle=True)

    def test_equal_counts_do_not_accept_replaced_owed_cycle_or_guard_epoch(self):
        for field in ("cycle", "guardEpoch"):
            s = fixture()
            debt = owed(s)
            b = s["bolt"]
            if field == "cycle":
                b["held"]["cycle"] += 1
            else:
                b["held"]["advancing"]["guardEpoch"] += 1
            with self.assertRaises(g.Refusal):
                g.bolt(s, NOW, debt)

    def test_profile_geometry_drives_non_m95_axis_and_outside_capture(self):
        s = fixture()
        path = g.magazine_path(s, "insert")
        self.assertEqual(path[-1][3][2], .12)
        self.assertLess(g.insertion_outside(s["magazine"]["profile"])[3][2], -.03)
        b = g.bolt_pose(s["bolt"]["calibration"], .09, .4)
        self.assertAlmostEqual(b[3][0], .09)
        self.assertAlmostEqual(b[1][2], math_sin(.4))
        for hand in (0, 1):
            mapped = g.control(s, "bolt", hand, b, NOW)
            self.assertLess(f.pose_error(mapped, b)[0], .000001)

    def test_two_hand_command_target_reuses_exact_ack_and_other_hand_gate(self):
        self.assertIs(d.MagazineBoltDriver.send, d.transport.Driver.send)
        self.assertIs(d.MagazineBoltDriver.receive_snapshot, d.transport.Driver.receive_snapshot)
        sample = dict(hand=1, positionMeters=[.1, .2, .3], yawPitchRollDegrees=[10, 20, 30], grip=1)
        other = dict(pos=[0, 0, 0], ypr=[0, 0, 0], followsHead=False)
        state = dict(handL=other, handR=dict(pos=sample["positionMeters"], ypr=sample["yawPitchRollDegrees"], followsHead=False),
                     controls=dict(gripR=1))
        target = dict(sample=sample, otherHand=other, controls=dict(gripR=1))
        d.MagazineBoltDriver.check_target(state, target)
        state["handL"] = dict(other, pos=[1, 0, 0])
        with self.assertRaisesRegex(g.Refusal, "other hand moved"):
            d.MagazineBoltDriver.check_target(state, target)

    def test_host_precision_is_emitted_without_weakening_acknowledgement(self):
        matrix = f.from_pose(dict(position=[.123456, 1.234567, -.345678],
            orientation=f.ypr_quaternion([12.3456, -23.4567, 34.5678])))
        sample = g.pose_sample(1, matrix, 1)
        self.assertEqual(sample["positionMeters"], [.123, 1.235, -.346])
        self.assertEqual(sample["yawPitchRollDegrees"], [12.35, -23.46, 34.57])
        other = dict(pos=[0, 0, 0], ypr=[0, 0, 0], followsHead=False)
        state = dict(handL=other, handR=dict(pos=sample["positionMeters"][:],
            ypr=sample["yawPitchRollDegrees"][:], followsHead=False), controls=dict(gripR=1))
        target = dict(sample=sample, otherHand=other, controls=dict(gripR=1))
        d.MagazineBoltDriver.check_target(state, target)
        state["handR"]["pos"][0] += .001
        with self.assertRaisesRegex(g.Refusal, "position"):
            d.MagazineBoltDriver.check_target(state, target)

    def test_clearance_keeps_original_action_bound_without_invented_review_ttl(self):
        s = fixture()
        state = dict(head={}, handL={}, handR={}, controls={})
        permission = dict(schema="bc2vr.root-fire-clearance.v1", rootReviewedVisibleCorridor=True,
            host={"pid": 1}, owner=s["owner"], configurationPath=s["selected"]["configurationPath"],
            rig=s["rig"], reviewedNs=1, referenceHead=s["input"]["referenceHead"],
            rightAim=s["input"]["hands"][1]["aim"], captures=[dict(eye="left"), dict(eye="right")], **state)
        self.assertEqual(g.fire_clearance(permission, s, state, {"pid": 1}, 5*NOW, 6*NOW), 100_000_000)
        with self.assertRaisesRegex(g.Refusal, "expired"):
            g.fire_clearance(permission, s, state, {"pid": 1}, 6*NOW, 6*NOW)
        state["controls"] = dict(gripR=1)
        with self.assertRaisesRegex(g.Refusal, "changed"):
            g.fire_clearance(permission, s, state, {"pid": 1}, 5*NOW, 6*NOW)


def math_sin(value):
    import math
    return math.sin(value)


def completion_fixture(kind="settled"):
    s = fixture()
    debt = owed(s)
    b = s["bolt"]
    event = dict(release=dict(cycle=copy.deepcopy(b["held"]), request=51, inputSequence=21,
                             observedNs=NOW+1, deadlineNs=NOW+50_000_000),
                 sequence=52, observedNs=NOW+2, deadlineNs=NOW+5,
                 nativeReady=True, unchangedAmmunition=True)
    b.update(held=None, blocksFire=False, completion=dict(acknowledged=True,
        native=copy.deepcopy(b["native"]["identity"]), settled=None, recovered=None))
    b["completion"][kind] = event
    b["native"].update(held=None, phase=5)
    return s, debt, event


def handoff_driver(failure=None):
    """Assemble real handoff/publication predicates against synthetic ACKs."""
    baseline, debt, ready = completion_fixture()
    debt["readyReceipt"] = copy.deepcopy(ready)
    gun = copy.deepcopy(baseline["bolt"]["rightClaim"]["token"])
    support = dict(copy.deepcopy(gun), id=92, hand=0, kind=2, prerequisiteClaim=gun["id"])
    driver = d.MagazineBoltDriver.__new__(d.MagazineBoltDriver)
    driver.family, driver.debt, driver.motion_reference = "bolt", debt, None
    driver.anchor = g.owner_key(baseline)
    driver.preserved = copy.deepcopy(baseline["input"])
    driver.handoff_guard = driver.handoff_phase = None
    driver.neutral_handoff_completed = False
    driver.bolt_cycle_completed = True
    driver.pending_command = driver.unsafe_owner = driver.held_trigger = False
    driver.held_left = driver.held_right = True
    driver.held_buttons = set()
    driver.deadline = driver.stage_deadline = NOW+9_700_000_000
    driver.hard_deadline = NOW+10*g.NS
    driver.last_write, driver.last_commit, driver.motion_started = NOW-20_000_000, None, None
    driver.artifacts = mock.Mock()
    driver.boundaries = [dict(name="right_gun_and_support_returned",
                             result=dict(gun=gun, support=support, ready=ready))]
    controls = {k: False for k in ("a", "b", "x", "y", "menu", "clickL", "clickR", "restL", "restR")}
    controls.update(trigL=0, trigR=0, gripL=1, gripR=1, stickL=[0, 0], stickR=[0, 0])
    pose = dict(pos=[0, 0, 0], ypr=[0, 0, 0], followsHead=False)
    driver.state = dict(head=copy.deepcopy(pose), handL=copy.deepcopy(pose),
                        handR=copy.deepcopy(pose), controls=controls)
    driver.read_state = lambda: copy.deepcopy(driver.state)
    def ownership(state):
        if failure == "writer":
            driver.unsafe_owner = True
            raise g.Refusal("synthetic competing writer")
    driver.ownership = ownership
    clock, sent = [NOW+10], []

    def current():
        s = copy.deepcopy(baseline)
        now, seq = clock[0], 21 + len(sent)
        ev = s["input_evidence"]
        ev.update(sequence=seq, observedNs=now-1000, deadlineNs=now+80_000_000, nowNs=now-1,
                  released=[driver.state["controls"]["gripL"] == 0, driver.state["controls"]["gripR"] == 0])
        s.update(observed_ns=now-1, valid_until_ns=now+60_000_000,
                 native_deadline_ns=now+80_000_000, shot_input_generation=seq)
        s["input"].update(generation=seq, predictedNs=now)
        s["selected"].update(sequence=seq, observedNs=now-1000, deadlineNs=now+80_000_000)
        for side, hand in enumerate(s["input"]["hands"]):
            pressed = driver.state["controls"]["gripL" if side == 0 else "gripR"]
            hand.update(held=pressed, squeeze=pressed)
        b = s["bolt"]
        b.update(capturedNs=now-1, custody=3, custodyCurrent=True,
                 rightClaim=dict(token=copy.deepcopy(gun), inputSequence=seq, deadlineNs=now+70_000_000),
                 leftClaim=(None if ev["released"][0] else
                     dict(token=copy.deepcopy(support), inputSequence=seq, deadlineNs=now+70_000_000)))
        b["gunClaim"] = copy.deepcopy(b["rightClaim"])
        if failure == "owner":
            s["equipment"]["asset"] = "foreign"
        if failure == "expired":
            s["valid_until_ns"] = now-1
        if sent and failure == "right_after_left":
            b["rightClaim"]["token"]["id"] += 1
        if sent and failure == "foreign_left_after_left":
            b["leftClaim"] = dict(token=dict(support, id=999), inputSequence=seq, deadlineNs=now+70_000_000)
        neutral = all(hand["squeeze"] == 0 for hand in s["input"]["hands"])
        s["bodyHolster"] = dict(available=True, nativeOwner=copy.deepcopy(s["owner"]),
            inputEvidence=copy.deepcopy(ev), sampledNs=now-1, validUntilNs=now+60_000_000,
            nativeTick=seq, physicalGun=copy.deepcopy(gun["item"]),
            inputSequenceRelation="same", inputMatchesSnapshot=True,
            input=copy.deepcopy(s["input"]), neutralTrackedInput=neutral,
            leftClaim=copy.deepcopy(b["leftClaim"]), rightClaim=copy.deepcopy(b["rightClaim"]),
            selectedSlot=dict(slot=1, item=copy.deepcopy(gun["item"])), phase=1,
            actualEmpty=False, blockWeaponActions=False, allowAutomaticGunHold=True, inventoryEvaluation=1)
        return s, now

    def fresh():
        s, now = current()
        driver.admit_snapshot(s, now, s["nonce"])
        driver.latest_snapshot = s
        return s, b"", now

    def send(lines, expected=None, cleanup=False, source_deadline=None):
        if failure == "expired_at_publication":
            clock[0] = driver.latest_snapshot["valid_until_ns"]
        driver.validate_publication(lines, expected, cleanup)
        g.require(clock[0] < source_deadline, "original release source expired")
        sent.append(lines)
        if failure == "pending_ack":
            driver.pending_command = True
            raise g.Refusal("synthetic ACK missing")
        driver.state["controls"] = copy.deepcopy(expected["controls"])
        driver.check_target(driver.state, expected)
        driver.last_write = clock[0]
        clock[0] += 5_000_000
        driver.last_commit = clock[0]
        driver.remember_controls(lines)

    driver.fresh, driver.send = fresh, send
    return driver, clock, sent, current


def motion_driver(change_claim=False):
    driver = d.MagazineBoltDriver.__new__(d.MagazineBoltDriver)
    driver.state = dict(handL=dict(pos=[0, 0, 0], ypr=[0, 0, 0]))
    driver.family, driver.debt, driver.motion_reference = "magazine", None, None
    driver.commands = 0
    driver.deadline = driver.stage_deadline = NOW+10*g.NS
    driver.last_write = NOW-25_000_000
    clock, published, renewed = [NOW], [], []
    def pause(until):
        clock[0] = max(clock[0], until)
        g.require(clock[0] < driver.deadline, "ten-second driver deadline reached")
    def command(*, sample, source):
        g.require(source is not None and clock[0] < source, "expired or missing publication source")
        published.append((clock[0], source))
        driver.last_write = clock[0]
        clock[0] += 1_000_000
    def fresh():
        delta = clock[0]-NOW
        shifted = set()
        def shift(value):
            if isinstance(value, (dict, list)):
                if id(value) in shifted:
                    return
                shifted.add(id(value))
            if isinstance(value, dict):
                for key in value:
                    if key in ("observedNs", "deadlineNs", "capturedNs", "nowNs", "observed_ns",
                               "valid_until_ns", "native_deadline_ns", "predictedNs"):
                        value[key] += delta
                    else:
                        shift(value[key])
            elif isinstance(value, list):
                for row in value:
                    shift(row)
        s = fixture()
        shift(s)
        if change_claim:
            s["magazine"]["rightClaim"]["token"]["id"] += 1
        renewed.append(copy.deepcopy(s))
        return s, b"", clock[0]
    driver.pause_until, driver.command, driver.fresh = pause, command, fresh
    return driver, clock, published, renewed


if __name__ == "__main__":
    unittest.main()
