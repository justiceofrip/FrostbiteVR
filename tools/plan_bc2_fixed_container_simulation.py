"""Offline single-round fixed-container motions from original native observations.

Uses the shared frame algebra/admission. Plans never execute commands or renew
leases. Actual custody and exact native receipts are mandatory at stop gates.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location("fixed_frame", Path(__file__).with_name("plan_bc2_feed_simulation.py"))
frame = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(frame)
Refusal, require, integer, finite = frame.Refusal, frame.require, frame.integer, frame.finite
STAGES = ("Open", "Pickup", "Insert", "Close")
SCHEMA = "bc2vr.fixed-container-simulation-plan.v1"
OBSERVATION_SCHEMA = "bc2vr.fixed-container-observation.v1"
STEP = {"Open": 0, "Pickup": 2, "Insert": 2, "Close": 3}


def window(value, now, sequence=True):
    require(isinstance(value, dict), "missing original lease")
    if sequence:
        require(integer(value["sequence"]) > 0, "missing original sequence")
    start, end = integer(value["observedNs"]), integer(value["deadlineNs"])
    require(0 < start <= now < end and 0 < end-start <= 100_000_000, "expired or unbounded original lease")


def key(value):
    require(isinstance(value, dict) and all(integer(value[k]) > 0 for k in ("id", "generation")), "missing resource key")
    return value


def claim(value, source, now, hand, kind):
    require(isinstance(value, dict), "missing actual hand custody")
    token = value["token"]
    require(token["owner"] == source["owner"] and integer(token["id"]) > 0
            and integer(token["hand"]) == hand and integer(token["kind"]) == kind,
            "claim owner/hand/kind mismatch")
    key(token["item"]); key(token["contact"])
    require(integer(value["inputSequence"]) == integer(source["sequence"])
            and now < integer(value["deadlineNs"]) <= integer(source["deadlineNs"]),
            "claim does not retain its original input lease")
    return token


def binding_key(binding):
    return {k: binding[k] for k in ("owner", "context", "inventory", "switching", "data", "persistence")}


def counts(value):
    result = {k: integer(value[k]) for k in ("loaded", "reserve", "capacity")}
    require(0 <= result["loaded"] <= result["capacity"] == 1 and 0 <= result["reserve"] <= 1_000_000,
            "unsupported or invalid single-round counts")
    return result


def ammo(snapshot, now, idle=False):
    x = snapshot["fixedContainer"]
    view, owner = x["view"], snapshot["owner"]
    require(isinstance(view, dict) and x["viewFresh"] is True, "missing exact fresh shared ammunition view")
    b, a = view["binding"], view["snapshot"]
    window(b, now, False); window(a, now)
    c, resource = b["context"], b["context"]["resource"]
    require(b["owner"] == owner and a["context"] == c
            and integer(resource["actor"]) == integer(owner["soldier"])
            and integer(resource["actorGeneration"]) == integer(owner["actorGeneration"])
            and integer(resource["weapon"]) == integer(owner["weapon"])
            and integer(resource["weaponGeneration"]) > 0
            and integer(c["equipGeneration"]) == integer(owner["equipGeneration"])
            and integer(c["space"]) == integer(owner["space"]), "native ammunition context/lifetime mismatch")
    require(integer(b["inventory"]) == integer(snapshot["selected"]["inventory"])
            and integer(b["data"]) == integer(snapshot["equipment"]["data"])
            and integer(b["persistence"]) == integer(snapshot["equipment"]["persistence"])
            and integer(b["switching"]) > 0, "native ammunition inventory binding changed")
    require(view["wellEmpty"] is False and a["coherentIdleVerified"] is True, "ammunition copies are not coherently observed")
    counts(a["counts"])
    identity = view["identity"]
    firing = [integer(value) for value in identity["firing"]]
    require(identity["owner"] == owner and len(firing) == len(set(firing)) == 3 and min(firing) > 0
            and all(integer(identity[k]) > 0 for k in ("serverPlayer", "serverSoldier", "serverItem")),
            "missing exact three-path native ammunition identity")
    if idle:
        require(integer(view["phase"]) == 1 and view["command"] is None
                and not x["tracking"]["pending"] and x["pendingRequest"] is None,
                "shared ammunition transaction is pending or ambiguous")
    return view


def extension(snapshot, now):
    x = snapshot.get("fixedContainer")
    require(isinstance(x, dict) and x.get("schema") == OBSERVATION_SCHEMA
            and x.get("available") is True and x.get("observationOnly") is True,
            "fixed-container observation unavailable")
    source, current = x["input"], snapshot["input_evidence"]
    require(all(source[k] == current[k] for k in
                ("owner", "sequence", "observedNs", "deadlineNs", "focused", "tracked", "released")),
            "fixed-container observation is not the original mapping packet")
    frame.evidence(source, snapshot["owner"], integer(snapshot["physical_equipment_generation"]), now)
    p, t, raw = x["profile"], x["tracking"], x["raw"]
    key(dict(id=p["id"], generation=p["revision"]))
    require(p["configurationPath"] == snapshot["selected"]["configurationPath"]
            and p["meshPath"] == snapshot["selected"]["matchedMeshPath"]
            and integer(p["rigFingerprint"]) == integer(snapshot["rig"]["fingerprint"]) > 0,
            "exact fixed-container configuration/mesh/rig mismatch")
    require(integer(p["kind"]) in (3, 4) and integer(p["capacity"]) == integer(p["roundsPerInsertion"]) == 1
            and integer(p["ammunitionKind"]) > 0 and 0 < integer(p["maxAgeNs"]) <= 100_000_000,
            "adapter supports measured single-round breech/muzzle profiles only")
    require(t["enabled"] is True and t["current"] is True and integer(t["lifetime"]) > 0 and integer(t["revision"]) > 0
            and integer(t["step"]) in (0, 2, 3, 4), "missing retained fixed-container lifetime/step")
    require(all(raw[k] is True for k in ("valid", "current", "admittedAtTick", "ownerMatches", "rigMatches", "profileMatches"))
            and integer(raw["rigFingerprint"]) == integer(p["rigFingerprint"]), "missing original raw fixed-container contact")
    frame.evidence(raw["input"], snapshot["owner"], integer(snapshot["physical_equipment_generation"]), now)
    require(integer(raw["input"]["sequence"]) <= integer(source["sequence"])
            and integer(raw["input"]["observedNs"]) <= integer(source["observedNs"]), "raw contact is newer than its publication")
    frame.rigid(raw["weaponFromWrist"]); frame.rigid(raw["weaponWorldMeters"])
    gun = claim(t["gun"], source, now, 1, 1)
    require(gun["item"] == dict(id=integer(snapshot["owner"]["weapon"]), generation=integer(snapshot["physical_equipment_generation"]))
            and integer(gun["prerequisiteClaim"]) == 0 and source["released"][1] is False, "missing exact right GunHold root")
    key(x["supply"]["resource"])
    require((t["pending"] is True) == (x["pendingRequest"] is not None), "pending transaction observation disagrees")
    for field in ("target", "carriedSupply"):
        if t[field] is not None:
            frame.rigid(t[field])
    return x


def request_proof(request, view):
    require(isinstance(request, dict), "missing exact pending request")
    command, intent, proof = request["preparedCommand"], request["intent"], request["sourceProof"]
    require(isinstance(command, dict) and isinstance(proof, dict)
            and binding_key(request["binding"]) == binding_key(view["binding"])
            and intent["context"] == command["context"] == view["binding"]["context"]
            and integer(intent["id"]) > 0 and integer(command["id"]) > 0
            and integer(intent["operation"]) == integer(command["operation"]) == 4
            and integer(intent["rounds"]) == 1 and intent["original"] is None
            and command["original"] is None and command["mutationRequired"] is True,
            "pending operation is not the exact shared InsertRounds transaction")
    require(request["discard"] is False and integer(proof["inputSequence"]) > 0
            and integer(proof["snapshotSequence"]) == integer(command["sourceSequence"])
            and 0 < integer(intent["observedNs"]) <= integer(proof["constructedNs"]) < integer(intent["deadlineNs"])
            and integer(intent["deadlineNs"])-integer(intent["observedNs"]) <= 100_000_000
            and integer(command["requestedNs"]) == integer(proof["constructedNs"])
            and integer(proof["snapshotObservedNs"]) <= integer(proof["constructedNs"]) < integer(proof["snapshotDeadlineNs"]),
            "pending transaction lost original construction evidence")
    before, after = counts(command["before"]), counts(command["after"])
    require(before["loaded"] == 0 and before["reserve"] > 0 and after == dict(loaded=1, reserve=before["reserve"]-1, capacity=1),
            "request is not one exact bounded InsertRounds transfer")
    window(request["binding"], integer(proof["constructedNs"]), False)


def pending_request(snapshot):
    x, view = snapshot["fixedContainer"], snapshot["fixedContainer"]["view"]
    request = x["pendingRequest"]
    if request is None:
        return
    request_proof(request, view)
    command = request["preparedCommand"]
    require(view["command"] is None or view["command"] == command, "pending shared ledger command changed")


def blocked_observation(snapshot, now):
    x = extension(snapshot, now)
    require(snapshot.get("feed") is None and x["tracking"]["blocksFire"] is True,
            "unrelated weapon interlock cannot authorize fixed-container motion")
    ammo(snapshot, now); pending_request(snapshot)
    return True


def admit(snapshot, now, nonce):
    frame.admit_frame(snapshot, now, nonce, mechanism_admission=blocked_observation)
    x = extension(snapshot, now)
    require(snapshot.get("feed") is None and snapshot["weapon_actions_blocked"] == x["tracking"]["blocksFire"],
            "competing weapon mechanism or interlock")
    for h in snapshot["input"]["hands"]:
        require(finite(h["trigger"]) == finite(h["stickX"]) == finite(h["stickY"]) == 0
                and integer(h["held"]) == 0, "unrelated controller input is active")
    ammo(snapshot, now); pending_request(snapshot)
    return x


def preserved(snapshot):
    i, m = snapshot["input"], snapshot["mapping"]
    return dict(referenceHead=i["referenceHead"], head=i["head"], right=i["hands"][1],
                spaceGeneration=i["spaceGeneration"], trackingBasisGeneration=i["trackingBasisGeneration"],
                worldUnitsPerMeter=i["worldUnitsPerMeter"], floorRelative=i["floorRelative"],
                trackingBodyFromWeapon=frame.multiply(frame.rigid(m["weaponWorldMeters"]),
                    frame.inverse(frame.rigid(m["trackingBodyWorldMeters"]))),
                anatomicalWristToGrip=m["anatomicalWristToGrip"])


def require_preserved(before, after):
    relative = "trackingBodyFromWeapon"
    require({k: v for k, v in before.items() if k != relative}
            == {k: v for k, v in after.items() if k != relative},
            "fixed-container head/right/controller basis changed; replan")
    # Match the shared transport's W * inverse(B) admission: common world
    # motion cancels without relaxing independent weapon/body drift limits.
    position, angle = frame.pose_error(frame.rigid(before[relative]), frame.rigid(after[relative]))
    require(position <= .002 and angle <= .005, "fixed-container relative weapon/body basis changed; replan")


def selection(snapshot):
    return dict(owner=snapshot["owner"], equipment=snapshot["equipment"], rig=snapshot["rig"],
                physical=snapshot["physical_equipment_generation"], profile=snapshot["fixedContainer"]["profile"])


def held(snapshot, now, kind):
    x = snapshot["fixedContainer"]
    t, raw, p = x["tracking"], x["raw"], x["profile"]
    token = claim(t["hand"], raw["input"], now, 0, kind)
    require(x["leftClaim"] == t["hand"] and raw["grip"] is True and t["grip"] is True
            and finite(snapshot["input"]["hands"][0]["squeeze"]) >= .7
            and raw["input"]["released"][0] is False and x["input"]["released"][0] is False,
            "left custody is not continuously held by this loader")
    require(token["contact"] == dict(id=p["geometry"][integer(t["step"])]["contact"]["role"], generation=p["revision"]),
            "held contact profile changed")
    if kind == 5:
        require(t["carried"] is True and t["carriedSupply"] is not None and token["item"] == x["supply"]["resource"]
                and integer(token["prerequisiteClaim"]) == 0, "missing actual acquired reserve custody")
    else:
        require(t["carried"] is False and token["item"] == t["gun"]["token"]["item"]
                and token["prerequisiteClaim"] == t["gun"]["token"]["id"], "mechanism claim lost its GunHold root")
    return token


def reobserve(previous, snapshot, now, nonce):
    x = admit(snapshot, now, nonce)
    require(previous["schema"] == SCHEMA and previous["selection"] == selection(snapshot), "owner/profile changed; replan")
    require_preserved(previous["preserve"], preserved(snapshot))
    baseline, t = previous["baseline"], x["tracking"]
    require(integer(snapshot["nonce"]) != integer(previous["snapshotNonce"])
            and integer(snapshot["observed_ns"]) > integer(previous["evaluatedAtNs"])
            and integer(snapshot["input"]["generation"]) > integer(previous["sourceInputSequence"]),
            "gate requires a newer original observation")
    require(t["lifetime"] == baseline["lifetime"] and integer(t["revision"]) > integer(baseline["revision"])
            and x["supply"] == baseline["supply"]
            and binding_key(x["view"]["binding"]) == baseline["binding"]
            and x["view"]["identity"] == baseline["nativeIdentity"], "loader/native lifetime or supply changed")
    return x


def check_gate(previous, snapshot, gate, now_ns, expected_nonce):
    now = integer(now_ns)
    x = reobserve(previous, snapshot, now, integer(expected_nonce))
    b, t, v = previous["baseline"], x["tracking"], ammo(snapshot, now, idle=True)
    stage, p = previous["stage"], x["profile"]
    require(gate in {g["id"] for g in previous["receiptGates"]}, "gate was not declared by this plan")
    token = None
    if gate in ("fixed_acquired", "fixed_held", "fixed_aligned"):
        require(integer(t["step"]) == STEP[stage] and v["snapshot"]["counts"] == b["counts"], "step or ammunition changed before completion")
        token = held(snapshot, now, 5 if stage in ("Pickup", "Insert") else 4)
        require(t["target"] is not None, "recognizer has no actual acquired target")
        if gate == "fixed_held":
            require(x["raw"]["pouchContact"] is True and t["pouchContact"] is True,
                    "acquired reserve lacks actual body pouch contact")
        if stage == "Insert":
            require(token == b["claim"], "acquired reserve claim changed during insertion")
        if gate == "fixed_aligned":
            motion = p["geometry"][2]["motion"]
            target = frame.rigid(motion["placement"]["weaponFromDestination"])
            for value in (t["target"], t["carriedSupply"]):
                error = frame.pose_error(frame.rigid(value), target)
                require(error[0] <= finite(motion["policy"]["pathToleranceMeters"])*.8
                        and error[1] <= finite(motion["policy"]["orientationToleranceRadians"])*.8,
                        "actual carried round is outside destination")
    elif gate == "fixed_inserted":
        require(stage == "Insert" and integer(t["step"]) == (3 if integer(p["kind"]) == 3 else 4)
                and t["carried"] is False and t["hand"] is None and x["pendingRequest"] is None,
                "insertion has not consumed its physical transaction")
        event = x["lastCompletion"]
        require(isinstance(event, dict) and event["lifetime"] == b["lifetime"] and event["supply"] == b["supply"],
                "missing same-lifetime completed native transaction")
        out = event["outcome"]
        request, receipt, post = out["request"], out["receipt"], event["postSnapshot"]
        request_proof(request, v)
        require(integer(out["state"]) == 3 and out["nativeDispatched"] is True and isinstance(receipt, dict)
                and out["actorRetirement"] is None and integer(request["intent"]["id"]) > integer(b["lastCompletedRequest"])
                and binding_key(request["binding"]) == b["binding"]
                and request["preparedCommand"] == receipt["command"], "no new exact Completed native request/receipt")
        command = receipt["command"]
        before = counts(command["before"])
        after = dict(loaded=before["loaded"]+1, reserve=before["reserve"]-1, capacity=1)
        require(before == b["counts"] and before["loaded"] == 0 and before["reserve"] > 0
                and command["after"] == receipt["after"] == post["counts"] == v["snapshot"]["counts"] == after
                and receipt["before"] == before and integer(command["operation"]) == 4
                and command["context"] == post["context"] == v["binding"]["context"]
                and integer(command["sourceSequence"]) >= integer(b["ammoSequence"])
                and integer(command["requestedNs"]) > integer(previous["evaluatedAtNs"])
                and receipt["authorityVerified"] is True and receipt["copiesVerified"] is True
                and integer(receipt["authorityInvocation"]) > 0, "native insertion receipt/copies/counts mismatch")
        require(integer(command["requestedNs"]) <= integer(receipt["beganNs"]) <= integer(receipt["completedNs"])
                <= integer(out["resolvedNs"]) <= integer(event["acceptedNs"]) <= now
                and integer(post["sequence"]) > integer(command["sourceSequence"])
                and integer(post["observedNs"]) >= integer(receipt["completedNs"])
                and integer(post["observedNs"]) <= integer(event["acceptedNs"]) < integer(post["deadlineNs"])
                and integer(v["snapshot"]["sequence"]) >= integer(post["sequence"])
                and post["coherentIdleVerified"] is True, "completion did not retain a valid original post-snapshot")
    else:
        require(gate == "fixed_step_complete" and stage in ("Open", "Close")
                and integer(t["step"]) == (2 if stage == "Open" else 4)
                and t["hand"] is None and t["carried"] is False
                and v["snapshot"]["counts"] == b["counts"], "physical opening/closure has not completed")
        if stage == "Close":
            require(t["blocksFire"] is False and snapshot["weapon_actions_blocked"] is False, "closure did not clear its exact interlock")
    return dict(gate=gate, observed=True, snapshotNonce=snapshot["nonce"], observedNs=snapshot["observed_ns"],
                inputObservedNs=x["raw"]["input"]["observedNs"], sourceValidUntilNs=snapshot["valid_until_ns"],
                claimToken=token, nativeAcceptance=False, headsetAcceptance=False)


def plan(snapshot, stage, now_ns, expected_nonce, step_ns=40_000_000, pickup_plan=None):
    require(stage in STAGES, "unsupported fixed-container stage")
    now, step_ns = integer(now_ns), integer(step_ns)
    x = admit(snapshot, now, integer(expected_nonce))
    p, t, v = x["profile"], x["tracking"], ammo(snapshot, now, idle=True)
    require(integer(t["step"]) == STEP[stage], "stage does not match actual retained loader step")
    require(integer(p["kind"]) == 3 or stage in ("Pickup", "Insert"), "muzzle profile has no opening/closing stage")
    geometry = p["geometry"][STEP[stage]]
    motion, policy = geometry["motion"], geometry["motion"]["policy"]
    require(motion["measured"] is True and motion["returnToStart"] is False, "unsupported/unmeasured production motion")
    maximum, gap, dwell = (integer(policy[k]) for k in ("maximumGestureNs", "maxInputGapNs", "endpointDwellNs"))
    max_pos, max_ang = finite(policy["maxStepMeters"]), finite(policy["maxStepRadians"])
    require(0 < gap <= 100_000_000 and 25_000_000 <= step_ns <= gap//2
            and 0 < dwell < maximum <= 10_000_000_000 and 0 < max_pos <= .2 and 0 < max_ang <= 1.5,
            "unbounded or unexecutable production motion policy")
    m, inp = snapshot["mapping"], snapshot["input"]
    w, body, raw, calibration = [frame.rigid(m[k]) for k in
        ("weaponWorldMeters", "trackingBodyWorldMeters", "rawLeftWristWorldMeters", "anatomicalWristToGrip")]
    reference, absolute = frame.from_pose(inp["referenceHead"]), frame.from_pose(inp["hands"][0]["grip"])
    def forward(grip):
        return frame.multiply(frame.multiply(calibration, frame.multiply(grip, frame.inverse(reference))), body)
    def control(local):
        return frame.multiply(frame.multiply(frame.multiply(frame.inverse(calibration), frame.multiply(local, w)), frame.inverse(body)), reference)
    require(all(e <= cap for e, cap in zip(frame.pose_error(forward(absolute), raw), (.002, .005))), "anatomical mapping does not round trip")
    current = frame.multiply(raw, frame.inverse(w))
    rows, gates = [], []
    def append(phase, grip, target):
        require(len(rows) < 120, "trajectory exceeds shared command budget")
        rows.append((phase, grip, target))
    def curve(phase, grip, function, objects=None):
        count = 1
        while count <= 1024:
            path = [function(i/count) for i in range(count+1)]
            paths = [path, [control(value) for value in path]]
            if objects is not None:
                paths.append([objects(i/count) for i in range(count+1)])
            if all(d <= max_pos*.7 and a <= max_ang*.7 for values in paths
                   for d, a in (frame.pose_error(a, b) for a, b in zip(values, values[1:]))):
                for value in path[1:]:
                    append(phase, grip, value)
                return
            count *= 2
        raise Refusal("motion exceeds bounded subdivision")
    def move(phase, grip, target):
        start = rows[-1][2]
        curve(phase, grip, lambda fraction: frame.blend(start, target, fraction))
    def gate(name, span=0):
        gates.append(dict(id=name, afterSample=len(rows)-1, holdGrip=rows[-1][1], stop=True,
            requiresFreshSnapshot=True, timeoutNs=2_000_000_000, countsTowardGestureBudget=True,
            requireContinuousFreshInput=True, maxInputGapNs=gap, minimumObservationSpanNs=span))
    baseline = dict(lifetime=t["lifetime"], revision=t["revision"], supply=x["supply"], binding=binding_key(v["binding"]), nativeIdentity=v["identity"],
                    counts=v["snapshot"]["counts"], ammoSequence=v["snapshot"]["sequence"],
                    lastCompletedRequest=((x["lastCompletion"] or {}).get("outcome", {}).get("request", {}).get("intent", {}).get("id", 0)))
    if stage == "Insert":
        require(pickup_plan is not None and pickup_plan["stage"] == "Pickup", "insertion requires this invocation's actual pickup")
        check_gate(pickup_plan, snapshot, "fixed_held", now, expected_nonce)
        baseline["claim"] = held(snapshot, now, 5)
        require(motion["kind"] == "FreePlacement", "insertion requires measured free placement")
        placement = motion["placement"]
        source, target, grip = (frame.rigid(t["carriedSupply"]), frame.rigid(placement["weaponFromDestination"]), frame.rigid(placement["sourceFromGrip"]))
        require(all(e <= cap for e, cap in zip(frame.pose_error(current, frame.multiply(grip, source)),
            (finite(policy["pathToleranceMeters"]), finite(policy["orientationToleranceRadians"])))), "actual pickup/source transform disagrees")
        append("continue_held", 1, current)
        object_curve = lambda fraction: frame.blend(source, target, fraction)
        source_from_actual = frame.multiply(current, frame.inverse(source))
        curve("insert", 1, lambda fraction: frame.multiply(source_from_actual, object_curve(fraction)), object_curve)
        gate("fixed_aligned", dwell)
        append("release_at_destination", 0, rows[-1][2])
        gate("fixed_inserted")
    else:
        require(pickup_plan is None and x["leftClaim"] is None and t["hand"] is None
                and t["carried"] is False and t["target"] is None and t["carriedSupply"] is None,
                "stage requires neutral free left hand with no partial recovery")
        append("neutral", 0, current)
        if stage == "Pickup":
            require(v["snapshot"]["counts"]["loaded"] == 0 and v["snapshot"]["counts"]["reserve"] > 0, "pickup needs native reserve and empty chamber")
            pouch = x["pouch"]
            require(integer(pouch["frame"]) == 1 and .03 <= finite(pouch["radiusMeters"]) <= .35
                    and len(pouch["centerMeters"]) == 3, "missing actual recentered body supply contact")
            body_frame = frame.supply_body_frame(inp)
            body_grip = frame.multiply(absolute, frame.inverse(body_frame))
            body_grip[3][:3] = [finite(value) for value in pouch["centerMeters"]]
            start = frame.multiply(forward(frame.multiply(body_grip, body_frame)), frame.inverse(w))
        else:
            require(motion["kind"] in ("Hinge", "Linear"), "opening/closure requires measured hinge or rail")
            require(stage != "Open" or v["snapshot"]["counts"]["loaded"] == 0, "bounded reload requires an observed empty chamber")
            contact, basis = frame.rigid(motion["motionFromContact"]), frame.rigid(motion["weaponFromMotion"])
            travel = finite(motion["travel"])
            require(abs(travel) > .001, "zero motion travel")
            start = frame.multiply(contact, basis)
        move("approach", 0, start)
        append("neutral_at_contact", 0, start)
        append("acquire", 1, start)
        if stage == "Pickup":
            gate("fixed_held")
        else:
            gate("fixed_acquired")
            def mechanical(fraction):
                if motion["kind"] == "Hinge":
                    moved = frame.along(contact, travel*fraction)
                else:
                    moved = [row[:] for row in contact]
                    moved[3][0] += travel*fraction
                return frame.multiply(moved, basis)
            curve("move", 1, mechanical)
            gate("fixed_step_complete")
    schedule = []
    previous_control, previous_wrist = absolute, raw
    for index, (phase, squeeze, target) in enumerate(rows):
        pose = frame.to_pose(control(target))
        values = [float(f"{v:.9f}") for v in pose["position"]+frame.quaternion_ypr(pose["orientation"])]
        emitted = frame.from_pose(dict(position=values[:3], orientation=frame.ypr_quaternion(values[3:])))
        wrist = forward(emitted)
        require(all(e < cap for e, cap in zip(frame.pose_error(wrist, frame.multiply(target, w)), (.0005, .005))), "formatted command round trip failed")
        for a, b in ((previous_control, emitted), (previous_wrist, wrist)):
            delta = frame.pose_error(a, b)
            require(delta[0] < max_pos and delta[1] < max_ang, "formatted controller/wrist motion exceeds native caps")
        coordinates = " ".join(f"{v:.9f}" for v in values)
        commands = ["hand l grip pose "+coordinates, "hand l aim pose "+coordinates, f"grip l {squeeze}"]
        if index == 0:
            commands = ["hand l follow off", "hand l aimtrim 0 0"]+commands
        schedule.append(dict(atNs=index*step_ns, phase=phase, grip=squeeze, positionMeters=values[:3], yawPitchRollDegrees=values[3:], commands=commands))
        previous_control, previous_wrist = emitted, wrist
    duration = schedule[-1]["atNs"]
    require(duration < maximum, "motion exceeds native gesture duration")
    leases = [snapshot["valid_until_ns"], snapshot["native_deadline_ns"], x["input"]["deadlineNs"],
              x["raw"]["input"]["deadlineNs"], v["snapshot"]["deadlineNs"], v["binding"]["deadlineNs"], t["gun"]["deadlineNs"]]
    if t["hand"] is not None:
        leases.append(t["hand"]["deadlineNs"])
    return dict(schema=SCHEMA, offlineOnly=True, executionAuthorized=False, nativeAcceptance=False, headsetAcceptance=False,
        stage=stage, snapshotNonce=snapshot["nonce"], sourceInputSequence=inp["generation"], evaluatedAtNs=now,
        sourceValidUntilNs=min(integer(v) for v in leases), originalLeases=leases, selection=selection(snapshot), preserve=preserved(snapshot),
        baseline=baseline, policy=policy, durationNs=duration, receiptGates=gates, schedule=schedule,
        verification=dict(everyFormattedCommandRoundTripped=True), requiresIndependentAllThreePathAudit=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--stage", choices=STAGES, required=True)
    parser.add_argument("--nonce", type=int, required=True)
    parser.add_argument("--now-ns", type=int)
    parser.add_argument("--step-ms", type=int, default=40)
    parser.add_argument("--pickup-plan", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        require(not args.output.exists() and args.output.suffix == ".json"
                and args.output.name not in {"state.json", "command.json", "mechanism-snapshot.json"}, "output must be a new offline artifact")
        snapshot, digest = frame.load_json(args.snapshot, frame.SNAPSHOT_LIMIT)
        pickup = frame.load_json(args.pickup_plan, 256*1024)[0] if args.pickup_plan else None
        result = plan(snapshot, args.stage, time.perf_counter_ns() if args.now_ns is None else args.now_ns,
                      args.nonce, args.step_ms*1_000_000, pickup)
        result["sourceHashes"] = dict(snapshotJson=digest, planner=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        with args.output.open("x", encoding="utf-8") as output:
            json.dump(result, output, allow_nan=False, separators=(",", ":"))
        return 0
    except (Exception, KeyboardInterrupt) as error:
        print(json.dumps(dict(status="refused", error=str(error), nativeAcceptance=False)))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
