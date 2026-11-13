"""Shared ordinary XR holster policy, using tracked sibling transport only.

Lifted from feedback80 -> 92 -> 94 -> 100 without changing admission predicates,
command bytes, ownership, observation budgets or cleanup rules. Layers retain
ordinary stow/draw, bounded observations, exact mapping and one stow cohort step.
No input, native work or session access occurs on import.
"""
import argparse
import copy
import importlib.util
import json
import math
from pathlib import Path
import re
import sys
import time
from types import SimpleNamespace

sys.dont_write_bytecode = True
CHECKOUT = Path(__file__).resolve().parents[1]
TRANSPORT_PATH = Path(__file__).with_name("drive_bc2_feed_simulation.py")
_spec = importlib.util.spec_from_file_location("bc2_holster_transport", TRANSPORT_PATH)
transport = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(transport)
frame = transport.planner
require, integer, REFUSAL = transport.require, transport.integer, transport.REFUSAL
NS = transport.NS
ANCHORS = {1: [-.20, -.12, -.18], 2: [.20, -.12, -.18], 4: [.24, -.62, .02]}
ACTOR = ("player", "soldier", "weak", "actorGeneration", "space")
REFERENCE = ("spaceGeneration", "trackingBasisGeneration", "worldUnitsPerMeter",
             "floorRelative", "referenceHead", "head")
HOLD_NS, MAX_REPLACEMENTS = 500_000_000, 2
OBSERVATION_REASONS = frozenset(("pending_or_expired", "sample_missing", "sample_age_expired", "input_lease_expired"))


def neutral(inp):
    return (inp["focused"] is True and inp["headValid"] is True
            and len(inp["hands"]) == 2 and all(
                h["gripTracked"] is True and h["aimTracked"] is True
                and integer(h["held"]) == 0 and 0 <= frame.finite(h["squeeze"]) <= .01
                and 0 <= frame.finite(h["trigger"]) <= .01
                and frame.finite(h["stickX"]) == frame.finite(h["stickY"]) == 0
                for h in inp["hands"]))


def holster_evidence(sample, snapshot, now):
    owner = snapshot["owner"]
    expected = {"actor": (integer(owner["weak"]) << 32) | integer(owner["soldier"]),
                "actorGeneration": integer(owner["actorGeneration"]),
                "equipGeneration": integer(snapshot["physical_equipment_generation"]),
                "space": integer(owner["space"])}
    require(sample["owner"] == expected, "holster original input owner changed")
    observed, deadline = integer(sample["observedNs"]), integer(sample["deadlineNs"])
    require(0 < observed <= integer(sample["nowNs"]) <= now < deadline
            and 0 < deadline - observed <= 150_000_000,
            "expired or invalid original holster input lease")
    require(integer(sample["sequence"]) > 0 and sample["focused"] is True
            and sample["tracked"] == [True, True], "holster input not focused/tracked")


def same_evidence(a, b):
    return all(a[k] == b[k] for k in ("owner", "sequence", "observedNs", "deadlineNs", "focused", "tracked"))


def holster_proof(snapshot, now):
    """Validate independent original gameplay leases; never infer Empty from Fire block."""
    h = snapshot["bodyHolster"]
    require(h.get("available") is True, "bodyHolster observation unavailable: " + str(h.get("reason")))
    require(h["nativeOwner"] == snapshot["owner"], "holster/native owner mismatch")
    ev, inp = h["inputEvidence"], h["input"]
    holster_evidence(ev, snapshot, now)
    sampled = integer(h["sampledNs"])
    until = integer(h["validUntilNs"])
    require(0 < sampled <= now < until <= min(integer(ev["deadlineNs"]), sampled + 150_000_000),
            "expired or extended holster observation lease")
    require(integer(h["nativeTick"]) > 0 and integer(h["physicalGun"]["id"]) > 0
            and integer(h["physicalGun"]["generation"]) == integer(ev["owner"]["equipGeneration"]),
            "holster physical gun mismatch")
    seq, outer_seq = integer(ev["sequence"]), integer(snapshot["input_evidence"]["sequence"])
    relation = "older" if seq < outer_seq else "newer" if seq > outer_seq else "same"
    require(h["inputSequenceRelation"] == relation
            and h["inputMatchesSnapshot"] is (seq == outer_seq), "holster independent input ordering mismatch")
    if seq == outer_seq:
        require(same_evidence(ev, snapshot["input_evidence"]), "same-sequence original leases differ")
    require(integer(inp["generation"]) == seq
            and integer(inp["spaceGeneration"]) == integer(snapshot["input"]["spaceGeneration"])
            and inp["focused"] is True and inp["headValid"] is True,
            "holster consumed input/space mismatch")
    require(h["neutralTrackedInput"] is neutral(inp), "holster neutral flag disagrees with consumed input")
    require(h["leftClaim"] is None, "left hand belongs to another interaction")
    right = h["rightClaim"]
    if right is not None:
        token = right["token"]
        require(integer(token["id"]) > 0 and token["owner"] == ev["owner"]
                and integer(token["hand"]) == 1 and integer(token["kind"]) == 1
                and token["item"] == h["physicalGun"] and integer(token["prerequisiteClaim"]) == 0
                and 0 < integer(right["inputSequence"]) <= seq
                and now < integer(right["deadlineNs"]) <= integer(ev["deadlineNs"]),
                "right hand lacks a current same-item GunHold claim")
    slot = h["selectedSlot"]
    require(isinstance(slot, dict) and integer(slot["slot"]) in ANCHORS
            and integer(slot["item"]["id"]) == integer(snapshot["owner"]["weapon"])
            and integer(slot["item"]["generation"]) > 0, "selected body assignment unavailable or foreign")
    if h["actualEmpty"] is True:
        require(integer(h["phase"]) == 3 and h["emptyHands"] is True and right is None
                and h["blockWeaponActions"] is True and h["allowAutomaticGunHold"] is False
                and h["freeRightCurrent"] is True, "incomplete genuine Empty proof")
        free = h["freeRight"]
        require(isinstance(free, dict) and same_evidence(free["inputEvidence"], ev), "free-right original input mismatch")
        holster_evidence(free["inputEvidence"], snapshot, now)
        vis, suppression = free["visibility"], free["suppression"]
        require(integer(h["request"]) > 0 and vis["nativeOwner"] == snapshot["owner"]
                and integer(vis["request"]) == integer(h["request"])
                and vis["hidden"] is True and integer(vis["verifiedCopyMask"]) == 3
                and integer(vis["drawSerial"]) > 0
                and integer(vis["physicalEquipGeneration"]) == integer(h["physicalGun"]["generation"])
                and 0 < integer(vis["inputSequence"]) <= seq
                and 0 < integer(vis["observedNs"]) <= now < integer(vis["deadlineNs"])
                and until <= integer(vis["deadlineNs"]), "free-right visibility receipt invalid or stale")
        require(suppression["owner"] == snapshot["owner"]
                and same_evidence(suppression["inputEvidence"], ev)
                and integer(suppression["request"]) == integer(h["request"])
                and integer(suppression["nativeTick"]) == integer(h["nativeTick"])
                and integer(suppression["cache"]) >= 0x10000
                and suppression["equipmentDispatched"] is False, "free-right suppression receipt mismatch")
        holster_evidence(suppression["inputEvidence"], snapshot, now)
        authorization = integer(free["authorizationDeadlineNs"])
        require(authorization == 0 or (now < authorization and until <= authorization),
                "original holster authorization expired or extended")
    return h


def source_deadline(snapshot):
    h = snapshot["bodyHolster"]
    values = [integer(snapshot["valid_until_ns"]),
              integer(snapshot["input_evidence"]["observedNs"]) + transport.SNAPSHOT_PERIOD,
              integer(h["validUntilNs"]), integer(h["inputEvidence"]["deadlineNs"])]
    if h["rightClaim"] is not None:
        values.append(integer(h["rightClaim"]["deadlineNs"]))
    return min(values)


def socket_pose(snapshot, slot):
    require(slot in ANCHORS, "unknown body weapon slot")
    # BodyAnchorHandPose: LH socket * upright reference, origin=current XR head.
    local = frame.identity()
    local[3][:3] = ANCHORS[slot][:]
    pose = frame.to_pose(frame.multiply(local, frame.supply_body_frame(snapshot["input"])))
    pose["orientation"] = snapshot["input"]["hands"][1]["grip"]["orientation"][:]
    frame.from_pose(pose)
    return pose


def source_identity(before, after):
    for key in ("owner", "equipment", "physical_equipment_generation", "rig"):
        require(before[key] == after[key], "source identity changed: " + key)
    for key in ("configurationPath", "configurationPathPointer", "inventory", "selectedSlot",
                "soleConfiguredArray", "matchedMeshPath", "meshes"):
        require(before["selected"][key] == after["selected"][key], "source selection changed: " + key)
    for key in ("physicalGun", "selectedSlot"):
        require(before["bodyHolster"][key] == after["bodyHolster"][key], "source body identity changed: " + key)


def selection_state(before, after, expected, draw_armed, after_press_ns):
    """Only the observed expected ordinary selection may change equipment lifetime."""
    for key in ACTOR:
        require(before["owner"][key] == after["owner"][key], "actor/space changed: " + key)
    require(before["selected"]["inventory"] == after["selected"]["inventory"], "native inventory changed")
    for key in REFERENCE:
        require(before["input"][key] == after["input"][key], "tracking/head reference changed: " + key)
    for key in ("grip", "aim", "held", "squeeze", "trigger", "stickX", "stickY"):
        require(before["input"]["hands"][0][key] == after["input"]["hands"][0][key], "left input changed")
    if (after["equipment"]["asset"] == before["equipment"]["asset"]
            and after["selected"]["configurationPath"] == before["selected"]["configurationPath"]):
        source_identity(before, after)
        return "source"
    require(draw_armed and after_press_ns > 0, "equipment changed outside acknowledged ordinary held draw")
    require(after["equipment"]["asset"] == expected["asset"]
            and after["selected"]["configurationPath"] == expected["configuration"]
            and integer(after["bodyHolster"]["selectedSlot"]["slot"]) == expected["slot"],
            "selected equipment is not the exact requested target/slot")
    require(integer(after["input_evidence"]["observedNs"]) >= after_press_ns
            and integer(after["bodyHolster"]["inputEvidence"]["observedNs"]) >= after_press_ns,
            "target observation predates ordinary draw press")
    require(integer(after["owner"]["weapon"]) != integer(before["owner"]["weapon"])
            and integer(after["owner"]["equipGeneration"]) > integer(before["owner"]["equipGeneration"])
            and integer(after["physical_equipment_generation"]) > integer(before["physical_equipment_generation"]),
            "target has no observed new equipment lifetime")
    return "target"


class OrdinaryHolsterDriver(transport.Driver):
    def __init__(self, args):
        self.baseline = self.target_baseline = None
        self.held_swap_ack = None
        self.expected = dict(asset=args.target_asset, configuration=args.target_config, slot=args.target_slot)
        self.phase, self.draw_armed, self.press_ns = "baseline", False, 0
        self.last_native = self.publication_source = None
        self.right_pose = None
        args.stage, args.profiles, args.capture, args.menu_batches, args.pickup_first = None, None, None, None, False
        super().__init__(args)
        require(self.link["mechanism_snapshots"]["requested"] is True
                and transport.read_bytes(self.folder / "mechanism-snapshot.enable", 2) == b"1\n",
                "snapshot opt-in was not established before native startup")
        self.artifacts.save("holster-invocation.json", dict(
            heldSwap=getattr(args, "held_swap", False),
            maximumObservationHoldNs=HOLD_NS, maximumReplacementRequests=MAX_REPLACEMENTS,
            maximumStowCohortAdvance=1, maximumStowCohortTransitions=1,stowAsset=args.stow_asset, stowConfiguration=args.stow_config,
            stowSlot=args.stow_slot, target=self.expected, driverSha256=transport.digest(Path(__file__).read_bytes()),
            transportPath=str(TRANSPORT_PATH), transportSha256=transport.digest(TRANSPORT_PATH.read_bytes()),
            anchorsSourceSha256=transport.digest((CHECKOUT / "include/fvr/interaction/BodyAnchors.h").read_bytes()),
            nativeAcceptance=False, headsetAcceptance=False))

    def snapshot_source_deadline(self, snapshot):
        h = snapshot.get("bodyHolster", {})
        return source_deadline(snapshot) if h.get("available") is True else super().snapshot_source_deadline(snapshot)

    def admit_snapshot(self, snapshot, now, nonce):
        h = holster_proof(snapshot, now)
        # A blocked transition may only be observed while holding the ordinary
        # stow/draw input; it never grants a new pose or a claim of Empty.
        allowed = h["actualEmpty"] is True or (
            self.phase in ("stow_wait", "draw_wait") and integer(h["phase"]) in (2, 4, 5)
            and h["blockWeaponActions"] is True)
        frame.admit_frame(snapshot, now, nonce, mechanism_admission=lambda s, n: allowed)
        require(h["blockWeaponActions"] == snapshot["weapon_actions_blocked"],
                "independent publications disagree on action block; no command authority")
        for inp in (snapshot["input"], h["input"]):
            require(all(hand["gripTracked"] is True and hand["aimTracked"] is True
                        and frame.finite(hand["trigger"]) == 0
                        and frame.finite(hand["stickX"]) == frame.finite(hand["stickY"]) == 0
                        for hand in inp["hands"]), "non-neutral trigger/stick or missing hand tracking")
            require(frame.finite(inp["hands"][0]["squeeze"]) == 0 and integer(inp["hands"][0]["held"]) == 0,
                    "left controller is not neutral")
        if self.baseline is not None:
            state = selection_state(self.baseline, snapshot, self.expected, self.draw_armed, self.press_ns)
            require(self.target_baseline is None or state == "target", "selected target reverted")
            if self.target_baseline is not None:
                source_identity(self.target_baseline, snapshot)
        if self.last_native is not None:
            for key in ("sequence", "observedNs"):
                require(integer(h["inputEvidence"][key]) >= integer(self.last_native["bodyHolster"]["inputEvidence"][key]),
                        "holster original input regressed")

    def accept_snapshot(self, snapshot, raw, now):
        super().accept_snapshot(snapshot, raw, now)
        self.last_native = snapshot

    def current(self):
        # Both independent original inputs must postdate our last publication.
        # Waiting produces ordinary XR packets, never a rewritten source lease.
        for _ in range(3):
            snapshot, _, now = self.fresh()
            h = snapshot["bodyHolster"]
            if min(integer(snapshot["input_evidence"]["observedNs"]), integer(h["inputEvidence"]["observedNs"])) < self.last_write:
                continue
            if self.right_pose is not None:
                for key in ("grip", "aim"):
                    p, a = frame.pose_error(frame.from_pose(snapshot["input"]["hands"][1][key]), frame.from_pose(self.right_pose))
                    require(p < .002 and a < .005, "consumed right pose differs from acknowledged command")
                squeeze = 1 if self.held_right else 0
                require(all(abs(float(inp["hands"][1]["squeeze"]) - squeeze) <= .01
                            for inp in (snapshot["input"], h["input"])), "consumed right grip differs from acknowledged command")
            return snapshot, now
        raise REFUSAL("no fresh independent original inputs after last command")

    def validate_publication(self, lines, expected, cleanup):
        require(all(line in ("grip r 0", "grip r 1", "hand r follow off")
                    or line.startswith(("hand r grip pose ", "hand r aim pose ")) for line in lines),
                "holster driver permits only ordinary right pose/grip")
        if cleanup:
            require(lines == ["grip r 0"], "failure cleanup permits owned right release only")
            return
        state = self.read_state()
        self.ownership(state)
        for key in ("head", "handL", "handR", "controls"):
            require(state[key] == self.state[key], "simulator input changed since owned acknowledgement: " + key)
        source = self.publication_source
        require(source is not None, "command lacks original source")
        now = time.perf_counter_ns()
        require(now < source_deadline(source), "original holster source expired before publication")
        self.admit_snapshot(source, now, integer(source["nonce"]))

    @staticmethod
    def check_target(state, target):
        require(abs(float(state["controls"]["gripR"]) - target["grip"]) < .001, "right grip did not commit")
        require(state["handR"]["followsHead"] is False
                and math.dist(state["handR"]["pos"], target["pose"]["position"]) <= .0002, "right pose did not commit")
        actual = frame.from_pose(dict(position=[0, 0, 0], orientation=frame.ypr_quaternion(state["handR"]["ypr"])))
        intended = frame.from_pose(dict(position=[0, 0, 0], orientation=target["pose"]["orientation"]))
        require(frame.pose_error(actual, intended)[1] <= .001, "right orientation did not commit")

    def command(self, snapshot, pose, grip):
        require(grip in (0, 1), "invalid ordinary squeeze")
        numbers = [*pose["position"], *frame.quaternion_ypr(pose["orientation"])]
        require(all(math.isfinite(v) for v in numbers) and all(abs(v) <= 10 for v in numbers[:3]), "pose outside normal bounds")
        text = " ".join(format(v, ".9g") for v in numbers)
        lines = ["hand r follow off", "hand r grip pose " + text, "hand r aim pose " + text, "grip r " + str(grip)]
        self.publication_source = snapshot
        if self.motion_started is None:
            self.motion_started = time.perf_counter_ns()
        self.send(lines, expected=dict(pose=pose, grip=grip), source_deadline=source_deadline(snapshot))
        self.right_pose = pose

    def save_observation(self, name, snapshot):
        # The complete immutable snapshot is already retained by transport.
        # Avoid duplicating up to six 32 KiB payloads inside its 1 MiB budget.
        self.artifacts.save(name, dict(nonce=snapshot["nonce"], observedNs=snapshot["observed_ns"],
            inputSequence=snapshot["input_evidence"]["sequence"], bodyHolster=snapshot["bodyHolster"],
            owner=snapshot["owner"], equipment=snapshot["equipment"], selected=snapshot["selected"]))

    def require_empty(self, snapshot, neutral_required=True):
        h = snapshot["bodyHolster"]
        require(h["actualEmpty"] is True, "genuine Empty was not observed")
        if neutral_required:
            require(h["neutralTrackedInput"] is True and neutral(snapshot["input"]), "genuine Empty lacks fresh neutral rearm")

    def require_held(self, snapshot):
        h = snapshot["bodyHolster"]
        require(integer(h["phase"]) == 1 and h["rightClaim"] is not None
                and h["actualEmpty"] is False and h["blockWeaponActions"] is False
                and snapshot["weapon_actions_blocked"] is False and h["allowAutomaticGunHold"] is True,
                "ordinary Held/GunHold/unblocked state not observed")

    def require_held_swap_source(self, snapshot):
        self.require_held(snapshot)
        h = snapshot["bodyHolster"]
        require(integer(h["inventoryEvaluation"]) in (0, 1)
                and h["emptyHands"] is False and h["leftClaim"] is None
                and h["freeRightCurrent"] is False and h["freeRight"] is None,
                "held swap requires current visible Held/GunHold source")
        for name in ("pendingOperation", "pendingRequest", "visibility", "suppression", "ordinaryDrawCommand",
                     "cancel", "reloadBusy", "interactionSuspended"):
            require(not h.get(name), "explicit held swap blocker: " + name)

    def reach(self, slot=None, withdraw=None, grip=0):
        for _ in range(16):
            snapshot, now = self.current()
            if self.phase in ("empty", "draw_rearm"):
                self.require_empty(snapshot)
            else:
                self.require_held(snapshot)
            target = socket_pose(snapshot, slot) if slot is not None else withdraw
            current = snapshot["input"]["hands"][1]["grip"]
            distance = math.dist(current["position"], target["position"])
            if distance <= .002 and self.right_pose is not None:
                return snapshot
            step = min(1., .10 / max(distance, .000001))
            pose = dict(position=[a + (b-a)*step for a, b in zip(current["position"], target["position"])],
                        orientation=target["orientation"][:])
            self.command(snapshot, pose, grip)
        raise REFUSAL("bounded body reach did not finish")

    def wait_state(self, name):
        until = min(self.deadline, time.perf_counter_ns() + 1_500_000_000)
        for _ in range(15):
            require(time.perf_counter_ns() < until, "bounded " + name + " observation timeout")
            snapshot, now = self.current()
            h = snapshot["bodyHolster"]
            if name == "empty" and h["actualEmpty"] is True:
                self.require_empty(snapshot, False)
                return snapshot
            if name == "target" and selection_state(self.baseline, snapshot, self.expected, self.draw_armed, self.press_ns) == "target":
                # Freeze the first observed expected lifetime, including blocked
                # ordinary recovery. No further lifetime change is authorized.
                if self.target_baseline is None:
                    self.target_baseline = snapshot
                if h["blockWeaponActions"] is False:
                    self.require_held(snapshot)
                    return snapshot
            require(integer(h["phase"]) in ((1, 2, 3) if name == "empty" else (3, 4, 5, 1)),
                    "unexpected body phase during held observation")
        raise REFUSAL("bounded " + name + " observation count exhausted")

    def run_holster(self):
        initial, now = self.current()
        self.require_held(initial)
        require(neutral(initial["input"]) and initial["bodyHolster"]["neutralTrackedInput"] is True,
                "initial source requires neutral tracked inputs")
        require(initial["equipment"]["asset"] == self.args.stow_asset
                and initial["selected"]["configurationPath"] == self.args.stow_config
                and integer(initial["bodyHolster"]["selectedSlot"]["slot"]) == self.args.stow_slot
                and integer(initial["bodyHolster"]["inventoryEvaluation"]) in (
                    (0, 1) if getattr(self.args, "held_swap", False) else (1,)),
                "selected source does not match exact intended stow asset/configuration/slot")
        require(self.args.stow_slot != self.args.target_slot
                and (self.args.stow_asset, self.args.stow_config) != (self.args.target_asset, self.args.target_config),
                "alternate draw requires distinct source and target")
        self.baseline = initial
        if getattr(self.args, "held_swap", False):
            self.run_held_swap(initial)
            return
        self.phase = "stow_reach"
        at_stow = self.reach(slot=self.args.stow_slot)
        require(neutral(at_stow["input"]) and at_stow["bodyHolster"]["neutralTrackedInput"] is True,
                "stow press lacks fresh neutral rearm")
        self.phase = "stow_wait"
        self.command(at_stow, self.right_pose, 1)
        empty = self.wait_state("empty")
        self.save_observation("observed-empty-held.json", empty)
        self.command(empty, self.right_pose, 0)
        self.phase = "empty"
        neutral_empty, now = self.current()
        self.require_empty(neutral_empty)
        self.save_observation("observed-empty-neutral.json", neutral_empty)
        self.phase = "draw_rearm"
        at_draw = self.reach(slot=self.args.target_slot)
        self.require_empty(at_draw)
        self.save_observation("observed-draw-rearm.json", at_draw)
        self.phase = "draw_wait"
        self.command(at_draw, self.right_pose, 1)
        self.press_ns, self.draw_armed = self.last_write, True
        selected = self.wait_state("target")
        self.save_observation("observed-target-held.json", selected)
        self.withdraw_target(selected)

    def run_held_swap(self, initial):
        """Ordinary neutral rearm at the OTHER assigned shoulder, without Empty."""
        self.require_held_swap_source(initial)
        require(self.args.stow_slot in (1, 2) and self.args.target_slot in (1, 2),
                "held swap requires two distinct assigned shoulder slots")
        self.phase = "held_reach"
        at_draw = self.reach(slot=self.args.target_slot)
        self.require_held_swap_source(at_draw)
        require(neutral(at_draw["input"]) and at_draw["bodyHolster"]["neutralTrackedInput"] is True,
                "held swap press lacks fresh neutral rearm")
        self.save_observation("observed-held-swap-rearm.json", at_draw)
        self.phase = "draw_wait"
        self.command(at_draw, self.right_pose, 1)
        require(not self.pending_command and self.held_right
                and 0 < self.last_write <= self.last_commit,
                "held swap press was not acknowledged")
        self.held_swap_ack = dict(snapshot=copy.deepcopy(at_draw), pose=copy.deepcopy(self.right_pose),
            identity=self.command_identity(), writtenNs=self.last_write, committedNs=self.last_commit)
        self.press_ns, self.draw_armed = self.last_write, True
        selected = self.wait_state("target")
        self.save_observation("observed-target-held.json", selected)
        self.withdraw_target(selected)

    def withdraw_target(self, selected):
        self.phase = "withdraw"
        # Return to the initial ordinary right pose. Refuse if it lies inside
        # any configured acquisition volume; releasing there could stow again.
        rest = self.baseline["input"]["hands"][1]["grip"]
        relative = frame.multiply(frame.from_pose(rest), frame.inverse(frame.supply_body_frame(selected["input"])))
        for slot, center in ANCHORS.items():
            nearest = center[:]
            if slot in (1, 2):
                nearest[1] = min(center[1]+.04, max(center[1], relative[3][1]))
                nearest[2] = min(center[2]+.08, max(center[2], relative[3][2]))
            require(math.dist(relative[3][:3], nearest) > (.17 if slot in (1, 2) else .16) + .03,
                    "initial rest pose is too close to a weapon contact for safe release")
        withdrawn = self.reach(withdraw=rest, grip=1)
        self.command(withdrawn, self.right_pose, 0)
        final, now = self.current()
        self.require_held(final)
        require(neutral(final["input"]) and final["bodyHolster"]["neutralTrackedInput"] is True,
                "final ordinary release not consumed as neutral input")
        self.save_observation("observed-target-released.json", final)


def pending_body(snapshot):
    body = snapshot.get("bodyHolster", {})
    return body.get("available") is False and body.get("reason") in OBSERVATION_REASONS


def same_outer(before, after):
    """No missing-body observation can authorize an equipment lifetime change."""
    for key in ("owner", "equipment", "physical_equipment_generation", "rig"):
        require(before[key] == after[key], "holster observation hold identity changed: " + key)
    for key in ("configurationPath", "configurationPathPointer", "inventory", "selectedSlot",
                "soleConfiguredArray", "matchedMeshPath", "meshes"):
        require(before["selected"][key] == after["selected"][key], "holster observation hold selection changed: " + key)
    for key in REFERENCE:
        require(before["input"][key] == after["input"][key], "holster observation hold reference changed: " + key)
    for key in ("grip", "aim", "held", "squeeze", "trigger", "stickX", "stickY"):
        require(before["input"]["hands"][0][key] == after["input"]["hands"][0][key],
                "holster observation hold left input changed: " + key)


# The existing readiness adapter accepts this narrow ordinary-policy interface.
# It points at the tracked implementation, with no historical module imports.
original = SimpleNamespace(HolsterDriver=OrdinaryHolsterDriver, frame=frame)


class ObservationHolsterDriver(OrdinaryHolsterDriver):
    def __init__(self, args):
        self.holster_hold_started = None
        self.holster_replacements = 0
        self.holster_hold_anchor = self.holster_hold_state = self.holster_hold_command = None
        super().__init__(args)

    def command_identity(self):
        return (self.sequence, self.sentinel, self.command_hash, self.last_write,
                self.last_commit, self.commands)

    def check_hold_owner(self):
        require(not self.pending_command, "holster observation hold needs an already acknowledged command")
        state = self.read_state()
        self.ownership(state)
        expected = self.holster_hold_state if self.holster_hold_state is not None else self.state
        for key in ("head", "handL", "handR", "controls", "captureSeq"):
            require(state[key] == expected[key], "acknowledged input changed during holster observation hold: " + key)
        if self.holster_hold_command is not None:
            require(self.command_identity() == self.holster_hold_command,
                    "command changed during holster observation hold")

    def bounds(self, now):
        super().bounds(now)
        if self.holster_hold_started is not None:
            require(now - self.holster_hold_started < HOLD_NS, "holster observation hold expired")
            self.check_hold_owner()

    def request_snapshot(self):
        if self.holster_hold_started is not None:
            self.bounds(time.perf_counter_ns())
            require(self.holster_replacements < MAX_REPLACEMENTS,
                    "holster observation replacement request budget exhausted")
            self.holster_replacements += 1
        return super().request_snapshot()

    def hold_observation(self, snapshot, now):
        self.bounds(now)
        self.check_hold_owner()
        # Validate the original outer frame solely for an observation-only wait.
        # This does not fabricate body evidence or authorize any command/Empty.
        frame.admit_frame(snapshot, now, integer(snapshot["nonce"]),
                          mechanism_admission=lambda s, n: s is snapshot and n == now)
        anchor = self.holster_hold_anchor or self.last_native
        if anchor is not None:
            same_outer(anchor, snapshot)
        if self.holster_hold_started is None:
            self.holster_hold_started = now
            self.holster_replacements = 0
            self.holster_hold_anchor = snapshot
            self.holster_hold_state = copy.deepcopy(self.state)
            self.holster_hold_command = self.command_identity()
        # A previous accepted snapshot remains identity history only; it cannot
        # serve as current command authority while the replacement is pending.
        self.latest_snapshot = self.publication_source = None
        self.artifacts.event({"type": "holster_observation_hold", "nonce": snapshot["nonce"],
                              "evaluatedNs": now, "startedNs": self.holster_hold_started,
                              "replacementsIssued": self.holster_replacements,
                              "lastCommandWrittenNs": self.last_write,
                              "reason": snapshot["bodyHolster"]["reason"]})
        self.request_snapshot()

    def hold_draw_publication_gap(self, snapshot, raw, now):
        """An unavailable publication proves nothing; retain only command ownership."""
        self.bounds(now)
        self.check_hold_owner()
        if now - self.last_request > transport.SNAPSHOT_TIMEOUT:
            self.snapshot_timeout(now, (snapshot, raw))
        require(snapshot.get("schema") == "fvr.bc2.live-mechanism-snapshot.v1"
                and snapshot.get("valid") is False
                and snapshot.get("invalid_reason") == "no_shot_publication"
                and snapshot.get("bodyHolster") == {"available": False, "reason": "snapshot_unavailable"},
                "draw publication gap is not the exact unavailable snapshot")
        require(self.phase == "draw_wait" and self.draw_armed and self.held_right
                and self.target_baseline is None and self.baseline is not None
                and self.press_ns == self.last_write and 0 < self.last_write <= self.last_commit <= now,
                "draw publication gap requires an acknowledged ordinary draw")
        anchor = self.holster_hold_anchor or self.last_native
        require(anchor is not None, "draw publication gap lacks admitted rearm history")
        if getattr(self.args, "held_swap", False):
            ack = self.held_swap_ack
            require(ack is not None and self.command_identity() == ack["identity"]
                    and self.right_pose == ack["pose"] and anchor == ack["snapshot"]
                    and self.press_ns == ack["writtenNs"] and self.last_commit == ack["committedNs"],
                    "draw publication gap lacks acknowledged held swap rearm")
            self.require_held_swap_source(anchor)
            require(neutral(anchor["input"]) and anchor["bodyHolster"]["neutralTrackedInput"] is True,
                    "held swap rearm history is not neutral")
            require(resolve(anchor, ack["writtenNs"], self.slot_binding) == self.slot_binding,
                    "held swap rearm history differs from complete binding")
        else:
            self.require_empty(anchor)
        require(selection_state(self.baseline, anchor, self.expected, False, 0) == "source"
                and integer(anchor["observed_ns"]) <= self.press_ns,
                "draw publication gap lacks the admitted source rearm")
        require(self.last_request <= integer(snapshot["observed_ns"]) <= now,
                "draw publication gap timestamp outside its request")
        if self.holster_hold_started is None:
            self.holster_hold_started = now
            self.holster_replacements = 0
            self.holster_hold_anchor = anchor
            self.holster_hold_state = copy.deepcopy(self.state)
            self.holster_hold_command = self.command_identity()
        self.latest_snapshot = self.publication_source = None
        self.artifacts.event({"type": "holster_draw_publication_gap", "nonce": snapshot["nonce"],
                              "evaluatedNs": now, "startedNs": self.holster_hold_started,
                              "replacementsIssued": self.holster_replacements,
                              "lastCommandWrittenNs": self.last_write,
                              "nativeProof": False, "reason": snapshot["invalid_reason"]})
        self.pending_nonce = None
        self.request_snapshot()

    def receive_native_snapshot(self):
        # Canonical receive copied unchanged except the exact invalid branch.
        # Preserve its original request ownership, nonce, timeout and artifact
        # checks before deciding whether the diagnostic may wait without input.
        if self.pending_nonce is None:
            return None
        require(transport.read_bytes(self.request_path, 22) == self.request_bytes, "snapshot request changed by another writer")
        failure = self.snapshot_failure()
        now = time.perf_counter_ns()
        if now - self.last_request > transport.SNAPSHOT_TIMEOUT:
            self.snapshot_timeout(now)
        if failure is not None:
            self.artifacts.event({"type": "snapshot_publication_failure", "nonce": self.pending_nonce,
                                  "receivedNs": now, "requestStartedNs": self.last_request,
                                  "requestPublishedNs": getattr(self, "last_request_published", None),
                                  "publication": failure})
            raise REFUSAL("native snapshot publication failed: "
                          f"nonce={self.pending_nonce} stage={failure['stage']} "
                          f"win32_error={failure['win32_error']} attempts={failure['attempts']}")
        if not self.snapshot_path.exists():
            return None
        snapshot, raw = transport.read_json(self.snapshot_path, frame.SNAPSHOT_LIMIT)
        nonce = integer(snapshot["nonce"])
        self.last_observed_response_nonce = nonce
        now = time.perf_counter_ns()
        if now - self.last_request > transport.SNAPSHOT_TIMEOUT:
            self.snapshot_timeout(now, (snapshot, raw))
        require(nonce <= self.pending_nonce, "snapshot response belongs to a later competing request")
        if nonce != self.pending_nonce:
            return None
        self.artifacts.save(f"snapshot-{self.snapshot_index:03}.json", raw)
        if snapshot.get("valid") is not True:
            if snapshot.get("valid") is False and snapshot.get("invalid_reason") == "no_shot_publication":
                self.hold_draw_publication_gap(snapshot, raw, time.perf_counter_ns())
                return None
            raise REFUSAL("native snapshot refused: " + str(snapshot.get("invalid_reason")))
        if self.reference is not None:
            self.stable_snapshot(self.reference, snapshot)
        evaluated = time.perf_counter_ns()
        if evaluated - self.last_request > transport.SNAPSHOT_TIMEOUT:
            self.snapshot_timeout(evaluated, (snapshot, raw))
        source_deadline = self.snapshot_source_deadline(snapshot)
        if evaluated >= source_deadline:
            self.admit_snapshot(snapshot, integer(snapshot["observed_ns"]), nonce)
            if self.snapshot_refresh_started is None:
                self.snapshot_refresh_started = evaluated
                self.snapshot_refresh_requests = 0
            self.bounds(time.perf_counter_ns())
            require(self.snapshot_refresh_requests < transport.MAX_SNAPSHOT_REFRESHES,
                    "expired snapshot refresh request budget exhausted")
            self.snapshot_refresh_requests += 1
            self.artifacts.event({"type": "snapshot_refresh_hold", "nonce": snapshot["nonce"],
                                  "evaluatedNs": evaluated, "sourceDeadlineNs": source_deadline,
                                  "refreshRequest": self.snapshot_refresh_requests,
                                  "lastCommandWrittenNs": self.last_write})
            self.pending_nonce = None
            self.request_snapshot()
            return None
        self.pending_nonce = None
        return snapshot, raw, evaluated

    def receive_snapshot(self):
        # Canonical receive preserves each matching response and applies nonce,
        # publication, request timeout and original outer deadline checks first.
        received = self.receive_native_snapshot()
        if received is not None and pending_body(received[0]):
            self.hold_observation(received[0], received[2])
            return None
        return received

    def accept_snapshot(self, snapshot, raw, now):
        self.bounds(time.perf_counter_ns())
        held = self.holster_hold_started
        if held is not None and self.baseline is None:
            same_outer(self.holster_hold_anchor, snapshot)
        # Original admission proves either the unchanged source or the exact
        # expected target after the acknowledged draw. Fully joined evidence
        # may show that target completing during this observation-only wait.
        # Original admission checks every independent body lease, claim,
        # visibility/suppression join and source/target identity at current time.
        super().accept_snapshot(snapshot, raw, now)
        if held is not None:
            self.bounds(time.perf_counter_ns())
            require(min(integer(snapshot["input_evidence"]["observedNs"]),
                        integer(snapshot["bodyHolster"]["inputEvidence"]["observedNs"])) >= self.last_write,
                    "replacement original inputs predate acknowledged command")
            self.artifacts.event({"type": "holster_observation_resumed", "nonce": snapshot["nonce"],
                                  "evaluatedNs": now, "heldNs": now - held,
                                  "replacementRequests": self.holster_replacements})
            self.holster_hold_started = None
            self.holster_hold_anchor = self.holster_hold_state = self.holster_hold_command = None
            self.holster_replacements = 0

    def pause_until(self, deadline):
        for _ in range(6000):
            self.pump()
            # fresh() deliberately disables monitor. A pending observation hold
            # still needs receiving here; it must not silently return old data.
            if self.holster_hold_started is not None and not self.monitor:
                received = self.receive_snapshot()
                if received is not None:
                    self.accept_snapshot(*received)
            if (time.perf_counter_ns() >= deadline and self.snapshot_refresh_started is None
                    and self.holster_hold_started is None):
                return
            time.sleep(.001)
        raise REFUSAL("bounded polling iteration limit")

    def validate_publication(self, lines, expected, cleanup):
        if not cleanup:
            require(self.holster_hold_started is None, "command refused while holster observation is pending")
            require(self.publication_source is self.latest_snapshot,
                    "command requires the latest fully admitted holster snapshot")
        return super().validate_publication(lines, expected, cleanup)


def lease(value, now, maximum):
    observed, deadline = integer(value["observedNs"]), integer(value["deadlineNs"])
    require(0 < observed <= now < deadline and deadline - observed <= maximum,
            "body mapping evidence expired or invalid")
    require(integer(value["sequence"]) > 0, "body mapping sequence missing")


def physical_identity(snapshot):
    """Use the reported verified family, otherwise the exact native weapon."""
    native = snapshot["equipment"]
    family = snapshot.get("sight", {}).get("family")
    physical = integer(native["weapon"])
    if family is not None:
        side = "secondary" if family["selectedSecondary"] is True else "primary"
        require(type(family["selectedSecondary"]) is bool
                and family[side + "Asset"] == native["asset"]
                and family[side + "Configuration"] == snapshot["selected"]["configurationPath"]
                and integer(family["inventory"]) == integer(snapshot["selected"]["inventory"])
                and integer(family["physicalItem"]) == integer(native["persistence"]),
                "physical family differs from exact selected configuration")
        physical = integer(family["physicalItem"])
    require(physical > 0 and integer(snapshot["bodyHolster"]["physicalGun"]["id"]) == physical,
            "physical gun differs from exact native item/family")


def mapping_rows(snapshot, now):
    """Pure reader; caller must also admit the complete original frame/holster."""
    d = snapshot.get("bodyInventory", {})
    require(d.get("available") is True and d.get("observationOnly") is True,
            "fresh body inventory mapping unavailable: " + str(d.get("reason", "missing")))
    h, selected = snapshot["bodyHolster"], snapshot["selected"]
    require(d["selectedOwner"] == snapshot["owner"] == h["nativeOwner"]
            and d["physicalOwner"] == h["inputEvidence"]["owner"]
            and d["physicalSelected"] == h["selectedSlot"]["item"], "body mapping owner/item mismatch")
    lease(d, now, 100_000_000)
    require(integer(d["observedNs"]) <= integer(d["sampledNs"]) <= now
            and integer(d["inventory"]) == integer(selected["inventory"])
            and integer(d["inventory"]) >= 0x10000 and integer(d["switching"]) >= 0x10000
            and integer(d["revision"]) > 0 and integer(d["cohort"]) > 0,
            "body mapping container/revision/clock invalid")
    ev = d["inputEvidence"]
    holster_evidence(ev, snapshot, now)
    require(ev["owner"] == d["physicalOwner"]
            and integer(ev["sequence"]) == integer(d["sequence"])
            and integer(ev["observedNs"]) == integer(d["observedNs"])
            and integer(d["deadlineNs"]) <= integer(ev["deadlineNs"]),
            "body mapping original input differs")
    for other in (h["inputEvidence"], snapshot["input_evidence"]):
        if integer(d["sequence"]) == integer(other["sequence"]):
            require(same_evidence(ev, other), "same-sequence body mapping input differs")
    rows = d["slots"]
    require(isinstance(rows, list) and 1 <= len(rows) <= 8 and integer(d["count"]) == len(rows)
            and len(rows) <= integer(d["nativeCount"]) <= 32, "body mapping slot count invalid")
    seen = [set(), set(), set()]
    for row in rows:
        a, n, c = row["assignment"], row["native"], row["configuration"]
        identities = (integer(a["slot"]), integer(a["item"]["id"]), integer(n["slot"]))
        require(identities[0] in ANCHORS and identities[1] == integer(n["weapon"])
                and integer(a["item"]["generation"]) > 0 and 0 <= identities[2] < integer(d["nativeCount"])
                and integer(n["weapon"]) >= 0x10000 and integer(n["data"]) >= 0x10000
                and 0 <= integer(n["persistence"]) <= 0xffffffff and 0 <= integer(n["category"]) <= 4,
                "invalid body/native item identity")
        for used, value in zip(seen, identities):
            require(value not in used, "ambiguous duplicate body/native slot or item")
            used.add(value)
        require(c.get("available") is True, "full assigned configuration mapping unavailable: " + str(c.get("reason")))
        lease(c, now, 200_000_000)
        require(c["owner"] == d["selectedOwner"] and integer(c["inventory"]) == integer(d["inventory"])
                and integer(c["weaponData"]) == integer(n["data"])
                and integer(c["selectedSlot"]) == integer(selected["selectedSlot"])
                and c["configurationPathVerified"] is True and integer(c["configurationPathPointer"]) >= 0x10000
                and integer(c["soleConfiguredArray"]) >= 0x10000
                and isinstance(c["asset"], str) and 0 < len(c["asset"]) < 128
                and isinstance(c["configurationPath"], str) and 0 < len(c["configurationPath"]) < 512,
                "body mapping exact configuration not joined")
        if integer(n["weapon"]) == integer(snapshot["owner"]["weapon"]):
            require(c["kind"] == "selected" and integer(n["slot"]) == integer(selected["selectedSlot"])
                    and c["asset"] == snapshot["equipment"]["asset"]
                    and all(c[k] == selected[k] for k in (
                        "sequence", "observedNs", "deadlineNs", "configurationPath", "configurationPathPointer",
                        "configurationPathVerified", "soleConfiguredArray")),
                    "selected configuration differs from original snapshot")
        else:
            require(c["kind"] == "carried" and integer(c["sequence"]) <= integer(d["sequence"])
                    and integer(c["observedNs"]) <= integer(d["observedNs"])
                    and (integer(c["sequence"]) != integer(d["sequence"])
                         or integer(c["observedNs"]) == integer(d["observedNs"])),
                    "carried configuration does not precede its body mapping")
    return d, rows


def pinned_row(row):
    c = row["configuration"]
    return dict(assignment=copy.deepcopy(row["assignment"]), native=copy.deepcopy(row["native"]),
                asset=c["asset"], configuration=c["configurationPath"],
                configurationPathPointer=c["configurationPathPointer"], soleConfiguredArray=c["soleConfiguredArray"])


def selected_matches(snapshot, row):
    n, selected = row["native"], snapshot["selected"]
    require(snapshot["equipment"] == dict(asset=row["asset"], weapon=n["weapon"], data=n["data"], persistence=n["persistence"])
            and snapshot["owner"]["weapon"] == n["weapon"]
            and snapshot["bodyHolster"]["selectedSlot"] == row["assignment"]
            and integer(selected["selectedSlot"]) == integer(n["slot"])
            and selected["configurationPath"] == row["configuration"]
            and selected["configurationPathVerified"] is True
            and selected["configurationPathPointer"] == row["configurationPathPointer"]
            and selected["soleConfiguredArray"] == row["soleConfiguredArray"],
            "selected native item/configuration/body assignment differs from observed binding")
    physical_identity(snapshot)


def resolve_slots(snapshot, now, source_asset, source_config, target_asset, target_config):
    # Execute every original outer/holster admission check with no constructor,
    # transport, history mutation or input publication. Empty remains admissible
    # solely for revalidating a pair after the driver's original Empty proof.
    admission = OrdinaryHolsterDriver.__new__(OrdinaryHolsterDriver)
    admission.baseline = admission.last_native = None
    admission.phase = "baseline"
    OrdinaryHolsterDriver.admit_snapshot(admission, snapshot, now, integer(snapshot["nonce"]))
    d, rows = mapping_rows(snapshot, now)
    require((source_asset, source_config) != (target_asset, target_config), "source and target must differ")
    matches = []
    for asset, configuration in ((source_asset, source_config), (target_asset, target_config)):
        found = [r for r in rows if (r["configuration"]["asset"], r["configuration"]["configurationPath"]) == (asset, configuration)]
        require(len(found) == 1, "exact requested asset/configuration absent or ambiguous in current body mapping")
        matches.append(pinned_row(found[0]))
    source, target = matches
    require(source["assignment"]["slot"] != target["assignment"]["slot"]
            and source["native"]["weapon"] != target["native"]["weapon"], "alternate body target is not distinct")
    selected_matches(snapshot, source)
    return dict(owner=copy.deepcopy(d["selectedOwner"]), physicalOwner=copy.deepcopy(d["physicalOwner"]),
                inventory=d["inventory"], switching=d["switching"], revision=d["revision"], cohort=d["cohort"],
                source=source, target=target)


def resolve_observer_artifact(result, run, now, source_asset, source_config, target_asset, target_config):
    """Read-only root prime adapter; Observe stdout deliberately omits body data.

    Import this module before starting the prime clock. Call immediately after
    Observe returns, using its artifactDirectory and the live perf_counter_ns.
    The returned binding is planning evidence only; main obtains a fresh binding
    and refuses any difference before its first ordinary command.
    """
    require(result.get("status") == "observed" and result.get("valid") is True
            and result.get("observationOnly") is True and result.get("nativeAcceptance") is False
            and integer(result["commands"]) == 0, "observer did not finish a read-only valid snapshot")
    run = transport.local_path(run)
    directory = transport.child(run, result["artifactDirectory"])
    require(directory.parent == run and re.fullmatch(r"live-driver-[0-9a-f]{32}", directory.name),
            "observer artifact directory is not a direct run child")
    saved, _ = transport.read_json(directory / "result.json")
    require(saved == result, "observer result differs from saved receipt")
    snapshot, _ = transport.read_json(directory / "snapshot-observed.json", transport.planner.SNAPSHOT_LIMIT)
    request = transport.read_bytes(directory / "request-001.txt", 22)
    timing, _ = transport.read_json(directory / "request-001-timing.json")
    evaluated = time.perf_counter_ns()  # Observer clock after reads; no source lease is restamped.
    require(re.fullmatch(rb"[1-9][0-9]{0,19}\r?\n?", request) is not None
            and int(request) == integer(snapshot["nonce"]), "observer raw snapshot/request nonce mismatch")
    require(timing["clock"] == "perf_counter_ns" and integer(timing["nonce"]) == integer(snapshot["nonce"])
            and integer(timing["budgetNs"]) == transport.SNAPSHOT_TIMEOUT
            and 0 < integer(timing["requestStartedNs"]) <= integer(timing["requestPublishedNs"])
            and integer(timing["requestPublishedNs"]) <= integer(snapshot["observed_ns"]) <= now <= evaluated
            and integer(timing["responseDeadlineNs"]) == integer(timing["requestStartedNs"]) + transport.SNAPSHOT_TIMEOUT
            and evaluated < integer(timing["responseDeadlineNs"]),
            "observer request clock or original response budget invalid")
    require(all(snapshot[k] == result[k] for k in ("equipment", "selected", "observed_ns", "valid_until_ns")),
            "observer summary differs from exact raw snapshot")
    OrdinaryHolsterDriver.require_held(None, snapshot)
    require(neutral(snapshot["input"]) and snapshot["bodyHolster"]["neutralTrackedInput"] is True,
            "observer binding requires neutral tracked source")
    return resolve_slots(snapshot, evaluated, source_asset, source_config, target_asset, target_config)


def revalidate_pair(snapshot, now, binding):
    source, target = binding["source"], binding["target"]
    current = resolve_slots(snapshot, now, source["asset"], source["configuration"], target["asset"], target["configuration"])
    require(current == binding, "body/native assignment changed after initial binding")


class BindingHolsterDriver(ObservationHolsterDriver):
    def __init__(self, args):
        self.slot_binding = None
        args.stow_slot = args.target_slot = None
        super().__init__(args)

    def current(self):
        snapshot, now = super().current()
        if self.slot_binding is None:
            self.require_held(snapshot)
            require(neutral(snapshot["input"]) and snapshot["bodyHolster"]["neutralTrackedInput"] is True,
                    "initial slot binding requires neutral tracked source")
            binding = resolve_slots(snapshot, now, self.args.stow_asset, self.args.stow_config,
                                    self.args.target_asset, self.args.target_config)
            primed = getattr(self.args, "observed_binding", None)
            require(primed is None or binding == primed, "body/native binding changed after observer prime")
            self.slot_binding = binding
            self.args.stow_slot = integer(self.slot_binding["source"]["assignment"]["slot"])
            self.args.target_slot = integer(self.slot_binding["target"]["assignment"]["slot"])
            self.expected["slot"] = self.args.target_slot
            self.artifacts.save("observed-slot-binding.json", dict(nonce=snapshot["nonce"],
                observedNs=snapshot["observed_ns"], bodyInventory=snapshot["bodyInventory"], binding=self.slot_binding))
        return snapshot, now

    def admit_snapshot(self, snapshot, now, nonce):
        super().admit_snapshot(snapshot, now, nonce)
        if self.slot_binding is not None:
            which = "source" if snapshot["owner"]["weapon"] == self.slot_binding["source"]["native"]["weapon"] else "target"
            selected_matches(snapshot, self.slot_binding[which])

    def validate_publication(self, lines, expected, cleanup):
        super().validate_publication(lines, expected, cleanup)
        if not cleanup:
            require(self.slot_binding is not None, "ordinary input lacks observed slot binding")
            # Every source-side pose and the final ordinary draw press rejoin the
            # complete mapping. Transition-only observations need no guessed map.
            # After selection, original admission and selected_matches pin target.
            if self.publication_source["owner"]["weapon"] == self.slot_binding["source"]["native"]["weapon"]:
                revalidate_pair(self.publication_source, time.perf_counter_ns(), self.slot_binding)


def resolve(snapshot, now, binding):
    source, target = binding["source"], binding["target"]
    return resolve_slots(snapshot, now, source["asset"], source["configuration"],
                                target["asset"], target["configuration"])


def admit_stow_cohort(before, after, now, binding, *, written_ns, committed_ns):
    """Read-only comparison at the original press clock and fresh release clock.

    Historical before data is identity/command history only. Every current outer,
    holster, Empty, mapping and configuration lease is independently admitted.
    """
    require(0 < integer(before["observed_ns"]) <= written_ns <= committed_ns <= now,
            "stow cohort lacks ordered acknowledged press")
    require(resolve(before, written_ns, binding) == binding,
            "stow cohort press differs from initial binding")
    OrdinaryHolsterDriver.require_held(None, before)
    require(neutral(before["input"]) and before["bodyHolster"]["neutralTrackedInput"] is True,
            "stow cohort press lacks neutral Held source")
    current = resolve(after, now, binding)
    require(current == dict(binding, cohort=integer(binding["cohort"]) + 1),
            "stow cohort is not exactly one display-only successor")
    same_outer(before, after)
    source_identity(before, after)
    OrdinaryHolsterDriver.require_empty(None, after, False)
    old, new = before["bodyHolster"], after["bodyHolster"]
    require(integer(after["nonce"]) > integer(before["nonce"])
            and integer(after["observed_ns"]) > integer(before["observed_ns"])
            and integer(new["request"]) == integer(old["request"]) + 1
            and integer(new["nativeTick"]) > integer(old["nativeTick"]),
            "stow cohort lacks a new exact Empty request")
    for key in ("input_evidence", "bodyHolster", "bodyInventory"):
        previous = before[key] if key == "input_evidence" else before[key]["inputEvidence"]
        current_input = after[key] if key == "input_evidence" else after[key]["inputEvidence"]
        require(integer(current_input["observedNs"]) >= written_ns
                and integer(current_input["observedNs"]) > integer(previous["observedNs"])
                and integer(current_input["sequence"]) > integer(previous["sequence"]),
                "stow cohort original input predates acknowledged press")
    require(integer(new["freeRight"]["visibility"]["observedNs"]) >= written_ns,
            "stow cohort hidden receipt predates acknowledged press")
    for inp in (after["input"], new["input"]):
        require(abs(float(inp["hands"][1]["squeeze"]) - 1) <= .01,
                "stow cohort requires consumed held stow grip")
    # The diagnostic serializes assigned rows only. Retain all of them and the
    # native count, not merely the requested pair. Revision is already exact.
    a, b = before["bodyInventory"], after["bodyInventory"]
    require(all(a[key] == b[key] for key in ("nativeCount", "count", "physicalSelected"))
            and sorted((pinned_row(row) for row in a["slots"]), key=lambda r: r["assignment"]["slot"])
            == sorted((pinned_row(row) for row in b["slots"]), key=lambda r: r["assignment"]["slot"]),
            "stow cohort changed complete observed assignments/count")
    return current


class HolsterDriver(BindingHolsterDriver):
    def __init__(self, args):
        self.stow_ack = self.stow_cohort_binding = None
        super().__init__(args)

    def command(self, snapshot, pose, grip):
        stow_press = self.phase == "stow_wait" and grip == 1
        if stow_press:
            require(self.stow_ack is None and self.stow_cohort_binding is None and not self.draw_armed,
                    "stow cohort allows one ordinary stow press")
        super().command(snapshot, pose, grip)
        if stow_press:
            require(not self.pending_command and self.held_right,
                    "stow cohort press was not acknowledged")
            self.stow_ack = dict(snapshot=copy.deepcopy(snapshot), pose=copy.deepcopy(pose),
                lines=list(self.pending_batch[:-1]), identity=self.command_identity(),
                writtenNs=self.last_write, committedNs=self.last_commit)

    def validate_publication(self, lines, expected, cleanup):
        # Retain observation ownership and complete ordinary publication
        # admission; replace only the binding equality at the stow boundary.
        ObservationHolsterDriver.validate_publication(self, lines, expected, cleanup)
        if cleanup:
            return
        if getattr(self.args, "held_swap", False):
            require(not self.draw_armed or self.target_baseline is not None,
                    "pending held swap selection permits observation only")
        require(self.slot_binding is not None, "ordinary input lacks observed slot binding")
        snapshot, now = self.publication_source, time.perf_counter_ns()
        if snapshot["owner"]["weapon"] != self.slot_binding["source"]["native"]["weapon"]:
            return  # binding admission still pins the exact armed target
        if getattr(self.args, "held_swap", False):
            self.require_held_swap_source(snapshot)
            require(neutral(snapshot["input"]) and snapshot["bodyHolster"]["neutralTrackedInput"] is True,
                    "held swap source command lacks fresh neutral input")
            if expected["grip"] == 1:
                require(self.phase == "draw_wait" and not self.draw_armed
                        and self.held_swap_ack is None,
                        "held swap permits one ordinary target shoulder press")
                target = socket_pose(snapshot, self.args.target_slot)
                require(frame.pose_error(frame.from_pose(expected["pose"]), frame.from_pose(target))[0] < .002,
                        "held swap press is outside the assigned target shoulder")
        binding = self.stow_cohort_binding or self.slot_binding
        current = resolve(snapshot, now, binding)
        if current == binding:
            return
        require(self.stow_cohort_binding is None and self.stow_ack is not None
                and self.phase == "stow_wait" and self.held_right
                and not self.pending_command and not self.draw_armed,
                "body/native assignment changed outside one acknowledged stow boundary")
        ack = self.stow_ack
        require(self.command_identity() == ack["identity"]
                and lines == ack["lines"][:-1] + ["grip r 0"]
                and expected == dict(pose=ack["pose"], grip=0),
                "stow cohort successor only permits the same-pose ordinary release")
        successor = admit_stow_cohort(ack["snapshot"], snapshot, now, self.slot_binding,
            written_ns=ack["writtenNs"], committed_ns=ack["committedNs"])
        self.stow_cohort_binding = copy.deepcopy(successor)
        self.artifacts.event(dict(type="ordinary_stow_display_cohort", beforeNonce=ack["snapshot"]["nonce"],
            afterNonce=snapshot["nonce"], evaluatedNs=now, writtenNs=ack["writtenNs"],
            committedNs=ack["committedNs"], beforeCohort=self.slot_binding["cohort"],
            afterCohort=successor["cohort"], revision=successor["revision"],
            nativeAcceptance=False, headsetAcceptance=False))


def main(argv=None, *, observed_binding=None, driver_class=None):
    driver_class = HolsterDriver if driver_class is None else driver_class
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    for name in ("stow-asset", "stow-config", "target-asset", "target-config"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--held-swap", action="store_true",
                        help="swap directly from Held at the other assigned shoulder; default uses genuine Empty")
    args = parser.parse_args(argv)
    args.observed_binding = copy.deepcopy(observed_binding)
    driver, code = None, 2
    result = dict(schema="bc2vr.live-holster-driver.v1", status="refused", nativeAcceptance=False,
                  headsetAcceptance=False, heldSwap=args.held_swap,
                  evidence="fresh observed body/native mapping plus ordinary guarded XR draw")
    try:
        driver = driver_class.__new__(driver_class)
        driver.__init__(args)
        driver.run_holster()
        result["status"], code = ("observed_held_swap_and_expected_target_released" if args.held_swap
                                  else "observed_empty_and_expected_target_released"), 0
    except (Exception, KeyboardInterrupt) as error:
        result["error"] = str(error)
        if driver is not None and getattr(driver, "artifacts", None):
            try:
                result["release"] = driver.release_after_failure()
            except Exception as release_error:
                result["release"] = "refused: " + str(release_error)
    finally:
        if driver is not None:
            if getattr(driver, "artifacts", None):
                result.update(durationNs=time.perf_counter_ns()-driver.started, commands=driver.commands,
                              phase=driver.phase, rightGripHeld=driver.held_right, pendingCommand=driver.pending_command,
                              artifactDirectory=str(driver.artifacts.directory), slotBinding=driver.slot_binding)
                try:
                    driver.artifacts.save("result.json", result, final=True)
                except Exception as error:
                    result["resultWriteError"], code = str(error), 2
            driver.close()
    print(json.dumps(result, separators=(",", ":"), allow_nan=False))
    return code
