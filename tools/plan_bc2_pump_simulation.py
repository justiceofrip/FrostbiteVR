"""Offline ordinary-XR pump/shell geometry from an original live snapshot.

No commands are executed. Each receipt gate stops the schedule until a NEW real
snapshot satisfies check_gate; scheduled offsets never extend source leases.
--now-ns is explicitly historical QPC evaluation, never a replacement clock.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import time

try:
    from . import plan_bc2_feed_simulation as frame
except ImportError:
    import importlib.util
    _spec = importlib.util.spec_from_file_location("bc2_pump_frame", Path(__file__).with_name("plan_bc2_feed_simulation.py"))
    frame = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(frame)

Refusal, require, integer, finite = frame.Refusal, frame.require, frame.integer, frame.finite
STAGES = ("PumpCycle", "ShellPickup", "ShellInsert")
MAX_SAMPLES = 2048


def vector(v):
    require(isinstance(v, list) and len(v) == 3, "expected a three-component vector")
    return [finite(x) for x in v]


def unit(v):
    v = vector(v)
    require(abs(sum(x*x for x in v)-1) < .0002, "profile axis is not unit length")
    return v


def key(v):
    require(isinstance(v, dict) and all(integer(v[k]) > 0 for k in ("id", "generation")), "missing object key")
    return v


def window(v, now):
    require(isinstance(v, dict) and integer(v["sequence"]) > 0
            and 0 < integer(v["observedNs"]) <= now < integer(v["deadlineNs"]), "expired original mechanism lease")


def native_identity(v, snapshot):
    require(isinstance(v, dict) and v["owner"] == snapshot["owner"], "native mechanism owner mismatch")
    firing = [integer(x) for x in v["firing"]]
    require(len(firing) == len(set(firing)) == 3 and min(firing) > 0
            and all(integer(v[k]) > 0 for k in ("serverPlayer", "serverSoldier", "serverItem")),
            "missing distinct native firing identities")
    return v


def claim(v, snapshot, now, hand, kind):
    require(isinstance(v, dict), "missing current hand claim")
    t, sample = v["token"], snapshot["input_evidence"]
    require(t["owner"] == sample["owner"] and integer(t["id"]) > 0
            and integer(t["hand"]) == hand and integer(t["kind"]) == kind,
            "claim owner/hand/kind mismatch")
    key(t["item"]); key(t["contact"])
    require(integer(v["inputSequence"]) == integer(sample["sequence"])
            and now < integer(v["deadlineNs"]) <= integer(sample["deadlineNs"]),
            "claim is not current original input evidence")
    return t


def extension(snapshot, now):
    sp = snapshot.get("shellPump")
    require(isinstance(sp, dict) and sp.get("available") is True and sp.get("observationOnly") is True,
            "shell/pump observation unavailable; request a fresh captured snapshot")
    require(all(sp["input"][k] == snapshot["input_evidence"][k] for k in
                ("owner", "sequence", "observedNs", "deadlineNs", "focused", "tracked", "released")),
            "mechanism publication/input mismatch")
    frame.evidence(sp["input"], snapshot["owner"], integer(snapshot["physical_equipment_generation"]), now)
    profile = sp["profile"]
    require(integer(profile["registrationId"]) > 0
            and profile["configurationPath"] == snapshot["selected"]["configurationPath"]
            and integer(profile["rigFingerprint"]) == integer(snapshot["rig"]["fingerprint"]) > 0,
            "exact selected pump registration/configuration/rig mismatch")
    require(profile["pump"] is not None, "selected shell weapon has no manual pump profile")
    for p in (profile["shell"], profile["pump"]):
        key({"id": p["id"], "generation": p["revision"]})
    gun = claim(sp["gunClaim"], snapshot, now, 1, 1)
    require(integer(gun["prerequisiteClaim"]) == 0 and sp["input"]["released"][1] is False,
            "missing right GunHold root")
    require(integer(gun["item"]["id"]) == integer(snapshot["owner"]["weapon"])
            and integer(gun["item"]["generation"]) == integer(snapshot["physical_equipment_generation"]),
            "GunHold item differs from selected physical weapon")
    return sp


def reserve(snapshot, now, idle=False):
    r = snapshot["shellPump"]["shell"]["reserve"]
    window(r, now)
    native_identity(r["identity"], snapshot)
    require(r["fresh"] is True and r["verified"] is True, "reserve is not verified fresh native evidence")
    require(0 <= integer(r["loaded"]) <= integer(r["capacity"]) and integer(r["capacity"]) > 0
            and integer(r["reserve"]) >= 0, "invalid native ammunition counts")
    if idle:
        require(r["allThreeIdle"] is True and r["reloadInputReady"] is True,
                "pickup requires all-three idle/reload-ready native evidence")
    return r


def shell_native_lease(snapshot, now):
    sp = extension(snapshot, now)
    shell, pump = sp["shell"], sp["pump"]
    require(shell["ticked"] is True and shell["active"] is True and shell["nativeHolding"] is True,
            "shell debt has no active native hold")
    require(pump["debt"] is False and pump["blocksFire"] is False and pump["pending"] is None
            and pump["recovery"] is None and pump["recoveryPending"] is None, "unpaid pump debt conflicts with shell custody")
    r, lease = reserve(snapshot, now), shell["heldLease"]
    window(lease, now)
    require(lease["fresh"] is True and lease["nativeBindingVerified"] is True and lease["allThreeHeld"] is True,
            "shell has no current all-three-held native lease")
    require(native_identity(lease["identity"], snapshot) == shell["identity"] == r["identity"]
            and integer(lease["cycle"]) == integer(shell["cycle"]) > 0, "shell native cycle/identity mismatch")
    require(all(integer(lease[k]) == integer(r[k]) for k in ("loaded", "reserve", "capacity")), "shell lease/reserve counts disagree")
    return lease


def held_shell(snapshot, now):
    lease = shell_native_lease(snapshot, now)
    sp = snapshot["shellPump"]
    shell, r, item = sp["shell"], reserve(snapshot, now), sp["shell"]["item"]
    require(shell["held"] is True and shell["ammoOwnsHand"] is True
            and shell["pending"] is False and shell["pendingRequest"] is None,
            "ShellInsert needs a real held shell without a pending request")
    require(integer(r["loaded"]) < integer(r["capacity"]) and integer(r["reserve"]) > 0, "no shell insertion capacity/reserve")
    require(isinstance(item, dict) and integer(item["units"]) == 1, "need exactly one owned shell")
    t = claim(item["claim"], snapshot, now, 0, 5)
    require(item["claim"] == sp["leftClaim"] and frame.key_equal(key(item["key"]), t["item"])
            and frame.key_equal(item["profile"], {"id": sp["profile"]["shell"]["id"], "generation": sp["profile"]["shell"]["revision"]})
            and integer(t["prerequisiteClaim"]) == 0, "held-shell object/profile/claim mismatch")
    require(sp["input"]["released"] == [False, False] and finite(snapshot["input"]["hands"][0]["squeeze"]) >= .7,
            "held-shell continuation requires original held left grip")
    return lease


def held_pump(snapshot, now):
    sp = extension(snapshot, now)
    p, s, c = sp["pump"], sp["shell"], sp["profile"]["pump"]
    require(p["ticked"] is True and p["debt"] is True and p["blocksFire"] is True
            and not any(s[k] for k in ("active", "held", "pending", "nativeHolding", "ammoOwnsHand")),
            "pump requires exact debt and no shell transaction")
    require(p["recovery"] is None and p["recoveryPending"] is None,
            "recovery grant details unavailable; do not invent a held pump lease")
    n, original = p["native"], p["held"]
    require(isinstance(n, dict) and n["ownerMatchesSnapshot"] is True and n["blocksFire"] is True
            and n["heldSuspended"] is False and integer(n["phase"]) == 3 and integer(n["failure"]) == 0,
            "pump needs actual native Held debt")
    native_identity(n["identity"], snapshot)
    lease = n["held"]
    window(lease, now)
    require(lease["fresh"] is True and lease["held"] is True and lease["advancing"] is None
            and isinstance(original, dict) and same_cycle(lease, original)
            and integer(original["sequence"]) <= integer(lease["sequence"])
            and 0 < integer(original["observedNs"]) <= integer(lease["observedNs"])
            and integer(original["observedNs"]) < integer(original["deadlineNs"])
            and lease["owner"] == sp["input"]["owner"], "pump/native held lease mismatch")
    gun = sp["gunClaim"]["token"]
    require(frame.key_equal(lease["item"], gun["item"])
            and frame.key_equal(lease["mechanism"], {"id": c["id"], "generation": gun["item"]["generation"]})
            and integer(lease["cycle"]) == integer(n["cycle"]) > 0
            and integer(lease["shot"]) == integer(n["shot"]) > 0, "pump cycle/shot/item/profile mismatch")
    return lease


def blocked_observation(snapshot, now):
    """Same-owner shell OR pump debt only; grants no Fire/control authority."""
    require(not (snapshot.get("feed") or {}).get("blocksFire", False), "unrelated feed block conflicts with pump observation")
    sp = extension(snapshot, now)
    if sp["shell"]["active"] or sp["shell"]["nativeHolding"]:
        shell_native_lease(snapshot, now)
    else:
        held_pump(snapshot, now)
    return True


def admit(snapshot, now, nonce):
    frame.admit_frame(snapshot, now, nonce, mechanism_admission=blocked_observation)
    sp = extension(snapshot, now)
    require(snapshot.get("feed") is None, "pump snapshot contains unrelated feed custody")
    for h in snapshot["input"]["hands"]:
        require(integer(h["held"]) == 0 and integer(h["touched"]) == 0
                and all(finite(h[k]) == 0 for k in ("trigger", "stickX", "stickY")),
                "unrelated buttons/sticks/triggers must be observed zero")
    return sp


def raw_contact(snapshot, now, kind):
    r = snapshot["shellPump"][kind]["raw"]
    require(all(r[k] is True for k in ("valid", "current", "ownerMatches", "rigMatches"))
            and integer(r["rigFingerprint"]) == integer(snapshot["rig"]["fingerprint"]), "missing current raw mechanism contact")
    frame.evidence(r["input"], snapshot["owner"], integer(snapshot["physical_equipment_generation"]), now)
    # Current pose may follow the callback sample; never stamp that old sample
    # with the newer frame. Fresh bounded lag is observable, not new authority.
    require(integer(r["input"]["sequence"]) <= integer(snapshot["input"]["generation"])
            and integer(r["input"]["observedNs"]) <= integer(snapshot["input_evidence"]["observedNs"]),
            "raw contact is from the future")
    if kind == "shell":
        require(r["profileMatches"] is True, "shell raw profile mismatch")
        for field in ("rawWristWorldMeters", "weaponWorldMeters"):
            frame.rigid(r[field])
    else:
        require(r["mappingValid"] is True, "pump raw contact has no valid mapping")
        frame.rigid(r["contact"])
    return r


def selection(snapshot):
    return dict(asset=snapshot["equipment"]["asset"], configurationPath=snapshot["selected"]["configurationPath"],
                meshPath=snapshot["selected"]["matchedMeshPath"], rigFingerprint=snapshot["rig"]["fingerprint"],
                profile=snapshot["shellPump"]["profile"])


def preserved(snapshot):
    i = snapshot["input"]
    return {**{k: i[k] for k in ("head", "referenceHead", "spaceGeneration", "trackingBasisGeneration", "worldUnitsPerMeter")},
            "rightHand": i["hands"][1], "weaponWorldMeters": snapshot["mapping"]["weaponWorldMeters"],
            "trackingBodyWorldMeters": snapshot["mapping"]["trackingBodyWorldMeters"],
            "anatomicalWristToGrip": snapshot["mapping"]["anatomicalWristToGrip"]}


def reobserve(previous, snapshot, now, nonce):
    sp = admit(snapshot, now, nonce)
    require(previous["schema"] == "bc2vr.pump-simulation-plan.v1" and previous["owner"] == snapshot["owner"]
            and previous["selection"] == selection(snapshot), "receipt/prior-plan owner or exact profile changed")
    require(integer(snapshot["nonce"]) != integer(previous["snapshotNonce"])
            and integer(snapshot["observed_ns"]) > integer(previous["evaluatedAtNs"])
            and integer(snapshot["input"]["generation"]) > integer(previous["sourceInputSequence"]),
            "receipt requires fresh real post-plan evidence")
    require(previous["preserve"] == preserved(snapshot), "receipt/prior-plan head, right hand or mapping changed; replan")
    return sp


def same_cycle(a, b):
    return all(a[k] == b[k] for k in ("owner", "item", "mechanism", "cycle", "shot"))


def pump_grasp(snapshot, now, lease):
    sp, p = snapshot["shellPump"], snapshot["shellPump"]["pump"]
    t = claim(p["mechanismClaim"], snapshot, now, 0, 4)
    require(sp["leftClaim"] == p["mechanismClaim"] and frame.key_equal(t["item"], lease["item"])
            and frame.key_equal(t["contact"], lease["mechanism"])
            and integer(t["prerequisiteClaim"]) == integer(sp["gunClaim"]["token"]["id"])
            and sp["input"]["released"] == [False, False] and p["ownsHand"] is True
            and finite(snapshot["input"]["hands"][0]["squeeze"]) >= .7, "pump needs current Mechanism rooted in GunHold")
    return t


def check_gate(previous, snapshot, gate, now_ns, expected_nonce):
    """Validate actual observations; counters/geometry alone never acknowledge."""
    now = integer(now_ns)
    sp = reobserve(previous, snapshot, now, integer(expected_nonce))
    require(gate in {g["id"] for g in previous["receiptGates"]}, "gate is absent from this plan")
    baseline, p, s = previous["baseline"], sp["pump"], sp["shell"]
    if gate == "shell_held":
        held_shell(snapshot, now)
        require(integer(s["lifetimeCounters"]["acquired"]) == integer(baseline["shellCounters"]["acquired"])+1
                and all(integer(s["lifetimeCounters"][k]) == integer(baseline["shellCounters"][k]) for k in ("submitted", "completed")),
                "pickup did not acquire exactly one unsubmitted shell")
        require(all(integer(reserve(snapshot, now)[k]) == integer(baseline["ammo"][k]) for k in ("loaded", "reserve", "capacity")),
                "pickup alone changed ammunition")
    elif gate in ("pump_acquired", "pump_rear"):
        lease = held_pump(snapshot, now)
        require(same_cycle(lease, baseline["pumpLease"]), "pump gate changed original cycle/shot")
        pump_grasp(snapshot, now, lease)
        raw_contact(snapshot, now, "pump")
        g, target = p["gesture"], p["target"]
        require(g["available"] is True and g["evaluated"] is True and integer(g["gate"]) == 0
                and integer(g["failure"]) == 0 and g["recovery"] is False
                and integer(g["input"]) == integer(sp["input"]["sequence"])
                and integer(g["rawInput"]) == integer(p["raw"]["input"]["sequence"]), "pump gesture was not actually evaluated")
        require(isinstance(target, dict) and integer(target["profile"]) == integer(sp["profile"]["pump"]["id"])
                and integer(target["revision"]) == integer(sp["profile"]["pump"]["revision"])
                and integer(previous["sourceInputSequence"]) < integer(target["inputSequence"])
                == integer(p["raw"]["input"]["sequence"]) <= integer(sp["input"]["sequence"])
                and integer(target["observedNs"]) == integer(p["raw"]["input"]["observedNs"])
                and 0 < integer(target["observedNs"]) <= now < integer(target["deadlineNs"])
                <= min(integer(lease["deadlineNs"]), integer(p["raw"]["input"]["deadlineNs"]),
                       integer(sp["input"]["deadlineNs"])), "pump target is not current original raw evidence")
        desired = 0 if gate == "pump_acquired" else finite(sp["profile"]["pump"]["strokeMeters"])
        phase = 3 if gate == "pump_acquired" else 4
        require(integer(p["phase"]) == integer(g["phase"]) == phase
                and abs(finite(g["travelMeters"])-desired) <= .006
                and abs(finite(target["travelMeters"])-desired) <= .006, "actual pump endpoint/phase not acknowledged")
    elif gate == "pump_ready":
        ready = p["lastSettled"]
        require(isinstance(ready, dict) and ready["nativeReady"] is True and ready["unchangedAmmunition"] is True,
                "pump has no actual Ready acknowledgement")
        release = ready["release"]
        require(same_cycle(release["cycle"], baseline["pumpLease"])
                and integer(release["request"]) > integer(baseline["lastPumpRequest"])
                and integer(release["inputSequence"]) > integer(previous["sourceInputSequence"])
                and integer(previous["evaluatedAtNs"]) < integer(release["observedNs"]) < integer(release["deadlineNs"])
                and integer(release["observedNs"]) <= integer(ready["observedNs"]) <= integer(p["acknowledgedNs"]) <= now
                and integer(ready["observedNs"]) < integer(ready["deadlineNs"])
                and integer(ready["sequence"]) > integer(release["cycle"]["sequence"]),
                "Ready is historical/unrelated or its original clocks disagree")
        n = p["native"]
        require(isinstance(n, dict) and native_identity(n["identity"], snapshot) == baseline["nativeIdentity"]
                and integer(n["cycle"]) == integer(release["cycle"]["cycle"])
                and integer(n["shot"]) == integer(release["cycle"]["shot"])
                and integer(n["phase"]) == 5 and integer(n["failure"]) == 0 and n["blocksFire"] is False
                and p["debt"] is False and p["blocksFire"] is False and p["pending"] is None
                and p["recoveryPending"] is None and snapshot["weapon_actions_blocked"] is False,
                "native pump debt has not settled Ready")
        support = claim(sp["leftClaim"], snapshot, now, 0, 2)
        require(p["supportHolding"] is True and p["supportReturnPending"] is False
                and integer(p["supportToken"]) == integer(support["id"])
                and integer(support["prerequisiteClaim"]) == integer(sp["gunClaim"]["token"]["id"])
                and frame.key_equal(support["item"], release["cycle"]["item"]), "Ready still awaits real support return")
        require(all(integer(reserve(snapshot, now)[k]) == integer(baseline["ammo"][k]) for k in ("loaded", "reserve", "capacity")),
                "pump cycle changed ammunition")
    elif gate == "shell_inserted":
        a, submitted = s["lastAcknowledged"], s["lastSubmitted"]
        require(isinstance(a, dict) and isinstance(submitted, dict) and a["verified"] is True
                and integer(a["operation"]) == integer(submitted["operation"]) == 3 and integer(a["status"]) == 1,
                "no verified InsertRound acknowledgement")
        require(native_identity(a["identity"], snapshot) == baseline["nativeIdentity"]
                and integer(a["cycle"]) == integer(baseline["shellCycle"])
                and integer(a["request"]) == integer(submitted["request"]) > integer(baseline["lastShellRequest"])
                and integer(submitted["reservation"]["request"]) == integer(a["request"])
                and integer(submitted["reservation"]["cycle"]) == integer(a["cycle"])
                and submitted["reservation"]["item"] == baseline["shellItem"]["key"]
                and submitted["reservation"]["claim"] == baseline["shellItem"]["claim"]["token"]
                and integer(previous["evaluatedAtNs"]) < integer(submitted["submittedNs"]) <= integer(a["observedNs"])
                and integer(a["observedNs"]) <= integer(a["acknowledgedNs"]) <= now
                and integer(a["observedNs"]) < integer(a["deadlineNs"])
                and integer(a["sampleSequence"]) > 0 and integer(a["serverInvocation"]) > 0,
                "shell receipt is historical/unrelated or original request clocks disagree")
        before = submitted["heldLease"]
        require(native_identity(before["identity"], snapshot) == baseline["nativeIdentity"]
                and integer(before["cycle"]) == integer(a["cycle"])
                and before["nativeBindingVerified"] is True and before["allThreeHeld"] is True
                and 0 < integer(before["observedNs"]) <= integer(submitted["submittedNs"]) < integer(before["deadlineNs"])
                and integer(submitted["reservation"]["seat"]) > 0
                and all(integer(before[k]) == integer(baseline["ammo"][k]) for k in ("loaded", "reserve", "capacity")),
                "InsertRound submission lacks its original admitted all-three-held lease")
        require(s["pending"] is False and s["pendingRequest"] is None and s["held"] is False
                and s["item"] is None and integer(s["lifetimeCounters"]["submitted"]) == integer(baseline["shellCounters"]["submitted"])+1
                and integer(s["lifetimeCounters"]["completed"]) == integer(baseline["shellCounters"]["completed"])+1,
                "shell request/custody has not completed exactly once")
        r = reserve(snapshot, now)
        require(integer(r["loaded"]) == integer(baseline["ammo"]["loaded"])+1
                and integer(r["reserve"]) == integer(baseline["ammo"]["reserve"])-1
                and integer(r["capacity"]) == integer(baseline["ammo"]["capacity"]), "insert acknowledgement lacks exact ammunition delta")
    elif gate == "shell_captured":
        held_shell(snapshot, now)
        raw_contact(snapshot, now, "shell")
        g = s["geometry"]
        # captured is a one-packet event; Guided is the persistent capture state.
        require(g["available"] is True and g["seated"] is False
                and integer(g["phase"]) == 1 and g["owner"] == sp["input"]["owner"]
                and g["item"] == baseline["shellItem"]["key"] and integer(g["cycle"]) == integer(baseline["shellCycle"])
                and integer(g["sourceSequence"]) > integer(previous["sourceInputSequence"])
                and integer(g["sourceSequence"]) <= integer(g["currentSequence"]) <= integer(sp["input"]["sequence"])
                and 0 < integer(g["observedNs"]) <= now < integer(g["deadlineNs"])
                and 0 <= finite(g["progress"]) < .1 and finite(g["alignment"]) >= .99,
                "shell has no fresh aligned capture before inward travel")
    return {"gate": gate, "observed": True, "snapshotNonce": snapshot["nonce"],
            "sourceValidUntilNs": snapshot["valid_until_ns"], "nativeAcceptance": False,
            "requiresIndependentAllThreePathAudit": True}


def shell_wrist(profile, travel):
    offset = frame.identity()
    offset[3][:3] = [x*travel for x in unit(profile["travelDirection"])]
    return frame.multiply(frame.multiply(frame.multiply(frame.rigid(profile["itemFromHand"]),
        frame.inverse(frame.rigid(profile["itemFromInsertion"]))), offset), frame.rigid(profile["weaponFromEntry"]))


def plan(snapshot, stage, now_ns, expected_nonce, step_ns=20_000_000, pickup_plan=None):
    require(stage in STAGES, "unsupported pump/shell stage")
    now, step_ns = integer(now_ns), integer(step_ns)
    sp = admit(snapshot, now, integer(expected_nonce))
    inp, mapping = snapshot["input"], snapshot["mapping"]
    w, b, f, k = [frame.rigid(mapping[n]) for n in
                  ("weaponWorldMeters", "trackingBodyWorldMeters", "rawLeftWristWorldMeters", "anatomicalWristToGrip")]
    reference, absolute = frame.from_pose(inp["referenceHead"]), frame.from_pose(inp["hands"][0]["grip"])
    def forward(grip):
        return frame.multiply(frame.multiply(k, frame.multiply(grip, frame.inverse(reference))), b)
    def to_grip(local):
        return frame.multiply(frame.multiply(frame.multiply(frame.inverse(k), frame.multiply(local, w)), frame.inverse(b)), reference)
    require(all(e <= cap for e, cap in zip(frame.pose_error(forward(absolute), f), (.002, .005))), "original W/B/F/K mapping fails round trip")
    current = frame.multiply(f, frame.inverse(w))
    r = reserve(snapshot, now, idle=stage == "ShellPickup")
    shell, pump, profile = sp["shell"], sp["pump"], sp["profile"]
    shell_profile = profile["shell"]
    max_pos = min(.014, finite(shell_profile["maxStepMeters"]))
    max_ang = min(.055, finite(shell_profile["maxStepRadians"]))
    gap = min(100_000_000, integer(shell_profile["maxSampleGapNs"]))
    require(max_pos > 0 and max_ang > 0 and 17_000_000 <= step_ns <= gap//2, "invalid step policy/period")
    maximum = 30_000_000_000 if stage == "PumpCycle" else integer(shell_profile["maxGuidedNs"])
    require(0 < maximum <= 30_000_000_000, "unbounded gesture policy")
    baseline = {"ammo": {v: r[v] for v in ("loaded", "reserve", "capacity")},
                "nativeIdentity": r["identity"], "shellCounters": shell["lifetimeCounters"],
                "lastShellRequest": (shell["lastSubmitted"] or {}).get("request", 0),
                "lastPumpRequest": ((pump["lastSettled"] or {}).get("release") or {}).get("request", 0)}
    leases = {"snapshot": snapshot["valid_until_ns"], "nativeFrame": snapshot["native_deadline_ns"],
              "input": inp and snapshot["input_evidence"]["deadlineNs"], "reserve": r["deadlineNs"],
              "gunClaim": sp["gunClaim"]["deadlineNs"]}
    rows, gates = [], []
    def append(phase, grip, target):
        require(len(rows) < MAX_SAMPLES, "trajectory exceeds bounded sample budget")
        rows.append((phase, grip, target))
    def move(phase, grip, destination, minimum_ns=0):
        start = rows[-1][2]
        count = max(1, math.ceil(minimum_ns/step_ns))
        while count < MAX_SAMPLES:
            path = [frame.blend(start, destination, n/count) for n in range(count+1)]
            controls = [to_grip(p) for p in path]
            if all(d < max_pos*.9 and a < max_ang*.9 for curve in (path, controls)
                   for d, a in (frame.pose_error(x, y) for x, y in zip(curve, curve[1:]))):
                for pose in path[1:]:
                    append(phase, grip, pose)
                return
            count *= 2
        raise Refusal("trajectory cannot meet strict displacement caps")
    def dwell(phase, grip, duration):
        require(0 <= duration <= maximum, "invalid dwell")
        for _ in range(math.ceil(duration/step_ns)):
            append(phase, grip, rows[-1][2])
    def gate(name, reason):
        gates.append(dict(id=name, afterSample=len(rows)-1, stop=True, requiresFreshSnapshot=True,
                          holdGrip=rows[-1][1], timeoutNs=2_000_000_000, countsTowardGestureBudget=True,
                          requireContinuousFreshInput=True, maxInputGapNs=gap, condition=reason))
    if stage == "PumpCycle":
        require(pickup_plan is None and pump["pending"] is None, "pump release is already pending")
        lease = held_pump(snapshot, now)
        raw = raw_contact(snapshot, now, "pump")
        leases.update(pumpHeld=lease["deadlineNs"], raw=raw["input"]["deadlineNs"])
        baseline.update(pumpLease=lease, historicalPumpLease=pump["held"])
        require(pump["native"]["identity"] == r["identity"], "reserve/pump native identities disagree")
        c = profile["pump"]
        closed = frame.rigid(c["closedWrist"])
        stroke, sign = finite(c["strokeMeters"]), finite(c["rearDirection"])
        axis = unit(c["axis"])
        require(0 < stroke <= .5 and sign in (-1, 1), "invalid exact pump stroke/direction")
        rear = [row[:] for row in closed]
        rear[3][:3] = [x+a*sign*stroke for x, a in zip(closed[3][:3], axis)]
        continuation = pump["mechanismClaim"] is not None
        require(integer(pump["phase"]) in (1, 3), "pump requires initial acquisition; partial/recovery stroke must be reobserved")
        if continuation:
            pump_grasp(snapshot, now, lease)
            require(all(e <= cap for e, cap in zip(frame.pose_error(current, closed), (.006, .10))), "held pump is not at its actual closed start")
            leases["mechanismClaim"] = pump["mechanismClaim"]["deadlineNs"]
            append("continue_held", 1, current)
        else:
            require(sp["leftClaim"] is None or integer(sp["leftClaim"]["token"]["kind"]) == 2, "left hand has competing custody")
            append("neutral", 0, current)
            move("approach", 0, closed)
            append("neutral_at_contact", 0, closed)
            append("acquire", 1, closed)
        gate("pump_acquired", "Fresh same-cycle native Held, current Mechanism rooted in GunHold, evaluated Rear phase at closed contact.")
        move("rear", 1, rear, 900_000_000)
        gate("pump_rear", "Fresh same-cycle native Held and evaluated Forward phase at the actual rear endpoint; only then start rear dwell/return.")
        dwell("rear_dwell", 1, 30_000_000)
        move("return", 1, closed, 900_000_000)
        dwell("front_dwell", 1, 30_000_000)
        gate("pump_ready", "Matching new release and native Ready acknowledgement, debt cleared, unchanged ammo and fresh support claim.")
    elif stage == "ShellPickup":
        require(pickup_plan is None and snapshot["weapon_actions_blocked"] is False and sp["leftClaim"] is None,
                "pickup requires unblocked weapon and a free left hand")
        require(not any(shell[k] for k in ("active", "held", "nativeHolding", "pending", "ammoOwnsHand"))
                and shell["item"] is None and shell["pendingRequest"] is None
                and pump["debt"] is False and pump["blocksFire"] is False and pump["pending"] is None,
                "pickup conflicts with existing shell/pump debt")
        require(integer(r["loaded"]) < integer(r["capacity"]) and integer(r["reserve"]) > 0,
                "pickup needs an empty tube slot and native reserve")
        pouch = shell["pouch"]
        require(integer(pouch["frame"]) == 1 and .03 <= finite(pouch["radiusMeters"]) <= .35,
                "pickup needs actual RecenteredBody production pouch")
        body = frame.supply_body_frame(inp)
        body_grip = frame.multiply(absolute, frame.inverse(body))
        body_grip[3][:3] = vector(pouch["centerMeters"])
        pickup = frame.multiply(forward(frame.multiply(body_grip, body)), frame.inverse(w))
        append("neutral", 0, current)
        move("approach_supply", 0, pickup)
        append("neutral_at_contact", 0, pickup)
        append("acquire_shell", 1, pickup)
        gate("shell_held", "Exactly one new owned shell, current item/GunHold claims and all-three-held native lease; ammunition unchanged.")
    else:
        require(pickup_plan is not None and pickup_plan["stage"] == "ShellPickup", "ShellInsert requires the prior pickup plan plus fresh actual custody")
        check_gate(pickup_plan, snapshot, "shell_held", now, expected_nonce)
        lease = held_shell(snapshot, now)
        raw = raw_contact(snapshot, now, "shell")
        leases.update(shellHeld=lease["deadlineNs"], itemClaim=shell["item"]["claim"]["deadlineNs"], raw=raw["input"]["deadlineNs"])
        baseline.update(shellCycle=shell["cycle"], shellItem=shell["item"])
        require(integer(shell_profile["approach"]) == 3 and integer(shell_profile["orientation"]) in (0, 1), "unsupported production shell approach")
        travel, capture, post = [finite(shell_profile[k]) for k in ("travelMeters", "captureDistanceMeters", "postCaptureTravelMeters")]
        require(0 < post < travel <= .5 and 0 < capture <= .2, "invalid actual shell rail geometry")
        require(integer(shell_profile["alignmentNs"]) > 0 and integer(shell_profile["seatDwellNs"]) >= 0, "invalid shell dwell policy")
        append("continue_held_shell", 1, current)
        move("approach_outside_rail", 1, shell_wrist(shell_profile, -capture-.03))
        move("enter_rail", 1, shell_wrist(shell_profile, -min(.02, capture/2)))
        dwell("alignment", 1, integer(shell_profile["alignmentNs"]))
        gate("shell_captured", "Fresh exact item/cycle raw geometry reports aligned Guided capture; only then begin new inward post-capture travel.")
        move("insert", 1, shell_wrist(shell_profile, travel), 800_000_000)
        dwell("seat_dwell", 1, integer(shell_profile["seatDwellNs"]))
        gate("shell_inserted", "New exact InsertRound request/ack/item/cycle, completed once, loaded+1/reserve-1; retain separate all-three-path audit.")
    require((len(rows)-1)*step_ns <= maximum, "trajectory exceeds original gesture policy")
    schedule, steps, errors = [], [], []
    previous_grip, previous_wrist = absolute, f
    for index, (phase, squeeze, target) in enumerate(rows):
        pose = frame.to_pose(to_grip(target))
        values = [float(f"{x:.9f}") for x in pose["position"]+frame.quaternion_ypr(pose["orientation"])]
        emitted = frame.from_pose(dict(position=values[:3], orientation=frame.ypr_quaternion(values[3:])))
        wrist = forward(emitted)
        error = frame.pose_error(wrist, frame.multiply(target, w))
        require(error[0] < .0005 and error[1] < .005, "formatted XR command fails anatomical round trip")
        errors.append(error)
        steps.extend((frame.pose_error(previous_grip, emitted), frame.pose_error(previous_wrist, wrist)))
        previous_grip, previous_wrist = emitted, wrist
        coordinates = " ".join(f"{x:.9f}" for x in values)
        commands = ["hand l grip pose "+coordinates, "hand l aim pose "+coordinates, f"grip l {squeeze}"]
        if index == 0:
            commands = ["hand l follow off", "hand l aimtrim 0 0"]+commands
        schedule.append(dict(atNs=index*step_ns, phase=phase, grip=squeeze, positionMeters=values[:3],
                             yawPitchRollDegrees=values[3:], commands=commands))
    require(all(d < max_pos and a < max_ang for d, a in steps), "formatted command exceeds strict motion caps")
    return dict(schema="bc2vr.pump-simulation-plan.v1", offlineOnly=True, executionAuthorized=False,
        nativeAcceptance=False, headsetAcceptance=False, implementedStages=list(STAGES), stage=stage,
        selection=selection(snapshot), owner=snapshot["owner"], snapshotNonce=snapshot["nonce"], evaluatedAtNs=now,
        sourceInputSequence=inp["generation"], sourceWeaponActionsBlocked=snapshot["weapon_actions_blocked"],
        originalLeases=leases, sourceValidUntilNs=min(integer(x) for x in leases.values()),
        preserve=preserved(snapshot), baseline=baseline, requiresPostPickupReobservation=stage == "ShellPickup",
        policy=dict(maxStepMeters=max_pos, maxStepRadians=max_ang, maxInputGapNs=gap, maximumGestureNs=maximum),
        controls=dict(leftGrip="schedule", rightGrip=inp["hands"][1]["squeeze"], triggers=[0, 0], sticks=[[0, 0], [0, 0]], buttons=[0, 0]),
        durationNs=schedule[-1]["atNs"], receiptGates=gates,
        executionConditions=["Geometry only. Native preflight and ordinary simulator input authorization remain external.",
            "Stop at every receipt gate. Continue only after check_gate accepts a fresh actual snapshot; never advance on elapsed dwell alone.",
            "Every batch needs a new original same-owner/profile/basis lease; this plan never renews any captured timestamp.",
            "Require xr-sim cmdSeq advancement, lastCmdError=0 and committed frame acknowledgement; abort on input gap or changed basis.",
            "Keep endpoint grip held while observing; no Fire, ammo writes or synthetic acknowledgements. Release/reacquire support only after receipt.",
            "Native per-branch counts are unavailable here; independent three-path request/ack/ammo audit is still required."],
        verification=dict(everyFormattedCommandRoundTripped=True, maximumStepMeters=max(d for d, _ in steps),
                          maximumStepRadians=max(a for _, a in steps), maximumPositionErrorMeters=max(d for d, _ in errors),
                          maximumAngleErrorRadians=max(a for _, a in errors)), schedule=schedule)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--nonce", type=int, required=True)
    parser.add_argument("--stage", choices=STAGES, required=True)
    parser.add_argument("--pickup-plan", type=Path)
    parser.add_argument("--now-ns", type=int, help="explicit historical QPC nanoseconds; source leases remain original")
    parser.add_argument("--step-ms", type=int, default=20)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        require(args.output.suffix.lower() == ".json" and args.output.name.lower() not in
                {"state.json", "command.json", "mechanism-snapshot.json"}, "output must be a separate offline JSON artifact")
        require(args.output.resolve() not in {args.snapshot.resolve(), args.pickup_plan.resolve() if args.pickup_plan else None},
                "output cannot replace input evidence")
        snapshot, digest = frame.load_json(args.snapshot, 16384)
        pickup = frame.load_json(args.pickup_plan, 4*1024*1024)[0] if args.pickup_plan else None
        result = plan(snapshot, args.stage, time.perf_counter_ns() if args.now_ns is None else args.now_ns,
                      args.nonce, args.step_ms*1_000_000, pickup_plan=pickup)
        result["evaluationClock"] = "local QPC perf_counter_ns" if args.now_ns is None else "explicit historical QPC nanoseconds"
        result["sourceHashes"] = dict(snapshotJson=digest, planner=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                                      framePlanner=hashlib.sha256(Path(frame.__file__).read_bytes()).hexdigest())
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")
        print(f"Offline {args.stage}: {len(result['schedule'])} bounded samples, {len(result['receiptGates'])} receipt gates; native acceptance=false")
        return 0
    except (Refusal, KeyError, TypeError, ValueError, OSError, OverflowError) as error:
        print(f"Refused offline trajectory: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
