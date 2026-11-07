"""Passive candidate only: geometric inversion, no game/commands/clearance authority.

All matrices are canonical LH, row-vector, metres. Reuse production planner math.
Caller must supply current monotonic now_ns; historical replay is only for tests.
This solves a direction (parallel ray), not a finite target or projectile trajectory.
"""
import math
from pathlib import Path
import sys

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import plan_bc2_feed_simulation as frame


def require(ok, why):
    if not ok:
        raise ValueError(why)


def unit(v):
    require(len(v) == 3 and all(math.isfinite(x) for x in v), "invalid direction")
    length = math.sqrt(sum(x*x for x in v))
    require(length > 1e-8, "zero direction")
    return [x/length for x in v]


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def vector(v, m):
    return [sum(v[k]*m[k][j] for k in range(3)) for j in range(3)]


def point(v, m):
    return [x+m[3][j] for j, x in enumerate(vector(v, m))]


def direction_basis(forward, up):
    z = unit(forward)
    x = unit(cross(up, z))
    y = unit(cross(z, x))
    result = frame.identity()
    result[0][:3], result[1][:3], result[2][:3] = x, y, z
    return frame.rigid(result)


def identity(s):
    return {"owner": s["owner"], "equipment": s["equipment"], "rig": s["rig"],
            "physical_equipment_generation": s["physical_equipment_generation"],
            "selection": {k: s["selected"][k] for k in
                          ("configurationPath", "inventory", "selectedSlot", "soleConfiguredArray", "meshes")},
            "reference": s["input"]["referenceHead"],
            "head": s["input"]["head"], "floor_relative": s["input"]["floorRelative"],
            "space": s["input"]["spaceGeneration"],
            "basis": s["input"]["trackingBasisGeneration"],
            "units": s["input"]["worldUnitsPerMeter"]}


def held_evidence(s, now_ns):
    """Join independently sampled Held to its own original input and GunHold.

    The placed weapon and controller pose still come from the outer snapshot's
    exact shot/input generation. Held may consume a different current packet;
    neither packet's generation, pose, owner, or deadline is rewritten.
    """
    h, outer = s["bodyHolster"], s["input_evidence"]
    ev, consumed = h["inputEvidence"], h["input"]
    # Reuse the same owner/physical-generation and original 100ms evidence
    # validator as the ordinary frame and knife paths.
    for sample in (outer, ev):
        frame.evidence(sample, s["owner"], s["physical_equipment_generation"], now_ns)
    require(h["nativeOwner"] == s["owner"] and ev["owner"] == outer["owner"],
            "Held native/physical owner mismatch")
    require(0 < h["sampledNs"] <= now_ns < h["validUntilNs"]
            <= min(ev["deadlineNs"], h["sampledNs"] + 150_000_000),
            "stale or extended Held lease")
    sequence, outer_sequence = ev["sequence"], outer["sequence"]
    relation = "older" if sequence < outer_sequence else "newer" if sequence > outer_sequence else "same"
    require(h["inputSequenceRelation"] == relation
            and h["inputMatchesSnapshot"] is (sequence == outer_sequence),
            "Held independent input ordering mismatch")
    if sequence == outer_sequence:
        require(all(ev[k] == outer[k] for k in ("observedNs", "deadlineNs", "focused", "tracked")),
                "same-sequence Held original evidence differs")
    else:
        require((sequence < outer_sequence) == (ev["observedNs"] < outer["observedNs"])
                and ev["observedNs"] != outer["observedNs"],
                "Held input sequence/time ordering conflicts")
    require(consumed["generation"] == sequence
            and consumed["spaceGeneration"] == s["input"]["spaceGeneration"]
            and consumed["focused"] is True and consumed["headValid"] is True
            and h["neutralTrackedInput"] is True and len(consumed["hands"]) == 2,
            "Held consumed input/space mismatch")
    for hand in consumed["hands"]:
        require(hand["gripTracked"] is True and hand["aimTracked"] is True
                and all(hand[k] == 0 for k in ("held", "squeeze", "trigger", "stickX", "stickY")),
                "nonneutral/untracked Held input")
    gun, slot = h["physicalGun"], h["selectedSlot"]
    require(h["nativeTick"] > 0 and gun["id"] > 0
            and gun["generation"] == s["physical_equipment_generation"]
            and slot["item"]["id"] == s["owner"]["weapon"] and slot["item"]["generation"] > 0,
            "Held selected/physical gun mismatch")
    claim = h["rightClaim"]
    require(h["leftClaim"] is None and claim is not None and h["allowAutomaticGunHold"] is True,
            "support/mechanism or missing GunHold")
    token = claim["token"]
    require(token["id"] > 0 and token["kind"] == 1 and token["hand"] == 1
            and token["owner"] == ev["owner"] and token["item"] == gun
            and token["prerequisiteClaim"] == 0 and claim["inputSequence"] == sequence
            and now_ns < claim["deadlineNs"] <= ev["deadlineNs"],
            "stale/mismatched Held claim")



def untrimmed_aim(grip, aim):
    """Compare two pose channels, not byte-preserved copies of one sample.

    xr-sim normalizes aim after applying even an identity trim. This can change
    more than one float32 component step without changing the represented pose.
    Keep exact translation/metadata and the existing four-epsilon angular cap;
    from_pose retains shape, finiteness and near-unit quaternion validation.
    """
    g, a = frame.from_pose(grip), frame.from_pose(aim)
    return ({k: v for k, v in grip.items() if k != "orientation"}
            == {k: v for k, v in aim.items() if k != "orientation"}
            and frame.pose_error(g, a)[1] <= 4 * 2**-23)

def calibrate(s, now_ns):
    require(s["valid"] is True and s["invalid_reason"] is None, "invalid snapshot")
    require(s["observed_ns"] <= now_ns < min(s["valid_until_ns"], s["native_deadline_ns"],
            s["input_evidence"]["deadlineNs"], s["selected"]["deadlineNs"]), "expired/future snapshot")
    inp, holster = s["input"], s["bodyHolster"]
    require(inp["focused"] and inp["headValid"] and s["input_evidence"]["focused"], "untracked head/focus")
    require(inp["worldUnitsPerMeter"] > 0 and inp["floorRelative"], "unsupported input basis")
    require(inp["generation"] == s["shot_input_generation"] == s["input_evidence"]["sequence"], "input generation mismatch")
    require(inp["spaceGeneration"] == s["owner"]["space"], "space mismatch")
    require(s["selected"]["configurationPathVerified"] is True, "unverified selection")
    require(s["equipment"]["weapon"] == s["owner"]["weapon"], "selected owner mismatch")
    require(not s["weapon_actions_blocked"] and holster["available"] and holster["phase"] == 1
            and not holster["actualEmpty"] and not holster["blockWeaponActions"], "not stable Held")
    held_evidence(s, now_ns)
    for hand in inp["hands"]:
        require(hand["gripTracked"] and hand["aimTracked"] and hand["held"] == 0
                and hand["squeeze"] == hand["trigger"] == hand["stickX"] == hand["stickY"] == 0,
                "nonneutral/untracked input")
    hand = inp["hands"][1]
    require(untrimmed_aim(hand["grip"], hand["aim"]), "aim trim unsupported")
    w = frame.rigid(s["mapping"]["weaponWorldMeters"])
    b = frame.rigid(s["mapping"]["trackingBodyWorldMeters"])
    r = frame.from_pose(inp["referenceHead"])
    h = frame.from_pose(hand["grip"])
    # W = K H R^-1 B; infer K from the actual placed weapon, never a yaw constant.
    k = frame.rigid(frame.multiply(frame.multiply(frame.multiply(w, frame.inverse(b)), r), frame.inverse(h)))
    reconstructed = frame.multiply(frame.multiply(frame.multiply(k, h), frame.inverse(r)), b)
    require(frame.pose_error(reconstructed, w)[0] < .002 and
            frame.pose_error(reconstructed, w)[1] < .005, "mapping round trip failed")
    return {"identity": identity(s), "k": k, "h": h, "r": r, "b": b}


def check_same_mapping(before, after):
    require(before["identity"] == after["identity"], "owner/selection/rig/reference changed")
    residual = frame.pose_error(before["k"], after["k"])
    # Same existing mapping tolerances used by bc2_reload_stimulus_geometry._same.
    require(residual[0] <= .002 and residual[1] <= .005, "weapon/controller mapping changed")
    return residual


def solve_direction(s, now_ns, model_forward, model_up, direction_world, up_world):
    c = calibrate(s, now_ns)
    tracking_from_world = frame.multiply(frame.inverse(c["b"]), c["r"])
    target = direction_basis(vector(direction_world, tracking_from_world), vector(up_world, tracking_from_world))
    local = direction_basis(model_forward, model_up)
    desired_weapon_rotation = frame.multiply(frame.inverse(local), target)
    k_rotation = [row[:] for row in c["k"]]
    k_rotation[3] = [0., 0., 0., 1.]
    desired_hand = frame.multiply(frame.inverse(k_rotation), desired_weapon_rotation)
    desired_hand[3] = c["h"][3][:]  # Preserve the actual admitted grip position.
    desired_hand = frame.rigid(desired_hand)
    predicted = frame.multiply(c["k"], desired_hand)
    actual_direction = unit(vector(model_forward, predicted))
    requested_direction = unit(vector(direction_world, tracking_from_world))
    require(math.dist(actual_direction, requested_direction) < 1e-5, "direction residual")
    pose = frame.to_pose(desired_hand)
    return {"candidate_only": True, "firing_clearance": False, "native_acceptance": False,
            "pose": pose, "ypr": frame.quaternion_ypr(pose["orientation"]),
            "predicted_weapon_tracking_lh_m": predicted,
            "direction_residual": math.dist(actual_direction, requested_direction),
            "identity": c["identity"]}


def solve_head_forward(s, now_ns, model_forward, model_up):
    c = calibrate(s, now_ns)
    head_world = frame.multiply(frame.multiply(frame.from_pose(s["input"]["head"]), frame.inverse(c["r"])), c["b"])
    return solve_direction(s, now_ns, model_forward, model_up, head_world[2][:3], head_world[1][:3])


def project_tracking(point_lh, eye_pose, fov, width, height):
    p = point(point_lh, frame.inverse(frame.from_pose(eye_pose)))
    require(p[2] > 0, "point behind eye")
    l, r, u, d = [math.tan(math.radians(fov[k])) for k in ("l", "r", "u", "d")]
    x, y = (p[0]/p[2]-l)/(r-l)*width, (u-p[1]/p[2])/(u-d)*height
    return {"pixels": [x, y], "inside_frustum": 0 <= x < width and 0 <= y < height,
            "occlusion_checked": False}
