"""Portable pure draw policy plus conditional saved legacy94/80 integration.

Synthetic snapshots test refusal and bounded uncertainty. They do not reproduce
native Busy, establish that missing diagnostic facts are false, or qualify draw.
"""
import copy
from dataclasses import replace
import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import bc2_draw_admission as policy

NS, MS, NOW = policy.NS, 1_000_000, 2_000_000_000
SOURCE = ("XM8_sp_s", "Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped")
TARGET = ("QJU88_sp", "Objects/Weapons/Handheld/MEC_lmg_QJY88/SP_lmg_QJY88")


def base_snapshot():
    """Synthetic complete source: no ignored fixture, profile or live import."""
    owner = dict(player=11, soldier=22, weak=33, weapon=0x20000,
                 actorGeneration=1, equipGeneration=1, space=2)
    physical = dict(actor=(33 << 32) | 22, actorGeneration=1, equipGeneration=2, space=2)
    ev = dict(owner=physical, sequence=10, observedNs=NOW-2*MS, deadlineNs=NOW+80*MS,
              nowNs=NOW-MS, focused=True, tracked=[True, True], released=[True, False])
    pose = dict(position=[.2, 1.4, .1], orientation=[0, 0, 0, 1])
    head = dict(position=[0, 1.6, 0], orientation=[0, 0, 0, 1])
    hands = [dict(grip=copy.deepcopy(pose), aim=copy.deepcopy(pose), gripTracked=True,
                  aimTracked=True, held=0, squeeze=0, trigger=0, stickX=0, stickY=0) for _ in range(2)]
    inp = dict(generation=10, spaceGeneration=2, predictedNs=NOW, focused=True, headValid=True,
               floorRelative=True, worldUnitsPerMeter=1, trackingBasisGeneration=2,
               referenceHead=head, head=copy.deepcopy(head), hands=hands)
    selected = dict(configurationPathVerified=True, sequence=10, observedNs=NOW-2*MS,
        deadlineNs=NOW+80*MS, configurationPath=SOURCE[1], configurationPathPointer=0x60000,
        inventory=0x50000, selectedSlot=0, soleConfiguredArray=0x70000,
        matchedMeshPath="mesh", meshes=[dict(path="mesh")])
    gun = dict(id=owner["weapon"], generation=2)
    assignment = dict(slot=2, item=dict(id=owner["weapon"], generation=1))
    h = dict(available=True, sampledNs=NOW-MS, validUntilNs=NOW+80*MS, phase=1,
        actualEmpty=False, nativeTick=110, request=2, nativeOwner=copy.deepcopy(owner),
        inputEvidence=copy.deepcopy(ev), physicalGun=gun, inputMatchesSnapshot=True,
        inputSequenceRelation="same", neutralTrackedInput=True, input=copy.deepcopy(inp),
        selectedSlot=assignment, leftClaim=None,
        rightClaim=dict(token=dict(id=1, owner=copy.deepcopy(physical), hand=1, kind=1,
                                  item=copy.deepcopy(gun), prerequisiteClaim=0),
                        inputSequence=10, deadlineNs=NOW+80*MS),
        inventoryEvaluation=1, emptyHands=False, blockWeaponActions=False,
        allowAutomaticGunHold=True, freeRightCurrent=False, freeRight=None)
    rows = []
    for index, (body_slot, native_slot, weapon, requested) in enumerate((
            (2, 0, 0x20000, SOURCE), (1, 7, 0x30000, TARGET))):
        c = dict(available=True, owner=copy.deepcopy(owner), asset=requested[0],
            configurationPath=requested[1], configurationPathPointer=0x60000+index*0x100,
            configurationPathVerified=True, soleConfiguredArray=0x70000+index*0x100,
            inventory=selected["inventory"], selectedSlot=0, kind="carried" if index else "selected",
            weaponData=weapon+0x1000, sequence=10, observedNs=NOW-2*MS, deadlineNs=NOW+80*MS)
        rows.append(dict(assignment=dict(slot=body_slot, item=dict(id=weapon, generation=index+1)),
            native=dict(slot=native_slot, weapon=weapon, data=weapon+0x1000,
                        persistence=17+index, category=index), configuration=c))
    d = dict(available=True, observationOnly=True, reason=None, sampledNs=NOW,
        selectedOwner=copy.deepcopy(owner), physicalOwner=copy.deepcopy(physical),
        physicalSelected=copy.deepcopy(assignment["item"]), inputEvidence=copy.deepcopy(ev),
        sequence=10, observedNs=NOW-2*MS, deadlineNs=NOW+80*MS, inventory=selected["inventory"],
        switching=0x51000, revision=3, cohort=4, count=2, nativeCount=8, slots=rows)
    return dict(schema="fvr.bc2.live-mechanism-snapshot.v1", valid=True, nonce=1,
        observed_ns=NOW, valid_until_ns=NOW+80*MS, native_deadline_ns=NOW+80*MS,
        shot_input_generation=10, physical_equipment_generation=2, weapon_actions_blocked=False,
        owner=owner, equipment=dict(weapon=owner["weapon"], data=0x21000, persistence=17, asset=SOURCE[0]),
        rig=dict(soldier=22, weak=33, count=1, skeleton=66, pose=77), selected=selected,
        input=inp, input_evidence=ev, bodyHolster=h, bodyInventory=d, feed=None)


def snapshot(now=NOW, nonce=1, unknown=False):
    s = base_snapshot()
    offset = now - NOW

    def advance(value):
        if isinstance(value, dict):
            for key, entry in value.items():
                if type(entry) is int and entry > 0 and (key.endswith("Ns") or key in (
                        "observed_ns", "valid_until_ns", "native_deadline_ns")):
                    value[key] += offset
                else:
                    advance(entry)
        elif isinstance(value, list):
            for entry in value:
                advance(entry)
    advance(s)
    s["nonce"] = nonce
    seq = 10 + nonce
    h, d = s["bodyHolster"], s["bodyInventory"]
    for sample in (s["input_evidence"], h["inputEvidence"], d["inputEvidence"], s["selected"], d):
        sample["sequence"] = seq
    for inp in (s["input"], h["input"]):
        inp["generation"] = seq
    s["shot_input_generation"] = seq
    h["rightClaim"]["inputSequence"] = seq
    h["nativeTick"] += nonce
    for row in d["slots"]:
        row["configuration"]["sequence"] = seq
    if unknown:
        h.update(inventoryEvaluation=0, allowAutomaticGunHold=False)
    return s


def host():
    controls = {key: False for key in (
        "a", "b", "x", "y", "menu", "clickL", "clickR", "restL", "restR")}
    controls.update(gripL=0, gripR=0, trigL=0, trigR=0, stickL=[0, 0], stickR=[0, 0])
    return dict(pid=101, runtime="xr-sim", head=dict(valid=True), handL=dict(valid=True),
                handR=dict(valid=True), controls=controls, captureSeq=0,
                sessionRunning=True, actionsAttached=True, sessionState="FOCUSED",
                paceMode="free", errors=0, lastCmdError="")


def context(s, used=None, phase="stow_reach"):
    now = s["observed_ns"]
    written, committed = (0, 0) if phase == "prime" else (NOW + 3*MS, NOW + 5*MS)
    identity = (27, "ack", "command-hash", written, committed, 20)
    return policy.Context(s["nonce"], now-MS, now-MS//2, now-MS+policy.REQUEST_NS,
        s["nonce"] if used is None else used, phase, written, committed, identity, identity,
        host(), host(), copy.deepcopy(s["input"]["hands"][1]["grip"]))


def validate_current(s, now):
    """Injected portable proof, independent of the admission decision under test."""
    check = policy.require
    check(s["valid"] is True and s["observed_ns"] <= now < s["valid_until_ns"]
          <= s["native_deadline_ns"], "outer original lease invalid")
    h, ev, owner = s["bodyHolster"], s["input_evidence"], s["owner"]
    check(h["nativeOwner"] == owner and s["equipment"]["weapon"] == owner["weapon"], "owner mismatch")
    expected = dict(actor=(owner["weak"] << 32) | owner["soldier"],
        actorGeneration=owner["actorGeneration"], equipGeneration=s["physical_equipment_generation"], space=owner["space"])
    for sample in (ev, h["inputEvidence"]):
        check(sample["owner"] == expected and sample["observedNs"] <= sample["nowNs"] <= now
              < sample["deadlineNs"], "current input owner/lease invalid")
    check(s["input"]["generation"] == s["shot_input_generation"] == ev["sequence"], "input generation mismatch")
    check(h["sampledNs"] <= now < h["validUntilNs"] <= h["inputEvidence"]["deadlineNs"], "body lease invalid")
    check(h["input"]["generation"] == h["inputEvidence"]["sequence"], "body input mismatch")
    seq, outer = h["inputEvidence"]["sequence"], ev["sequence"]
    check(h["inputMatchesSnapshot"] is (seq == outer)
          and h["inputSequenceRelation"] == ("older" if seq < outer else "newer" if seq > outer else "same"),
          "body original ordering mismatch")
    if seq == outer:
        check(all(ev[k] == h["inputEvidence"][k] for k in (
            "owner", "sequence", "observedNs", "deadlineNs", "focused", "tracked")), "same input differs")
    token = h["rightClaim"]["token"]
    check(token["id"] > 0 and token["kind"] == token["hand"] == 1
          and token["owner"] == expected and token["item"] == h["physicalGun"]
          and token["prerequisiteClaim"] == 0, "current GunHold mismatch")


def selected_matches(s, row):
    n = row["native"]
    policy.require(s["equipment"] == dict(asset=row["asset"], weapon=n["weapon"], data=n["data"], persistence=n["persistence"])
        and s["owner"]["weapon"] == n["weapon"] and s["bodyHolster"]["selectedSlot"] == row["assignment"]
        and s["bodyHolster"]["physicalGun"]["id"] == n["weapon"]
        and s["selected"]["configurationPath"] == row["configuration"]
        and s["selected"]["configurationPathPointer"] == row["configurationPathPointer"]
        and s["selected"]["soleConfiguredArray"] == row["soleConfiguredArray"], "current selected item mismatch")


def admit(s, now):
    validate_current(s, now)
    d, h = s["bodyInventory"], s["bodyHolster"]
    policy.require(d["available"] is True and d["observationOnly"] is True
        and d["observedNs"] <= d["sampledNs"] <= now < d["deadlineNs"] <= d["inputEvidence"]["deadlineNs"]
        and d["selectedOwner"] == s["owner"] and d["physicalOwner"] == h["inputEvidence"]["owner"]
        and d["physicalSelected"] == h["selectedSlot"]["item"]
        and len(d["slots"]) == d["count"] <= d["nativeCount"] <= 32, "current mapping invalid")
    rows = []
    for row in d["slots"]:
        c = row["configuration"]
        policy.require(c["available"] is True and c["configurationPathVerified"] is True
            and c["observedNs"] <= now < c["deadlineNs"] and c["owner"] == s["owner"]
            and c["inventory"] == d["inventory"] and c["weaponData"] == row["native"]["data"], "current configuration invalid")
        rows.append(dict(assignment=copy.deepcopy(row["assignment"]), native=copy.deepcopy(row["native"]),
            asset=c["asset"], configuration=c["configurationPath"],
            configurationPathPointer=c["configurationPathPointer"], soleConfiguredArray=c["soleConfiguredArray"]))
    source = next(row for row in rows if row["asset"] == SOURCE[0])
    target = next(row for row in rows if row["asset"] == TARGET[0])
    selected_matches(s, source)
    return dict(owner=copy.deepcopy(d["selectedOwner"]), physicalOwner=copy.deepcopy(d["physicalOwner"]),
        inventory=d["inventory"], switching=d["switching"], revision=d["revision"], cohort=d["cohort"],
        source=source, target=target)


def observe_admit(s, now, binding):
    validate_current(s, now)
    selected_matches(s, binding["source"])


class DrawAdmissionTests(unittest.TestCase):
    def initial(self, used=1):
        session = policy.begin(NOW-NS, NOW+29*NS)
        s = snapshot()
        result = policy.assess(s, NOW, context(s, used=used, phase="prime"), session, admit)
        self.assertEqual(result.status, policy.Status.READY, result.reason)
        return result

    def draw(self, used=1):
        return policy.start_draw(self.initial(used).session, NOW+MS, NOW+9700*MS)

    def unknown(self):
        s = snapshot(NOW+20*MS, 2, True)
        result = policy.assess(s, s["observed_ns"], context(s), self.draw(), admit)
        self.assertEqual(result.status, policy.Status.OBSERVE_ONLY, result.reason)
        self.assertEqual(result.commands, ())
        self.assertFalse(result.binding)
        self.assertEqual(json.loads(result.observation), s)
        self.assertEqual(result.session.uncertain_records, (result.observation,))
        self.assertTrue(result.uncertainties)
        return result

    def check_refused(self, s, session=None, c=None, callback=admit, now=None):
        result = policy.assess(s, s["observed_ns"] if now is None else now,
            c or context(s), session or self.draw(), callback)
        self.assertEqual(result.status, policy.Status.REFUSE, result.reason)
        self.assertEqual(result.commands, ())
        self.assertFalse(result.binding)
        self.assertTrue(result.session.refusal)
        return result

    def test_prime_ready_uses_full_original_admission_without_mutation(self):
        s = snapshot()
        original = copy.deepcopy(s)
        called = []
        def checked(value, now):
            self.assertIs(value, s)
            called.append(now)
            return admit(value, now)
        result = policy.assess(s, NOW, context(s, phase="prime"),
            policy.begin(NOW-NS, NOW+29*NS), checked)
        self.assertEqual(result.status, policy.Status.READY, result.reason)
        self.assertEqual(json.loads(result.binding), admit(s, NOW))
        self.assertEqual(called, [NOW])
        self.assertEqual(s, original)
        self.assertEqual(result.commands, ())

    def test_unknown_retains_exact_ready_anchor_and_fresh_ready_can_resume(self):
        waiting = self.unknown()
        s = snapshot(NOW+40*MS, 3)
        seen = []
        def checked(value, now):
            seen.append(copy.deepcopy(value))
            return admit(value, now)
        result = policy.assess(s, s["observed_ns"], context(s), waiting.session, checked)
        self.assertEqual(result.status, policy.Status.READY, result.reason)
        self.assertEqual(seen, [s])
        self.assertEqual(result.session.identity, waiting.session.identity)
        self.assertEqual(result.session.uncertain_records, waiting.session.uncertain_records)
        self.assertEqual(result.session.hold_started_ns, 0)
        self.assertEqual(result.session.admission_deadline_ns, NOW+29*NS)
        self.assertEqual(result.session.draw_deadline_ns, NOW+9700*MS)

    def test_opaque_flag_never_claims_idle_busy_or_grants_action(self):
        result = self.unknown()
        self.assertIn("UNKNOWN", policy.__doc__)
        self.assertIn("does not identify", result.uncertainties[0])
        # Same observable packet can represent a hidden real blocker. It still
        # has no authority; only a wholly new full Ready packet may resume.
        s = snapshot(NOW+40*MS, 3, True)
        next_result = policy.assess(s, s["observed_ns"], context(s), result.session, admit)
        self.assertEqual(next_result.status, policy.Status.OBSERVE_ONLY, next_result.reason)
        self.assertEqual(next_result.commands, ())
        self.assertFalse(next_result.binding)

    def test_explicit_blockers_refuse_before_wait_or_ready(self):
        for unknown in (False, True):
            for key in ("blockWeaponActions", "freeRightCurrent", "pendingOperation", "pendingRequest",
                        "visibility", "suppression", "ordinaryDrawCommand", "cancel", "reloadBusy",
                        "interactionSuspended"):
                with self.subTest(unknown=unknown, key=key):
                    s = snapshot(NOW+20*MS, 2, unknown)
                    s["bodyHolster"][key] = True
                    self.check_refused(s)
            s = snapshot(NOW+20*MS, 2, unknown)
            s["weapon_actions_blocked"] = True
            self.check_refused(s)
            self.check_refused(snapshot(NOW+20*MS, 2, unknown), c=replace(
                context(snapshot(NOW+20*MS, 2, unknown)), explicit_blockers=("observed cancellation",)))

    def test_changed_owner_token_item_mapping_and_reference_refuse(self):
        for mutate in (
            lambda s: s["owner"].update(actorGeneration=99),
            lambda s: s["bodyHolster"]["rightClaim"]["token"].update(id=99),
            lambda s: s["bodyHolster"]["physicalGun"].update(id=99),
            lambda s: s["bodyInventory"].update(cohort=99),
            lambda s: s["bodyInventory"]["slots"][1]["assignment"]["item"].update(generation=99),
            lambda s: s["bodyInventory"].update(nativeCount=32),
            lambda s: s["input"].update(trackingBasisGeneration=99),
        ):
            for unknown in (False, True):
                s = snapshot(NOW+20*MS, 2, unknown)
                mutate(s)
                self.check_refused(s)

    def test_stale_independent_leases_are_refused_never_extended(self):
        for section, field in (("outer", "deadlineNs"), ("body", "deadlineNs"),
                               ("claim", "deadlineNs"), ("mapping", "deadlineNs")):
            s = snapshot(NOW+20*MS, 2, True)
            part = dict(outer=s["input_evidence"], body=s["bodyHolster"]["inputEvidence"],
                        claim=s["bodyHolster"]["rightClaim"], mapping=s["bodyInventory"])[section]
            part[field] = s["observed_ns"]
            before = copy.deepcopy(s)
            self.check_refused(s)
            self.assertEqual(s, before)

    def test_control_tracking_focus_and_command_ownership_loss_refuse(self):
        for mutate in (
            lambda s: s["input"].update(focused=False),
            lambda s: s["bodyHolster"]["input"]["hands"][1].update(gripTracked=False),
            lambda s: s["input"]["hands"][1].update(held=1),
            lambda s: s["bodyHolster"]["input"]["hands"][1].update(trigger=.01),
        ):
            s = snapshot(NOW+20*MS, 2, True)
            mutate(s)
            self.check_refused(s)
        s = snapshot(NOW+20*MS, 2, True)
        for change in (dict(pending_command=True), dict(grip_action_started=True),
                       dict(draw_armed=True), dict(current_command_identity=("other",)),
                       dict(phase="draw_wait")):
            self.check_refused(s, c=replace(context(s), **change))
        for mutate in (lambda h: h.update(sessionState="VISIBLE"),
                       lambda h: h["handR"].update(valid=False),
                       lambda h: h["controls"].update(trigR=1),
                       lambda h: h["controls"].update(stickR=[.1, 0])):
            c = context(s)
            mutate(c.current_state)
            self.check_refused(s, c=c)

    def test_hold_accepts_at_most_two_new_replacements(self):
        waiting = self.unknown()
        second = snapshot(NOW+40*MS, 3, True)
        result = policy.assess(second, second["observed_ns"], context(second), waiting.session, admit)
        self.assertEqual(result.status, policy.Status.OBSERVE_ONLY, result.reason)
        third = snapshot(NOW+60*MS, 4)
        ready = policy.assess(third, third["observed_ns"], context(third), result.session, admit)
        self.assertEqual(ready.status, policy.Status.READY, ready.reason)
        third["bodyHolster"].update(inventoryEvaluation=0, allowAutomaticGunHold=False)
        refusal = self.check_refused(third, result.session)
        with self.assertRaises(policy.Refusal):
            policy.check_next_observation(refusal.session, NOW+70*MS)

    def test_hold500ms_and_original_draw10s_and_admission30s_never_restart(self):
        waiting = self.unknown()
        for now in (NOW+520*MS, NOW+9700*MS, NOW+29*NS):
            s = snapshot(now, 3)
            self.check_refused(s, waiting.session)
        with self.assertRaisesRegex(policy.Refusal, "cannot restart"):
            policy.start_draw(waiting.session, NOW+30*MS, NOW+9990*MS)
        with self.assertRaises(policy.Refusal):
            policy.begin(NOW, NOW+30*NS+1)

    def test_global_observation20_includes_prior_prime_attempts(self):
        session = self.draw(used=19)
        s = snapshot(NOW+20*MS, 2)
        result = policy.assess(s, s["observed_ns"], context(s, used=20), session, admit)
        self.assertEqual(result.status, policy.Status.READY, result.reason)
        with self.assertRaisesRegex(policy.Refusal, "20-observation"):
            policy.check_next_observation(result.session, NOW+30*MS)
        s["bodyHolster"].update(inventoryEvaluation=0, allowAutomaticGunHold=False)
        self.check_refused(s, session, context(s, used=20))

    def test_unknown_cannot_create_anchor_or_wait_outside_neutral_reach(self):
        s = snapshot(unknown=True)
        self.check_refused(s, policy.begin(NOW-NS, NOW+29*NS), context(s, phase="prime"))
        for evaluation, automatic in ((0, True), (1, False), (2, True), (3, False), (4, True)):
            s = snapshot(NOW+20*MS, 2)
            s["bodyHolster"].update(inventoryEvaluation=evaluation, allowAutomaticGunHold=automatic)
            self.check_refused(s)

    def test_replay_and_skipped_replacement_and_same_packet_restamp_refuse(self):
        waiting = self.unknown()
        s = json.loads(waiting.observation)
        self.check_refused(s, waiting.session)
        s = snapshot(NOW+40*MS, 3)
        self.check_refused(s, waiting.session, context(s, used=5))
        old = snapshot(NOW+20*MS, 2, True)
        for section in ("input_evidence",):
            s[section]["sequence"] = old[section]["sequence"]
            s["input"]["generation"] = s["shot_input_generation"] = old[section]["sequence"]
        self.check_refused(s, waiting.session)

    def test_new_pose_during_hold_and_callback_rejection_cannot_resume(self):
        waiting = self.unknown()
        s = snapshot(NOW+40*MS, 3)
        c = context(s)
        c.acknowledged_state["handR"]["pos"] = [.1, .2, .3]
        c.current_state["handR"]["pos"] = [.1, .2, .3]
        self.check_refused(s, waiting.session, c)
        def reject(value, now):
            raise ValueError("full original proof refused")
        self.check_refused(s, waiting.session, callback=reject)

    def test_uncertain_history_is_immutable_and_refusal_is_terminal(self):
        waiting = self.unknown()
        original = waiting.observation
        s = json.loads(original)
        s["bodyHolster"]["rightClaim"]["token"]["id"] = 999
        self.assertEqual(waiting.session.uncertain_records, (original,))
        s = snapshot(NOW+40*MS, 3)
        refused = self.check_refused(s, waiting.session, replace(context(s), pending_command=True))
        s = snapshot(NOW+60*MS, 4)
        self.check_refused(s, refused.session)

    def test_normal_motion_does_not_consume_shared_readiness_budget(self):
        session = self.draw(used=20)
        for index in range(2, 8):
            s = snapshot(NOW+index*20*MS, index)
            result = policy.assess(s, s["observed_ns"], context(s, used=20), session, admit)
            self.assertEqual(result.status, policy.Status.READY, result.reason)
            self.assertEqual(result.session.observations_used, 20)
            session = result.session

    def test_nested_missing_requests_cannot_restart_hold_budget(self):
        waiting = self.unknown()
        s = snapshot(NOW+40*MS, 3)
        # One unavailable response handled by92, then this fresh full response:
        # two extra requests total, both charged to the same pending episode.
        c = replace(context(s, used=4), hold_started_ns=waiting.session.hold_started_ns,
                    hold_replacements=2)
        result = policy.assess(s, s["observed_ns"], c, waiting.session, admit)
        self.assertEqual(result.status, policy.Status.READY, result.reason)
        self.check_refused(s, waiting.session, replace(c, hold_replacements=3))
        self.check_refused(s, waiting.session, replace(c, hold_started_ns=NOW-500*MS))
        with self.assertRaises(policy.Refusal):
            policy.check_next_observation(waiting.session, NOW+30*MS, observations_used=4)

    def test_only_exact_display_missing_can_preserve_mapping_as_history(self):
        session = self.draw()
        binding = json.loads(self.initial().binding)
        def observe(s, now):
            observe_admit(s, now, binding)
        s = snapshot(NOW+20*MS, 2, True)
        s["bodyInventory"] = dict(available=False, observationOnly=True,
                                  reason="display_missing", sampledNs=s["observed_ns"])
        result = policy.assess(s, s["observed_ns"], context(s), session, admit, observe)
        self.assertEqual(result.status, policy.Status.OBSERVE_ONLY, result.reason)
        self.assertEqual(result.session.mapping_identity, session.mapping_identity)
        self.assertIn("current complete mapping UNKNOWN: display_missing", result.uncertainties)
        for reason in ("display_expired_or_invalid", "display_owner_or_input_not_joined", "selected_assignment_not_joined"):
            s["bodyInventory"]["reason"] = reason
            self.check_refused(s, session)
        s["bodyInventory"]["reason"] = "display_missing"
        s["bodyHolster"].update(inventoryEvaluation=1, allowAutomaticGunHold=True)
        self.check_refused(s, session)

    def test_callback_mutation_is_refused_and_cannot_return_binding(self):
        s = snapshot(NOW+20*MS, 2)
        def mutate(value, now):
            binding = admit(value, now)
            value["bodyHolster"]["allowAutomaticGunHold"] = False
            return binding
        result = self.check_refused(s, callback=mutate)
        self.assertIn("mutated original evidence", result.reason)

    def test_initial_owned_ack_allows_stow_reach_before_first_command(self):
        for unknown in (False, True):
            s = snapshot(NOW+20*MS, 2, unknown)
            owned = (27, "initial-owned-ack", "initial-command-hash", 0, 0, 0)
            c = replace(context(s, used=1), last_write_ns=0, last_commit_ns=0,
                        command_identity=owned, current_command_identity=owned)
            result = policy.assess(s, s["observed_ns"], c, self.draw(), admit)
            self.assertEqual(result.status,
                policy.Status.OBSERVE_ONLY if unknown else policy.Status.READY, result.reason)
            if unknown:
                acknowledgement = json.loads(result.session.hold_ack)
                self.assertEqual((acknowledgement["written"], acknowledgement["committed"]), (0, 0))


class RetainedActualDrawAdmissionTests(unittest.TestCase):
    """Unmodified saved005/006 at historical time; no actual-game rerun claim.

    Context reconstructs the known acknowledged command from its saved receipt;
    the test assumes that acknowledged host pose remains held. It tests the pure
    decision, not fresh process ownership or unrecorded post006 native success.
    """
    DIRECTORY = Path("G:/Unity/VRMMO/FrostbiteVR/reports/coordinator-20261103-feedback221/feed49/xr-game03/") / (
        "live-driver-8ddeabf862a64cb187219a0f70ea4248")

    @classmethod
    def setUpClass(cls):
        legacy_path = ROOT / "reports/coordinator-20261102-feedback94/slot-binding/drive_bc2_holster_binding94.py"
        required = [legacy_path,
            ROOT / "reports/coordinator-20261101-feedback92/holster-draw-fix/drive_bc2_holster_observation92.py",
            ROOT / "reports/coordinator-20261101-feedback80/draw/drive_bc2_holster_simulation.py",
            Path("G:/Unity/VRMMO/FrostbiteVR/tools/drive_bc2_feed_simulation.py")]
        required += [cls.DIRECTORY / name for name in (
            "snapshot-005.json", "snapshot-006.json", "events.jsonl", "initial-state.json",
            "request-005-timing.json", "request-006-timing.json")]
        absent = [str(path) for path in required if not path.is_file()]
        if absent:
            raise unittest.SkipTest("retained actual221/run03 and original legacy callback chain absent: " + ", ".join(absent))
        spec = importlib.util.spec_from_file_location("draw_admission_actual94", legacy_path)
        cls.legacy = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.legacy)

    def test_original005_to006_becomes_unknown_only_without_rewriting_raw(self):
        self.assertTrue(self.DIRECTORY.is_dir(), "retained actual221/run03 evidence required")
        legacy = self.legacy
        def actual_admit(s, now):
            return legacy.resolve_slots(s, now, *SOURCE, *TARGET)
        read = lambda name: json.loads((self.DIRECTORY / name).read_text(encoding="utf-8-sig"))
        before, after = read("snapshot-005.json"), read("snapshot-006.json")
        original = copy.deepcopy((before, after))
        self.assertIsInstance(after["nonce"], str)
        self.assertEqual(after["bodyInventory"]["reason"], "display_missing")
        events = [json.loads(line) for line in (self.DIRECTORY / "events.jsonl").read_text(encoding="utf-8-sig").splitlines()]
        requests = [e for e in events if e["type"] == "command_request"]
        commits = [e for e in events if e["type"] == "command_commit" and not e["cleanup"]]

        def historical_context(s, index, ack_index):
            timing = read("request-%03d-timing.json" % index)
            ack = commits[ack_index]
            retained_host = read("initial-state.json")
            retained_host.update(ack["state"])
            command = next(e for e in requests if e["atNs"] == ack["writtenNs"])
            line = next(line for line in command["commands"] if line.startswith("hand r grip pose "))
            numbers = [float(v) for v in line.split()[4:]]
            pose = dict(position=numbers[:3], orientation=legacy.original.frame.ypr_quaternion(numbers[3:]))
            identity = (ack["state"]["cmdSeq"], ack["state"]["lastCmd"], ack["writtenNs"], ack["committedNs"])
            return policy.Context(timing["nonce"], timing["requestStartedNs"], timing["requestPublishedNs"],
                timing["responseDeadlineNs"], 2, "stow_reach", ack["writtenNs"], ack["committedNs"],
                identity, identity, retained_host, copy.deepcopy(retained_host), pose)

        draw_started = 243170025096700
        session = policy.start_draw(policy.begin(draw_started-NS, draw_started+29*NS),
                                    draw_started, draw_started+9700*MS)
        ready = policy.assess(before, before["observed_ns"]+MS, historical_context(before, 5, 2), session, actual_admit)
        self.assertEqual(ready.status, policy.Status.READY, ready.reason)
        binding = json.loads(ready.binding)
        calls = []

        def observe(s, now):
            self.assertIs(s, after)
            calls.append("original80_frame_holster_and94_selected")
            admission = legacy.original.HolsterDriver.__new__(legacy.original.HolsterDriver)
            admission.baseline = admission.last_native = None
            admission.phase = "baseline"
            legacy.original.HolsterDriver.admit_snapshot(admission, s, now, policy.integer(s["nonce"]))
            legacy.selected_matches(s, binding["source"])

        waiting = policy.assess(after, after["observed_ns"]+MS, historical_context(after, 6, 3),
                                ready.session, actual_admit, observe)
        self.assertEqual(waiting.status, policy.Status.OBSERVE_ONLY, waiting.reason)
        self.assertEqual(calls, ["original80_frame_holster_and94_selected"])
        self.assertEqual(waiting.commands, ())
        self.assertFalse(waiting.binding)
        self.assertEqual(waiting.session.identity, ready.session.identity)
        self.assertEqual(waiting.session.mapping_identity, ready.session.mapping_identity)
        self.assertEqual((before, after), original)
        self.assertIn("current complete mapping UNKNOWN: display_missing", waiting.uncertainties)
        with self.assertRaises(legacy.REFUSAL):
            actual_admit(after, after["observed_ns"]+MS)


if __name__ == "__main__":
    unittest.main()
