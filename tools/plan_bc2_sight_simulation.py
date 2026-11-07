"""Offline ordinary-controller sight trajectory; never writes xr-sim commands.

Input is the live mechanism snapshot, including its production sight extension.
No feed registration or historical receiver calibration supplies sight geometry.
--now-ns evaluates saved evidence at an explicit historical QPC time; it does not
renew its source leases. Release requires the exact observed family selection;
native acceptance additionally requires request/commit evidence from the trace.
"""
import argparse
import copy
import hashlib
import math
from pathlib import Path
import sys
import time

try:
    from . import plan_bc2_feed_simulation as frame
except ImportError:
    # Also support direct execution and importlib-based deterministic tests.
    import importlib.util
    _spec = importlib.util.spec_from_file_location("bc2_feed_frame", Path(__file__).with_name("plan_bc2_feed_simulation.py"))
    frame = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(frame)

Refusal, require, integer, finite = frame.Refusal, frame.require, frame.integer, frame.finite

# Bc2Gameplay.cpp constructs this policy; SightFlip.h owns edge/ack semantics.
POLICY = dict(grabRadiusMeters=.08, holdRadiusMeters=.20, minLeverMeters=.015,
              thresholdRadians=.40, hysteresisRadians=.08, maxStepRadians=.70,
              detentHoldNs=70_000_000, gestureTimeoutNs=2_500_000_000,
              ackTimeoutNs=1_500_000_000, maxSampleGapNs=250_000_000,
              pressSqueeze=.7, releaseSqueeze=.35)


def vector(value):
    require(isinstance(value, list) and len(value) == 3, "expected finite three-component sight vector")
    return [finite(v) for v in value]


def point(value, matrix):
    return [sum(value[k]*matrix[k][c] for k in range(3))+matrix[3][c] for c in range(3)]


def proper(value):
    """Native animated sight matrices may be slightly nonorthogonal, never reflected."""
    require(isinstance(value, list) and len(value) == 4
            and all(isinstance(row, list) and len(row) == 4 for row in value), "invalid sight matrix shape")
    m = [[finite(v) for v in row] for row in value]
    require(max(abs(m[r][3]-float(r == 3)) for r in range(4)) < .0002, "sight matrix is not affine")
    det = sum(m[0][j]*(m[1][(j+1) % 3]*m[2][(j+2) % 3]
                      - m[1][(j+2) % 3]*m[2][(j+1) % 3]) for j in range(3))
    require(math.isfinite(det) and det > 1e-8, "sight matrix is singular or reflected")
    return m


def hinge(pivot, axis, angle):
    """Row-vector Rodrigues transform, identical sign to SightFlip/SightGrasp."""
    x, y, z = axis
    c, s, t = math.cos(angle), math.sin(angle), 1-math.cos(angle)
    out = frame.identity()
    out[0][:3] = [c+x*x*t, x*y*t+z*s, x*z*t-y*s]
    out[1][:3] = [x*y*t-z*s, c+y*y*t, y*z*t+x*s]
    out[2][:3] = [x*z*t+y*s, y*z*t-x*s, c+z*z*t]
    out[3][:3] = [a-b for a, b in zip(pivot, point(pivot, out))]
    return out


def admit(snapshot, now, nonce, target):
    frame.admit_frame(snapshot, now, nonce)
    require(snapshot["weapon_actions_blocked"] is False, "sight requires unblocked weapon actions")
    require(target in ("primary", "secondary"), "unknown sight target mode")
    sight = snapshot.get("sight")
    require(isinstance(sight, dict), "live snapshot lacks production sight extension")
    require(sight.get("status") == "available", "sight unavailable: " + str(sight.get("status")))
    family, adapter, contact = sight["family"], sight["adapter"], sight["contact"]
    require(all(isinstance(v, dict) for v in (family, adapter, contact)), "missing live sight family/adapter/contact")
    inp, selected, eq = snapshot["input"], snapshot["selected"], snapshot["equipment"]
    require(isinstance(family["selectedSecondary"], bool), "invalid native sight mode")
    secondary = family["selectedSecondary"]
    primary_action = integer(family.get("primaryAction", 0))
    require(primary_action in (25, 36), "missing or invalid resolved primary action")
    require(secondary != (target == "secondary"), "requested sight mode is already selected")
    for key in ("primaryAsset", "secondaryAsset", "primaryConfiguration", "secondaryConfiguration", "persistentId"):
        require(isinstance(family[key], str) and bool(family[key]), "missing exact sight family identity")
    require(family["primaryConfiguration"] != family["secondaryConfiguration"], "ambiguous sight family configurations")
    prefix = "secondary" if secondary else "primary"
    require(eq["asset"] == family[prefix+"Asset"]
            and selected["configurationPath"] == family[prefix+"Configuration"], "selected asset/configuration differs from live sight family")
    require(integer(family["observedMs"]) > 0 and integer(family["physicalItem"]) > 0
            and integer(family["physicalItem"]) == integer(eq["persistence"]), "sight physical family identity mismatch")
    require(integer(family["inventory"]) == integer(selected["inventory"]) > 0
            and integer(family["targetWeapon"]) > 0
            and integer(family["targetWeapon"]) != integer(eq["weapon"])
            and 0 <= integer(family["targetSlot"]) != integer(selected["selectedSlot"])
            and integer(selected["selectedSlot"]) >= 0
            and integer(family["targetAction"]) == (primary_action if secondary else 33), "sight opposite target/inventory/action mismatch")
    require(integer(selected["configurationPathPointer"]) > 0 and integer(selected["soleConfiguredArray"]) > 0
            and integer(snapshot["rig"]["fingerprint"]) > 0, "missing exact sight selection/rig evidence")
    require(len([m for m in selected["meshes"] if m["path"] == selected["matchedMeshPath"]]) == 1
            and all(integer(m[k]) > 0 for m in selected["meshes"] for k in ("address", "typeInfo", "namePointer")),
            "ambiguous or invalid sight selected mesh")
    require(integer(sight["generation"]) == integer(contact["generation"]) == integer(inp["generation"]), "sight geometry/input generation mismatch")
    require(integer(sight["deadlineQpc"]) == integer(contact["deadlineQpc"]) == integer(snapshot["native_deadline_qpc"])
            and integer(sight["deadlineNs"]) == integer(contact["deadlineNs"]) == integer(snapshot["native_deadline_ns"])
            and now < integer(contact["deadlineNs"])
            and integer(snapshot["valid_until_ns"]) <= integer(contact["deadlineNs"]), "expired or mismatched original sight lease")
    require(contact["valid"] is True and contact["previewValid"] is True, "sight contact/preview unavailable")
    require(adapter["assembly"] in ("opposed_leaves", "subtree") and adapter["root"] and adapter["rear"]
            and adapter["root"] != adapter["rear"], "invalid production sight adapter")
    travel = finite(adapter["travelRadians"])
    require(POLICY["thresholdRadians"]+.02 < travel <= math.pi, "sight travel cannot reach production detent")
    pivot, axis = vector(contact["pivotMeters"]), vector(contact["axis"])
    require(abs(sum(v*v for v in axis)-1) < .0001, "sight hinge axis is not unit length")
    rear = proper(contact["sightLocalMeters"])
    require(math.dist(pivot, rear[3][:3]) <= .002, "sight pivot differs from observed rear origin")
    if adapter["assembly"] == "opposed_leaves":
        require(adapter["front"] and adapter["front"] not in (adapter["rear"], adapter["root"]), "opposed sight front identity missing")
        proper(contact["frontLocalMeters"])
        length = math.sqrt(sum(v*v for v in rear[0][:3]))
        require(length > 0 and math.dist(axis, [v/length for v in rear[0][:3]]) < .002, "opposed sight axis differs from observed rear")
    else:
        require(adapter["front"] is None and contact["frontLocalMeters"] is None, "subtree cannot invent a front leaf")
    raw = frame.rigid(contact["rawHandLocalFrameMeters"])
    frame.rigid(contact["handLocalFrameMeters"])
    palm = vector(contact["rawPalmPointWristMeters"])
    vector(contact["palmPointWristMeters"])
    hand, grasp = vector(contact["handLocalMeters"]), vector(contact["graspPointMeters"])
    require(math.dist(point(palm, raw), hand) <= .002, "sight raw mechanism point disagrees with original wrist")
    distance = finite(contact["contactDistanceMeters"])
    require(distance >= 0 and abs(math.dist(hand, grasp)-distance) <= .002, "sight contact distance disagrees with geometry")
    world = frame.rigid(snapshot["mapping"]["weaponWorldMeters"])
    observed_wrist = frame.rigid(snapshot["mapping"]["rawLeftWristWorldMeters"])
    require(max(abs(a-b) for row_a, row_b in zip(frame.multiply(raw, world), observed_wrist)
                for a, b in zip(row_a, row_b)) <= .002, "sight wrist differs from original W/F mapping")
    support = sight["support"]
    require(isinstance(support["valid"], bool), "invalid sight support evidence")
    if support["valid"]:
        require(finite(support["distanceMeters"]) >= 0, "invalid support distance")
    else:
        require(support["distanceMeters"] is None, "absent support has fabricated distance")
    preferred = distance <= POLICY["grabRadiusMeters"] and (not support["valid"] or distance < support["distanceMeters"])
    require(sight["preferredByDistance"] is preferred, "sight/support preference disagrees with original geometry")
    native = sight["native"]
    if native is not None:
        require(native["valid"] is True and integer(native["physicalItem"]) == integer(family["physicalItem"]), "native sight physical identity mismatch")
        proper(native["rear"])
        if adapter["assembly"] == "opposed_leaves":
            proper(native["front"])
        else:
            require(native["front"] is None, "native subtree cannot invent a front leaf")
        if native["rawHand"] is not None:
            frame.rigid(native["rawHand"])
    require(integer(inp["hands"][0]["active"]) & 4, "left squeeze action is not active")
    require(all(finite(h["trigger"]) == 0 for h in inp["hands"]), "release triggers before planning sight manipulation")
    return sight


def plan(snapshot, now_ns, expected_nonce, step_ns=20_000_000, target="secondary"):
    now_ns, step_ns = integer(now_ns), integer(step_ns)
    sight = admit(snapshot, now_ns, integer(expected_nonce), target)
    require(17_000_000 <= step_ns <= POLICY["maxSampleGapNs"]//2, "command period must exceed 16ms and be at most half sample gap")
    mapping, inp, contact = snapshot["mapping"], snapshot["input"], sight["contact"]
    w, b, f = [frame.rigid(mapping[k]) for k in ("weaponWorldMeters", "trackingBodyWorldMeters", "rawLeftWristWorldMeters")]
    reference, absolute = frame.from_pose(inp["referenceHead"]), frame.from_pose(inp["hands"][0]["grip"])
    # Reuse only this invocation's validated, unchanged frame matrices.
    inverse_reference, inverse_body = frame.inverse(reference), frame.inverse(b)
    relative = frame.multiply(absolute, inverse_reference)
    calibration = frame.rigid(frame.multiply(frame.multiply(f, inverse_body), frame.inverse(relative)))
    require(math.dist(calibration[3][:3], [0, 0, 0]) <= .002, "anatomical calibration translation exceeds 2mm")
    calibration[3] = [0., 0., 0., 1.]
    bound = frame.rigid(mapping["anatomicalWristToGrip"])
    require(math.dist(bound[3][:3], [0, 0, 0]) <= .00001, "bind calibration is not zero translation")
    require(all(e <= limit for e, limit in zip(frame.pose_error(calibration, bound), (.002, .005))), "derived calibration disagrees with actual anatomical binding")
    require(all(e <= limit for e, limit in zip(frame.pose_error(frame.multiply(frame.multiply(calibration, relative), b), f), (.002, .005))), "original anatomical forward round trip failed")
    inverse_world = frame.inverse(w)
    current = frame.multiply(f, inverse_world)
    pivot, axis, palm, grasp = [vector(contact[k]) for k in ("pivotMeters", "axis", "rawPalmPointWristMeters", "graspPointMeters")]
    radial = [g-p for g, p in zip(grasp, pivot)]
    axial = sum(r*a for r, a in zip(radial, axis))
    require(math.sqrt(sum((r-axial*a)**2 for r, a in zip(radial, axis))) >= POLICY["minLeverMeters"]+.002,
            "observed grasp is too close to sight hinge axis; reobserve a usable lever")
    pickup = copy.deepcopy(current)
    rotated_palm = point(palm, pickup)
    pickup[3][:3] = [v+g-p for v, g, p in zip(pickup[3][:3], grasp, rotated_palm)]
    # A modest detent crossing avoids an unconditional full-travel sweep after
    # the request. Production native/visual handoff owns the remaining motion.
    signed_angle = min(finite(sight["adapter"]["travelRadians"]), POLICY["thresholdRadians"]+.12)
    if target == "primary":
        signed_angle = -signed_angle

    inverse_calibration = frame.inverse(calibration)

    def inverse_target(local):
        return frame.multiply(frame.multiply(frame.multiply(inverse_calibration, frame.multiply(local, w)), inverse_body), reference)

    # Additional planner caps limit ordinary-controller motion, not native policy.
    position_cap, angle_cap = .025, .175

    def subdivide(curve):
        count = 1
        while count <= 1024:
            local = [curve(i/count) for i in range(count+1)]
            grips = [inverse_target(p) for p in local]
            if all(d <= position_cap*.75 and a <= angle_cap*.75
                   for path in (local, grips) for d, a in (frame.pose_error(x, y) for x, y in zip(path, path[1:]))):
                return local
            count *= 2
        raise Refusal("sight trajectory exceeds bounded sample budget")

    approach = subdivide(lambda t: frame.blend(current, pickup, t))
    motion = subdivide(lambda t: frame.multiply(pickup, hinge(pivot, axis, signed_angle*t)))
    local_schedule = [("neutral", 0, current)] + [("approach", 0, p) for p in approach[1:]]
    local_schedule += [("neutral_at_contact", 0, pickup), ("acquire", 1, pickup)]
    acquire_index = len(local_schedule)-1
    local_schedule += [("move", 1, p) for p in motion[1:]]
    dwell_samples = math.ceil(POLICY["detentHoldNs"]/step_ns)
    local_schedule += [("detent_dwell", 1, motion[-1]) for _ in range(dwell_samples)]
    local_schedule += [("await_acknowledgement", 1, motion[-1]), ("release", 0, motion[-1])]
    require((len(local_schedule)-1)*step_ns < POLICY["gestureTimeoutNs"], "sight approach/motion exceeds bounded gesture duration")
    schedule, errors, paths = [], [], [[], []]
    for index, (phase, squeeze, local) in enumerate(local_schedule):
        pose = frame.to_pose(inverse_target(local))
        numbers = [float(f"{v:.9f}") for v in pose["position"]+frame.quaternion_ypr(pose["orientation"])]
        emitted = frame.from_pose(dict(position=numbers[:3], orientation=frame.ypr_quaternion(numbers[3:])))
        wrist = frame.multiply(frame.multiply(calibration, frame.multiply(emitted, inverse_reference)), b)
        error = frame.pose_error(wrist, frame.multiply(local, w))
        require(error[0] <= .0005 and error[1] <= .005, "formatted sight XR/anatomical round trip failed")
        actual = frame.multiply(wrist, inverse_world)
        require(math.dist(point(palm, actual), point(palm, local)) <= .0005, "formatted bind-derived mechanism point misses hinge path")
        errors.append(error)
        paths[0].append(actual)
        paths[1].append(emitted)
        commands = ["hand l grip pose " + " ".join(f"{v:.9f}" for v in numbers), f"grip l {squeeze}"]
        if index == 0:
            commands = ["hand l follow off", "hand l aimtrim 0 0"]+commands
        schedule.append(dict(atNs=index*step_ns, phase=phase, grip=squeeze,
                             positionMeters=numbers[:3], yawPitchRollDegrees=numbers[3:], commands=commands))
        if phase == "acquire":
            schedule[-1]["requiresFreshSightPreference"] = True
        if phase == "await_acknowledgement":
            schedule[-1]["maximumWaitNs"] = POLICY["ackTimeoutNs"]
        if phase == "release":
            schedule[-1]["requiresObservedFamilyTransition"] = True
    steps = [frame.pose_error(a, b) for path in paths for a, b in zip(path, path[1:])]
    require(all(d <= position_cap and a <= angle_cap for d, a in steps), "formatted sight command exceeds planner motion caps")
    family = sight["family"]
    transition = dict(sourceMode="secondary" if family["selectedSecondary"] else "primary", targetMode=target,
                      sourceWeapon=snapshot["owner"]["weapon"], targetWeapon=family["targetWeapon"],
                      targetSlot=family["targetSlot"], targetAction=family["targetAction"], primaryAction=family["primaryAction"],
                      asset=family[target+"Asset"], configurationPath=family[target+"Configuration"],
                      inventory=family["inventory"], physicalItem=family["physicalItem"], persistentId=family["persistentId"],
                      nativeAcceptanceRequiresExactRequestAcknowledgement=True)
    return dict(schema="bc2vr.sight-simulation-plan.v1", offlineOnly=True, nativeAcceptance=False,
                headsetAcceptance=False, executionAuthorized=False, stage="SightFlip", target=target,
                snapshotNonce=snapshot["nonce"], evaluatedAtNs=now_ns, sourceInputSequence=inp["generation"],
                sourceValidUntilNs=snapshot["valid_until_ns"], owner=copy.deepcopy(snapshot["owner"]),
                physicalEquipmentGeneration=snapshot["physical_equipment_generation"],
                selection=dict(asset=snapshot["equipment"]["asset"], configurationPath=snapshot["selected"]["configurationPath"],
                               meshPath=snapshot["selected"]["matchedMeshPath"], rigFingerprint=snapshot["rig"]["fingerprint"]),
                family=copy.deepcopy(family), adapter=copy.deepcopy(sight["adapter"]), expectedTransition=transition,
                geometry=dict(pivotMeters=pivot, axis=axis, rawPalmPointWristMeters=palm, graspPointMeters=grasp,
                              rearLocalMeters=contact["sightLocalMeters"], frontLocalMeters=contact["frontLocalMeters"], signedTravelRadians=signed_angle),
                calibration=calibration, policy=copy.deepcopy(POLICY),
                preserve=dict(referenceHead=inp["referenceHead"], head=inp["head"], rightGrip=inp["hands"][1]["grip"],
                              rightHand=copy.deepcopy(inp["hands"][1]), spaceGeneration=inp["spaceGeneration"],
                              trackingBasisGeneration=inp["trackingBasisGeneration"], worldUnitsPerMeter=inp["worldUnitsPerMeter"]),
                durationNs=schedule[-1]["atNs"], gestureNs=(len(local_schedule)-1-acquire_index)*step_ns,
                acknowledgementWaitAdditional=True,
                executionConditions=[
                    "Original snapshot leases are not extended: require fresh owner/config/rig/reference/W/B evidence before each command batch.",
                    "After neutral approach, reobserve paired contact and preferredByDistance=true before acquisition; an old support distance cannot authorize a new grab.",
                    "Keep head/reference/right hand fixed, triggers released and menu/use/weapon-cycle inputs neutral; native hand claims remain authoritative.",
                    "Require advancing input and committed-frame acknowledgements; abort on expired paired contact, tracking loss or sample gap exceeding policy.",
                    "Hold left grip at the endpoint until fresh observation selects the exact target weapon/configuration/slot in the same actor/space/physical family.",
                    "Release is conditional on that observed selection within the production timeout; on refusal use ordinary safety release, never force selection or retry a request.",
                    "Selected-family observation is not request acknowledgement: native acceptance separately requires exact sight request, target action, noncancelled native acknowledgement and sight commit trace evidence.",
                    "Do not fire until fresh observed left-hand release and unblocked weapon actions; command acknowledgement is not native transition success."],
                verification=dict(everyFormattedCommandRoundTripped=True,
                                  maximumPositionErrorMeters=max(e[0] for e in errors), maximumAngleErrorRadians=max(e[1] for e in errors),
                                  maximumStepMeters=max(d for d, _ in steps), maximumStepRadians=max(a for _, a in steps),
                                  detentHeldNs=dwell_samples*step_ns), schedule=schedule)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--nonce", type=int, required=True)
    parser.add_argument("--target", choices=("primary", "secondary"), default="secondary")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--now-ns", type=int, help="explicit historical QPC nanoseconds; never renews leases")
    parser.add_argument("--step-ms", type=int, default=20)
    args = parser.parse_args(argv)
    try:
        require(args.output.suffix.lower() == ".json" and args.output.name.lower() not in
                {"command.json", "state.json", "mechanism-snapshot.json"}, "output must be a separate offline JSON artifact")
        require(args.output.resolve() != args.snapshot.resolve(), "output cannot replace source evidence")
        snapshot, source_hash = frame.load_json(args.snapshot, frame.SNAPSHOT_LIMIT)
        now = args.now_ns if args.now_ns is not None else time.perf_counter_ns()
        result = plan(snapshot, now, args.nonce, args.step_ms*1_000_000, args.target)
        result["evaluationClock"] = "explicit historical QPC nanoseconds" if args.now_ns is not None else "local QPC perf_counter_ns"
        result["sourceHashes"] = dict(snapshotJson=source_hash, planner=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                                      framePlanner=hashlib.sha256(Path(frame.__file__).read_bytes()).hexdigest(),
                                      coordinateReport=frame.COORDINATE_REPORT_SHA256, xrSimRevision=frame.XRSIM_REVISION, xrSimMath=frame.XRSIM_MATH_SHA256)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(frame.json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")
        print(f"Offline sight {args.target}: {len(result['schedule'])} bounded samples; native acceptance=false; {args.output}")
        return 0
    except (Refusal, KeyError, TypeError, ValueError, OSError, OverflowError) as error:
        print(f"Refused offline sight trajectory: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
