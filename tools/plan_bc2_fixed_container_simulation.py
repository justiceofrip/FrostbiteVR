"""Offline single-round fixed-container motions from original native observations.

Uses the shared frame algebra/admission. Plans never execute commands or renew
leases. Actual custody and exact native receipts are mandatory at stop gates.
"""
import argparse
import copy
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
STAGES = ("Open", "EjectSpent", "Pickup", "Insert", "Close")
SCHEMA = "bc2vr.fixed-container-simulation-plan.v1"
OBSERVATION_SCHEMA = "bc2vr.fixed-container-observation.v1"
STEP = {"Open": 0, "EjectSpent": 1, "Pickup": 2, "Insert": 2, "Close": 3}


def window(value, now, sequence=True):
    require(isinstance(value, dict), "missing original lease")
    if sequence:
        require(integer(value["sequence"]) > 0, "missing original sequence")
    start, end = integer(value["observedNs"]), integer(value["deadlineNs"])
    require(0 < start <= now < end and 0 < end-start <= 100_000_000, "expired or unbounded original lease")


def key(value):
    require(isinstance(value, dict) and all(integer(value[k]) > 0 for k in ("id", "generation")), "missing resource key")
    return value


def physical_item(snapshot, now):
    x, owner = snapshot["fixedContainer"], snapshot["owner"]
    proof = x.get("physicalFamily")
    item = integer(owner["weapon"])
    if proof is not None:
        b = x["view"]["binding"]
        start, end = integer(proof["observedNs"]), integer(proof["deadlineNs"])
        item = integer(proof["physicalItem"])
        require(proof["current"] is True and proof["owner"] == owner and proof["selectedSecondary"] is True
                and 0 < start <= now < end and 0 < end-start <= 500_000_000
                and item == integer(b["persistence"]) == integer(snapshot["equipment"]["persistence"])
                and item > 0 and item != integer(owner["weapon"])
                and integer(proof["inventory"]) == integer(b["inventory"]) == integer(snapshot["selected"]["inventory"])
                and integer(proof["targetWeapon"]) > 0 and integer(proof["targetWeapon"]) not in (item, integer(owner["weapon"]))
                and 0 <= integer(proof["targetSlot"]) < 9
                and integer(proof["action"]) == integer(proof["primaryAction"]) > 0
                and proof["configurationPath"] == x["profile"]["configurationPath"] == snapshot["selected"]["configurationPath"]
                and proof["asset"] == snapshot["equipment"]["asset"] and bool(proof["persistentId"]),
                "missing exact current selected-secondary physical family")
    return dict(id=item, generation=integer(snapshot["physical_equipment_generation"]))


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


def extension(snapshot, now, *, completion=None):
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
            and integer(t["step"]) in (0, 1, 2, 3, 4), "missing retained fixed-container lifetime/step")
    require(all(raw[k] is True for k in ("valid", "current", "ownerMatches", "rigMatches", "profileMatches"))
            and integer(raw["rigFingerprint"]) == integer(p["rigFingerprint"]), "missing original raw fixed-container contact")
    frame.evidence(raw["input"], snapshot["owner"], integer(snapshot["physical_equipment_generation"]), now)
    require(integer(raw["input"]["sequence"]) <= integer(source["sequence"])
            and integer(raw["input"]["observedNs"]) <= integer(source["observedNs"]), "raw contact is newer than its publication")
    frame.rigid(raw["weaponFromWrist"]); frame.rigid(raw["weaponWorldMeters"])
    gun = claim(t["gun"], source, now, 1, 1)
    require(gun["item"] == physical_item(snapshot, now)
            and integer(gun["prerequisiteClaim"]) == 0 and source["released"][1] is False, "missing exact right GunHold root")
    key(x["supply"]["resource"])
    require((t["pending"] is True) == (x["pendingRequest"] is not None), "pending transaction observation disagrees")
    for field in ("target", "carriedSupply"):
        if t[field] is not None:
            frame.rigid(t[field])
    if raw["admittedAtTick"] is not True:
        require(raw["admittedAtTick"] is False and completion is not None, "missing original raw fixed-container contact")
        # PhysicalStep and insertion dispatch release their claim/evidence.
        # This admits only a terminal check (or exact pending receipt wait),
        # never another motion.
        previous, acquired = completion
        require_reobserved(previous, snapshot)
        require_acquired(previous, acquired, snapshot)
        if previous["stage"] == "Insert":
            require_insertion_observation(previous, snapshot, now, acquired)
        else:
            require_physical_completion(previous, snapshot, now, acquired=acquired)
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


def blocked_observation(snapshot, now, *, completion=None):
    x = extension(snapshot, now, completion=completion)
    require(snapshot.get("feed") is None and x["tracking"]["blocksFire"] is True,
            "unrelated weapon interlock cannot authorize fixed-container motion")
    ammo(snapshot, now); pending_request(snapshot)
    return True


def admit(snapshot, now, nonce, *, completion=None):
    frame.admit_frame(snapshot, now, nonce, mechanism_admission=lambda original, evaluated:
        blocked_observation(original, evaluated, completion=completion))
    x = extension(snapshot, now, completion=completion)
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
    require(frame.preserved_equal({k: v for k, v in before.items() if k != relative},
                                 {k: v for k, v in after.items() if k != relative},
                                 pose_keys=("head", "referenceHead"), hand_keys=("right",)),
            "fixed-container head/right/controller basis changed; replan")
    # Match the shared transport's W * inverse(B) admission: common world
    # motion cancels without relaxing independent weapon/body drift limits.
    position, angle = frame.pose_error(frame.rigid(before[relative]), frame.rigid(after[relative]))
    require(position <= .002 and angle <= .005, "fixed-container relative weapon/body basis changed; replan")


def selection(snapshot):
    proof = snapshot["fixedContainer"].get("physicalFamily")
    family = None if proof is None else {k: v for k, v in proof.items() if k not in ("observedNs", "deadlineNs", "current")}
    return dict(owner=snapshot["owner"], equipment=snapshot["equipment"], rig=snapshot["rig"], physicalFamily=family,
                physical=snapshot["physical_equipment_generation"], profile=snapshot["fixedContainer"]["profile"])


def held(snapshot, now, kind):
    x = snapshot["fixedContainer"]
    t, raw, p = x["tracking"], x["raw"], x["profile"]
    require(raw["admittedAtTick"] is True, "missing original raw fixed-container contact")
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


def case_state(snapshot):
    t = snapshot["fixedContainer"]["tracking"]
    names = ("spentCasePresent", "spentCaseEpoch", "spentCaseEjectedNs", "spentCaseEjectionWorldMeters")
    require(all(name in t for name in names), "missing published fixed-container spent-case state")
    require(type(t["spentCasePresent"]) is bool and integer(t["spentCaseEpoch"]) >= 0
            and integer(t["spentCaseEjectedNs"]) >= 0
            and (not t["spentCasePresent"] or integer(t["spentCaseEpoch"]) > 0),
            "invalid published spent-case state")
    return {name: copy.deepcopy(t[name]) for name in names}


def case_geometry(snapshot):
    t = snapshot["fixedContainer"]["tracking"]
    require(t.get("spentCasePoseWeaponMeters") is not None and isinstance(t.get("spentCasePolicy"), dict),
            "missing published spentCasePoseWeaponMeters/spentCasePolicy")
    policy = t["spentCasePolicy"]
    require(policy.get("modOwned") is True and policy.get("outwardAxis") == [0, 0, 1]
            and finite(policy["maximumWorldUpDot"]) == -.5
            and integer(policy["dwellNs"]) == 100_000_000
            and integer(policy["maxInputGapNs"]) == 100_000_000,
            "unsupported published mod spent-case tilt policy")
    return frame.rigid(t["spentCasePoseWeaponMeters"]), policy


def tilt_mapping(snapshot):
    """Observe the rigid right grip/weapon relationship; no invented bore axis."""
    inp, mapping = snapshot["input"], snapshot["mapping"]
    right = frame.from_pose(inp["hands"][1]["grip"])
    world = frame.multiply(frame.multiply(right, frame.inverse(frame.from_pose(inp["referenceHead"]))),
                           frame.rigid(mapping["trackingBodyWorldMeters"]))
    kept = preserved(snapshot)
    kept.pop("trackingBodyFromWeapon")
    kept["right"] = {k: v for k, v in inp["hands"][1].items() if k not in ("grip", "aim")}
    kept["left"] = inp["hands"][0]
    return dict(preserve=kept, weaponFromRight=frame.multiply(frame.rigid(mapping["weaponWorldMeters"]), frame.inverse(world)),
                aimFromGrip=frame.multiply(frame.from_pose(inp["hands"][1]["aim"]), frame.inverse(right)))


def require_tilt_mapping(before, after):
    require(frame.preserved_equal(before["preserve"], after["preserve"],
            pose_keys=("head", "referenceHead"), hand_keys=("left",)),
            "spent-case tilt changed head/left hand/controls/reference")
    for name in ("weaponFromRight", "aimFromGrip"):
        d, a = frame.pose_error(frame.rigid(before[name]), frame.rigid(after[name]))
        require(d <= .002 and a <= .005, "spent-case tilt changed measured right/weapon mapping")


def case_neutral(snapshot):
    x = snapshot["fixedContainer"]
    t = x["tracking"]
    require(x["leftClaim"] is None and t["hand"] is None and t["target"] is None
            and t["carried"] is False and t["carriedSupply"] is None and t["grip"] is False
            and x["raw"]["grip"] is False and snapshot["input"]["hands"][0]["squeeze"] == 0
            and all(e["released"][0] is True for e in (x["raw"]["input"], x["input"], snapshot["input_evidence"])),
            "spent-case tilt requires fresh neutral unclaimed left input")
    d, a = frame.pose_error(frame.rigid(x["raw"]["weaponWorldMeters"]), frame.rigid(snapshot["mapping"]["weaponWorldMeters"]))
    require(d <= .002 and a <= .005, "spent-case raw weapon frame differs from current right mapping")


def require_case_observation(previous, snapshot, now, *, complete=False):
    x, baseline = snapshot["fixedContainer"], previous["baseline"]
    t, raw = x["tracking"], x["raw"]["input"]
    case_neutral(snapshot)
    state = case_state(snapshot)
    pose, policy = case_geometry(snapshot)
    d, a = frame.pose_error(pose, previous["casePoseWeaponMeters"])
    require(d <= .002 and a <= .005 and policy == previous["casePolicy"], "published case frame/policy changed")
    require(integer(raw["sequence"]) > integer(baseline["rawInputSequence"])
            and integer(raw["observedNs"]) > integer(baseline["rawObservedNs"]),
            "spent-case tilt requires advancing original raw input")
    require(ammo(snapshot, now, idle=True)["snapshot"]["counts"] == baseline["counts"]
            and ((x["lastCompletion"] or {}).get("outcome", {}).get("request", {}).get("intent", {}).get("id", 0))
                == baseline["lastCompletedRequest"], "spent-case tilt changed ammunition/transaction")
    require(state["spentCaseEpoch"] == baseline["case"]["spentCaseEpoch"], "spent-case epoch changed")
    if integer(t["step"]) == 1:
        require(state == baseline["case"], "spent-case debt/event changed before completion")
        require(not complete, "mod spent-case ejection not yet observed")
        return False
    require(integer(t["step"]) == 2 and state["spentCasePresent"] is False
            and integer(baseline["rawObservedNs"]) < integer(state["spentCaseEjectedNs"]) <= integer(raw["observedNs"])
            and integer(state["spentCaseEjectedNs"]) > integer(baseline["case"]["spentCaseEjectedNs"]),
            "missing new same-case Insert/ejection observation")
    ejected = frame.rigid(state["spentCaseEjectionWorldMeters"])
    require(ejected[2][1] <= finite(policy["maximumWorldUpDot"]), "case ejection frame lacks authored downward tilt")
    require(t["blocksFire"] is True and snapshot["weapon_actions_blocked"] is True,
            "case ejection unexpectedly cleared insertion interlock")
    return True


def require_reobserved(previous, snapshot):
    x = snapshot["fixedContainer"]
    require(previous["schema"] == SCHEMA and previous["selection"] == selection(snapshot), "owner/profile changed; replan")
    if previous["stage"] == "EjectSpent":
        require_tilt_mapping(previous["tiltMapping"], tilt_mapping(snapshot))
    else:
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
    require(t["gun"]["token"] == baseline["gun"], "fixed-container GunHold root changed")


def completion_context(previous, gate, acquired):
    if gate == "fixed_inserted":
        # Existing strict receipt checks remain usable without the optional
        # read-only context. Released raw input additionally needs alignment.
        if acquired is None:
            return None
        require(previous["stage"] == "Insert" and isinstance(acquired, dict)
                and acquired.get("gate") == "fixed_aligned" and acquired.get("observed") is True,
                "insertion completion requires this invocation's fixed alignment")
        return previous, acquired
    if gate != "fixed_step_complete":
        return None
    require(previous["stage"] in ("Open", "Close") and isinstance(acquired, dict)
            and acquired.get("gate") == "fixed_acquired" and acquired.get("observed") is True,
            "physical completion requires this invocation's fixed acquisition")
    return previous, acquired


def require_acquired(previous, acquired, snapshot):
    insertion = previous["stage"] == "Insert"
    completion_context(previous, "fixed_inserted" if insertion else "fixed_step_complete", acquired)
    proof, token = acquired.get("completionPrerequisite"), acquired.get("claimToken")
    x, baseline = snapshot["fixedContainer"], previous["baseline"]
    t, profile = x["tracking"], x["profile"]
    require(isinstance(proof, dict) and isinstance(token, dict)
            and proof["planSourceNonce"] == previous["snapshotNonce"]
            and proof["planEvaluatedAtNs"] == previous["evaluatedAtNs"] and proof["stage"] == previous["stage"]
            and proof["lifetime"] == baseline["lifetime"] == t["lifetime"]
            and proof["gunToken"] == baseline["gun"] == t["gun"]["token"],
            "physical completion acquisition belongs to another plan/root/lifetime")
    gun = proof["gunToken"]
    require(integer(token["id"]) > 0 and token["owner"] == x["input"]["owner"]
            and integer(token["hand"]) == 0 and integer(token["kind"]) == (5 if insertion else 4)
            and token["item"] == (x["supply"]["resource"] if insertion else gun["item"])
            and token["prerequisiteClaim"] == (0 if insertion else gun["id"])
            and (not insertion or token == baseline["claim"])
            and token["contact"] == dict(id=profile["geometry"][STEP[previous["stage"]]]["contact"]["role"],
                                         generation=profile["revision"]),
            "physical completion lacks its exact acquired mechanism claim")
    require(integer(snapshot["nonce"]) != integer(acquired["snapshotNonce"])
            and integer(previous["evaluatedAtNs"]) < integer(acquired["observedNs"]) < integer(snapshot["observed_ns"])
            and integer(acquired["inputObservedNs"]) <= integer(acquired["observedNs"]) < integer(acquired["sourceValidUntilNs"])
            and integer(t["revision"]) > integer(proof["revision"]) > integer(baseline["revision"])
            and integer(snapshot["input"]["generation"]) > integer(proof["inputSequence"]) > integer(previous["sourceInputSequence"])
            and integer(x["raw"]["input"]["sequence"]) > integer(proof["rawInputSequence"])
            and integer(x["raw"]["input"]["observedNs"]) > integer(acquired["inputObservedNs"]),
            "physical completion needs newer original input/revision after acquisition")


def require_physical_completion(previous, snapshot, now, *, acquired=None):
    x, stage = snapshot["fixedContainer"], previous["stage"]
    t, v = x["tracking"], ammo(snapshot, now, idle=True)
    next_step = (1 if previous["baseline"]["case"]["spentCasePresent"] else 2) if stage == "Open" else 4
    require(stage in ("Open", "Close") and integer(t["step"]) == next_step
            and t["carried"] is False and t["carriedSupply"] is None and t["pending"] is False and x["pendingRequest"] is None
            and v["snapshot"]["counts"] == previous["baseline"]["counts"],
            "physical opening/closure has not completed")
    if stage == "Open":
        require(case_state(snapshot) == previous["baseline"]["case"], "opening changed retained spent-case state")
    # The native recognizer advances its step at the endpoint but may keep
    # that exact mechanism grip magnetized until the player releases it.
    # Observe completion before authorizing the ordinary release; never demand
    # that the native adapter disconnect the hand first.
    if any(value is not None for value in (t["hand"], x["leftClaim"], t["target"])):
        require_acquired(previous, acquired, snapshot)
        raw = x["raw"]
        require(raw["admittedAtTick"] is True and t["target"] is not None
                and integer(t["targetStep"]) == STEP[stage]
                and t["hand"] == x["leftClaim"] and raw["grip"] is True and t["grip"] is True
                and finite(snapshot["input"]["hands"][0]["squeeze"]) >= .7
                and raw["input"]["released"][0] is False and x["input"]["released"][0] is False,
                "completed endpoint lacks its continuously held original mechanism")
        require(claim(t["hand"], raw["input"], now, 0, 4) == acquired["claimToken"],
                "completed endpoint mechanism claim changed")
        frame.rigid(t["target"])
    require(((x["lastCompletion"] or {}).get("outcome", {}).get("request", {}).get("intent", {}).get("id", 0))
            == previous["baseline"]["lastCompletedRequest"], "physical completion observed another ammunition transaction")
    if stage == "Close":
        require(t["blocksFire"] is False and snapshot["weapon_actions_blocked"] is False, "closure did not clear its exact interlock")


def require_inserted(previous, snapshot, now):
    x, b = snapshot["fixedContainer"], previous["baseline"]
    t, p, v = x["tracking"], x["profile"], ammo(snapshot, now, idle=True)
    require(previous["stage"] == "Insert" and integer(t["step"]) == (3 if integer(p["kind"]) == 3 else 4)
            and t["carried"] is False and t["carriedSupply"] is None and t["hand"] is None and x["leftClaim"] is None
            and t["target"] is None and t["pending"] is False and x["pendingRequest"] is None,
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


def require_insertion_observation(previous, snapshot, now, aligned):
    x, b = snapshot["fixedContainer"], previous["baseline"]
    t, v = x["tracking"], ammo(snapshot, now)
    request = x["pendingRequest"]
    if request is None:
        require_inserted(previous, snapshot, now)
        request = x["lastCompletion"]["outcome"]["request"]
    else:
        pending_request(snapshot)
        require(t["pending"] is True and integer(t["step"]) == STEP["Insert"]
                and t["hand"] is None and x["leftClaim"] is None and t["target"] is None
                and t["carried"] is True and t["carriedSupply"] is not None
                and v["snapshot"]["counts"] == b["counts"]
                and request["preparedCommand"]["before"] == b["counts"]
                and integer(request["preparedCommand"]["sourceSequence"]) >= integer(b["ammoSequence"])
                and integer(request["intent"]["id"]) > integer(b["lastCompletedRequest"]),
                "pending insertion lost its exact aligned transaction")
    require(integer(aligned["completionPrerequisite"]["rawInputSequence"]) < integer(request["sourceProof"]["inputSequence"])
            <= integer(x["raw"]["input"]["sequence"])
            and integer(aligned["inputObservedNs"]) < integer(request["intent"]["observedNs"])
            <= integer(x["raw"]["input"]["observedNs"])
            and integer(aligned["observedNs"]) < integer(request["preparedCommand"]["requestedNs"]) <= now,
            "insertion request predates this invocation's aligned round")


def reobserve(previous, snapshot, now, nonce, *, completion=None):
    x = admit(snapshot, now, nonce, completion=completion)
    require_reobserved(previous, snapshot)
    if completion is not None:
        require(completion[0] is previous, "physical completion plan changed")
        require_acquired(previous, completion[1], snapshot)
    return x


def check_gate(previous, snapshot, gate, now_ns, expected_nonce, *, acquired=None):
    now = integer(now_ns)
    completion = completion_context(previous, gate, acquired)
    x = reobserve(previous, snapshot, now, integer(expected_nonce), completion=completion)
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
    elif gate in ("fixed_tilt_current", "fixed_case_ejected"):
        ejected = require_case_observation(previous, snapshot, now, complete=gate == "fixed_case_ejected")
    elif gate == "fixed_inserted":
        require_inserted(previous, snapshot, now)
        if completion is not None:
            require_insertion_observation(previous, snapshot, now, acquired)
    else:
        require(gate == "fixed_step_complete", "not a physical completion gate")
        require_physical_completion(previous, snapshot, now, acquired=acquired)
    result = dict(gate=gate, observed=True, snapshotNonce=snapshot["nonce"], observedNs=snapshot["observed_ns"],
                inputObservedNs=x["raw"]["input"]["observedNs"], sourceValidUntilNs=snapshot["valid_until_ns"],
                claimToken=token, completionOnly=completion is not None, nativeAcceptance=False, headsetAcceptance=False)
    if stage == "EjectSpent":
        result.update(caseEjected=ejected, caseEpoch=t["spentCaseEpoch"], rawInputSequence=x["raw"]["input"]["sequence"],
                      modCaseObservation=True, nativeAmmunitionTransaction=False)
    if gate in ("fixed_acquired", "fixed_aligned"):
        result["completionPrerequisite"] = dict(planSourceNonce=previous["snapshotNonce"], planEvaluatedAtNs=previous["evaluatedAtNs"],
            stage=stage, lifetime=t["lifetime"], revision=t["revision"], inputSequence=snapshot["input"]["generation"],
            rawInputSequence=x["raw"]["input"]["sequence"], gunToken=t["gun"]["token"])
    return result


def plan_case_tilt(snapshot, now, step_ns):
    x, inp, m = snapshot["fixedContainer"], snapshot["input"], snapshot["mapping"]
    t, v = x["tracking"], ammo(snapshot, now, idle=True)
    case_neutral(snapshot)
    state, (local, case_policy) = case_state(snapshot), case_geometry(snapshot)
    require(state["spentCasePresent"] and v["snapshot"]["counts"]["loaded"] == 0
            and t["blocksFire"] is True and snapshot["weapon_actions_blocked"] is True,
            "EjectSpent requires actual open breech with retained spent-case debt")
    require(25_000_000 <= step_ns <= integer(case_policy["maxInputGapNs"])//2,
            "unbounded spent-case sample period")
    source_policy = x["profile"]["geometry"][2]["motion"]["policy"]
    policy = copy.deepcopy(source_policy)
    policy.update(maxStepRadians=min(finite(source_policy["maxStepRadians"]), .12),
                  endpointDwellNs=integer(case_policy["dwellNs"]), maxInputGapNs=integer(case_policy["maxInputGapNs"]))
    require(0 < policy["maxStepRadians"] <= .12 and 0 < integer(policy["maximumGestureNs"]) <= 10_000_000_000,
            "unbounded spent-case tilt policy")
    reference, body = frame.from_pose(inp["referenceHead"]), frame.rigid(m["trackingBodyWorldMeters"])
    grip = frame.from_pose(inp["hands"][1]["grip"])
    world_grip = frame.multiply(frame.multiply(grip, frame.inverse(reference)), body)
    case_world = frame.multiply(local, frame.rigid(x["raw"]["weaponWorldMeters"]))
    # Rotate the observed mod-authored +Z toward gravity, around the fixed
    # controller origin. No native barrel/ejector direction is inferred.
    outward = case_world[2][:3]
    dot = max(-1., min(1., -outward[1]))
    if dot < -.999999:
        quaternion = [1., 0., 0., 0.]
    else:
        quaternion = frame.normalize([outward[2], 0., -outward[0], 1.+dot])
    # from_pose consumes XR handedness, while this quaternion was solved in
    # the published row-vector game world.
    delta = frame.from_pose(dict(position=[0., 0., 0.],
                                 orientation=[-quaternion[0], -quaternion[1], quaternion[2], quaternion[3]]))
    # Clear the published threshold by 0.15, without turning farther toward
    # gravity than this ordinary input demonstration needs.
    toward_down = math.acos(dot)
    fraction = max(0., toward_down-math.acos(.65))/toward_down if toward_down > .00001 else 0.
    delta = frame.blend(frame.identity(), delta, fraction)
    target_world = frame.multiply(world_grip, delta)
    target_world[3][:3] = world_grip[3][:3]
    target = frame.multiply(frame.multiply(target_world, frame.inverse(body)), reference)
    aim_relative = frame.multiply(frame.from_pose(inp["hands"][1]["aim"]), frame.inverse(grip))
    distance, angle = frame.pose_error(grip, target)
    require(distance <= .00001, "spent-case tilt would translate right controller")
    count = max(1, math.ceil(angle/(policy["maxStepRadians"]*.7)))
    require(count <= 64, "spent-case tilt exceeds bounded ordinary samples")
    schedule = []
    previous = grip
    for index in range(count+1):
        right = frame.blend(grip, target, index/count)
        pose, aim = frame.to_pose(right), frame.to_pose(frame.multiply(aim_relative, right))
        values = [float(f"{n:.9f}") for n in pose["position"]+frame.quaternion_ypr(pose["orientation"])]
        aim_values = [float(f"{n:.9f}") for n in aim["position"]+frame.quaternion_ypr(aim["orientation"])]
        emitted = frame.from_pose(dict(position=values[:3], orientation=frame.ypr_quaternion(values[3:])))
        d, a = frame.pose_error(emitted, right)
        step_distance, step_angle = frame.pose_error(previous, emitted)
        require(d <= .0002 and a <= .001 and step_distance <= .0002 and step_angle < policy["maxStepRadians"],
                "formatted spent-case command exceeds original bounded tilt")
        previous = emitted
        commands = ["hand r grip pose "+" ".join(f"{n:.9f}" for n in values),
                    "hand r aim pose "+" ".join(f"{n:.9f}" for n in aim_values), "grip l 0"]
        if index == 0:
            commands.insert(0, "hand r follow off")
        schedule.append(dict(atNs=index*step_ns, phase="case_tilt", hand="r", grip=0,
            positionMeters=values[:3], yawPitchRollDegrees=values[3:],
            aimPositionMeters=aim_values[:3], aimYawPitchRollDegrees=aim_values[3:], commands=commands))
    require(schedule[-1]["atNs"]+case_policy["dwellNs"] < integer(policy["maximumGestureNs"]),
            "spent-case tilt exceeds original gesture budget")
    leases = [snapshot["valid_until_ns"], snapshot["native_deadline_ns"], x["input"]["deadlineNs"],
              x["raw"]["input"]["deadlineNs"], v["snapshot"]["deadlineNs"], v["binding"]["deadlineNs"], t["gun"]["deadlineNs"]]
    if x.get("physicalFamily") is not None:
        leases.append(x["physicalFamily"]["deadlineNs"])
    baseline = dict(lifetime=t["lifetime"], revision=t["revision"], supply=x["supply"], binding=binding_key(v["binding"]),
        nativeIdentity=v["identity"], gun=t["gun"]["token"], counts=v["snapshot"]["counts"], ammoSequence=v["snapshot"]["sequence"],
        case=state, rawInputSequence=x["raw"]["input"]["sequence"], rawObservedNs=x["raw"]["input"]["observedNs"],
        lastCompletedRequest=((x["lastCompletion"] or {}).get("outcome", {}).get("request", {}).get("intent", {}).get("id", 0)))
    gates = [dict(id=name, afterSample=index, holdGrip=0, timeoutNs=250_000_000 if index == 0 else 2_000_000_000,
             minimumObservationSpanNs=0, requiresFreshSnapshot=True) for name, index in
             (("fixed_tilt_current", 0), ("fixed_case_ejected", len(schedule)-1))]
    return dict(schema=SCHEMA, offlineOnly=True, executionAuthorized=False, nativeAcceptance=False, headsetAcceptance=False,
        stage="EjectSpent", snapshotNonce=snapshot["nonce"], sourceInputSequence=inp["generation"], evaluatedAtNs=now,
        sourceValidUntilNs=min(integer(value) for value in leases), originalLeases=leases, selection=selection(snapshot),
        preserve=preserved(snapshot), tiltMapping=tilt_mapping(snapshot), casePoseWeaponMeters=local, casePolicy=case_policy,
        baseline=baseline, policy=policy, durationNs=schedule[-1]["atNs"], receiptGates=gates, schedule=schedule)


def plan(snapshot, stage, now_ns, expected_nonce, step_ns=40_000_000, pickup_plan=None):
    require(stage in STAGES, "unsupported fixed-container stage")
    now, step_ns = integer(now_ns), integer(step_ns)
    x = admit(snapshot, now, integer(expected_nonce))
    p, t, v = x["profile"], x["tracking"], ammo(snapshot, now, idle=True)
    require(integer(t["step"]) == STEP[stage], "stage does not match actual retained loader step")
    require(integer(p["kind"]) == 3 or stage in ("Pickup", "Insert"), "muzzle profile has no opening/closing stage")
    if stage == "EjectSpent":
        require(pickup_plan is None, "spent-case ejection cannot consume a pickup")
        return plan_case_tilt(snapshot, now, step_ns)
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
                    gun=t["gun"]["token"],
                    counts=v["snapshot"]["counts"], ammoSequence=v["snapshot"]["sequence"],
                    lastCompletedRequest=((x["lastCompletion"] or {}).get("outcome", {}).get("request", {}).get("intent", {}).get("id", 0)))
    if stage == "Open":
        baseline["case"] = case_state(snapshot)
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
    if x.get("physicalFamily") is not None:
        leases.append(x["physicalFamily"]["deadlineNs"])
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
