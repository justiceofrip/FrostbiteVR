"""Offline shared-entry checks; synthetic fixtures are not native acceptance.

The entry is loaded from an isolated tree containing only its five tracked tools.
No historical report fixture, session, game process or native API is required.
"""
import copy
import importlib.util
import io
import json
from pathlib import Path
import runpy
import shutil
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
NOW = 2_000_000_000
SOURCE = ("source", "source/config")
TARGET = ("target", "target/config")
TOOLS = ("drive_bc2_holster_prepared.py", "bc2_holster_core.py",
         "bc2_holster_snapshot_transport.py", "drive_bc2_feed_simulation.py",
         "plan_bc2_feed_simulation.py")


def outer_snapshot(now, nonce, sequence, kind="empty"):
    """Construct joined original leases from scratch; no saved native payload."""
    if kind.startswith("invalid:"):
        return dict(schema="fvr.bc2.live-mechanism-snapshot.v1", valid=False, nonce=nonce,
                    invalid_reason=kind.split(":", 1)[1], observed_ns=now,
                    bodyHolster=dict(available=False, reason="snapshot_unavailable"))
    if kind == "empty_expired":
        now -= 110_000_000
    observed, deadline = now - 2_000_000, now + 80_000_000
    pose = {"position": [.2, 1.4, .1], "orientation": [0, 0, 0, 1]}
    head = {"position": [0, 1.6, 0], "orientation": [0, 0, 0, 1]}
    owner = dict(player=11, soldier=22, weak=33, weapon=44, actorGeneration=1, equipGeneration=1, space=2)
    if kind == "owner_changed":
        owner["actorGeneration"] = 2
    input_owner = dict(actor=(33 << 32) | 22, actorGeneration=owner["actorGeneration"], equipGeneration=2, space=2)
    evidence = dict(owner=input_owner, sequence=sequence, observedNs=observed, deadlineNs=deadline,
                    nowNs=now-1_000_000, focused=True, tracked=[True, True], released=[True, False])
    hands = [dict(grip=copy.deepcopy(pose), aim=copy.deepcopy(pose), gripTracked=True, aimTracked=True,
                  held=0, squeeze=0, trigger=0, stickX=0, stickY=0) for _ in range(2)]
    if kind != "held":
        hands[1].update(held=1, squeeze=1)
    inp = dict(generation=sequence, spaceGeneration=2, predictedNs=now, focused=True, headValid=True,
               floorRelative=True, worldUnitsPerMeter=1, trackingBasisGeneration=2,
               referenceHead=head, head=copy.deepcopy(head), hands=hands)
    gun = dict(id=88, generation=2)
    h = dict(available=True, sampledNs=now-1_000_000, validUntilNs=deadline, phase=3,
             actualEmpty=True, nativeTick=sequence+100, request=2, nativeOwner=copy.deepcopy(owner),
             inputEvidence=copy.deepcopy(evidence), physicalGun=gun, inputMatchesSnapshot=True,
             inputSequenceRelation="same", neutralTrackedInput=False, input=copy.deepcopy(inp),
             selectedSlot=dict(slot=1, item=dict(id=44, generation=1)), leftClaim=None, rightClaim=None,
             inventoryEvaluation=1, emptyHands=True, blockWeaponActions=True, allowAutomaticGunHold=False,
             freeRightCurrent=True)
    visibility = dict(nativeOwner=copy.deepcopy(owner), request=2, hidden=True, verifiedCopyMask=3,
                      drawSerial=1, physicalEquipGeneration=2, inputSequence=sequence,
                      observedNs=observed, deadlineNs=deadline)
    suppression = dict(owner=copy.deepcopy(owner), inputEvidence=copy.deepcopy(evidence), request=2,
                       nativeTick=h["nativeTick"], cache=0x10000, equipmentDispatched=False)
    h["freeRight"] = dict(inputEvidence=copy.deepcopy(evidence), visibility=visibility,
                          suppression=suppression, authorizationDeadlineNs=0)
    if kind == "held":
        h.update(phase=1, actualEmpty=False, neutralTrackedInput=True, emptyHands=False,
                 blockWeaponActions=False, allowAutomaticGunHold=True, freeRightCurrent=False, freeRight=None,
                 rightClaim=dict(token=dict(id=1, owner=copy.deepcopy(input_owner), hand=1, kind=1,
                                            item=copy.deepcopy(gun), prerequisiteClaim=0),
                                 inputSequence=sequence, deadlineNs=deadline))
    result = dict(schema="fvr.bc2.live-mechanism-snapshot.v1", valid=True, nonce=nonce,
                  observed_ns=now, valid_until_ns=deadline, native_deadline_ns=deadline,
                  shot_input_generation=sequence, physical_equipment_generation=2,
                  weapon_actions_blocked=h["blockWeaponActions"], owner=owner,
                  equipment=dict(weapon=44, data=55, asset="source"),
                  rig=dict(soldier=22, weak=33, count=1, skeleton=66, pose=77),
                  selected=dict(configurationPathVerified=True, sequence=sequence, observedNs=observed,
                                deadlineNs=deadline, configurationPath="source-config", configurationPathPointer=99,
                                inventory=111, selectedSlot=0, soleConfiguredArray=222, matchedMeshPath="mesh",
                                meshes=[dict(path="mesh")]), input=inp, input_evidence=evidence,
                  bodyHolster=h, feed=None)
    if kind in ("missing", "other_refusal", "owner_changed", "bad_outer") or kind.startswith("reason:"):
        result["bodyHolster"] = dict(available=False,
                                     reason="not_enabled" if kind == "other_refusal" else "pending_or_expired")
        if kind.startswith("reason:"):
            result["bodyHolster"]["reason"] = kind.split(":", 1)[1]
    if kind == "bad_join":
        h["freeRight"]["suppression"]["inputEvidence"]["sequence"] += 1
    if kind == "bad_outer":
        result["input_evidence"]["owner"]["space"] += 1
    if kind in ("target", "wrong_target"):
        owner.update(weapon=45, equipGeneration=2)
        evidence["owner"]["equipGeneration"] = 3
        gun = dict(id=89, generation=3)
        result.update(physical_equipment_generation=3, weapon_actions_blocked=False,
                      equipment=dict(weapon=45, data=56, asset="target" if kind == "target" else "foreign"))
        result["selected"].update(configurationPath="target-config", configurationPathPointer=100,
                                  selectedSlot=1, soleConfiguredArray=223, matchedMeshPath="target-mesh",
                                  meshes=[dict(path="target-mesh")])
        h.update(nativeOwner=copy.deepcopy(owner), inputEvidence=copy.deepcopy(evidence),
                 physicalGun=gun, selectedSlot=dict(slot=2, item=dict(id=45, generation=2)),
                 phase=1, actualEmpty=False, neutralTrackedInput=False, emptyHands=False,
                 blockWeaponActions=False, allowAutomaticGunHold=True, freeRightCurrent=False, freeRight=None,
                 rightClaim=dict(token=dict(id=2, owner=copy.deepcopy(evidence["owner"]), hand=1, kind=1,
                                            item=copy.deepcopy(gun), prerequisiteClaim=0),
                                 inputSequence=sequence, deadlineNs=deadline))
    return result


def snapshot(source_body=2, target_body=1, source_native=0, target_native=7,
             source_weapon=0x20000, target_weapon=0x30000, reverse=False):
    s = outer_snapshot(NOW, 91, 10, "held")
    s["owner"]["weapon"] = source_weapon
    s["equipment"] = dict(weapon=source_weapon, data=source_weapon+0x1000, persistence=17, asset=SOURCE[0])
    selected = s["selected"]
    selected.update(configurationPath=SOURCE[1], configurationPathPointer=0x60000,
                    inventory=0x50000, selectedSlot=source_native, soleConfiguredArray=0x70000)
    h = s["bodyHolster"]
    h["nativeOwner"] = copy.deepcopy(s["owner"])
    h["physicalGun"]["id"] = source_weapon
    h["rightClaim"]["token"]["item"] = copy.deepcopy(h["physicalGun"])
    h["selectedSlot"] = dict(slot=source_body, item=dict(id=source_weapon, generation=1))
    rows = []
    for index, (body_slot, native_slot, weapon, identity) in enumerate((
            (source_body, source_native, source_weapon, SOURCE),
            (target_body, target_native, target_weapon, TARGET))):
        configuration = dict(available=True, owner=copy.deepcopy(s["owner"]), asset=identity[0],
            configurationPath=identity[1], configurationPathPointer=0x60000+index*0x100,
            configurationPathVerified=True, soleConfiguredArray=0x70000+index*0x100,
            inventory=selected["inventory"], selectedSlot=source_native, kind="carried" if index else "selected",
            weaponData=weapon+0x1000, sequence=selected["sequence"],
            observedNs=selected["observedNs"], deadlineNs=selected["deadlineNs"])
        rows.append(dict(assignment=dict(slot=body_slot, item=dict(id=weapon, generation=index+1)),
                         native=dict(slot=native_slot, weapon=weapon, data=weapon+0x1000,
                                     persistence=17+index, category=index), configuration=configuration))
    if reverse:
        rows.reverse()
    s["bodyInventory"] = dict(available=True, observationOnly=True, reason=None,
        sampledNs=NOW, selectedOwner=copy.deepcopy(s["owner"]), physicalOwner=copy.deepcopy(h["inputEvidence"]["owner"]),
        physicalSelected=copy.deepcopy(h["selectedSlot"]["item"]), inputEvidence=copy.deepcopy(h["inputEvidence"]),
        sequence=10, observedNs=selected["observedNs"], deadlineNs=selected["deadlineNs"],
        inventory=selected["inventory"], switching=0x51000, revision=3, cohort=4,
        count=2, nativeCount=max(source_native, target_native)+1, slots=rows)
    return s


def target_snapshot(baseline, binding):
    s = copy.deepcopy(baseline)
    row = binding["target"]
    n, h = row["native"], s["bodyHolster"]
    s["owner"].update(weapon=n["weapon"], equipGeneration=2)
    s["physical_equipment_generation"] = 3
    s["input_evidence"]["owner"]["equipGeneration"] = 3
    s["equipment"] = dict(weapon=n["weapon"], data=n["data"], persistence=n["persistence"], asset=row["asset"])
    s["selected"].update(configurationPath=row["configuration"], configurationPathPointer=row["configurationPathPointer"],
                         soleConfiguredArray=row["soleConfiguredArray"], selectedSlot=n["slot"])
    h.update(nativeOwner=copy.deepcopy(s["owner"]), inputEvidence=copy.deepcopy(s["input_evidence"]),
             physicalGun=dict(id=n["weapon"], generation=3), selectedSlot=copy.deepcopy(row["assignment"]))
    h["rightClaim"]["token"].update(owner=copy.deepcopy(h["inputEvidence"]["owner"]), item=copy.deepcopy(h["physicalGun"]))
    # Target observation does not need to manufacture a new carried mapping.
    s.pop("bodyInventory")
    return s


class Artifacts:
    def __init__(self, directory):
        self.directory, self.saved, self.events = directory, {}, []

    def save(self, name, value, **kwargs):
        self.saved[name] = copy.deepcopy(value)

    def event(self, value, *args, **kwargs):
        self.events.append(copy.deepcopy(value))


class SharedEntryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # The caller controls TEMP/TMP/TMPDIR; no repository reports are copied.
        cls.temporary = tempfile.TemporaryDirectory(prefix="holster-source-only-")
        cls.root = Path(cls.temporary.name)
        cls.tools = cls.root / "tools"
        cls.tools.mkdir()
        for name in TOOLS:
            shutil.copyfile(ROOT / "tools" / name, cls.tools / name)
        spec = importlib.util.spec_from_file_location(
            "source_only_holster", cls.tools / "drive_bc2_holster_prepared.py")
        cls.entry = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.entry)
        cls.core, cls.transport = cls.entry.core, cls.entry.transport

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def arguments(self):
        return ["--run", str(self.root / "missing-run"), "--stow-asset", SOURCE[0],
                "--stow-config", SOURCE[1], "--target-asset", TARGET[0], "--target-config", TARGET[1]]

    def driver(self, source=None):
        s = snapshot() if source is None else source
        d = self.entry.HolsterDriver.__new__(self.entry.HolsterDriver)
        d.args = SimpleNamespace(stow_asset=SOURCE[0], stow_config=SOURCE[1],
            target_asset=TARGET[0], target_config=TARGET[1], observed_binding=None)
        d.slot_binding = self.core.resolve_slots(s, NOW, *SOURCE, *TARGET)
        d.args.stow_slot = d.slot_binding["source"]["assignment"]["slot"]
        d.args.target_slot = d.slot_binding["target"]["assignment"]["slot"]
        d.stow_ack = d.stow_cohort_binding = None
        d.phase, d.held_right, d.pending_command, d.draw_armed = "stow_reach", False, False, False
        d.unsafe_owner = d.held_left = d.held_trigger = False
        d.held_buttons = set()
        d.press_ns = 0
        d.baseline = d.latest_snapshot = d.last_native = d.publication_source = s
        d.target_baseline = None
        d.expected = dict(asset=TARGET[0], configuration=TARGET[1], slot=d.args.target_slot)
        d.holster_hold_started = None
        d.holster_hold_state = d.holster_hold_command = None
        d.holster_replacements = 0
        d.sequence, d.sentinel, d.commands = 42, "fixture-owned", 42
        d.last_write, d.last_commit = NOW-10_000_000, NOW-5_000_000
        d.started = NOW-20_000_000
        d.deadline = d.stage_deadline = NOW+9_000_000_000
        d.hard_deadline = NOW+10_000_000_000
        d.snapshot_refresh_started = d.motion_started = None
        d.state = dict(cmdSeq=d.sequence, lastCmd=d.sentinel, captureSeq=0,
                       head={}, handL={}, handR={}, controls={})
        d.read_state = lambda: copy.deepcopy(d.state)
        d.command_path = self.root / "command.txt"
        d.command_path.write_bytes(b"fixture-owned\n")
        d.command_hash = self.transport.digest(d.command_path.read_bytes())
        d.artifacts = Artifacts(self.root)
        d.right_pose = copy.deepcopy(s["input"]["hands"][1]["grip"])
        return d

    def test_only_tracked_siblings_import_and_public_cli_executes(self):
        self.assertEqual({p.name for p in self.tools.iterdir()}, set(TOOLS))
        self.assertFalse((self.root / "reports").exists())
        for module in (self.entry, self.core, self.transport, self.transport.planner,
                       self.entry._snapshot_transport):
            self.assertEqual(Path(module.__file__).parent, self.tools)
        with patch.object(self.transport.Driver, "receive_snapshot", return_value="canonical") as receive:
            d = self.entry.HolsterDriver.__new__(self.entry.HolsterDriver)
            self.assertEqual(d.receive_native_snapshot(), "canonical")
            receive.assert_called_once_with(d)
        with patch.object(sys, "argv", [str(self.tools / TOOLS[0]), "--help"]), redirect_stdout(io.StringIO()):
            with self.assertRaises(SystemExit) as stopped:
                runpy.run_path(str(self.tools / TOOLS[0]), run_name="__main__")
        self.assertEqual(stopped.exception.code, 0)
        output = io.StringIO()
        with redirect_stdout(output):
            code = self.entry.main(self.arguments(), observed_binding={"prime": "unchanged"})
        result = json.loads(output.getvalue())
        self.assertEqual((code, result["status"]), (2, "refused"))
        self.assertFalse(result["nativeAcceptance"])
        self.assertFalse(result["headsetAcceptance"])
        self.assertNotIn("FileNotFoundError: reports", result["error"])

    def test_full_mapping_and_independent_owner_and_deadline_refusals(self):
        source = snapshot()
        saved = copy.deepcopy(source)
        binding = self.core.resolve_slots(source, NOW, *SOURCE, *TARGET)
        self.assertEqual((binding["source"]["assignment"]["slot"], binding["target"]["assignment"]["slot"]), (2, 1))
        self.assertEqual(source, saved)
        paths = (("owner", "weapon"), ("bodyHolster", "nativeOwner", "actorGeneration"),
                 ("bodyInventory", "physicalOwner", "space"), ("valid_until_ns",),
                 ("bodyHolster", "validUntilNs"), ("bodyInventory", "deadlineNs"),
                 ("bodyInventory", "slots", 1, "configuration", "deadlineNs"))
        for path in paths:
            with self.subTest(path=path):
                changed = copy.deepcopy(source)
                obj = changed
                for key in path[:-1]:
                    obj = obj[key]
                obj[path[-1]] = NOW if "Ns" in str(path[-1]) or path[-1] == "valid_until_ns" else 999
                with self.assertRaises(self.core.REFUSAL):
                    self.core.resolve_slots(changed, NOW, *SOURCE, *TARGET)

    def test_publication_rechecks_owner_source_deadline_and_observation_hold(self):
        for condition in ("owner", "source", "hold", "input"):
            with self.subTest(condition=condition), patch.object(self.core.time, "perf_counter_ns", return_value=NOW):
                d = self.driver()
                if condition == "owner":
                    d.command_path.write_bytes(b"another-writer\n")
                elif condition == "source":
                    d.publication_source["bodyHolster"]["validUntilNs"] = NOW
                elif condition == "hold":
                    d.holster_hold_started = NOW-1
                else:
                    d.read_state = lambda: dict(d.state, controls={"gripR": 1})
                with self.assertRaises(self.core.REFUSAL):
                    d.validate_publication(["grip r 0"], dict(pose=d.right_pose, grip=0), False)
                if condition == "owner":
                    self.assertTrue(d.unsafe_owner)

    def test_ordinary_command_bytes_and_prepared_batch_cannot_change(self):
        d = self.driver()
        captured = []
        def send(lines, **kwargs):
            d.validate_publication(lines, kwargs["expected"], False)
            captured.append((lines, kwargs))
        d.send = send
        pose = dict(position=[.125, 1.5, -.25], orientation=[0, 0, 0, 1])
        with patch.object(self.core.time, "perf_counter_ns", return_value=NOW):
            d.command(d.latest_snapshot, pose, 0)
        expected = ["hand r follow off", "hand r grip pose 0.125 1.5 -0.25 0 0 0",
                    "hand r aim pose 0.125 1.5 -0.25 0 0 0", "grip r 0"]
        self.assertEqual(captured[0][0], expected)
        self.assertEqual(captured[0][1]["source_deadline"], self.core.source_deadline(d.latest_snapshot))
        with patch.object(self.transport, "open_prepared_command", return_value=io.BytesIO()), \
             patch.object(self.transport, "prepare_command_rename", return_value=None), \
             patch.object(self.transport, "delete_prepared_command"), \
             patch.object(self.core.time, "perf_counter_ns", return_value=NOW):
            d.prepare_next_command(expected)
            prepared = d._prepared_command
            self.assertEqual(prepared.raw, ("\n".join(expected + [prepared.sentinel])+"\n").encode("ascii"))
            prepared.matches(d, expected)
            with self.assertRaises(self.core.REFUSAL):
                prepared.matches(d, expected[:-1]+["grip r 1"])
            d.sequence += 1
            with self.assertRaises(self.core.REFUSAL):
                prepared.matches(d, expected)
            d.discard_prepared_command()
            self.assertIsNone(prepared.stream)

    def test_original_bounds_and_owned_release_cleanup(self):
        with patch.object(self.core.time, "perf_counter_ns", return_value=NOW):
            d = self.driver()
            for field in ("deadline", "stage_deadline"):
                before = getattr(d, field)
                setattr(d, field, NOW)
                with self.assertRaises(self.core.REFUSAL):
                    d.bounds(NOW)
                setattr(d, field, before)
            d.held_right = True
            sent = []
            def send(lines, **kwargs):
                d.validate_publication(lines, None, kwargs["cleanup"])
                sent.append(lines)
            d.send = send
            self.assertEqual(d.release_after_failure(), "ordinary owned controller release acknowledged")
            self.assertEqual(sent, [["grip r 0"]])
            with self.assertRaises(self.core.REFUSAL):
                d.validate_publication(["grip r 1"], None, True)
            d.unsafe_owner = True
            self.assertIn("not attempted", d.release_after_failure())
            self.assertEqual(len(sent), 1)

    def test_prime_binding_is_enforced_before_any_command(self):
        d = self.driver()
        primed = copy.deepcopy(d.slot_binding)
        primed["cohort"] += 1
        d.slot_binding = None
        d.args.observed_binding = primed
        with patch.object(self.core.OrdinaryHolsterDriver, "current", return_value=(snapshot(), NOW)), \
             patch.object(self.transport.Driver, "send", side_effect=AssertionError("unexpected input")):
            with self.assertRaisesRegex(self.core.REFUSAL, "changed after observer prime"):
                d.current()
        self.assertIsNone(d.slot_binding)
        self.assertEqual(d.args.observed_binding, primed)

    def test_preparation_requires_exact_ack_and_discards_on_observation_failure(self):
        for changed in (None, "sequence", "right_pose", "unsafe_owner"):
            with self.subTest(changed=changed):
                d = self.driver()
                d.phase, d.held_right = "stow_wait", True
                lines = ["hand r follow off", "hand r grip pose 0 1 0 0 0 0",
                         "hand r aim pose 0 1 0 0 0 0", "grip r 1"]
                d.stow_ack = dict(identity=d.command_identity(), pose=copy.deepcopy(d.right_pose), lines=lines)
                if changed == "sequence":
                    d.sequence += 1
                elif changed == "right_pose":
                    d.right_pose = dict(position=[0, 0, 0], orientation=[0, 0, 0, 1])
                elif changed == "unsafe_owner":
                    d.unsafe_owner = True
                with patch.object(d, "prepare_next_command") as prepare, \
                     patch.object(d, "discard_prepared_command") as discard, \
                     patch.object(self.core.HolsterDriver, "wait_state", side_effect=self.core.REFUSAL("synthetic expired observation")):
                    with self.assertRaises(self.core.REFUSAL):
                        d.wait_state("empty")
                    if changed is None:
                        prepare.assert_called_once_with(lines[:-1]+["grip r 0"])
                        discard.assert_called_once_with()
                    else:
                        prepare.assert_not_called()

    def test_readiness_composes_with_core_and_runs_full_admission(self):
        modules = []
        with tempfile.TemporaryDirectory(prefix="readiness-", dir=self.root) as temporary:
            directory = Path(temporary)
            for name in ("bc2_draw_admission", "bc2_draw_readiness_driver"):
                path = directory / (name + ".py")
                shutil.copyfile(ROOT / "tools" / path.name, path)
                spec = importlib.util.spec_from_file_location("source_only_"+name, path)
                module = importlib.util.module_from_spec(spec)
                sys.modules[spec.name] = module
                self.addCleanup(sys.modules.pop, spec.name, None)
                spec.loader.exec_module(module)
                modules.append(module)
            policy, adapter = modules
            budget = adapter.AdmissionBudget(policy, NOW-100_000_000, NOW+20_000_000_000)
            budget.prime_attempt(NOW-50_000_000)
            composed = adapter.make_driver(self.entry.HolsterDriver, self.entry.core,
                                           budget=budget, clock=lambda: NOW)
            d = composed.__new__(composed)
            d.__dict__.update(self.driver().__dict__)
            budget.attach_draw(d.started, d.deadline)
            d._readiness_ack = d.command_identity()
            d._readiness_ready = d._readiness_record = None
            source = d.latest_snapshot
            d.state.update(pid=123, runtime="xr-sim", sessionRunning=True, actionsAttached=True,
                sessionState="FOCUSED", paceMode="free", errors=0, lastCmdError="",
                head=dict(valid=True), handL=dict(valid=True),
                handR=dict(valid=True, followsHead=False, pos=copy.deepcopy(d.right_pose["position"]), ypr=[0, 0, 0]),
                controls=dict(gripL=0, gripR=0, trigL=0, trigR=0, stickL=[0, 0], stickR=[0, 0],
                    a=False, b=False, x=False, y=False, menu=False, clickL=False, clickR=False, restL=False, restR=False))
            d.read_state = lambda: copy.deepcopy(d.state)
            d.last_request, d.last_request_published = NOW-5_000_000, NOW-4_000_000
            # Exercise the adapter's original-frame conversion as well as full
            # mapping admission; only the native receive channel is substituted.
            d.right_pose = None
            with patch.object(self.entry.HolsterDriver, "receive_snapshot", return_value=(source, b"synthetic", NOW)), \
                 patch.object(self.core.time, "perf_counter_ns", return_value=NOW):
                received = d.receive_snapshot()
                self.assertIs(received[0], source)
                self.assertEqual(budget.decisions[-1]["status"], "Ready")
                self.assertIs(d._readiness_ready, source)
                d.validate_publication(["grip r 0"], dict(pose=source["input"]["hands"][1]["grip"], grip=0), False)
                missing = copy.deepcopy(source)
                missing["bodyInventory"] = dict(available=False, reason="display_missing")
                d._observe_admission(missing, NOW)
                missing["owner"]["actorGeneration"] += 1
                with self.assertRaises(self.core.REFUSAL):
                    d._observe_admission(missing, NOW)
                source["bodyInventory"]["deadlineNs"] = NOW
                with self.assertRaises(self.core.REFUSAL):
                    d.validate_publication(["grip r 0"], dict(pose=source["input"]["hands"][1]["grip"], grip=0), False)

    def test_main_keeps_observed_binding_keyword_and_copies_it(self):
        primed = {"source": {"exact": "original"}}
        observed = []
        entry = self.entry
        class OfflineDriver(entry.HolsterDriver):
            def __init__(self, args):
                observed.append(args.observed_binding)
                args.observed_binding["source"]["exact"] = "local-copy"
                self.artifacts = None

            def run_holster(self):
                raise entry.core.REFUSAL("synthetic stop before live input")

            def close(self):
                observed.append("closed")

        with patch.object(entry, "HolsterDriver", OfflineDriver), redirect_stdout(io.StringIO()):
            self.assertEqual(entry.main(self.arguments(), observed_binding=primed), 2)
        self.assertEqual(primed, {"source": {"exact": "original"}})
        self.assertEqual(observed, [{"source": {"exact": "local-copy"}}, "closed"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
