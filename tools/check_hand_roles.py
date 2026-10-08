"""Read-only audit of BC2 free, native support, and sight-mechanism hand roles.

Only --output is written. This validates numerical telemetry, not visual skinning,
finger-pad collision, optical hand tracking, or headset comfort.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np

FINGER_NAMES = ("Thumb", "Index", "Middle", "Ring", "Pinky")
EXPECTED_FINGERPRINT = "a7f219a1426216ab"
TRANSLATION_TOLERANCE = 1e-5
ROTATION_TOLERANCE = 1e-4
PHYSICAL_TRANSLATION_CAP_METERS = .0005


def fingerprint(value):
    return str(value).lower().removeprefix("fnv1a64:")


def source(path):
    path = Path(path)
    return {"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def matrix(value):
    m = np.asarray(value, dtype=float)
    if m.size != 16:
        raise ValueError("Matrix must contain exactly 16 numbers")
    m = m.reshape(4, 4)
    if not np.isfinite(m).all() or not np.allclose(m[:, 3], [0, 0, 0, 1], atol=1e-5, rtol=0):
        raise ValueError("Invalid finite affine matrix")
    r = m[:3, :3]
    if np.max(np.abs(r @ r.T - np.eye(3))) > .02 or abs(np.linalg.det(r) - 1) > .02:
        raise ValueError("Invalid proper rigid matrix")
    return m


def vector(value):
    v = np.asarray(value, dtype=float)
    if v.shape != (3,) or not np.isfinite(v).all():
        raise ValueError("Invalid finite 3-vector")
    return v


def unit(v):
    length = float(np.linalg.norm(v))
    if not math.isfinite(length) or length < 1e-7:
        raise ValueError("Degenerate bind anatomy")
    return v / length


def positive_integer(value):
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


def maximum(values):
    return max(values, default=None)


def world_rounding_budget(wrist, grip, reach, units, recovered_locals=1):
    """Conservative float32 affine audit allowance, bounded in physical metres.

    The telemetry recovers local translation by a three-term inverse-translation
    dot product followed by a four-term affine product. Each dot has standard
    gamma_n rounding amplification. Include one stored world-position half-ULP
    per component. Rotation errors are correlated in the cancelling terms; the
    independent rotation checks remain unchanged. Native parent worlds are not
    retained, so use the recorded wrist/grip envelope plus the verified maximum
    finger-chain reach for a conservative magnitude envelope, not a claim that
    every operation's exact rounding error was measured.
    """
    u = 2.0 ** -24
    gamma3, gamma4 = 3 * u / (1 - 3 * u), 4 * u / (1 - 4 * u)
    magnitude = np.maximum(np.abs(wrist[3, :3]), np.abs(grip[3, :3])) + reach
    rounded = magnitude.astype(np.float32)
    ulps = np.spacing(rounded).astype(float)
    if not np.isfinite(ulps).all():
        raise ValueError("World position exceeds finite float32 audit range")
    one_recovery = (gamma3 + gamma4) * float(np.sum(magnitude)) + .5 * float(np.sum(ulps))
    uncapped = TRANSLATION_TOLERANCE + recovered_locals * one_recovery
    cap = PHYSICAL_TRANSLATION_CAP_METERS * units
    return min(uncapped, cap), {"world_max_ulp_m": float(np.max(ulps)) / units,
        "world_magnitude_l1_m": float(np.sum(magnitude)) / units,
        "uncapped_budget_m": uncapped / units, "capped": uncapped > cap}


def check(folder, bind_path):
    folder, bind_path = Path(folder), Path(bind_path)
    trace_path, completion_path = folder / "native-trace.json", folder / "completion.json"
    doc, done, bind = read_json(trace_path), read_json(completion_path), read_json(bind_path)
    rig = doc["gameplay"]["rig_publication"]
    roles = rig.get("hand_roles", {})
    failures = []

    def require(ok, reason):
        if not ok and reason not in failures:
            failures.append(reason)

    require(doc.get("hooks_disabled") is True and doc.get("state") == "observed", "Hooks did not retire")
    require(done.get("game_responding") is True and done.get("game_exited") is False and
            done.get("new_crash_report") is False, "Native completion failed")
    require(rig.get("native_animation_written") is False, "Native animation was changed")
    for key in ("source_changes", "packing_failures", "fallback_failures"):
        require(rig.get(key) == 0, key + " must be zero")
    for key in ("camera_restore_failures", "gpu_failure_stage", "gpu_failure_code"):
        require(doc["native_stream"].get(key) == 0, key + " must be zero")
    require(roles.get("rejected") == 0, "Hand roles were rejected or absent")
    require(fingerprint(roles.get("binding_fingerprint")) == EXPECTED_FINGERPRINT,
            "Unexpected runtime hand binding fingerprint")
    require(fingerprint(bind.get("fingerprint")) == EXPECTED_FINGERPRINT,
            "Unexpected independent bind fingerprint")
    for key in ("read_only", "identity_rechecked", "inverse_bind_rechecked"):
        require(bind.get(key) is True, "Independent bind evidence lacks " + key)
    bind_units = float(bind["units_per_meter"])
    require(math.isfinite(bind_units) and bind_units > 0, "Invalid bind unit scale")
    if not math.isfinite(bind_units) or bind_units <= 0:
        raise ValueError("Invalid bind unit scale")

    bones = {b["name"]: b for b in bind["bones"]}
    if len(bones) != len(bind["bones"]):
        raise ValueError("Duplicate names in bind evidence")
    wrist_index = bones["LeftHand"]["index"]
    wrist_inverse = np.linalg.inv(matrix(bones["LeftHand"]["bind_world"]))
    expected = []
    for finger in FINGER_NAMES:
        parent = wrist_index
        for joint in range(1, 4):
            bone = bones["LeftHand" + finger + str(joint)]
            require(bone["parent"] == parent, "Independent bind finger topology is not a direct chain")
            expected.append(bone)
            parent = bone["index"]
    require(len({b["index"] for b in expected}) == 15, "Independent bind joints are not unique")
    bind_reach = max(sum(float(np.linalg.norm(matrix(b["bind_local_to_parent"])[3, :3]))
                        for b in expected[start:start + 3]) for start in range(0, 15, 3))

    def wrist_point(name):
        return (matrix(bones[name]["bind_world"]) @ wrist_inverse)[3, :3]

    forward = unit(sum((wrist_point("LeftHand" + n + "1") for n in FINGER_NAMES[1:]), np.zeros(3)) / 4)
    across = wrist_point("LeftHandIndex1") - wrist_point("LeftHandPinky1")
    across = unit(across - forward * np.dot(across, forward))
    normal = unit(np.cross(across, forward))
    require(float(wrist_point("LeftHandThumb1") @ normal) / bind_units > 1e-5,
            "Bind thumb does not validate the selected palmar side")
    # OpenXR grip/pose -Z goes little-finger to thumb. The engine conversion
    # flips Z, so across -> +Z, forward -> -Y, palmar normal -> +X. This is
    # deliberately not the different grip_surface/pose contract.
    wrist_to_grip = np.eye(4)
    wrist_to_grip[:3, :3] = np.column_stack((normal, -forward, across))
    matrix(wrist_to_grip)

    pose_counts = roles.get("poses", [])
    require(isinstance(pose_counts, list) and len(pose_counts) == 3 and
            all(positive_integer(n) for n in pose_counts), "All three hand roles require nonzero pose counts")
    rows = roles.get("records", [])
    require(isinstance(rows, list) and 0 < len(rows) <= 256, "Hand role record count must be 1..256")
    if not isinstance(rows, list):
        raise ValueError("Invalid hand role records")
    samples = rig.get("weapon_profile_samples", [])
    units_by_identity = {}
    for sample in samples:
        if fingerprint(sample.get("skeleton")) != EXPECTED_FINGERPRINT:
            continue
        key = (sample["weapon"], sample["owner_generation"], sample["space"])
        units_by_identity.setdefault(key, set()).add(float(sample["units_per_meter"]))

    counts = [0, 0, 0]
    neutral_count = 0
    errors = {k: [] for k in ("local_translation", "support_rotation", "neutral_free_rotation",
                              "bind_reference_rotation", "bind_reference_translation", "free_wrist_rotation",
                              "mechanism_midpoint", "mechanism_rotation_drift", "mechanism_translation_drift")}
    native_differences = [[], [], []]
    mechanism_rows = []
    previous_generation = {}
    mechanism_reference = None
    mechanism_reference_budget = 0.0
    adaptive = {k: [] for k in ("local_translation", "mechanism_midpoint", "mechanism_translation_drift")}
    rounding_evidence = []
    expected_indices = [b["index"] for b in expected]
    expected_parents = [b["parent"] for b in expected]
    for row in rows:
        role = row["role"]
        if isinstance(role, bool) or role not in (0, 1, 2):
            raise ValueError("Unknown hand pose role")
        counts[role] += 1
        require(all(positive_integer(row[k]) for k in ("generation", "owner", "space", "weapon")),
                "Missing hand role identity")
        sequence_key = (row["owner"], row["space"])
        require(row["generation"] >= previous_generation.get(sequence_key, 0), "Hand input generation went backward")
        previous_generation[sequence_key] = row["generation"]
        identity = (row["weapon"], row["owner"], row["space"])
        scales = units_by_identity.get(identity, set())
        require(len(scales) == 1, "Hand identity lacks unique matching weapon-profile unit evidence")
        units = next(iter(scales)) if len(scales) == 1 else bind_units
        require(math.isfinite(units) and units > 0 and abs(units - bind_units) < 1e-7,
                "Hand and independent bind unit scales disagree")
        if not math.isfinite(units) or units <= 0:
            raise ValueError("Invalid hand unit scale")
        squeeze, trigger = float(row["squeeze"]), float(row["trigger"])
        require(all(math.isfinite(x) and 0 <= x <= 1 for x in (squeeze, trigger)), "Invalid hand input scalar")
        wrist, grip, anchor = matrix(row["wrist"]), matrix(row["grip"]), vector(row["mechanism_point"])
        translation_budget, rounding = world_rounding_budget(
            wrist, grip, bind_reach, units, 2 if role == 1 else 1)
        rounding_evidence.append(rounding)
        fingers = row["fingers"]
        require(len(fingers) == 15 and [f["index"] for f in fingers] == expected_indices and
                [f["parent"] for f in fingers] == expected_parents, "Hand joints do not match the verified named topology/order")
        if len(fingers) != 15:
            raise ValueError("Exactly 15 finger joints are required")
        require(len({f["index"] for f in fingers}) == 15, "Duplicate finger joints")
        neutral = role == 0 and abs(squeeze) <= 1e-6 and abs(trigger) <= 1e-6
        neutral_count += int(neutral)
        posed_matrices = []
        for finger, bone in zip(fingers, expected):
            reference, native, posed = (matrix(finger[k]) for k in ("reference", "native", "posed"))
            expected_reference = matrix(bone["bind_local_to_parent"])
            errors["bind_reference_rotation"].append(float(np.max(np.abs(reference[:3, :3] - expected_reference[:3, :3]))))
            errors["bind_reference_translation"].append(float(np.max(np.abs(reference[3, :3] - expected_reference[3, :3]))))
            baseline = native if role == 1 else reference
            local_error = float(np.max(np.abs(posed[3, :3] - baseline[3, :3])))
            errors["local_translation"].append(local_error)
            adaptive["local_translation"].append((local_error, translation_budget))
            rotation_difference = float(np.max(np.abs(posed[:3, :3] - native[:3, :3])))
            native_differences[role].append(rotation_difference)
            if role == 1:
                errors["support_rotation"].append(rotation_difference)
            if neutral:
                errors["neutral_free_rotation"].append(float(np.max(np.abs(posed[:3, :3] - reference[:3, :3]))))
            posed_matrices.append(posed)
        if role == 0:
            errors["free_wrist_rotation"].append(float(np.max(np.abs(wrist[:3, :3] - (wrist_to_grip @ grip)[:3, :3]))))
        if role == 2:
            # Local row-vector chain: distal @ middle @ proximal @ wrist.
            thumb = posed_matrices[2] @ posed_matrices[1] @ posed_matrices[0]
            index = posed_matrices[5] @ posed_matrices[4] @ posed_matrices[3]
            midpoint = (thumb[3, :3] + index[3, :3]) / (2 * units)
            midpoint_error = float(np.linalg.norm(midpoint - anchor))
            errors["mechanism_midpoint"].append(midpoint_error)
            # Three local translations reach each distal joint. Average the two
            # chain bounds and convert the componentwise bound to Euclidean.
            midpoint_budget = min(PHYSICAL_TRANSLATION_CAP_METERS,
                3 * math.sqrt(3) * translation_budget / units)
            adaptive["mechanism_midpoint"].append((midpoint_error, midpoint_budget))
            if mechanism_reference is None:
                mechanism_reference = posed_matrices
                mechanism_reference_budget = translation_budget
            for current, first in zip(posed_matrices, mechanism_reference):
                errors["mechanism_rotation_drift"].append(float(np.max(np.abs(current[:3, :3] - first[:3, :3]))))
                drift = float(np.max(np.abs(current[3, :3] - first[3, :3])))
                errors["mechanism_translation_drift"].append(drift)
                drift_budget = min(PHYSICAL_TRANSLATION_CAP_METERS * units,
                    translation_budget + mechanism_reference_budget)
                adaptive["mechanism_translation_drift"].append((drift, drift_budget))
            mechanism_rows.append((row, wrist))

    require(all(n > 0 for n in counts), "Telemetry must retain Free, WeaponSupport, and MechanismGrip samples")
    if isinstance(pose_counts, list) and len(pose_counts) == 3:
        require(all(isinstance(total, int) and total >= count for total, count in zip(pose_counts, counts)),
                "Recorded hand role samples exceed pose counters")
    require(neutral_count > 0, "No neutral free-hand sample verifies the open reference pose")
    for role, name in ((0, "Free"), (2, "Mechanism")):
        require(max(native_differences[role], default=0) > 1e-3, name + " never differs meaningfully from the native weapon hand")
    limits = {"local_translation": TRANSLATION_TOLERANCE, "support_rotation": ROTATION_TOLERANCE,
              "neutral_free_rotation": ROTATION_TOLERANCE, "bind_reference_rotation": ROTATION_TOLERANCE,
              "bind_reference_translation": TRANSLATION_TOLERANCE, "free_wrist_rotation": ROTATION_TOLERANCE,
              "mechanism_midpoint": TRANSLATION_TOLERANCE, "mechanism_rotation_drift": ROTATION_TOLERANCE,
              "mechanism_translation_drift": TRANSLATION_TOLERANCE}
    for key, pairs in adaptive.items():
        require(bool(pairs) and all(error <= budget for error, budget in pairs),
                key + " exceeds its per-record float32/physical budget or lacks coverage")
    for key, limit in limits.items():
        if key in adaptive:
            continue
        require(bool(errors[key]) and max(errors[key], default=math.inf) <= limit,
                key + " exceeds " + str(limit) + " or lacks coverage")
    mechanism_weapons = sorted({row["weapon"] for row, _ in mechanism_rows})
    mechanism_motion = 0.0
    if mechanism_rows:
        first_wrist = mechanism_rows[0][1]
        mechanism_motion = max(float(np.max(np.abs(wrist - first_wrist))) for _, wrist in mechanism_rows)
    require(len(mechanism_rows) >= 2 and mechanism_motion > .001,
            "Mechanism invariance lacks moving-wrist coverage")
    require(len(mechanism_weapons) >= 2, "Mechanism invariance lacks both native weapon modes")

    sources = {"native_trace": source(trace_path), "completion": source(completion_path),
               "independent_bind": source(bind_path), "checker": source(__file__)}
    root = Path(__file__).resolve().parent.parent
    current_sources = []
    for relative in ("src/games/bc2/Bc2HandPose.h", "include/fvr/interaction/HandPose.h",
                     "src/games/bc2/Bc2RigPublication.cpp", "src/math/StereoMath.cpp"):
        path = root / relative
        if path.is_file():
            current_sources.append(source(path))
    return {"passed": not failures, "failures": failures, "headset_tested": False,
            "visual_skinning_inspected": False, "finger_pad_contact_verified": False,
            "sources": sources, "current_source_files_not_build_provenance": current_sources,
            "binding_fingerprint": fingerprint(roles.get("binding_fingerprint")),
            "pose_counts": pose_counts, "record_counts": counts, "neutral_free_samples": neutral_count,
            "mechanism_weapons": mechanism_weapons, "mechanism_wrist_motion_max_component": mechanism_motion,
            "max_errors": {key: maximum(value) for key, value in errors.items()},
            "max_native_rotation_differences": [maximum(values) for values in native_differences],
            "translation_budget": {
                "nominal_native_units": TRANSLATION_TOLERANCE,
                "physical_cap_m": PHYSICAL_TRANSLATION_CAP_METERS,
                "model": "gamma3+gamma4 float32 inverse/affine dots plus stored world half-ULPs; two recoveries for Support; propagated chain bounds; hard physical cap",
                "max_world_ulp_m": max((x["world_max_ulp_m"] for x in rounding_evidence), default=None),
                "max_world_magnitude_l1_m": max((x["world_magnitude_l1_m"] for x in rounding_evidence), default=None),
                "records_capped": sum(x["capped"] for x in rounding_evidence),
                "maximum_applied_budgets": {k: max((p[1] for p in pairs), default=None) for k, pairs in adaptive.items()},
                "maximum_error_budget_ratios": {k: max((p[0] / p[1] for p in pairs), default=None) for k, pairs in adaptive.items()},
                "limitations": "Parent world translations are not retained; envelope uses wrist/grip plus verified chain reach. This does not establish physical fingertip contact."},
            "wrist_to_grip": wrist_to_grip.tolist(),
            "contract": "grip/pose; canonical across +Z, finger-forward -Y, left palmar +X; joint-center midpoint proxy"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", required=True, type=Path, help="Native report folder")
    parser.add_argument("--bind", required=True, type=Path, help="Independent read-only hand-bind report")
    parser.add_argument("--output", required=True, type=Path, help="Only file this checker writes")
    args = parser.parse_args()
    result = {"passed": False, "headset_tested": False, "visual_skinning_inspected": False}
    try:
        if args.output.resolve() in {args.bind.resolve(), (args.native / "native-trace.json").resolve(),
                                     (args.native / "completion.json").resolve(), Path(__file__).resolve()}:
            parser.error("--output must not overwrite a source input or the checker")
        result = check(args.native, args.bind)
    except (KeyError, ValueError, TypeError, OSError, IndexError, OverflowError, np.linalg.LinAlgError) as exc:
        result["failures"] = [type(exc).__name__ + ": " + str(exc)]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
