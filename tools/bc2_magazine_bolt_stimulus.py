"""Shared observation contracts and geometry for ordinary magazine/bolt input.

No native writes, fixture imports, resource creation or timestamp renewal.
All matrices are the normal producer's row-vector, metre transforms.
"""
import copy
import importlib.util
import math
from pathlib import Path


def shared(name):
    for parent in Path(__file__).resolve().parents:
        path = parent / "tools" / (name + ".py")
        if path.is_file():
            spec = importlib.util.spec_from_file_location("magbolt_" + name, path)
            value = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(value)
            return value
    raise ImportError("shared tool missing: " + name)


frame = shared("plan_bc2_feed_simulation")
require, integer, Refusal = frame.require, frame.integer, frame.Refusal
mul, inv, rigid = frame.multiply, frame.inverse, frame.rigid
NS = 1_000_000_000


class Pending(Refusal):
    """Wait for evidence under the original boundary deadline, without motion."""


def wait_for(condition, reason):
    if not condition:
        raise Pending(reason)


def window(value, now, maximum=200_000_000):
    require(0 < integer(value["observedNs"]) <= now < integer(value["deadlineNs"])
            and integer(value["deadlineNs"]) - integer(value["observedNs"]) <= maximum,
            "expired or extended original observation lease")


def owner_key(snapshot):
    return {k: copy.deepcopy(snapshot[k]) for k in
            ("owner", "equipment", "rig", "physical_equipment_generation")} | {
        "selection": {k: snapshot["selected"][k] for k in
                      ("configurationPath", "inventory", "selectedSlot", "matchedMeshPath")}}


def same_input(a, b):
    return all(a[k] == b[k] for k in
               ("owner", "sequence", "observedNs", "deadlineNs", "focused", "tracked", "released"))


def source_deadline(snapshot, family=None):
    deadlines = [integer(snapshot[k]) for k in ("valid_until_ns", "native_deadline_ns")]
    deadlines.append(integer(snapshot["input_evidence"]["deadlineNs"]))
    if family is not None:
        x = snapshot[family]
        deadlines.append(integer(x["input"]["deadlineNs"]))
        raw = x.get("raw")
        if raw and raw.get("valid") and raw.get("current"):
            deadlines.append(integer(raw["input"]["deadlineNs"]))
        for key in ("leftClaim", "rightClaim", "gunClaim", "mechanismClaim"):
            if x.get(key) is not None:
                deadlines.append(integer(x[key]["deadlineNs"]))
        contact = x.get("currentContact")
        if contact and contact.get("joined") is True:
            deadlines.append(integer(contact["deadlineNs"]))
        if family == "magazine":
            deadlines.append(integer(x["family"]["deadlineNs"]))
            r = x.get("resource")
            if r and r.get("current"):
                deadlines.extend(integer(r[k]["deadlineNs"]) for k in ("binding", "snapshot"))
    return min(deadlines)


def observation(snapshot, name, now, raw=False):
    value = snapshot.get(name)
    require(isinstance(value, dict), "normal runtime lacks " + name + " observation")
    wait_for(value.get("available") is True, name + " capture unavailable: " + str(value.get("reason")))
    require(value["observationOnly"] is True and value["cancel"] is False,
            name + " cancelled or invalid observation contract")
    require(value["owner"] == snapshot["owner"] and same_input(value["input"], snapshot["input_evidence"]),
            name + " owner/input join differs")
    window(value["input"], now, 150_000_000)
    require(integer(value["input"]["observedNs"]) <= integer(value["capturedNs"]) <= now
            < integer(value["input"]["deadlineNs"]), "capture did not retain original input clock")
    if raw:
        r = value["raw"]
        require(r["valid"] is True and r["current"] is True and r["rigMatches"] is True
                and r["owner"] == snapshot["owner"]
                and integer(r["rigFingerprint"]) == integer(snapshot["rig"]["fingerprint"]),
                name + " raw contact is not current same-owner rig evidence")
        window(r["input"], now)
        require(r["input"]["owner"] == value["input"]["owner"]
                and integer(r["input"]["sequence"]) <= integer(value["input"]["sequence"]),
                "raw contact/input owner mismatch")
    return value


def admit(snapshot, now, nonce, family):
    # Generic frame admission delegates only its retained-action-block gate.
    def blocked(s, t):
        x = observation(s, family, t)
        return x["blocksWeaponActions" if family == "magazine" else "blocksFire"] is True
    frame.admit_frame(snapshot, now, nonce, mechanism_admission=blocked)
    observation(snapshot, family, now)
    require(now < source_deadline(snapshot, family), "original native snapshot expired")


def claim(value, hand, kind, now, item=None):
    c = value["leftClaim" if hand == 0 else "rightClaim"]
    require(isinstance(c, dict), "required current hand claim absent")
    t, i = c["token"], value["input"]
    require(integer(t["id"]) > 0 and integer(t["hand"]) == hand and integer(t["kind"]) == kind
            and t["owner"] == i["owner"] and integer(c["inputSequence"]) == integer(i["sequence"])
            and now < integer(c["deadlineNs"]) <= integer(i["deadlineNs"])
            and i["released"][hand] is False and (item is None or t["item"] == item),
            "current hand owner/token/lease/kind differs")
    return t


def counts(value):
    c = {k: integer(value[k]) for k in ("loaded", "reserve", "capacity")}
    require(0 <= c["loaded"] <= c["capacity"] and 0 < c["capacity"] <= 1000
            and 0 <= c["reserve"] <= 1_000_000, "incoherent ammunition counts")
    return c


def magazine(snapshot, now, raw=False, idle=False):
    x = observation(snapshot, "magazine", now, raw)
    p, f, r = x["profile"], x["family"], x["resource"]
    require(p["geometryVerified"] is True and p["experimentalGeometry"] is False
            and p["configurationPath"] == snapshot["selected"]["configurationPath"]
            and p["mesh"] == snapshot["selected"]["matchedMeshPath"]
            and integer(p["rigFingerprint"]) == integer(snapshot["rig"]["fingerprint"]),
            "magazine profile is not exact reviewed selected geometry")
    require(f["current"] is True and f["verified"] is True and f["owner"] == x["owner"]
            and f["weapon"] == x["weapon"], "magazine family owner differs")
    window(f, now)
    require(x["resourceBackend"] is True and isinstance(r, dict) and r["current"] is True,
            "normal ammunition resource backend unavailable")
    require(r["binding"]["owner"] == r["identity"]["owner"] == x["owner"]
            and r["binding"]["context"] == r["snapshot"]["context"], "resource binding/owner mismatch")
    copies = r["identity"]["firing"]
    require(len(copies) == 3 and len(set(map(integer, copies))) == 3
            and all(integer(v) > 0 for v in copies), "three exact native copies required")
    window(r["binding"], now)
    window(r["snapshot"], now)
    counts(r["snapshot"]["counts"])
    if idle:
        require(integer(r["phase"]) != 4 and integer(r["requestState"]) not in (4, 5, 6),
                "native ammunition transaction was rejected, uncertain or retired")
        wait_for(r["snapshot"]["coherentIdleVerified"] is True and integer(r["phase"]) == 1
                 and x["requestPending"] is False, "native ammunition transaction is not settled")
    if raw:
        require(x["raw"]["originalHandEvidence"] is not None
                and same_input(x["raw"]["input"], x["raw"]["originalHandEvidence"]),
                "original magazine hand evidence differs")
    return x


def baseline(snapshot, now):
    x = magazine(snapshot, now, raw=True, idle=True)
    r, c = x["resource"], counts(x["resource"]["snapshot"]["counts"])
    require(0 < c["loaded"] < c["capacity"] and c["reserve"] >= c["capacity"]
            and r["wellEmpty"] is False and integer(x["phase"]) == 0,
            "settled route requires a partial attached magazine and a complete reserve replacement")
    gun = claim(x, 1, 1, now, x["weapon"])
    require(x["leftClaim"] is None and x["input"]["released"][0] is True,
            "start with left hand released after any ordinary support release")
    b = observation(snapshot, "bolt", now)
    require(b["held"] is None and b["recovery"] is None and b["reloadPause"] is None
            and b["blocksFire"] is False, "settled magazine route must preserve, never erase, an owed bolt")
    return dict(owner=owner_key(snapshot), gun=copy.deepcopy(gun), counts=c,
                identity=copy.deepcopy(r["identity"]), binding=copy.deepcopy(r["binding"]),
                request=integer(r["request"]), sequence=integer(r["snapshot"]["sequence"]),
                startedNs=now, profile=copy.deepcopy(x["profile"]))


def preparation_magazine(snapshot, now):
    """Observe attached, unencumbered ammunition; never grant a reload receipt."""
    x = magazine(snapshot, now, raw=True, idle=True)
    require(x["resource"]["wellEmpty"] is False and x["raw"]["nativeMagazineAttached"] is True
            and integer(x["phase"]) == 0
            and x["resource"]["command"] is None
            and integer(x["resource"]["originalState"]) in (0, 2, 3)
            and all(x[k] is None for k in ("originalPhysical", "pendingRequest", "submittedSeat",
                                           "carriedReplacement", "reservation"))
            and integer(x["seatedRequest"]) == 0 and x["blocksWeaponActions"] is False,
            "preparation requires an attached magazine without held ammunition or pending transfer")
    return x


def preparation_baseline(snapshot, now):
    x, b = preparation_magazine(snapshot, now), bolt(snapshot, now)
    r, n = x["resource"], b["native"]
    c = counts(r["snapshot"]["counts"])
    require(c["loaded"] == c["capacity"] > 1 and c["reserve"] >= c["capacity"],
            "preparation requires a freshly observed full magazine and complete reserve replacement")
    require(n["identity"] == r["identity"] and b["held"] is None and b["recovery"] is None
            and n["held"] is None and n["ready"] is None and integer(n["phase"]) in (0, 5)
            and integer(n["failure"]) == 0 and n["heldSuspended"] is False
            and n["blocksFire"] is False and b["blocksFire"] is False
            and b["mechanismClaim"] is None and snapshot["weapon_actions_blocked"] is False,
            "preparation requires the same native ready bolt without pending cycle or debt")
    gun = claim(x, 1, 1, now, x["weapon"])
    require(gun == claim(b, 1, 1, now, x["weapon"]) and integer(gun["prerequisiteClaim"]) == 0
            and x["leftClaim"] == b["leftClaim"], "preparation gun/support custody differs")
    if x["leftClaim"] is None:
        require(x["input"]["released"][0] is True, "preparation free left hand must be released")
    else:
        support = claim(x, 0, 2, now, x["weapon"])
        require(support["prerequisiteClaim"] == gun["id"], "preparation support belongs to another gun")
    p, references = b["calibration"], b["originalGripReferences"]
    require(references is not None and references["geometryOnly"] is True
            and references["sourceInputClockAvailable"] is False
            and len(references["wristInWeapon"]) == 2,
            "preparation requires retained ordinary support/return geometry")
    for wrist in references["wristInWeapon"]:
        rigid(wrist)
    bolt_cycle_budget(p)
    require(0 < frame.finite(p["maxStepMeters"]), "preparation bolt step policy is not bounded")
    for travel, angle in ((0, 0), (p["strokeMeters"], p["unlockRadians"])):
        bolt_pose(p, travel, angle)
    return copy.deepcopy(dict(owner=owner_key(snapshot), identity=r["identity"],
        context=r["binding"]["context"], counts=c, gun=gun, request=integer(r["request"]),
        sequence=integer(r["snapshot"]["sequence"]), original=r["original"],
        originalState=r["originalState"], cycle=integer(n["cycle"]), shot=integer(n["shot"]),
        startedNs=now))


def preparation_counts(snapshot, now, base):
    x = preparation_magazine(snapshot, now)
    r = x["resource"]
    require(owner_key(snapshot) == base["owner"] and r["identity"] == base["identity"]
            and r["binding"]["context"] == base["context"]
            and integer(r["request"]) == base["request"]
            and r["original"] == base["original"] and r["originalState"] == base["originalState"],
            "preparation owner/native copies or ammunition transaction changed")
    after = dict(base["counts"], loaded=base["counts"]["loaded"]-1)
    require(counts(r["snapshot"]["counts"]) == after,
            "preparation must consume exactly one round with unchanged reserve/capacity")
    require(integer(r["snapshot"]["sequence"]) > base["sequence"]
            and integer(r["snapshot"]["observedNs"]) >= base["pressedNs"],
            "preparation ammunition observation predates its ordinary shot")
    return after


def preparation_debt(snapshot, now, base, debt):
    after = preparation_counts(snapshot, now, base)
    b = bolt(snapshot, now, debt)
    require(debt["identity"] == base["identity"]
            and integer(debt["debt"]["shot"]) > base["shot"]
            and integer(debt["debt"]["cycle"]) > base["cycle"]
            and debt["countsFreshnessAvailable"] is True and debt["counts"] == after
            and debt["guardEpoch"] is not None and integer(debt["guardEpoch"]) > 0
            and len(debt["updates"]) == 3 and all(integer(v) > 0 for v in debt["updates"])
            and counts(b["native"]["counts"]) == after,
            "preparation requires a new same-owner guarded three-copy owed shot with unchanged reserve")
    require(claim(b, 1, 1, now, base["gun"]["item"]) == base["gun"],
            "preparation shot changed its gun custody root")
    return copy.deepcopy(dict(counts=after, guardedDebt=debt, coherentThreeCopyCounts=True,
                              shotAllThreeCopiesVerified=False))


def preparation_complete(snapshot, now, base, debt):
    after = preparation_counts(snapshot, now, base)
    b = bolt(snapshot, now, debt)
    require(b["blocksFire"] is False and snapshot["weapon_actions_blocked"] is False,
            "preparation bolt return is not ready")
    root = custody(snapshot, now, debt, 1)
    support = claim(b, 0, 2, now, root["item"])
    require(support["prerequisiteClaim"] == root["id"] and debt["counts"] == after,
            "preparation did not return the same gun and support with unchanged ammunition")
    return dict(preparationOnly=True, counts=after, ready=ready(snapshot, now, debt),
                coherentThreeCopyCounts=True, shotAllThreeCopiesVerified=False)


def bolt_cycle_budget(profile):
    # A longer authored native ceiling grants no longer ordinary input episode.
    duration = min(integer(profile["maximumCycleNs"]), 10*NS)
    require(0 < integer(profile["endpointDwellNs"]) < duration, "invalid bounded bolt cycle policy")
    return duration


def receipt(snapshot, now, base, operation, before, after):
    x = magazine(snapshot, now, idle=True)
    r = x["resource"]
    require(owner_key(snapshot) == base["owner"] and r["identity"] == base["identity"]
            and r["binding"]["context"] == base["binding"]["context"], "transaction owner/native copies changed")
    wait_for(r["receipt"] is not None and r["terminal"] is not None, "matching native transaction receipt pending")
    z, terminal = r["receipt"], r["terminal"]
    c, request = z["command"], terminal["request"]
    wait_for(integer(request["intent"]["id"]) > base["request"], "only an earlier transaction receipt is visible")
    require(r["receiptJoined"] is True and integer(r["requestState"]) == integer(terminal["state"]) == 3
            and integer(r["request"]) == integer(request["intent"]["id"])
            and terminal["receipt"] == z and terminal["nativeDispatched"] is True
            and request["discard"] is False and request["binding"]["owner"] == snapshot["owner"],
            "request/terminal/native dispatch join failed")
    require(c["context"] == request["intent"]["context"] == r["snapshot"]["context"]
            and integer(c["operation"]) == integer(request["intent"]["operation"]) == operation
            and c["original"] == request["intent"]["original"]
            and integer(c["id"]) > 0 and integer(c["sourceSequence"]) >= base["sequence"],
            "native command does not match this original request")
    proof = request["sourceProof"]
    require(proof is not None and integer(proof["inputSequence"]) > 0
            and integer(proof["snapshotSequence"]) >= base["sequence"]
            and 0 < integer(request["intent"]["observedNs"]) <= integer(proof["constructedNs"])
            < integer(request["intent"]["deadlineNs"])
            and 0 < integer(proof["snapshotObservedNs"]) <= integer(proof["constructedNs"])
            < integer(proof["snapshotDeadlineNs"]), "original request construction proof missing")
    require(z["authorityVerified"] is True and z["copiesVerified"] is True
            and integer(z["authorityInvocation"]) > 0 and c["mutationRequired"] is True,
            "all-three native authority receipt missing")
    require(base["startedNs"] <= integer(c["requestedNs"]) <= integer(z["beganNs"])
            <= integer(z["completedNs"]) <= integer(terminal["resolvedNs"]) <= now
            and integer(z["beganNs"]) < integer(c["admissionDeadlineNs"])
            and integer(z["completedNs"]) < integer(c["deadlineNs"])
            and integer(r["snapshot"]["observedNs"]) >= integer(z["completedNs"]),
            "native transaction uses stale or extended original clocks")
    require(counts(c["before"]) == counts(z["before"]) == before
            and counts(c["after"]) == counts(z["after"]) == counts(r["snapshot"]["counts"]) == after,
            "native receipt conservation mismatch")
    return copy.deepcopy(dict(request=r["request"], receipt=z, original=r["original"], identity=r["identity"]))


def discarded(snapshot, now, base, removed):
    x = magazine(snapshot, now, idle=True)
    r, original = x["resource"], removed["original"]
    wait_for(integer(r["originalState"]) == 2, "explicit original discard pending")
    t = r["terminal"]
    require(r["identity"] == base["identity"] and r["wellEmpty"] is True
            and r["original"] == original and original["rounds"] == base["counts"]["loaded"]
            and counts(r["snapshot"]["counts"]) == dict(base["counts"], loaded=0)
            and x["leftClaim"] is None and t is not None and integer(t["state"]) == 3
            and t["request"]["discard"] is True and t["nativeDispatched"] is False
            and t["receipt"] is None and t["request"]["intent"]["original"] == original
            and t["request"]["binding"]["owner"] == snapshot["owner"]
            and t["request"]["intent"]["context"] == r["snapshot"]["context"]
            and integer(t["request"]["intent"]["id"]) == integer(r["request"]) > integer(removed["request"]),
            "discard did not retain the exact original rounds without reserve credit")
    return copy.deepcopy(t)


def replacement(snapshot, now, base, previous=None):
    x = magazine(snapshot, now, raw=True, idle=True)
    c = x["carriedReplacement"]
    wait_for(c is not None, "actual chest pickup custody pending")
    token = claim(x, 0, 5, now)
    require(c["current"] is True and c["claim"]["token"] == token and c["item"] == token["item"]
            and c["identity"]["owner"] == token["owner"] and c["identity"]["weapon"] == x["weapon"]
            and integer(c["identity"]["trackingEpoch"]) == integer(token["owner"]["space"])
            and integer(c["family"]) == 1 and integer(c["units"]) == base["counts"]["capacity"]
            and x["resource"]["wellEmpty"] is True and integer(x["resource"]["originalState"]) == 2,
            "replacement is not the real owned compatible reserve object")
    retained = {k: copy.deepcopy(c[k]) for k in ("identity", "item", "units", "family")}
    retained["token"] = copy.deepcopy(token)
    if previous is not None:
        require(retained == previous, "held replacement identity/claim changed during continuation")
    return retained


def debt_key(value):
    return {k: copy.deepcopy(value[k]) for k in ("owner", "item", "mechanism", "cycle", "shot")}


def completed_event(b, expected):
    """A native-acknowledged outcome; never a hand/contact/action lease."""
    completed = b.get("completion")
    if completed is None:
        return None
    require(completed["acknowledged"] is True
            and completed["native"] == b["native"]["identity"] == expected["identity"],
            "historical completion native acknowledgement/identity differs")
    events = [completed.get(k) for k in ("settled", "recovered") if completed.get(k) is not None]
    require(len(events) == 1 and isinstance(events[0], dict),
            "historical completion must contain exactly one ready receipt")
    event = events[0]
    require(debt_key(event["release"]["cycle"]) == expected["debt"]
            and integer(b["native"]["cycle"]) == integer(expected["debt"]["cycle"])
            and integer(b["native"]["shot"]) == integer(expected["debt"]["shot"]),
            "historical completion belongs to another owed debt")
    return event


def bolt(snapshot, now, expected=None, raw=True):
    b = observation(snapshot, "bolt", now, raw)
    p = b["calibration"]
    require(p["nativeJoined"] is True and p["authoredReviewed"] is True
            and p["configurationPath"] == snapshot["selected"]["configurationPath"]
            and p["mesh"] == snapshot["selected"]["matchedMeshPath"]
            and integer(p["rigFingerprint"]) == integer(snapshot["rig"]["fingerprint"])
            and (integer(p["gunHand"]), integer(p["mechanismHand"])) == (0, 1),
            "unsupported or mismatched normal bolt calibration")
    require(b["reloadPause"] is None and b["reloadMechanicalPose"] is None,
            "paused owed-reload route requires its separate resume contract")
    n = b["native"]
    require(n is not None and n["identity"]["owner"] == snapshot["owner"], "bolt native identity missing")
    require(len(set(map(integer, n["identity"]["firing"]))) == 3,
            "bolt native identity lacks three distinct copies")
    if expected is not None:
        require(n["identity"] == expected["identity"], "bolt native firing copies changed")
        candidates = [b.get("held"), b.get("recovery"), n.get("held")]
        completed = completed_event(b, expected)
        candidates += [(v.get("release") or {}).get("cycle") for v in
                       (b.get("settled"), b.get("recovered"), n.get("ready"), completed) if v]
        candidates = [v for v in candidates if v is not None]
        # A successful return retires the physical lease. A ready receipt
        # already observed by THIS invocation remains historical evidence; it
        # supplies no new hand/contact lease and is never injected into JSON.
        retained = expected.get("readyReceipt")
        if not candidates and retained is not None:
            require(integer(n["phase"]) == 5 and integer(b["custody"]) == 3
                    and b["blocksFire"] is False and b["held"] is None and b["recovery"] is None,
                    "retired debt lacks current completed/returned native owner state")
            candidates = [retained["release"]["cycle"]]
        require(candidates and all(debt_key(v) == expected["debt"] for v in candidates),
                "old owed owner/cycle/shot was lost or replaced")
        require(integer(n["cycle"]) == integer(expected["debt"]["cycle"])
                and integer(n["shot"]) == integer(expected["debt"]["shot"]), "native owed debt changed")
        for lease in candidates:
            advancing = lease.get("advancing")
            if advancing is not None and expected.get("guardEpoch") is not None:
                require(integer(advancing["guardEpoch"]) == expected["guardEpoch"]
                        and counts(advancing["counts"]) == expected["counts"]
                        and len(advancing["updates"]) == 3
                        and all(integer(a) >= integer(b) > 0 for a, b in
                                zip(advancing["updates"], expected["updates"])),
                        "old guarded bolt debt/counts/copy updates changed")
            if lease.get("grant") is not None and expected.get("guardEpoch") is not None:
                original = lease["grant"]["debt"]
                require(integer(original["guardEpoch"]) == expected["guardEpoch"]
                        and counts(original["counts"]) == expected["counts"]
                        and original["ammunitionRebase"] is None,
                        "recovery replaced or rebased original owed debt")
    return b


def owed(snapshot, now):
    b = bolt(snapshot, now)
    held = b["held"] or b["native"]["held"]
    wait_for(held is not None, "actual ordinary shot has not produced an owed bolt")
    window(held, now)
    require(held["held"] is True and b["blocksFire"] is True
            and held["owner"] == b["input"]["owner"]
            and integer(held["item"]["id"]) == integer(snapshot["owner"]["weapon"]),
            "owed bolt does not belong to the current physical gun")
    advancing = held.get("advancing")
    if advancing is not None:
        require(integer(advancing["guardEpoch"]) > 0 and len(advancing["updates"]) == 3
                and all(integer(v) > 0 for v in advancing["updates"]), "guarded three-copy debt evidence missing")
    return dict(debt=debt_key(held), identity=copy.deepcopy(b["native"]["identity"]),
                counts=counts(advancing["counts"] if advancing else b["native"]["counts"]), acquiredNs=now,
                acquiredInputSequence=integer(b["input"]["sequence"]),
                guardEpoch=integer(advancing["guardEpoch"]) if advancing else None,
                updates=copy.deepcopy(advancing["updates"]) if advancing else None,
                countsFreshnessAvailable=advancing is not None)


def custody(snapshot, now, debt, hand, previous=None):
    b = bolt(snapshot, now, debt)
    wait_for(b["custodyCurrent"] is True and b["gunClaim"] is not None
             and integer(b["gunClaim"]["token"]["hand"]) == hand, "requested gun custody transfer pending")
    token = claim(b, hand, 1, now, debt["debt"]["item"])
    require(b["gunClaim"]["token"] == token and integer(token["prerequisiteClaim"]) == 0,
            "bolt tracked gun differs from exact current custody root")
    # The normal producer emits accepted current mechanism contact only while
    # it has a target/release. AwaitingGrip and retained native-ready custody
    # intentionally have no target; their original raw contact stays required.
    if hand == 0 and integer(b["mechanismPhase"]) in (2, 3, 4, 5):
        contact = b["currentContact"]
        wait_for(contact["joined"] is True, "new custody has not yet accepted its current controller contact")
        require(contact["gun"] == b["controllerCustody"] == token
                and integer(contact["inputSequence"]) == integer(b["input"]["sequence"])
                and integer(contact["observedNs"]) == integer(b["input"]["observedNs"])
                and now < integer(contact["deadlineNs"]) <= min(integer(b["input"]["deadlineNs"]),
                    integer(b["gunClaim"]["deadlineNs"])), "current controller contact borrowed another custody root")
    if previous is not None:
        require(token == previous, "continuation replaced exact gun custody root")
    return copy.deepcopy(token)


def ready(snapshot, now, debt):
    b = bolt(snapshot, now, debt, raw=False)
    completed = completed_event(b, debt)
    events = [v for v in (completed, b["settled"], b["recovered"], b["native"]["ready"]) if v is not None]
    if not events and debt.get("readyReceipt") is not None:
        events = [debt["readyReceipt"]]
    wait_for(events, "native same-debt ready receipt pending")
    z = events[0]
    require(z["nativeReady"] is True and z["unchangedAmmunition"] is True
            and debt_key(z["release"]["cycle"]) == debt["debt"]
            and integer(z["release"]["request"]) > 0
            and integer(z["release"]["inputSequence"]) >= debt["acquiredInputSequence"]
            and integer(z["sequence"]) > integer(z["release"]["cycle"]["sequence"]) > 0
            and debt["acquiredNs"] <= integer(z["release"]["observedNs"]) <= integer(z["observedNs"]) <= now
            and integer(z["release"]["observedNs"]) < integer(z["release"]["deadlineNs"])
            <= integer(z["release"]["cycle"]["deadlineNs"])
            and integer(z["observedNs"]) < integer(z["deadlineNs"])
            and counts(b["native"]["counts"]) == debt["counts"], "native ready receipt does not settle old debt")
    return copy.deepcopy(z)


def movement_continuation(reference, snapshot, now, family, debt=None):
    """Join a trajectory to fresh same-custody evidence without renewing it."""
    require(owner_key(snapshot) == owner_key(reference), "movement owner/configuration changed")
    x = magazine(snapshot, now, raw=True) if family == "magazine" else bolt(snapshot, now, debt, raw=True)
    prior = reference[family]
    profile = "profile" if family == "magazine" else "calibration"
    require(x[profile] == prior[profile], "movement geometry profile changed")
    for hand, key in enumerate(("leftClaim", "rightClaim")):
        old, current = prior.get(key), x.get(key)
        require((old is None) == (current is None), "movement hand custody appeared or disappeared")
        if current is not None:
            token = claim(x, hand, integer(current["token"]["kind"]), now)
            require(token == old["token"], "movement hand custody token changed")
    if family == "magazine":
        require(x["weapon"] == prior["weapon"]
                and x["resource"]["identity"] == prior["resource"]["identity"]
                and x["resource"]["binding"]["context"] == prior["resource"]["binding"]["context"],
                "movement native magazine identity changed")
    else:
        require(x["native"]["identity"] == prior["native"]["identity"], "movement native bolt identity changed")
        for key in ("gunClaim", "mechanismClaim"):
            old, current = prior.get(key), x.get(key)
            require((old is None) == (current is None)
                    and (current is None or old["token"] == current["token"]),
                    "movement bolt custody root changed")
        contact = x.get("currentContact")
        if contact and contact.get("joined") is True:
            require(contact["gun"] == x["controllerCustody"] == x["gunClaim"]["token"]
                    and integer(contact["inputSequence"]) == integer(x["input"]["sequence"])
                    and integer(contact["observedNs"]) == integer(x["input"]["observedNs"]),
                    "movement contact belongs to another input/root")
    # The endpoint is expressed in the observed world mapping. A changed gun
    # transform needs a new planned stage, not extrapolation of this path.
    for old, current in ((prior["raw"]["weaponWorldMeters"], x["raw"]["weaponWorldMeters"]),
                         ((prior["raw"]["bodyWorldMeters"] if family == "bolt" else reference["mapping"]["trackingBodyWorldMeters"]),
                          (x["raw"]["bodyWorldMeters"] if family == "bolt" else snapshot["mapping"]["trackingBodyWorldMeters"]))):
        require(all(v <= cap for v, cap in zip(frame.pose_error(rigid(old), rigid(current)), (.002, .005))),
                "movement world mapping changed beyond its original geometry")
    require(now < source_deadline(snapshot, family), "movement original owner/contact/claim lease expired")
    return x


def control(snapshot, family, hand, local, now):
    x = observation(snapshot, family, now, raw=True)
    r = x["raw"]
    w = rigid(r["weaponWorldMeters"])
    body = rigid(r["bodyWorldMeters"] if family == "bolt" else snapshot["mapping"]["trackingBodyWorldMeters"])
    cal = rigid(r["wristToGrip"][hand] if family == "bolt" else snapshot["mapping"]["anatomicalWristToGrip"])
    require(family == "bolt" or hand == 0, "magazine maps its observed left hand only")
    raw = rigid(r["rawWristWorldMeters"][hand] if family == "bolt" else r["rawWristWorldMeters"])
    reference = frame.from_pose(snapshot["input"]["referenceHead"])
    current = frame.from_pose(snapshot["input"]["hands"][hand]["grip"])
    reconstructed = mul(mul(cal, mul(current, inv(reference))), body)
    require(all(v <= cap for v, cap in zip(frame.pose_error(raw, reconstructed), (.002, .005))),
            "original controller/wrist mapping does not round trip")
    return rigid(mul(mul(mul(inv(cal), mul(rigid(local), w)), inv(body)), reference))


def insertion_items(profile):
    p = profile["insertion"]
    require(integer(p["orientation"]) == 0 and integer(p["approach"]) == 0,
            "magazine requires reviewed keyed axial insertion")
    n = integer(p["pathCount"])
    if n:
        require(2 <= n <= 32 and len(p["path"]) == n, "malformed exact insertion path")
        require(all(b["arcMeters"] > a["arcMeters"] for a, b in zip(p["path"], p["path"][1:])),
                "insertion path arc order changed")
        return [rigid(row["weaponFromItem"]) for row in p["path"]]
    delta = frame.identity()
    delta[3][:3] = [frame.finite(v) * frame.finite(p["travelMeters"]) for v in p["travelDirection"]]
    return [mul(inv(rigid(p["itemFromInsertion"])), rigid(p["weaponFromEntry"])),
            mul(mul(inv(rigid(p["itemFromInsertion"])), delta), rigid(p["weaponFromEntry"]))]


def magazine_path(snapshot, action):
    p = snapshot["magazine"]["profile"]
    items = insertion_items(p)
    require(all(v <= cap for v, cap in zip(frame.pose_error(items[-1], rigid(p["attachedItem"])), (.0002, .001))),
            "profile path endpoint differs from attached item")
    wrist = [mul(rigid(p["insertion"]["itemFromHand"]), item) for item in items]
    return list(reversed(wrist)) if action == "remove" else wrist


def insertion_outside(profile):
    """The recognizer requires a real outside-volume sample before capture."""
    items = insertion_items(profile)
    first, second = items[:2]
    direction = [second[3][k]-first[3][k] for k in range(3)]
    length = math.sqrt(sum(v*v for v in direction))
    require(length > .00001, "entry path has no measured inward tangent")
    outside = copy.deepcopy(first)
    distance = frame.finite(profile["insertion"]["captureDistanceMeters"])*1.5
    for k in range(3):
        outside[3][k] -= distance*direction[k]/length
    return mul(rigid(profile["insertion"]["itemFromHand"]), outside)


def bolt_pose(calibration, travel, angle):
    p, result = calibration, rigid(calibration["closedContact"])
    axis = [frame.finite(v) for v in p["axis"]]
    require(abs(sum(v*v for v in axis)-1) < .0002, "bolt axis is not unit length")
    c, s = math.cos(angle), math.sin(angle)
    for row in range(3):
        v = result[row][:3]
        cross = [axis[1]*v[2]-axis[2]*v[1], axis[2]*v[0]-axis[0]*v[2], axis[0]*v[1]-axis[1]*v[0]]
        dot = sum(a*b for a, b in zip(axis, v))
        result[row][:3] = [v[k]*c + cross[k]*s + axis[k]*dot*(1-c) for k in range(3)]
    for k in range(3):
        result[3][k] += axis[k]*travel
    return mul(rigid(p["wristFromPart"]), rigid(result))


def subdivide(start, end, max_pos, max_angle):
    require(0 < max_pos <= .2 and 0 < max_angle <= 1.5, "unbounded trajectory policy")
    d, a = frame.pose_error(start, end)
    n = max(1, math.ceil(d/(max_pos*.65)), math.ceil(a/(max_angle*.65)))
    require(n <= 80, "trajectory exceeds bounded command budget")
    return [frame.blend(start, end, i/n) for i in range(1, n+1)]


def pose_sample(hand, matrix, grip):
    pose = frame.to_pose(rigid(matrix))
    # The host acknowledges position at three decimals and YPR at two.
    # Emit that same pose so acknowledgement does not reject a valid move
    # merely because it cannot represent the unrounded requested position.
    return dict(hand=hand, positionMeters=[float(f"{v:.3f}") for v in pose["position"]],
                yawPitchRollDegrees=[float(f"{v:.2f}") for v in frame.quaternion_ypr(pose["orientation"])], grip=grip)


def fire_clearance(clearance, snapshot, state, host, now, action_deadline):
    require(clearance["schema"] == "bc2vr.root-fire-clearance.v1"
            and clearance["rootReviewedVisibleCorridor"] is True and clearance["host"] == host
            and clearance["owner"] == snapshot["owner"]
            and clearance["configurationPath"] == snapshot["selected"]["configurationPath"]
            and clearance["rig"] == snapshot["rig"], "ROOT firing clearance owner/visible corridor missing")
    require(0 < integer(clearance["reviewedNs"]) <= now < action_deadline,
            "ROOT review missing or original bounded action episode expired")
    require(all(clearance[k] == state[k] for k in ("head", "handR", "handL", "controls"))
            and frame.preserved_pose_equal(clearance["referenceHead"], snapshot["input"]["referenceHead"])
            and frame.preserved_pose_equal(clearance["rightAim"], snapshot["input"]["hands"][1]["aim"]),
            "ROOT reviewed firing aim/head/reference changed")
    require(len(clearance["captures"]) == 2 and {p["eye"] for p in clearance["captures"]} == {"left", "right"},
            "ROOT review must name both exact rendered captures")
    return 100_000_000  # Existing shared post-reload ordinary trigger contract.
