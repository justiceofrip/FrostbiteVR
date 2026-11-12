"""Geometry and fresh boundaries for separately authorized ordinary XR stimuli.

The legacy planners remain offline, non-authorizing evidence. This module copies
their measured geometry into a different controller-input contract. It neither
grants native claims nor turns diagnostic snapshots into per-pose command leases.
Every acquisition/custody/receipt boundary still needs fresh original evidence.
"""
import copy
import importlib.util
from pathlib import Path


def _load(name):
    spec = importlib.util.spec_from_file_location("stimulus_" + name, Path(__file__).with_name(name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


feed = _load("plan_bc2_feed_simulation")
pump = _load("plan_bc2_pump_simulation")
fixed = _load("plan_bc2_fixed_container_simulation")
sight = _load("plan_bc2_sight_simulation")
GeometryRefusal = feed.Refusal
require, integer = feed.require, feed.integer
SCHEMA = "bc2vr.reload-stimulus.v1"
FAMILIES = ("feed", "pump", "fixed", "sight")
STAGES = {"feed": tuple(feed.STAGES) + ("SupplyPickup",), "pump": pump.STAGES,
          "fixed": fixed.STAGES, "sight": ("SightFlip",)}
RIGHT_FIELDS = ("grip", "aim", "gripTracked", "aimTracked", "active", "held", "touchActive",
                "touched", "stickX", "stickY", "trigger", "squeeze")
LEFT_POSE_SETUP = ["hand l follow off", "hand l aimtrim 0 0"]


class BoundaryPending(GeometryRefusal):
    """No extra motion is allowed; observe again within this original boundary."""


def _guard(function):
    """Malformed or unsupported diagnostic input always fails closed."""
    def guarded(*args, **kwargs):
        try:
            return function(*args, **kwargs)
        except GeometryRefusal:
            raise
        except (ValueError, KeyError, TypeError, IndexError, OverflowError) as error:
            raise GeometryRefusal(str(error)) from error
    return guarded


def _family(family):
    require(family in FAMILIES, "unsupported stimulus family")
    return family


@_guard
def source_deadline(snapshot, family):
    _family(family)
    deadlines = [integer(snapshot["valid_until_ns"]), integer(snapshot["native_deadline_ns"]),
                 integer(snapshot["input_evidence"]["deadlineNs"]),
                 integer(snapshot["input_evidence"]["observedNs"]) + 100_000_000]
    if family == "fixed" and snapshot.get("fixedContainer", {}).get("available") is True:
        x = snapshot["fixedContainer"]
        deadlines += [integer(v) for v in (x["input"]["deadlineNs"], x["raw"]["input"]["deadlineNs"],
            x["view"]["snapshot"]["deadlineNs"], x["view"]["binding"]["deadlineNs"], x["tracking"]["gun"]["deadlineNs"])]
        for value in (x.get("physicalFamily"), x["tracking"]["hand"]):
            if value is not None:
                deadlines.append(integer(value["deadlineNs"]))
    return min(deadlines)


@_guard
def admit_observation(family, profiles, snapshot, now_ns, *, completion=None):
    """Typed current observation; a selected sight target is allowed here."""
    _family(family)
    require(completion is None or family == "fixed", "completion observation is fixed-container only")
    now, nonce = integer(now_ns), integer(snapshot["nonce"])
    require(now < source_deadline(snapshot, family), "stimulus boundary source expired")
    if family == "feed":
        return feed.admit(profiles, snapshot, now, nonce)
    if family == "pump":
        return pump.admit(snapshot, now, nonce)
    if family == "fixed":
        return fixed.admit(snapshot, now, nonce, completion=completion)
    feed.admit_frame(snapshot, now, nonce)
    require(all(float(h["trigger"]) == 0 for h in snapshot["input"]["hands"]), "sight trigger is active")
    require(snapshot.get("sight", {}).get("status") == "available", "current sight family observation unavailable")
    return snapshot["sight"]


@_guard
def admit_boundary(family, profiles, snapshot, now_ns, stage, *, pickup=None, target="secondary"):
    return _prepare_boundary(family, profiles, snapshot, now_ns, stage, pickup=pickup, target=target)[0]


def _prepare_boundary(family, profiles, snapshot, now_ns, stage, *, pickup=None, target="secondary", step_ns=20_000_000):
    # Feed stage admission already computes the trajectory. Return that exact
    # non-authorizing evidence for this build instead of solving it again.
    require(stage in STAGES[_family(family)], "unsupported stage for stimulus family")
    result = admit_observation(family, profiles, snapshot, now_ns)
    geometry = None
    if family == "sight":
        result = sight.admit(snapshot, integer(now_ns), integer(snapshot["nonce"]), target)
    if family == "feed":
        _pending_feed(snapshot)
        _feed_state(snapshot)  # No bootstrap geometry before native presentation.
        if stage != "AttachContainer":
            _feed_neutral(snapshot)
        # Validate the current typed stage, including closed-cover/hinge and
        # retained free-placement geometry. This does not infer native eligibility.
        geometry = feed.plan(profiles, snapshot, stage, integer(now_ns), integer(snapshot["nonce"]), step_ns,
                             pickup_plan=None if pickup is None else pickup["geometryEvidence"])
    if pickup is not None:
        require(pickup.get("schema") == SCHEMA and pickup["family"] == family,
                "pickup must be this stimulus pipeline's actual preceding stage")
    return result, geometry


def _feed_state(snapshot):
    f = snapshot.get("feed")
    require(f is not None, "fresh retained feed presentation required before stimulus")
    state = {k: copy.deepcopy(f[k]) for k in
             ("lifetime", "operation", "targetKind", "blocksFire", "coverRadians", "carriedPartsMask", "supply")}
    state["weaponActionsBlocked"] = snapshot["weapon_actions_blocked"]
    return state


def _feed_neutral(snapshot):
    f = snapshot["feed"]
    require(f["mechanismClaim"] is None and f["mechanismInput"] is None and f["supplyObject"] is None
            and f["input"]["released"][0] is True and snapshot["input_evidence"]["released"][0] is True
            and snapshot["input"]["hands"][0]["squeeze"] == 0, "feed contact is not neutral and unclaimed")


def _mapping(snapshot, family, stage=None):
    inp, m = snapshot["input"], snapshot["mapping"]
    result = {k: copy.deepcopy(snapshot[k]) for k in ("owner", "equipment", "rig", "physical_equipment_generation")}
    result["selected"] = {k: copy.deepcopy(snapshot["selected"][k]) for k in
        ("configurationPath", "configurationPathPointer", "inventory", "selectedSlot", "soleConfiguredArray", "matchedMeshPath", "meshes")}
    result["preserve"] = {k: copy.deepcopy(inp[k]) for k in
        ("referenceHead", "head", "spaceGeneration", "trackingBasisGeneration", "worldUnitsPerMeter", "floorRelative")}
    result["preserve"]["rightHand"] = {k: copy.deepcopy(inp["hands"][1][k]) for k in RIGHT_FIELDS}
    result["weaponFromBody"] = feed.multiply(feed.rigid(m["weaponWorldMeters"]), feed.inverse(feed.rigid(m["trackingBodyWorldMeters"])))
    result["calibration"] = copy.deepcopy(m["anatomicalWristToGrip"])
    if family in ("pump", "fixed"):
        result["mechanismSelection"] = (pump if family == "pump" else fixed).selection(snapshot)
    if family == "sight":
        require(snapshot.get("sight", {}).get("status") == "available", "current sight family unavailable")
        result["family"] = copy.deepcopy(snapshot["sight"]["family"])
    if family == "fixed" and stage == "EjectSpent":
        result["tilt"] = fixed.tilt_mapping(snapshot)
    return result


def _same(before, after, *, observation_only=False):
    if "tilt" in before or "tilt" in after:
        omitted = ("tilt", "preserve", "weaponFromBody", "calibration")
        require({k: v for k, v in before.items() if k not in omitted}
                == {k: v for k, v in after.items() if k not in omitted}, "spent-case owner/selection/profile changed")
        fixed.require_tilt_mapping(before["tilt"], after["tilt"])
        return "source"
    identity = [{k: v for k, v in mapping.items() if k not in ("weaponFromBody", "calibration", "family", "preserve")}
                for mapping in (before, after)]
    old, new = (value.get("mechanismSelection", {}) for value in identity)
    missing_reference = (observation_only and old.get("physicalFamily") is not None
                         and new.get("physicalFamily") is None)
    if missing_reference:
        # The fixed observation serializes this reference from runtime tracking.
        # Absent tracking is missing evidence, not a contradictory selection.
        # Compare every independently observed identity field; do not borrow the
        # old reference into the snapshot or use this result at a fresh boundary.
        for value in identity:
            value["mechanismSelection"] = {k: v for k, v in value["mechanismSelection"].items()
                                           if k != "physicalFamily"}
    require(identity[0] == identity[1],
            "stimulus owner/selection/profile/reference changed")
    require(feed.preserved_equal(before["preserve"], after["preserve"],
                                 pose_keys=("head", "referenceHead"), hand_keys=("rightHand",)),
            "stimulus preserved pose/control/reference changed")
    require(all(p <= .002 and a <= .005 for p, a in
                (feed.pose_error(feed.rigid(before[k]), feed.rigid(after[k])) for k in ("weaponFromBody", "calibration"))),
            "stimulus relative weapon/body or anatomical basis changed")
    return "incomplete" if missing_reference else "source"


def _sight_state(before, after, target):
    old, new = before["family"], after["family"]
    for k in ("physicalItem", "inventory", "persistentId", "primaryAsset", "primaryConfiguration",
              "secondaryAsset", "secondaryConfiguration", "primaryAction"):
        require(old[k] == new[k], "sight physical family changed: " + k)
    require(integer(old["primaryAction"]) in (25, 36) and integer(new["observedMs"]) >= integer(old["observedMs"])
            and isinstance(new["selectedSecondary"], bool), "sight family publication invalid")
    require(feed.preserved_equal(before["preserve"], after["preserve"],
                                 pose_keys=("head", "referenceHead"), hand_keys=("rightHand",))
            and before["physical_equipment_generation"] == after["physical_equipment_generation"]
            and all(before["owner"][k] == after["owner"][k] for k in ("player", "soldier", "weak", "actorGeneration", "space")),
            "sight actor/reference changed")
    mode = "secondary" if new["selectedSecondary"] else "primary"
    require(after["equipment"]["persistence"] == old["physicalItem"]
            and after["selected"]["inventory"] == old["inventory"]
            and after["equipment"]["asset"] == new[mode + "Asset"]
            and after["selected"]["configurationPath"] == new[mode + "Configuration"],
            "sight selection left its exact physical family")
    if new["selectedSecondary"] == old["selectedSecondary"]:
        _same(before, after)
        require(all(old[k] == new[k] for k in ("targetWeapon", "targetSlot", "targetAction")), "opposite sight target changed")
        return "source"
    require(mode == target and after["owner"]["weapon"] == after["equipment"]["weapon"] == old["targetWeapon"]
            and after["selected"]["selectedSlot"] == old["targetSlot"]
            and new["targetWeapon"] == before["owner"]["weapon"]
            and new["targetSlot"] == before["selected"]["selectedSlot"]
            and new["targetAction"] == (old["primaryAction"] if mode == "secondary" else 33),
            "sight target or reciprocal selection does not match")
    return "target"


@_guard
def same_mapping(before, after, family, stage, target="secondary"):
    require(stage in STAGES[_family(family)], "unknown mapping stage")
    require(before.get("valid") is True and after.get("valid") is True, "invalid observation is not mapping or transition evidence")
    a, b = _mapping(before, family, stage), _mapping(after, family, stage)
    if family == "sight":
        return _sight_state(a, b, target)
    return _same(a, b, observation_only=True)


def _boundary(name, index, schedule, timeout=250_000_000, span=0):
    return dict(id=name, afterSample=index, holdGrip=schedule[index]["grip"], timeoutNs=timeout,
                minimumObservationSpanNs=span, requiresFreshSnapshot=True, countsTowardGestureBudget=True)


def controller_pose(row):
    return feed.from_pose(dict(position=row["positionMeters"],
        orientation=feed.ypr_quaternion(row["yawPitchRollDegrees"])))


@_guard
def neutral_motion(stimulus):
    """Compile only the existing straight, unheld approach into xr-sim's curve.

    The runtime uses smoothstep(position lerp, shortest quaternion slerp), not
    a waypoint queue. A curved inverse controller path cannot use this backend.
    Timing retains the original total/stage limits. A slower interpolation may
    use their existing slack; the peak change per original interval retains
    its caps, and the driver never restarts those clocks.
    """
    rows = stimulus["schedule"]
    if stimulus.get("continuesHeldCustody") or rows[0]["phase"] != "neutral" or rows[0]["grip"] != 0:
        return None
    end = next((i for i, row in enumerate(rows) if row["phase"] == "neutral_at_contact"), None)
    if end is None or end < 2 or any(row["grip"] != 0 or row["phase"] not in
            ("neutral", "approach", "approach_supply", "neutral_at_contact") for row in rows[:end+1]):
        return None
    if any(g["afterSample"] < end for g in stimulus["boundaries"]):
        return None
    if end+1 >= len(rows) or rows[end+1]["phase"] not in ("acquire", "acquire_shell") or rows[end+1]["grip"] != 1:
        return None
    first, last = controller_pose(rows[0]), controller_pose(rows[end])
    moving_end = end-1
    duration = integer(rows[end]["atNs"]) - integer(rows[0]["atNs"])
    linear_duration = integer(rows[moving_end]["atNs"]) - integer(rows[0]["atNs"])
    if not 0 < duration <= 10_000_000_000 or duration % 1_000_000 or linear_duration <= 0:
        return None
    for row in rows[:end]:
        t = (integer(row["atNs"])-integer(rows[0]["atNs"]))/linear_duration
        distance, angle = feed.pose_error(controller_pose(row), feed.blend(first, last, t))
        if distance > .000002 or angle > .00002:
            return None
    distance, angle = feed.pose_error(first, last)
    if distance < .000002 and angle < .00002:
        return None
    # Smoothstep's largest interval delta is centered on t=.5. This includes
    # its 1.5x peak speed instead of silently reusing linear sample timing.
    policy = stimulus["policy"]
    position_cap = float(policy.get("maxStepMeters", .025))
    angle_cap = min(float(policy["maxStepRadians"]), .175) if stimulus["family"] == "sight" else float(policy["maxStepRadians"])
    def change(ms):
        interval = min(1., integer(stimulus["stepNs"])/(ms*1_000_000))
        peak = 1.5*interval-.5*interval**3
        # The pinned quaternion implementation uses normalized lerp for nearly
        # parallel quaternions; its angular speed is not exactly constant.
        return distance*peak, feed.pose_error(feed.blend(first, last, (1-peak)/2),
                                             feed.blend(first, last, (1+peak)/2))[1]
    tail = integer(rows[-1]["atNs"])-integer(rows[end]["atNs"])
    maximum = min(10_000_000_000, integer(stimulus["budget"]["totalNs"])-tail)
    if integer(stimulus["budget"]["gestureStartSample"]) == 0:
        maximum = min(maximum, integer(stimulus["budget"]["maximumGestureNs"])-tail)
    low, high = duration//1_000_000, maximum//1_000_000
    if low > high or any(v > cap for v, cap in zip(change(high), (position_cap, angle_cap))):
        return None
    while low < high:
        middle = (low+high)//2
        if all(v <= cap for v, cap in zip(change(middle), (position_cap, angle_cap))):
            high = middle
        else:
            low = middle+1
    peak_distance, peak_angle = change(low)
    return dict(backend="xr-sim.hand-to-grip.smoothstep.v1", firstSample=0, lastSample=end,
                durationNs=low*1_000_000, durationMs=low, originalPrefixNs=duration,
                peakStepMeters=peak_distance, peakStepRadians=peak_angle)


@_guard
def check_neutral_contact(stimulus, snapshot, now_ns, *, profiles=None):
    """Fresh exact contact for pump/fixed, before their existing acquisition.

    Rebuild from the current real geometry, preserving all family admissions;
    the new observed controller must coincide with both old and new contact.
    The rebuilt plan is comparison evidence and is never executed.
    """
    family = stimulus["family"]
    require(family in ("pump", "fixed"), "unexpected neutral contact family")
    now = integer(now_ns)
    require(integer(snapshot["nonce"]) != integer(stimulus["source"]["nonce"])
            and integer(snapshot["observed_ns"]) > integer(stimulus["source"]["evaluatedAtNs"]),
            "neutral contact needs new original evidence")
    _same(stimulus["mapping"], _mapping(snapshot, family))
    require(snapshot["input"]["hands"][0]["squeeze"] == 0
            and snapshot["input_evidence"]["released"][0] is True,
            "neutral contact is not released")
    current = build_stimulus(family, profiles, snapshot, now, stimulus["stage"],
                            step_ms=integer(stimulus["stepNs"])//1_000_000)
    original_plan, current_plan = stimulus["geometryEvidence"], current["geometryEvidence"]
    if family == "fixed":
        x = fixed.reobserve(original_plan, snapshot, now, integer(snapshot["nonce"]))
        require(x["tracking"]["step"] == fixed.STEP[stimulus["stage"]]
                and fixed.ammo(snapshot, now, idle=True)["snapshot"]["counts"] == original_plan["baseline"]["counts"],
                "neutral contact changed native step/ammunition")
    else:
        pump.reobserve(original_plan, snapshot, now, integer(snapshot["nonce"]))
        before, after = original_plan["baseline"], current_plan["baseline"]
        require(all(before[k] == after[k] for k in
                    ("ammo", "nativeIdentity", "shellCounters", "lastShellRequest", "lastPumpRequest")),
                "neutral contact changed ammunition/lifetime/request")
        if "pumpLease" in before:
            require(pump.same_cycle(before["pumpLease"], after["pumpLease"]), "neutral contact changed pump cycle")
    old, new = [next(row for row in plan["schedule"] if row["phase"] == "neutral_at_contact")
                for plan in (stimulus, current)]
    observed = feed.from_pose(snapshot["input"]["hands"][0]["grip"])
    require(all(d <= .0002 and a <= .001 for d, a in
        (feed.pose_error(controller_pose(old), controller_pose(new)),
         feed.pose_error(controller_pose(new), observed))), "fresh exact neutral contact differs")
    return dict(boundary="neutral_motion_contact", observed=True, snapshotNonce=snapshot["nonce"],
                observedNs=snapshot["observed_ns"], sourceValidUntilNs=source_deadline(snapshot, family),
                nativeAcceptance=False, headsetAcceptance=False)


@_guard
def build_stimulus(family, profiles, snapshot, now_ns, stage, *, step_ms=None, pickup=None, target="secondary"):
    step = integer(20 if family == "sight" and step_ms is None else 40 if step_ms is None else step_ms)*1_000_000
    _, geometry = _prepare_boundary(family, profiles, snapshot, now_ns, stage, pickup=pickup, target=target, step_ns=step)
    if (family, stage) in (("feed", "AttachContainer"), ("pump", "ShellInsert"), ("fixed", "Insert")):
        require(pickup is not None, "continuation requires this invocation's pickup and fresh actual custody")
    prior = None if pickup is None else pickup["geometryEvidence"]
    now, nonce = integer(now_ns), integer(snapshot["nonce"])
    if family == "pump":
        geometry = pump.plan(snapshot, stage, now, nonce, step, pickup_plan=prior)
    elif family == "fixed":
        geometry = fixed.plan(snapshot, stage, now, nonce, step, pickup_plan=prior)
    elif family == "sight":
        geometry = sight.plan(snapshot, now, nonce, step, target)
    require(geometry["offlineOnly"] is True and geometry["executionAuthorized"] is False,
            "legacy geometry must remain explicitly non-authorizing")
    schedule = []
    for row in geometry["schedule"]:
        if family == "fixed" and stage == "EjectSpent":
            schedule.append(copy.deepcopy(row))
            continue
        # Reconstruct only permitted controller data; do not execute legacy command text.
        values = [feed.finite(v) for v in row["positionMeters"] + row["yawPitchRollDegrees"]]
        require(len(values) == 6 and all(abs(v) <= 10 for v in values[:3]) and all(abs(v) <= 360 for v in values[3:])
                and row["grip"] in (0, 1), "unbounded ordinary controller row")
        coordinates = " ".join(f"{v:.9f}" for v in values)
        commands = ["hand l grip pose " + coordinates]
        if family in ("pump", "fixed"):
            commands.append("hand l aim pose " + coordinates)
        commands.append(f"grip l {row['grip']}")
        schedule.append(dict(atNs=integer(row["atNs"]), phase=row["phase"], grip=row["grip"],
                             positionMeters=values[:3], yawPitchRollDegrees=values[3:], commands=commands))
    if pickup is None and stage != "EjectSpent":
        schedule[0]["commands"] = LEFT_POSE_SETUP + schedule[0]["commands"]
    require(0 < len(schedule) <= 512 and all(b["atNs"] > a["atNs"] for a, b in zip(schedule, schedule[1:])), "invalid bounded schedule")
    require(sum(len(row["commands"])+1 for row in schedule)+5 <= 512,
            "stimulus plus setup/sentinels/ordinary release exceeds command budget")
    policy = copy.deepcopy(geometry["policy"])
    maximum = integer(policy["gestureTimeoutNs"] if family == "sight" else policy["maximumGestureNs"])
    maximum = min(maximum, 10_000_000_000)
    boundaries = []
    if family == "feed":
        for i, row in enumerate(schedule):
            if row["phase"] == "neutral_at_contact":
                boundaries.append(_boundary("feed_contact", i, schedule))
            if row["phase"] == "acquire":
                boundaries.append(_boundary("feed_supply_held" if stage == "SupplyPickup" else "feed_acquired", i, schedule))
        if stage != "SupplyPickup":
            boundaries.append(_boundary("feed_released", len(schedule)-1, schedule, 600_000_000))
    elif family in ("pump", "fixed"):
        for g in geometry["receiptGates"]:
            boundaries.append(_boundary(g["id"], integer(g["afterSample"]), schedule,
                                        integer(g["timeoutNs"]), integer(g.get("minimumObservationSpanNs", 0))))
    else:
        for i, row in enumerate(schedule):
            if row["phase"] == "neutral_at_contact":
                boundaries.append(_boundary("sight_contact", i, schedule))
            if row["phase"] == "await_acknowledgement":
                boundaries.append(_boundary("sight_selected", i, schedule, integer(policy["ackTimeoutNs"])))
    start = next((i for i, r in enumerate(schedule) if r["phase"] == "acquire"), 0) if family == "sight" else 0
    require(schedule[-1]["atNs"] - schedule[start]["atNs"] <= maximum
            and schedule[-1]["atNs"] <= 10_000_000_000, "stimulus exceeds original gesture/invocation budget")
    return dict(schema=SCHEMA, ordinaryInputOnly=True, rootAuthorizationRequired=True,
        nativeAcceptance=False, headsetAcceptance=False, family=family, stage=stage, target=target if family == "sight" else None,
        source=dict(nonce=snapshot["nonce"], evaluatedAtNs=now, validUntilNs=min(source_deadline(snapshot, family), integer(geometry["sourceValidUntilNs"]))),
        mapping=_mapping(snapshot, family, stage), schedule=schedule, policy=policy, durationNs=schedule[-1]["atNs"],
        budget=dict(totalNs=10_000_000_000, gestureStartSample=start, maximumGestureNs=maximum,
                    endpointDwellNs=integer(policy.get("endpointDwellNs", policy.get("detentHoldNs", 0))), maximumCommands=512),
        boundaries=boundaries, finishGrip=schedule[-1]["grip"], stepNs=step, continuesHeldCustody=pickup is not None,
        geometryEvidence=geometry,
        initialFeedObservation=copy.deepcopy(snapshot.get("feedObservation")),
        initialFeedState=_feed_state(snapshot) if family == "feed" else None,
        requiredCompletionEvidence="Fresh per-boundary native owner/custody/operation/receipt/ammunition join; command delivery and feed counters are not native acceptance.")


def _counters(snapshot):
    value = snapshot["feedObservation"]
    require(value["observations"] == "independent_atomics_not_native_proof", "unknown feed observation schema")
    require(not value["latest_tick"]["gate_failed"] and not value["latest_tick"]["submit_refused"], "current feed gate refused")
    result = {k: integer(value[k]) for k in ("submit_attempts", "submit_refusals", "receipts_accepted", "gate_refusals")}
    require(all(v >= 0 for v in result.values()), "invalid feed observation counter")
    return result


def _pending_feed(snapshot, baseline=None, *, completing=False):
    # Call only after current generic frame/registration admission and, at a
    # named boundary, the original mapping checks. Missing data grants nothing.
    require("feed" in snapshot, "malformed observation: feed field absent")
    if snapshot["feed"] is not None:
        return
    counts = _counters(snapshot)
    if baseline is not None:
        require(counts["submit_refusals"] == integer(baseline["submit_refusals"]),
                "absent feed observation contains a new refusal")
        if completing:
            require(counts["gate_refusals"] >= integer(baseline["gate_refusals"]),
                    "feed gate refusal counter regressed")
        else:
            require(counts["gate_refusals"] == integer(baseline["gate_refusals"]),
                    "absent feed observation contains a new refusal")
        attempts = counts["submit_attempts"] - integer(baseline["submit_attempts"])
        accepted = counts["receipts_accepted"] - integer(baseline["receipts_accepted"])
        require(0 <= accepted <= attempts <= (1 if completing else 0),
                "absent feed observation counters regressed or repeated")
    raise BoundaryPending("fresh retained feed presentation not yet observed")


def _receipt_gate(module, plan, snapshot, boundary, now, nonce, *, acquired=None):
    """Only narrowly identified waiting states are retryable observations."""
    try:
        return module.check_gate(plan, snapshot, boundary, now, nonce,
                                 **({"acquired": acquired} if module is fixed else {}))
    except module.Refusal as error:
        reason = str(error)
        if module is fixed:
            x, baseline = snapshot["fixedContainer"], plan["baseline"]
            tracking, view = x["tracking"], x["view"]
            unchanged_ammo = view["snapshot"]["counts"] == baseline["counts"]
            if reason == "mod spent-case ejection not yet observed" and boundary == "fixed_case_ejected":
                raise BoundaryPending(reason) from error
            if (reason == "shared ammunition transaction is pending or ambiguous"
                    and boundary == "fixed_inserted" and x["pendingRequest"] is not None):
                fixed.pending_request(snapshot)  # Typed original request still must validate.
                raise BoundaryPending("native fixed insertion pending") from error
            if (unchanged_ammo and x["raw"]["admittedAtTick"] is True and reason == "physical opening/closure has not completed"
                    and integer(tracking["step"]) == fixed.STEP[plan["stage"]]):
                raise BoundaryPending("native physical step still in progress") from error
            if (unchanged_ammo and reason == "insertion has not consumed its physical transaction"
                    and integer(tracking["step"]) == 2 and tracking["carried"] is True
                    and tracking["hand"] is not None and tracking["hand"]["token"] == baseline["claim"]):
                raise BoundaryPending("native carried round not yet consumed") from error
        else:
            p, s = snapshot["shellPump"]["pump"], snapshot["shellPump"]["shell"]
            if ((reason == "pump has no actual Ready acknowledgement" and p["lastSettled"] is None)
                    or (reason == "no verified InsertRound acknowledgement" and s["lastAcknowledged"] is None)
                    or reason == "actual pump endpoint/phase not acknowledged"):
                raise BoundaryPending(reason) from error
        raise


@_guard
def check_boundary(stimulus, snapshot, boundary_id, now_ns, *, command_written_ns=None, prior=None, profiles=None):
    require(stimulus.get("schema") == SCHEMA, "not a stimulus contract")
    declared = [g for g in stimulus["boundaries"] if g["id"] == boundary_id]
    require(len(declared) == 1, "boundary was not declared by this stimulus")
    family, stage, plan = stimulus["family"], stimulus["stage"], stimulus["geometryEvidence"]
    now, nonce = integer(now_ns), integer(snapshot["nonce"])
    prerequisite = "fixed_aligned" if boundary_id == "fixed_inserted" else "fixed_acquired"
    acquired = ((prior or {}).get(prerequisite) or {}).get("gate") if family == "fixed" else None
    completion = fixed.completion_context(plan, boundary_id, acquired) if family == "fixed" else None
    # A named boundary already has an admitted registration and retained stage.
    # A missing rendered feed does not erase the native reload's Fire block.
    # Inspect the original common frame through that block, then verify the
    # original mapping/counters below and wait. This path can never return a
    # boundary or authorize another controller sample while feed is absent.
    missing_feed = family == "feed" and "feed" in snapshot and snapshot["feed"] is None
    if missing_feed:
        require(stimulus.get("initialFeedState") is not None, "retained feed stage absent from stimulus")
        require(now < source_deadline(snapshot, family), "stimulus boundary source expired")
        feed.admit_frame(snapshot, now, nonce,
            mechanism_admission=lambda original, evaluated: original is snapshot and evaluated == now)
        admitted = None
    else:
        admitted = admit_observation(family, profiles, snapshot, now, completion=completion)
    require(nonce != integer(stimulus["source"]["nonce"])
            and integer(snapshot["observed_ns"]) > integer(stimulus["source"]["evaluatedAtNs"]), "boundary needs a new original snapshot")
    if command_written_ns is not None:
        require(0 < integer(command_written_ns) <= integer(snapshot["input_evidence"]["observedNs"]), "boundary predates its ordinary command")
    mapped = _mapping(snapshot, family, stage)
    state = _sight_state(stimulus["mapping"], mapped, stimulus["target"]) if family == "sight" else "source"
    if family != "sight":
        _same(stimulus["mapping"], mapped)
    result = dict(boundary=boundary_id, observed=True, snapshotNonce=snapshot["nonce"], observedNs=snapshot["observed_ns"],
                  sourceValidUntilNs=source_deadline(snapshot, family), nativeReceiptProof=False, nativeAcceptance=False, headsetAcceptance=False)
    if family in ("pump", "fixed"):
        if family == "fixed" and stage == "EjectSpent":
            expected = stimulus.get("tiltExpectedSample")
            require(expected is not None, "spent-case boundary lacks its submitted right pose")
            hand = snapshot["input"]["hands"][1]
            for name, pos, angles in (("grip", "positionMeters", "yawPitchRollDegrees"),
                                      ("aim", "aimPositionMeters", "aimYawPitchRollDegrees")):
                target = feed.from_pose(dict(position=expected[pos], orientation=feed.ypr_quaternion(expected[angles])))
                d, a = feed.pose_error(feed.from_pose(hand[name]), target)
                require(d <= .002 and a <= .005, "native spent-case right pose differs from submitted sample")
            old = (prior or {}).get("fixed_tilt_current", {}).get("gate")
            raw = snapshot["fixedContainer"]["raw"]["input"]
            if old is not None:
                require(integer(raw["sequence"]) > integer(old["rawInputSequence"])
                        and integer(raw["observedNs"]) > integer(old["inputObservedNs"]),
                        "spent-case boundary reused original raw input")
        # No claim yet is a pending acquisition only if there is no competing
        # claim and the exact already-admitted native stage/ammunition persists.
        if family == "fixed" and boundary_id in ("fixed_acquired", "fixed_held"):
            x = fixed.reobserve(plan, snapshot, now, nonce)
            if x["tracking"]["hand"] is None and x["leftClaim"] is None:
                require(x["tracking"]["step"] == fixed.STEP[stage]
                        and fixed.ammo(snapshot, now, idle=True)["snapshot"]["counts"] == plan["baseline"]["counts"],
                        "fixed acquisition changed step/ammunition")
                raise BoundaryPending("native fixed hand claim not yet observed")
        result["gate"] = _receipt_gate(pump if family == "pump" else fixed, plan, snapshot, boundary_id, now, nonce,
                                       acquired=acquired)
        if completion is not None:
            result["completionOnly"] = True
    elif family == "sight":
        if boundary_id == "sight_contact":
            x = sight.admit(snapshot, now, nonce, stimulus["target"])
            require(state == "source" and x["preferredByDistance"] is True
                    and snapshot["input_evidence"]["released"][0] is True
                    and snapshot["input"]["hands"][0]["squeeze"] == 0, "fresh neutral sight preference absent")
        else:
            if state == "source":
                raise BoundaryPending("exact native sight family transition not yet observed")
            require(state == "target", "exact native sight family transition not observed")
        result["selectionState"] = state
    else:
        f, counts = snapshot["feed"], _counters(snapshot)
        if f is None:
            baseline = (prior or {}).get("feed_acquired", (prior or {}).get("feed_contact"))
            _pending_feed(snapshot, baseline["counters"] if baseline else stimulus["initialFeedObservation"],
                          completing=boundary_id == "feed_released")
        result.update(lifetime=f["lifetime"], counters=counts)
        if boundary_id == "feed_contact":
            _feed_neutral(snapshot)
            require(_feed_state(snapshot) == stimulus["initialFeedState"],
                    "feed native stage/lifetime/cover/carry/supply changed during approach")
            current = feed.plan(profiles, snapshot, stage, now, nonce, stimulus["stepNs"])
            # Re-evaluate the current measured geometry from this new actual
            # snapshot; the original plan stays untouched and non-authorizing.
            # A moved retained pouch/terminal must not silently keep the old path.
            for phase in ("acquire", "release"):
                original = next((r for r in stimulus["schedule"] if r["phase"] == phase), None)
                fresh = next((r for r in current["schedule"] if r["phase"] == phase), None)
                require((original is None) == (fresh is None), "fresh feed stage trajectory changed")
                if original is not None:
                    poses = [feed.from_pose(dict(position=r["positionMeters"],
                             orientation=feed.ypr_quaternion(r["yawPitchRollDegrees"]))) for r in (original, fresh)]
                    p, a = feed.pose_error(*poses)
                    require(p <= .002 and a <= .005, "fresh feed stage contact/endpoint geometry changed")
            result["stageRevalidated"] = True
        elif boundary_id in ("feed_acquired", "feed_supply_held"):
            require(prior is not None and "feed_contact" in prior, "fresh neutral contact boundary missing")
            require(counts == prior["feed_contact"]["counters"], "new feed refusal/submission occurred during acquisition")
            if f["mechanismClaim"] is None and f["mechanismInput"] is None and f["supplyObject"] is None:
                require(f["lifetime"] == prior["feed_contact"]["lifetime"], "unclaimed feed lifetime changed")
                raise BoundaryPending("native feed claim not yet observed")
            if boundary_id == "feed_acquired":
                require(command_written_ns is not None, "acquisition needs original squeeze publication time")
                result["claim"] = feed.held_feed(profiles, admitted, snapshot, now, plan, integer(command_written_ns))
            else:
                result["supply"] = feed.held_supply(profiles, admitted, snapshot, now, plan)
        else:
            require(snapshot["input_evidence"]["released"][0] is True and f["mechanismClaim"] is None
                    and f["mechanismInput"] is None and f["supplyObject"] is None, "feed was not freshly released")
            baseline = (prior or {}).get("feed_acquired")
            if stage == "AttachContainer":
                baseline = {"counters": stimulus["initialFeedObservation"], "lifetime": plan["sourceFeedLifetime"]}
            require(baseline is not None and f["lifetime"] == baseline["lifetime"], "held reload lifetime changed or absent")
            old = baseline["counters"]
            require(counts["submit_refusals"] == integer(old["submit_refusals"]),
                    "feed completion observed a new refusal")
            gate_delta = counts["gate_refusals"] - integer(old["gate_refusals"])
            require(gate_delta >= 0, "feed gate refusal counter regressed")
            # Gate publications include Busy and Rejected across equipment;
            # this historical aggregate is not an operation rejection receipt.
            # _counters still rejects a current gate/submit failure, and all
            # release, identity and transaction checks remain mandatory.
            if gate_delta:
                result["unresolvedGateRefusalDelta"] = gate_delta
                result["evidenceLimitations"] = [
                    "Historical gate-publication refusals increased; cause and operation attribution are unresolved."]
            attempts = counts["submit_attempts"] - integer(old["submit_attempts"])
            accepted = counts["receipts_accepted"] - integer(old["receipts_accepted"])
            require(0 <= accepted <= attempts <= 1, "feed completion counters regressed or repeated")
            if accepted != 1 or snapshot["feedObservation"]["latest_tick"]["pending"]:
                raise BoundaryPending("feed completion counters pending; retain operation-specific trace")
            result["receiptCounterAdvanced"] = True
    return result
