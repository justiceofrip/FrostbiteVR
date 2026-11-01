"""Offline, stdlib-only feed trajectory planner; never writes xr-sim commands.

Run with an explicit Python executable and --help. Input profiles must come from
ExportBc2FeedSimulation linked to the normal production registry. Snapshots are
the admitted mechanism-snapshot.json response, not final traces/live counters.
The optional --now-ns is an explicit historical QPC evaluation time for offline
reproduction; it never renews the original source leases.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import time


STAGES = {"OpenCover": 2, "DetachContainer": 4, "AttachContainer": 5,
          "SeatFeed": 6, "CloseCover": 7}
SNAPSHOT_LIMIT = 32768
XRSIM_REVISION = "ca14f5b1c549cf49dca49233250b8f3cdad013f4"
XRSIM_MATH_SHA256 = "2e23909fdf3aa98ba3314571509df1db55e854ad8e820c55eafec504d7d3889e"
COORDINATE_REPORT_SHA256 = "341f59330f6a48cece1dceb9119495d3422e36efd3fff75e7425c9908f4b9bfe"


class Refusal(ValueError):
    pass


def require(ok, reason):
    if not ok:
        raise Refusal(reason)


def integer(x):
    require(isinstance(x, (int, str)) and not isinstance(x, bool), "invalid integer identity/time")
    try:
        return int(x, 16 if x.startswith("0x") else 10) if isinstance(x, str) else x
    except ValueError as error:
        raise Refusal("invalid integer identity/time") from error


def finite(x):
    require(isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x),
            "nonfinite or nonnumeric value")
    return float(x)


def identity():
    return [[float(r == c) for c in range(4)] for r in range(4)]


def multiply(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def rigid(m):
    require(isinstance(m, list) and len(m) == 4 and all(isinstance(r, list) and len(r) == 4 for r in m),
            "matrix must be nested 4x4 LH row metres")
    m = [[finite(x) for x in row] for row in m]
    require(max(abs(m[r][3] - float(r == 3)) for r in range(4)) < 0.0002,
            "matrix is not affine")
    require(max(abs(sum(m[r][k] * m[c][k] for k in range(3)) - float(r == c))
                for r in range(3) for c in range(3)) < 0.0002, "matrix is not orthonormal")
    determinant = sum(m[0][j] * (m[1][(j+1) % 3]*m[2][(j+2) % 3]
                                - m[1][(j+2) % 3]*m[2][(j+1) % 3]) for j in range(3))
    require(abs(determinant - 1) < 0.0003, "matrix is not a proper rigid rotation")
    return m


def inverse(m):
    out = identity()
    for r in range(3):
        for c in range(3):
            out[r][c] = m[c][r]
    for c in range(3):
        out[3][c] = -sum(m[3][k] * out[k][c] for k in range(3))
    return out


def normalize(q):
    require(len(q) == 4, "quaternion must have four components")
    q = [finite(v) for v in q]
    n = math.sqrt(sum(v*v for v in q))
    require(n > 1e-8, "zero quaternion")
    return [v/n for v in q]


def quat_mul(a, b):
    x, y, z, w = a
    i, j, k, s = b
    return [w*i+x*s+y*k-z*j, w*j-x*k+y*s+z*i,
            w*k+x*j-y*i+z*s, w*s-x*i-y*j-z*k]


def quat_rotate(q, v):
    return quat_mul(quat_mul(q, [*v, 0]), [-q[0], -q[1], -q[2], q[3]])[:3]


def from_pose(pose):
    """C(P) = inverse(MakeLhViewFromOpenXRPose(P)), no position scaling."""
    p, raw = pose["position"], pose["orientation"]
    require(len(p) == 3 and len(raw) == 4, "invalid OpenXR pose shape")
    require(abs(sum(finite(v)**2 for v in raw) - 1) < 0.002, "unnormalized OpenXR pose")
    x, y, z, w = normalize([-raw[0], -raw[1], raw[2], raw[3]])
    out = identity()
    # Transpose of the conventional column quaternion rotation.
    out[0][:3] = [1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w)]
    out[1][:3] = [2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w)]
    out[2][:3] = [2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y)]
    out[3][:3] = [finite(p[0]), finite(p[1]), -finite(p[2])]
    return out


def to_pose(row):
    """Bc2PhysicalReloadProbe.cpp Pose conversion, with arbitrary XR reference."""
    m = [[row[c][r] for c in range(3)] for r in range(3)]
    tr = sum(m[i][i] for i in range(3))
    if tr > 0:
        s = math.sqrt(tr+1)*2
        q = [(m[2][1]-m[1][2])/s, (m[0][2]-m[2][0])/s, (m[1][0]-m[0][1])/s, s/4]
    else:
        i = max(range(3), key=lambda n: m[n][n])
        j, k = (i+1) % 3, (i+2) % 3
        s = math.sqrt(1+m[i][i]-m[j][j]-m[k][k])*2
        q = [0., 0., 0., (m[k][j]-m[j][k])/s]
        q[i], q[j], q[k] = s/4, (m[i][j]+m[j][i])/s, (m[i][k]+m[k][i])/s
    q = normalize(q)
    return {"position": [row[3][0], row[3][1], -row[3][2]],
            "orientation": [-q[0], -q[1], q[2], q[3]]}


def pose_error(a, b):
    distance = math.dist(a[3][:3], b[3][:3])
    qa, qb = to_pose(a)["orientation"], to_pose(b)["orientation"]
    angle = 2*math.acos(min(1., abs(sum(x*y for x, y in zip(qa, qb)))))
    return distance, angle


def ypr_quaternion(degrees):
    """Pinned xr-sim quat_from_ypr: +Y yaw, +X pitch, -Z roll."""
    y, p, r = [math.radians(v)/2 for v in degrees]
    return quat_mul(quat_mul([0, math.sin(y), 0, math.cos(y)],
                             [math.sin(p), 0, 0, math.cos(p)]),
                    [0, 0, -math.sin(r), math.cos(r)])


def quaternion_ypr(q):
    # Same rotated-basis extraction as pinned xrsim_math.h. At exact gimbal
    # lock choose roll=0 and the equivalent yaw; verify the emitted round trip.
    fwd, up = quat_rotate(q, [0, 0, -1]), quat_rotate(q, [0, 1, 0])
    pitch = math.asin(max(-1., min(1., fwd[1])))
    horizontal = math.hypot(fwd[0], fwd[2])
    if horizontal < 1e-8:
        yaw = math.atan2(up[0], up[2]) if fwd[1] > 0 else math.atan2(-up[0], -up[2])
        roll = 0.
    else:
        yaw = math.atan2(-fwd[0], -fwd[2])
        right = [-fwd[2]/horizontal, 0, fwd[0]/horizontal]
        true_up = [-right[2]*fwd[1], right[2]*fwd[0]-right[0]*fwd[2], right[0]*fwd[1]]
        roll = math.atan2(sum(a*b for a, b in zip(up, right)), sum(a*b for a, b in zip(up, true_up)))
    return [math.degrees(v) for v in (yaw, pitch, roll)]


def blend(a, b, t):
    pa, pb = to_pose(a), to_pose(b)
    qa, qb = pa["orientation"], pb["orientation"]
    dot = sum(x*y for x, y in zip(qa, qb))
    if dot < 0:
        qb, dot = [-v for v in qb], -dot
    if dot > .9995:
        q = normalize([x+(y-x)*t for x, y in zip(qa, qb)])
    else:
        theta = math.acos(min(1., dot))
        q = [(x*math.sin((1-t)*theta)+y*math.sin(t*theta))/math.sin(theta) for x, y in zip(qa, qb)]
    return from_pose({"position": [x+(y-x)*t for x, y in zip(pa["position"], pb["position"])],
                      "orientation": q})


def along(contact, angle):
    rot = identity()
    c, s = math.cos(angle), math.sin(angle)
    rot[0][:2], rot[1][:2] = [c, s], [-s, c]
    return multiply(contact, rot)


def supply_body_frame(inp):
    """Exact BodyAnchorHandPose frame: upright reference yaw, current head position.

    Supply uses the original controller grip, not the anatomical wrist or the
    rendered tracking-body matrix. UprightReference refuses vertical forward.
    """
    x, y, z, w = inp["referenceHead"]["orientation"]
    forward_x, forward_z = 2*(x*z+w*y), 1-2*(x*x+y*y)
    require(math.hypot(forward_x, forward_z) >= .05, "supply reference has no upright forward")
    yaw = math.atan2(forward_x, forward_z)
    return from_pose({"position": inp["head"]["position"],
                      "orientation": [0, math.sin(yaw/2), 0, math.cos(yaw/2)]})


def supply_config(profiles):
    config = profiles.get("supplyConfig")
    require(isinstance(config, dict), "export production Bc2FeedSupplyConfig before supply planning")
    require(integer(config["hand"]) == 0 and integer(config["itemNamespace"]) > 0
            and integer(config["pouch"]["id"]) > 0 and integer(config["pouch"]["generation"]) > 0
            and integer(config["maxSourceLifetimeNs"]) > 0, "invalid production supply identity")
    for contact in (config["chest"], config["alternate"]):
        if contact is not None:
            require(len(contact["centerMeters"]) == 3 and all(math.isfinite(finite(v)) for v in contact["centerMeters"])
                    and finite(contact["radiusMeters"]) > 0, "invalid production supply contact")
    return config


def key_equal(a, b):
    return all(integer(a[k]) == integer(b[k]) for k in ("id", "generation"))


def held_supply(profiles, row, snapshot, now, pickup_plan):
    """Admit actual reserve custody, never infer it from operation/pouch position."""
    feed, inp = snapshot["feed"], snapshot["input"]
    config = supply_config(profiles)
    obj = feed.get("supplyObject")
    claim, hand = feed.get("mechanismClaim"), feed["mechanismInput"]
    require(obj is not None and claim is not None and hand is not None, "AttachContainer requires reobserved acquired reserve custody")
    token, ident = claim["token"], obj["identity"]
    evidence(hand, snapshot["owner"], integer(snapshot["physical_equipment_generation"]), now)
    require(integer(token["id"]) > 0 and integer(token["hand"]) == 0 and integer(token["kind"]) == 5
            and integer(token["prerequisiteClaim"]) == 0 and token["owner"] == feed["input"]["owner"]
            and ident["owner"] == token["owner"] == hand["owner"], "reserve claim owner/kind mismatch")
    require(obj["claim"] == claim and key_equal(obj["item"], token["item"])
            and integer(token["item"]["id"]) == integer(config["itemNamespace"])
            and integer(token["item"]["generation"]) > 0 and key_equal(token["contact"], config["pouch"])
            and key_equal(ident["weapon"], feed["weapon"])
            and key_equal(ident["profile"], {"id": row["key"], "generation": row["mechanism"]["revision"]})
            and integer(ident["pool"]["id"]) == integer(snapshot["selected"]["inventory"])
            and integer(ident["pool"]["generation"]) == integer(feed["supply"]["resource"]["generation"])
            and integer(ident["trackingEpoch"]) == integer(hand["owner"]["space"])
            and integer(obj["family"]) == 1 and integer(obj["units"]) > 0, "reserve object identity mismatch")
    require(integer(claim["inputSequence"]) == integer(hand["sequence"])
            and now < integer(claim["deadlineNs"]) <= integer(hand["deadlineNs"])
            and hand["released"] == [False, False] and finite(inp["hands"][0]["squeeze"]) >= .7
            and integer(feed["operation"]) == STAGES["AttachContainer"]
            and integer(feed["targetKind"]) == 2 and integer(feed["carriedPartsMask"]) & 1,
            "reserve is not held at an acquired attachment source")
    require(integer(feed["input"]["sequence"]) <= integer(hand["sequence"])
            and integer(feed["input"]["observedNs"]) <= integer(hand["observedNs"]), "reserve claim precedes its retained geometry")
    if integer(feed["input"]["sequence"]) == integer(hand["sequence"]):
        require(all(feed["input"][k] == hand[k] for k in ("observedNs", "deadlineNs", "released")), "same-packet reserve evidence disagrees")
    require(feed["mechanismWristWorldMeters"] is not None, "acquired reserve lacks bound anatomical wrist")
    distance, angle = pose_error(rigid(feed["mechanismWristWorldMeters"]), rigid(snapshot["mapping"]["rawLeftWristWorldMeters"]))
    policy = row["operations"]["AttachContainer"]["motion"]["policy"]
    require(distance <= finite(policy["pathToleranceMeters"]) and angle <= finite(policy["orientationToleranceRadians"]),
            "reobserve held reserve after its actual pickup raw contact arrives")
    if pickup_plan is not None:
        require(pickup_plan["stage"] == "SupplyPickup" and pickup_plan["owner"] == snapshot["owner"]
                and integer(pickup_plan["selection"]["key"]) == integer(row["key"])
                and pickup_plan["selection"]["profileDigest"] == row["profileDigest"], "pickup prerequisite identity mismatch")
        require(integer(snapshot["nonce"]) != integer(pickup_plan["snapshotNonce"])
                and integer(snapshot["observed_ns"]) > integer(pickup_plan["evaluatedAtNs"])
                and integer(feed["input"]["sequence"]) > integer(pickup_plan["sourceInputSequence"])
                and integer(feed["revision"]) > integer(pickup_plan["sourceFeedRevision"]),
                "attachment needs fresh post-pickup reobservation")
        require(integer(feed["lifetime"]) == integer(pickup_plan["sourceFeedLifetime"])
                and all(inp[k] == pickup_plan["preserve"][k] for k in
                        ("referenceHead", "head", "spaceGeneration", "trackingBasisGeneration", "worldUnitsPerMeter"))
                and inp["hands"][1]["grip"] == pickup_plan["preserve"]["rightGrip"], "pickup prerequisite basis/lifetime changed")
    return obj


def held_feed(profiles, row, snapshot, now, acquisition_plan, command_written_ns):
    """Require native ownership after the actual squeeze, before moving a part."""
    selected = admit(profiles, snapshot, now, integer(snapshot["nonce"]))
    stage = acquisition_plan["stage"]
    require(stage in ("OpenCover", "DetachContainer", "SeatFeed", "CloseCover"),
            "stage does not acquire a Feed mechanism claim")
    require(integer(selected["key"]) == integer(row["key"]) == integer(acquisition_plan["selection"]["key"])
            and selected["profileDigest"] == row["profileDigest"] == acquisition_plan["selection"]["profileDigest"]
            and snapshot["owner"] == acquisition_plan["owner"], "feed acquisition identity changed")
    feed, inp = snapshot["feed"], snapshot["input"]
    require(feed is not None and feed.get("mechanismClaim") is not None
            and feed.get("mechanismInput") is not None,
            stage + " requires reobserved owned Feed mechanism claim before motion")
    claim, hand = feed["mechanismClaim"], feed["mechanismInput"]
    token = claim["token"]
    part = {"OpenCover": 0, "DetachContainer": 2, "SeatFeed": 3, "CloseCover": 0}[stage]
    contact_id = integer(row["key"]) + part
    if contact_id > 2**64-1:
        contact_id -= 2**64-1  # Bc2PhysicalFeed::PartKey skips zero on wrap.
    require(integer(token["id"]) > 0 and integer(token["hand"]) == 0 and integer(token["kind"]) == 4
            and integer(token["prerequisiteClaim"]) > 0 and token["owner"] == hand["owner"]
            and key_equal(token["item"], feed["weapon"])
            and integer(token["contact"]["id"]) == contact_id
            and integer(token["contact"]["generation"]) == integer(feed["lifetime"])
            and feed.get("supplyObject") is None, "feed acquisition claim owner/kind/part mismatch")
    # Mechanism claims use the exact original raw packet, unlike independently
    # acquired reserve custody. Neither the command nor this check renews it.
    packet_fields = ("owner", "sequence", "observedNs", "deadlineNs", "focused", "tracked", "released")
    require(all(hand[k] == feed["input"][k] for k in packet_fields)
            and integer(claim["inputSequence"]) == integer(hand["sequence"])
            and now < integer(claim["deadlineNs"]) <= integer(hand["deadlineNs"])
            and hand["released"] == [False, False] and finite(inp["hands"][0]["squeeze"]) >= .7,
            "feed acquisition lacks current held original-source evidence")
    motion = row["operations"][stage]["motion"]
    require(integer(feed["operation"]) == STAGES[stage]
            and integer(feed["targetKind"]) == (1 if motion["kind"] == "Hinge" else 2)
            and feed["mechanismWristWorldMeters"] is not None,
            "feed acquisition lacks bound operation/wrist")
    require(integer(snapshot["nonce"]) != integer(acquisition_plan["snapshotNonce"])
            and integer(snapshot["observed_ns"]) > integer(acquisition_plan["evaluatedAtNs"])
            and 0 < command_written_ns <= integer(hand["observedNs"])
            and integer(hand["sequence"]) > integer(acquisition_plan["sourceInputSequence"])
            and integer(feed["revision"]) > integer(acquisition_plan["sourceFeedRevision"] or 0),
            "feed acquisition needs fresh post-squeeze reobservation")
    require(stage == "OpenCover" or integer(feed["lifetime"]) == integer(acquisition_plan["sourceFeedLifetime"]),
            "feed acquisition lifetime changed")
    require(all(inp[k] == acquisition_plan["preserve"][k] for k in
                ("referenceHead", "head", "spaceGeneration", "trackingBasisGeneration", "worldUnitsPerMeter"))
            and inp["hands"][1]["grip"] == acquisition_plan["preserve"]["rightGrip"],
            "feed acquisition reference/right-hand basis changed")
    distance, angle = pose_error(rigid(feed["mechanismWristWorldMeters"]),
                                 rigid(snapshot["mapping"]["rawLeftWristWorldMeters"]))
    policy = motion["policy"]
    require(distance <= finite(policy["pathToleranceMeters"])
            and angle <= finite(policy["orientationToleranceRadians"]),
            "reobserve feed acquisition after its actual raw contact arrives")
    return claim


def evidence(sample, owner, physical_generation, now):
    expected = {"actor": (integer(owner["weak"]) << 32) | integer(owner["soldier"]),
                "actorGeneration": integer(owner["actorGeneration"]),
                "equipGeneration": physical_generation, "space": integer(owner["space"])}
    require({k: integer(sample["owner"][k]) for k in expected} == expected, "hand evidence owner/space mismatch")
    observed, deadline = integer(sample["observedNs"]), integer(sample["deadlineNs"])
    require(0 < observed <= now < deadline and 0 < deadline-observed <= 100_000_000,
            "stale or invalid original hand evidence lease")
    require(observed <= integer(sample["nowNs"]) <= now, "invalid original evidence time")
    require(integer(sample["sequence"]) > 0 and sample["focused"] is True
            and sample["tracked"] == [True, True], "untracked or unfocused hand evidence")


def admit_frame(snapshot, now, nonce, *, mechanism_admission=None):
    """Admit the original generic body/input frame, without a feed registration.

    This is observation admission only. Mechanism planners must still apply
    their own identity, geometry and operation guards.
    """
    require(snapshot["schema"] == "fvr.bc2.live-mechanism-snapshot.v1" and snapshot["valid"] is True,
            "snapshot admission refused")
    require(integer(snapshot["nonce"]) == nonce and nonce > 0, "snapshot request nonce mismatch")
    require(0 < integer(snapshot["observed_ns"]) <= now < integer(snapshot["valid_until_ns"]),
            "stale snapshot or evaluation time precedes snapshot")
    # The normal feed reload deliberately blocks Fire while its cover/container
    # is open. BodyObservation supplies geometry through that block; it does not
    # authorize firing or bypass the native operation's own preflight.
    feed_block = snapshot.get("feed") is not None and snapshot["feed"].get("blocksFire") is True
    owner, eq, rig = snapshot["owner"], snapshot["equipment"], snapshot["rig"]
    require(all(integer(owner[k]) > 0 for k in ("player", "soldier", "weak", "weapon", "actorGeneration", "equipGeneration", "space")),
            "missing native owner lifetime")
    physical = integer(snapshot["physical_equipment_generation"])
    require(physical > 0 and integer(eq["weapon"]) == integer(owner["weapon"]) and integer(eq["data"]) > 0,
            "equipment owner mismatch")
    require(integer(rig["soldier"]) == integer(owner["soldier"]) and integer(rig["weak"]) == integer(owner["weak"])
            and 0 < integer(rig["count"]) <= 1024 and integer(rig["skeleton"]) > 0 and integer(rig["pose"]) > 0,
            "rig owner/identity mismatch")
    inp, source, selected = snapshot["input"], snapshot["input_evidence"], snapshot["selected"]
    evidence(source, owner, physical, now)
    generation = integer(inp["generation"])
    require(generation == integer(source["sequence"]) == integer(snapshot["shot_input_generation"]),
            "mapping/pose input generation mismatch")
    require(integer(inp["spaceGeneration"]) == integer(owner["space"]) and integer(inp["trackingBasisGeneration"]) > 0,
            "unknown or mismatched XR reference space")
    require(inp["focused"] is True and inp["headValid"] is True and integer(inp["predictedNs"]) > 0,
            "invalid input focus/head/time")
    require(.01 <= finite(inp["worldUnitsPerMeter"]) <= 1000, "invalid world units; matrices must already be metres")
    require(len(inp["hands"]) == 2 and all(h["gripTracked"] is True for h in inp["hands"])
            and inp["hands"][1]["aimTracked"] is True, "required controller tracking missing")
    for h in inp["hands"]:
        from_pose(h["grip"])
        from_pose(h["aim"])
        require(0 <= finite(h["squeeze"]) <= 1, "invalid squeeze")
    from_pose(inp["head"])
    require(selected["configurationPathVerified"] is True and 0 < integer(selected["sequence"]) <= generation,
            "configuration not tied to admitted input")
    require(0 < integer(selected["observedNs"]) <= now < integer(selected["deadlineNs"]), "stale selected configuration")
    require(integer(snapshot["valid_until_ns"]) <= min(integer(source["deadlineNs"]), integer(selected["deadlineNs"])),
            "snapshot improperly extends source lease")
    require(now < integer(snapshot["native_deadline_ns"])
            and integer(snapshot["valid_until_ns"]) <= integer(snapshot["native_deadline_ns"]),
            "expired native shot or snapshot extends original shot lease")
    require(any(m["path"] == selected["matchedMeshPath"] for m in selected["meshes"]), "matched mesh absent from selection")
    # An opt-in observer may validate its own exact same-owner debt. Pass the
    # ORIGINAL snapshot, after all common guards, and never clear its Fire block.
    # Existing feed/sight callers retain their previous admission behavior.
    mechanism_block = (snapshot["weapon_actions_blocked"] is True and mechanism_admission is not None
                       and callable(mechanism_admission) and mechanism_admission(snapshot, now) is True)
    require(snapshot["weapon_actions_blocked"] is False or feed_block or mechanism_block,
            "weapon actions blocked without an admitted retained mechanism")


def admit(profiles, snapshot, now, nonce):
    require(profiles["schema"] == "bc2vr.feed-simulation-profiles.v1" and len(profiles["profiles"]) == 24,
            "need all 24 exported production registrations")
    rows = profiles["profiles"]
    require(len({integer(p["key"]) for p in rows}) == 24, "ambiguous registry keys")
    admit_frame(snapshot, now, nonce)
    owner, eq, rig = snapshot["owner"], snapshot["equipment"], snapshot["rig"]
    physical = integer(snapshot["physical_equipment_generation"])
    inp, source, selected = snapshot["input"], snapshot["input_evidence"], snapshot["selected"]
    generation = integer(inp["generation"])
    matches = [p for p in rows if p["asset"] == eq["asset"]
               and p["configurationPath"] == selected["configurationPath"]
               and p["meshPath"] == selected["matchedMeshPath"]
               and integer(p["rigFingerprint"]) == integer(rig["fingerprint"])]
    require(len(matches) == 1, "exact registration/configuration/mesh/rig not found")
    row = matches[0]
    feed = snapshot["feed"]
    if feed is not None:
        require(feed["owner"] == owner and integer(feed["registrationKey"]) == integer(row["key"])
                and integer(feed["rigFingerprint"]) == integer(row["rigFingerprint"]), "retained feed identity mismatch")
        require(integer(feed["weapon"]["id"]) == integer(owner["weapon"])
                and integer(feed["weapon"]["generation"]) == physical
                and integer(feed["lifetime"]) > 0 and integer(feed["revision"]) > 0, "retained feed lifetime mismatch")
        for key in feed["supply"].values():
            require(integer(key["id"]) > 0 and integer(key["generation"]) > 0, "missing exact retained supply")
        evidence(feed["input"], owner, physical, now)
        require(integer(snapshot["valid_until_ns"]) <= integer(feed["input"]["deadlineNs"]), "extended retained feed lease")
        for sample in [feed["input"], feed["mechanismInput"]]:
            if sample is not None:
                evidence(sample, owner, physical, now)
                # Supply claims have their own original packet and lease; a
                # newer claim never updates the older rendered raw input.
                if sample is feed["input"]:
                    require(integer(sample["sequence"]) <= generation and integer(sample["observedNs"]) <= integer(source["observedNs"]),
                            "retained feed paired with a later raw input")
        count = integer(feed["chainCount"])
        require(count == row["geometry"]["chainCount"] and len(feed["weaponFromChain"]) == count, "retained articulation mismatch")
        require(integer(feed["carriedPartsMask"]) >= 0 and not integer(feed["carriedPartsMask"]) & ~(((1 << (count+2))-1) & ~2),
                "invalid retained carry mask")
        for name in ("weaponFromPouch", "weaponFromCoverHinge", "pouchCarryWeaponWorldMeters", "chainCarryWeaponWorldMeters", "originalWeaponWorldMeters"):
            rigid(feed[name])
        for matrix in feed["weaponFromChain"]:
            rigid(matrix)
        if feed["mechanismWristWorldMeters"] is not None:
            rigid(feed["mechanismWristWorldMeters"])
    return row


def plan(profiles, snapshot, stage, now_ns, expected_nonce, step_ns=20_000_000, pickup_plan=None, contact="chest"):
    """Return a geometric schedule. Native eligibility remains outside this tool."""
    supply_pickup = stage == "SupplyPickup"
    require(stage in STAGES or supply_pickup, "stage has no implemented measured trajectory")
    row = admit(profiles, snapshot, integer(now_ns), integer(expected_nonce))
    motion = row["operations"]["AttachContainer" if supply_pickup else stage]["motion"]
    require(motion is not None and motion["measured"] is True and not motion["returnToStart"], "missing measured production motion")
    policy = motion["policy"]
    pos_cap, ang_cap = finite(policy["maxStepMeters"]), finite(policy["maxStepRadians"])
    dwell, maximum, gap = [integer(policy[k]) for k in ("endpointDwellNs", "maximumGestureNs", "maxInputGapNs")]
    require(pos_cap > 0 and ang_cap > 0 and dwell > 0 and maximum > dwell and 17_000_000 <= step_ns <= gap//2,
            "invalid policy or command period: require >16ms filesystem tick and <=half input gap")
    mapping, inp, feed = snapshot["mapping"], snapshot["input"], snapshot["feed"]
    w, b, f = [rigid(mapping[k]) for k in ("weaponWorldMeters", "trackingBodyWorldMeters", "rawLeftWristWorldMeters")]
    reference = from_pose(inp["referenceHead"])
    absolute = from_pose(inp["hands"][0]["grip"])
    relative = multiply(absolute, inverse(reference))
    calibration = rigid(multiply(multiply(f, inverse(b)), inverse(relative)))
    require(math.dist(calibration[3][:3], [0, 0, 0]) <= .002, "anatomical calibration translation exceeds 2mm")
    calibration[3] = [0., 0., 0., 1.]
    bound = rigid(mapping["anatomicalWristToGrip"])
    require(math.dist(bound[3][:3], [0, 0, 0]) <= .00001, "bind calibration is not zero translation")
    require(all(e <= limit for e, limit in zip(pose_error(calibration, bound), (.002, .005))),
            "derived calibration disagrees with actual anatomical binding")
    require(all(e <= limit for e, limit in zip(pose_error(multiply(multiply(calibration, relative), b), f), (.002, .005))),
            "original anatomical forward round trip failed")
    current_local = multiply(f, inverse(w))
    if feed is not None:
        require(0 <= integer(feed["operation"]) <= 9, "unknown retained presentation operation")
    else:
        require(stage == "OpenCover", "stage requires actual retained feed presentation and world bases")
    continuation = stage == "AttachContainer"
    supply_object = held_supply(profiles, row, snapshot, now_ns, pickup_plan) if continuation else None

    def inverse_target(local):
        return multiply(multiply(multiply(inverse(calibration), multiply(local, w)), inverse(b)), reference)

    object_curve = None
    if supply_pickup:
        config = supply_config(profiles)
        require(contact in ("chest", "alternate") and config[contact] is not None, "missing production supply contact")
        require(feed.get("supplyObject") is None, "already-held reserve requires AttachContainer continuation")
        body = supply_body_frame(inp)
        body_grip = multiply(absolute, inverse(body))
        body_grip[3][:3] = [finite(v) for v in config[contact]["centerMeters"]]
        target_absolute = multiply(body_grip, body)
        authored_start = multiply(multiply(multiply(calibration, multiply(target_absolute, inverse(reference))), b), inverse(w))
        pickup, use_actual_pickup = authored_start, False
        source_description = "production Bc2FeedSupplyConfig " + contact + "; BodyAnchorHandPose original controller frame"
        curve = lambda t: pickup
    elif motion["kind"] == "Hinge":
        hinge, contact = rigid(motion["weaponFromMotion"]), rigid(motion["motionFromContact"])
        travel = finite(motion["travel"])
        require(abs(travel) > 1e-6, "zero hinge travel")
        start_contact, remaining = contact, travel
        source_description = "authored opening contact"
        if feed is not None:
            full = finite(row["geometry"]["coverTravelRadians"])
            angle = finite(feed["coverRadians"])
            if stage == "OpenCover" and integer(feed["operation"]) != STAGES[stage]:
                require(abs(angle) <= .0005, "idle/restarted opening requires coherently closed retained cover")
            fraction = angle/full
            require(-1e-5 <= fraction <= 1+1e-5, "retained cover angle outside authored range")
            expected_hinge = multiply(along(identity(), angle), hinge)
            require(all(e <= limit for e, limit in zip(pose_error(expected_hinge, feed["weaponFromCoverHinge"]), (.0005, .005))),
                    "retained cover angle and hinge disagree")
            start_contact = along(contact, angle if stage == "OpenCover" else angle-full)
            remaining = full-angle if stage == "OpenCover" else -angle
            require(finite(policy["completeFraction"]) <= remaining/travel <= 1+(1-finite(policy["releaseFraction"])),
                    "retained cover requires an opaque partial-gesture recovery, not a fresh complete stage")
            source_description = "actual retained cover angle/hinge in current weapon basis"
        authored_start = multiply(start_contact, hinge)
        binding = row["mechanism"]["contacts"][0]
    else:
        require(motion["kind"] == "FreePlacement" and feed is not None, "unsupported motion kind")
        terminal = stage == "SeatFeed"
        bit = 1 << (feed["chainCount"]+1) if terminal else 1
        object_local = feed["weaponFromChain"][-1] if terminal else feed["weaponFromPouch"]
        basis = feed["chainCarryWeaponWorldMeters"] if terminal else feed["pouchCarryWeaponWorldMeters"]
        if not integer(feed["carriedPartsMask"]) & bit:
            basis = w
        source = rigid(multiply(multiply(object_local, basis), inverse(w)))
        destination = rigid(motion["placement"]["weaponFromDestination"])
        authored_start = multiply(rigid(motion["placement"]["sourceFromGrip"]), source)
        object_curve = lambda t: blend(source, destination, t)
        binding = row["mechanism"]["contacts"][3 if terminal else 2]
        source_description = "actual retained terminal" if terminal else "actual retained pouch"
        source_description += "; carry-mask-selected world basis reprojected into current weapon"
    if not supply_pickup:
        pickup_error = pose_error(current_local, authored_start)
        use_actual_pickup = continuation or (pickup_error[0] <= finite(binding["radiusMeters"])*.8 and (
            binding["orientation"] == "Acquired" or pickup_error[1] <= finite(binding["maxAngleRadians"])*.8))
        pickup = current_local if use_actual_pickup else authored_start
    if not supply_pickup and object_curve is None:
        grabbed = multiply(pickup, inverse(hinge))
        require(math.hypot(grabbed[3][0], grabbed[3][1]) > 2*finite(policy["pathToleranceMeters"]),
                "pickup too close to hinge axis")
        curve = lambda t: multiply(along(grabbed, remaining*t), hinge)
    elif object_curve is not None:
        source_from_pickup = multiply(pickup, inverse(source))
        curve = lambda t: multiply(source_from_pickup, object_curve(t))

    # Refine against actual anatomical AND controller displacements; free carry
    # also checks the object's displacement. Uniform nominal ticks alone cannot
    # constrain angular motion or an offset grip's arc.
    def subdivide(functions):
        count = 1
        while count <= 4096:
            samples = [[fn(i/count) for i in range(count+1)] for fn in functions]
            if all(pose_error(a, b)[0] <= pos_cap*.75 and pose_error(a, b)[1] <= ang_cap*.75
                   for path in samples for a, b in zip(path, path[1:])):
                return samples[0]
            count *= 2
        raise Refusal("trajectory cannot meet displacement caps in bounded sample budget")

    approach = lambda t: blend(current_local, pickup, t)
    approach_samples = subdivide([approach, lambda t: inverse_target(approach(t))])
    curves = [curve, lambda t: inverse_target(curve(t))]
    if object_curve is not None:
        curves.append(object_curve)
    motion_samples = subdivide(curves)
    if continuation:
        local_schedule = [("continue_held", 1, current_local)]
    else:
        local_schedule = [("neutral", 0, current_local)]
        local_schedule += [("approach", 0, p) for p in approach_samples[1:]]
        local_schedule += [("neutral_at_contact", 0, pickup), ("acquire", 1, pickup)]
    acquire_index = len(local_schedule)-1
    if not supply_pickup:
        local_schedule += [("move", 1, p) for p in motion_samples[1:]]
    endpoint_index = len(local_schedule)-1
    if not supply_pickup:
        local_schedule += [("endpoint_dwell", 1, motion_samples[-1]) for _ in range(math.ceil(dwell/step_ns))]
        local_schedule += [("release", 0, motion_samples[-1])]
    gesture_ns = (len(local_schedule)-1-acquire_index)*step_ns
    require(gesture_ns <= maximum and (len(local_schedule)-1)*step_ns <= maximum,
            "bounded motion and dwell exceed maximum gesture duration")
    require(supply_pickup or (len(local_schedule)-2-endpoint_index)*step_ns >= dwell, "endpoint dwell shorter than policy")
    schedule, max_position_error, max_angle_error = [], 0., 0.
    actual_local, actual_grips = [current_local], [absolute]
    for index, (phase, grip, target) in enumerate(local_schedule):
        absolute_target = inverse_target(target)
        pose = to_pose(absolute_target)
        numbers = [float(f"{v:.9f}") for v in pose["position"]+quaternion_ypr(pose["orientation"])]
        require(all(math.isfinite(v) for v in numbers), "nonfinite xr-sim command")
        emitted = from_pose({"position": numbers[:3], "orientation": ypr_quaternion(numbers[3:])})
        forward = multiply(multiply(calibration, multiply(emitted, inverse(reference))), b)
        error = pose_error(forward, multiply(target, w))
        require(error[0] <= .0005 and error[1] <= .005, "formatted xr-sim YPR/anatomical round trip failed")
        max_position_error, max_angle_error = max(max_position_error, error[0]), max(max_angle_error, error[1])
        actual_local.append(multiply(forward, inverse(w)))
        actual_grips.append(emitted)
        commands = ["hand l grip pose " + " ".join(f"{v:.9f}" for v in numbers), f"grip l {grip}"]
        if index == 0:
            commands = ["hand l follow off", "hand l aimtrim 0 0"]+commands
        schedule.append({"atNs": index*step_ns, "phase": phase, "grip": grip,
                         "positionMeters": numbers[:3], "yawPitchRollDegrees": numbers[3:], "commands": commands})
    measured_steps = [pose_error(a, b) for path in (actual_local, actual_grips) for a, b in zip(path, path[1:])]
    require(all(d <= pos_cap and a <= ang_cap for d, a in measured_steps), "formatted command exceeds motion caps")
    source_until = integer(snapshot["valid_until_ns"])
    if continuation:
        source_until = min(source_until, integer(feed["mechanismClaim"]["deadlineNs"]),
                           integer(feed["mechanismInput"]["deadlineNs"]))
    return {"schema": "bc2vr.feed-simulation-plan.v1", "offlineOnly": True,
            "nativeAcceptance": False, "headsetAcceptance": False, "executionAuthorized": False,
            "implementedStages": list(STAGES), "implementedPrerequisites": ["SupplyPickup"], "stage": stage,
            "selection": {k: row[k] for k in ("key", "profileDigest", "asset", "configurationPath", "meshPath", "rigFingerprint")},
            "snapshotNonce": snapshot["nonce"], "evaluatedAtNs": now_ns,
            "sourceInputSequence": inp["generation"], "sourceFeedRevision": feed["revision"] if feed else None,
            "sourceFeedLifetime": feed["lifetime"] if feed else None,
            "sourceValidUntilNs": source_until, "owner": snapshot["owner"],
            "heldSupply": supply_object, "requiresPostPickupReobservation": supply_pickup,
            "requiresPostAcquisitionReobservation": not supply_pickup and not continuation,
            "fieldSources": {"mapping": "original admitted shot input; LH row metres, never rescale rotation/translation",
                             "referenceAndGrips": "same original input generation and XR tracking/reference space",
                             "calibration": "F * inverse(B) * inverse(C(grip) * inverse(C(referenceHead))); checked against actual bind",
                             "motion": "production BuildBc2FeedMotion export", "start": source_description},
            "calibration": calibration, "pickup": ("neutral approach to production supply contact" if supply_pickup else
                "actual observed offset" if use_actual_pickup else "neutral approach to authored bone-local wrist"),
            "preserve": {"referenceHead": inp["referenceHead"], "head": inp["head"], "rightGrip": inp["hands"][1]["grip"],
                         "spaceGeneration": inp["spaceGeneration"], "trackingBasisGeneration": inp["trackingBasisGeneration"],
                         "worldUnitsPerMeter": inp["worldUnitsPerMeter"]},
            "executionConditions": ["Geometric plan only: existing native preflight and exact previous-stage receipt must admit the requested stage.",
                                    "Reserve pickup ends held: reobserve actual custody/relocated pouch before AttachContainer; its continuation retains grip.",
                                    "After each fresh squeeze, reobserve the exact owned Feed mechanism claim before any motion; an xr-sim acknowledgement is insufficient.",
                                    "Other start/regrasps require fresh neutral; reobserve after neutral, owner, reference, weapon/body basis or right-hand changes.",
                                    "Before each batch require fresh matching owner/config/mesh/rig/reference evidence; this snapshot lease is never extended by this schedule.",
                                    "Wait for xr-sim cmdSeq, lastCmdError=0 and committed frame acknowledgement; abort if actual input gap exceeds policy.",
                                    "Do not execute subsequent stages until the real runtime receipt is observed; presentation operation is not native eligibility."],
            "policy": policy, "durationNs": schedule[-1]["atNs"], "gestureNs": gesture_ns,
            "verification": {"everyFormattedCommandRoundTripped": True, "maximumPositionErrorMeters": max_position_error,
                             "maximumAngleErrorRadians": max_angle_error, "maximumStepMeters": max(d for d, _ in measured_steps),
                             "maximumStepRadians": max(a for _, a in measured_steps), "endpointHeldNs": 0 if supply_pickup else math.ceil(dwell/step_ns)*step_ns},
            "profileSourceHashes": row["sourceHashes"], "schedule": schedule}


def plan_supply_pickup(profiles, snapshot, now_ns, expected_nonce, step_ns=20_000_000, contact="chest"):
    """Plan neutral chest/belt pickup only; attachment requires fresh custody."""
    return plan(profiles, snapshot, "SupplyPickup", now_ns, expected_nonce, step_ns, contact=contact)


def load_json(path, maximum):
    data = Path(path).read_bytes()
    require(len(data) <= maximum, "input exceeds bounded size")
    def reject_constant(value):
        raise Refusal("nonfinite JSON constant: "+value)
    return json.loads(data.decode("utf-8-sig"), parse_constant=reject_constant), hashlib.sha256(data).hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profiles", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--nonce", type=int, required=True, help="exact requested snapshot nonce")
    parser.add_argument("--stage", choices=[*STAGES, "SupplyPickup"], required=True)
    parser.add_argument("--supply-contact", choices=("chest", "alternate"), default="chest")
    parser.add_argument("--pickup-plan", type=Path, help="prior SupplyPickup JSON; require fresh post-pickup observation for AttachContainer")
    parser.add_argument("--output", type=Path, required=True, help="offline JSON artifact; command.txt/state.json refused")
    parser.add_argument("--now-ns", type=int, help="explicit historical QPC nanoseconds; omitted uses local perf_counter_ns")
    parser.add_argument("--step-ms", type=int, default=20)
    args = parser.parse_args(argv)
    try:
        require(args.output.suffix.lower() == ".json" and args.output.name.lower() not in
                {"command.json", "state.json", "mechanism-snapshot.json"}, "output must be a separate offline JSON artifact")
        require(args.output.resolve() not in (args.profiles.resolve(), args.snapshot.resolve()), "output cannot replace source evidence")
        profiles, profile_hash = load_json(args.profiles, 8*1024*1024)
        snapshot, snapshot_hash = load_json(args.snapshot, SNAPSHOT_LIMIT)
        evaluated = args.now_ns if args.now_ns is not None else time.perf_counter_ns()
        pickup = load_json(args.pickup_plan, 8*1024*1024)[0] if args.pickup_plan else None
        require(pickup is None or args.stage == "AttachContainer", "pickup prerequisite only applies to AttachContainer")
        result = plan(profiles, snapshot, args.stage, evaluated, args.nonce, args.step_ms*1_000_000,
                      pickup_plan=pickup, contact=args.supply_contact)
        result["evaluationClock"] = "explicit historical QPC nanoseconds" if args.now_ns is not None else "local QPC perf_counter_ns"
        result["sourceHashes"] = {"profilesJson": profile_hash, "snapshotJson": snapshot_hash,
                                  "planner": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                                  "coordinateReport": COORDINATE_REPORT_SHA256,
                                  "xrSimRevision": XRSIM_REVISION, "xrSimMath": XRSIM_MATH_SHA256}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")
        print(f"Offline {args.stage}: {len(result['schedule'])} bounded samples; native acceptance=false; {args.output}")
        return 0
    except (Refusal, KeyError, TypeError, ValueError, OSError, OverflowError) as error:
        print(f"Refused offline trajectory: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
