"""Offline, evidence-only weapon profile batch audit. Never opens a process.

Requires numpy, as do the other native matrix checkers in this workspace.
Candidates are measurements for review, not installed runtime configuration.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
import hashlib
import json
import math
from pathlib import Path
import re
import sys

import numpy as np


FEATURES = ("aim_alignment", "support_grip", "translated_muzzle")
STATUSES = ("unverified", "native_verified", "headset_accepted")
ASSET = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_./:-]*$")


@dataclass(frozen=True)
class Policy:
    min_samples: int = 8
    position_tolerance_m: float = .003
    angle_tolerance_degrees: float = 2.
    inlier_fraction: float = .8
    rigid_tolerance: float = .005
    min_stable_span_ms: float = 150.


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_json(path):
    def reject(value):
        raise ValueError(f"non-finite JSON number {value}")
    return json.loads(Path(path).read_text(encoding="utf-8-sig"), parse_constant=reject)


def matrix(value, tolerance=.005):
    try:
        result = np.asarray(value, dtype=float)
    except (TypeError, ValueError) as exc:
        raise ValueError("matrix is not numeric") from exc
    if result.size != 16:
        raise ValueError("matrix must contain 16 values")
    result = result.reshape(4, 4)
    if not np.isfinite(result).all():
        raise ValueError("matrix has non-finite values")
    if not np.allclose(result[:, 3], [0, 0, 0, 1], atol=tolerance, rtol=0):
        raise ValueError("matrix is not affine row-vector format")
    rotation = result[:3, :3]
    if (np.max(np.abs(rotation @ rotation.T - np.eye(3))) > tolerance
            or abs(np.linalg.det(rotation) - 1) > tolerance):
        raise ValueError("matrix is not a proper rigid transform")
    return result


def distance(a, b):
    translation = float(np.linalg.norm(a[3, :3] - b[3, :3]))
    # Normalize tiny native floating point deviations for a meaningful angle.
    ua, _, va = np.linalg.svd(a[:3, :3])
    ub, _, vb = np.linalg.svd(b[:3, :3])
    relative = (ua @ va) @ (ub @ vb).T
    angle = math.degrees(math.acos(float(np.clip((np.trace(relative) - 1) / 2, -1, 1))))
    return translation, angle


def near(a, b, policy):
    position, angle = distance(a, b)
    return position <= policy.position_tolerance_m and angle <= policy.angle_tolerance_degrees


def stable_measurement(samples, policy):
    """Select an observed transform, never average incompatible animation poses."""
    if len(samples) < policy.min_samples:
        return {"status": "insufficient_samples", "sample_count": len(samples)}
    transforms = [sample["transform"] for sample in samples]
    neighborhoods = [
        [j for j, other in enumerate(transforms) if near(value, other, policy)]
        for value in transforms
    ]
    medoid = max(range(len(samples)), key=lambda i: (len(neighborhoods[i]), i))
    inliers = neighborhoods[medoid]
    outliers = sorted(set(range(len(samples))) - set(inliers))
    longest = current = 0
    previous = None
    for i in inliers:
        row = samples[i]["row"]
        current = current + 1 if previous is not None and row == previous + 1 else 1
        previous = row
        longest = max(longest, current)
    competing = max((len(set(neighborhoods[i]) & set(outliers)) for i in outliers), default=0)
    times = [samples[i].get("captured_ms") for i in inliers]
    span = max(times) - min(times) if times and all(t is not None for t in times) else None
    sufficient = (len(inliers) >= policy.min_samples
                  and len(inliers) / len(samples) >= policy.inlier_fraction
                  and longest >= policy.min_samples
                  and (span is None or span >= policy.min_stable_span_ms))
    result = {
        "status": "candidate" if sufficient and competing < policy.min_samples else "ambiguous_or_unstable",
        "sample_count": len(samples), "inlier_count": len(inliers),
        "outlier_rows": [samples[i]["row"] for i in outliers],
        "longest_consecutive_inliers": longest, "inlier_span_ms": span,
        "competing_pose_samples": competing,
        "max_position_deviation_m": max(distance(transforms[medoid], t)[0] for t in transforms),
        "max_angle_deviation_degrees": max(distance(transforms[medoid], t)[1] for t in transforms),
        "inlier_max_position_deviation_m": max(distance(transforms[medoid], transforms[i])[0] for i in inliers),
        "inlier_max_angle_deviation_degrees": max(distance(transforms[medoid], transforms[i])[1] for i in inliers),
    }
    if result["status"] == "candidate":
        result["matrix"] = transforms[medoid].reshape(-1).tolist()
        result["representative_row"] = samples[medoid]["row"]
    return result


def reference(value, base, label):
    if not isinstance(value, dict) or not value.get("scope"):
        raise ValueError(f"{label} requires path, sha256 and explicit scope")
    path = (base / value["path"]).resolve()
    actual = digest(path)
    if actual != str(value.get("sha256", "")).lower():
        raise ValueError(f"{label} evidence SHA-256 mismatch: {path}")
    return {"path": str(path), "sha256": actual, "scope": value["scope"]}


def read_reviews(path):
    if not path:
        return {}
    path = Path(path).resolve()
    document = load_json(path)
    if document.get("schema") != "fvr.weapon_profile_reviews" or document.get("schema_version") != 1:
        raise ValueError("unsupported review schema")
    result = {}
    for item in document.get("reviews", []):
        stable_id = item["stable_id"]
        if stable_id in result:
            raise ValueError(f"duplicate review for {stable_id}")
        features = {}
        for name, feature in item.get("features", {}).items():
            if name not in FEATURES or feature.get("verification") not in STATUSES:
                raise ValueError("unknown reviewed feature or verification state")
            state = feature["verification"]
            if state != "unverified" and not item.get("applies_to_checkpoint"):
                raise ValueError("verified review requires applies_to_checkpoint")
            reviewed = {"verification": state, "origin": "explicit_review_manifest"}
            if state != "unverified":
                reviewed["native_evidence"] = reference(feature.get("native_evidence"), path.parent, name)
            if state == "headset_accepted":
                reviewed["headset_evidence"] = reference(feature.get("headset_evidence"), path.parent, name)
            features[name] = reviewed
        result[stable_id] = {"features": features,
                            "manifest": {"path": str(path), "sha256": digest(path)},
                            "applies_to_checkpoint": item.get("applies_to_checkpoint")}
    return result


def read_mapping(path):
    if not path:
        return {}
    path = Path(path).resolve()
    document = load_json(path)
    if document.get("schema") != "fvr.weapon_capture_map" or document.get("schema_version") != 1:
        raise ValueError("unsupported capture mapping schema")
    result = {}
    for entry in document.get("captures", []):
        trace = (path.parent / entry["trace"]).resolve()
        if trace.is_dir():
            trace /= "native-trace.json"
        if str(trace) in result:
            raise ValueError(f"duplicate mapping for {trace}")
        if digest(trace) != str(entry.get("sha256", "")).lower():
            raise ValueError(f"capture mapping SHA-256 mismatch: {trace}")
        result[str(trace)] = entry.get("items", [])
    return result


def asset_identity(row, index, bindings):
    matches = []
    for binding in bindings:
        pointer = binding["weapon"]
        pointer = int(pointer, 0) if isinstance(pointer, str) else pointer
        if pointer != row.get("weapon"):
            continue
        if "owner_generation" in binding and binding["owner_generation"] != row.get("owner_generation"):
            continue
        if not binding.get("first_row", 0) <= index <= binding.get("last_row", 2**63 - 1):
            continue
        matches.append(binding["asset_id"])
    embedded = row.get("asset_name")
    identities = set(matches + ([embedded] if embedded else []))
    if len(identities) > 1:
        raise ValueError("ambiguous capture-local pointer to asset mapping")
    asset = next(iter(identities), None)
    if asset is not None and (not isinstance(asset, str) or not ASSET.fullmatch(asset)):
        raise ValueError("invalid persistent asset name")
    if row.get("profile_id") and asset and row["profile_id"] != "bc2:" + asset:
        raise ValueError("embedded profile_id disagrees with asset_name")
    return asset


def pose_asset_provenance(row):
    """Selected item and sampled rig are separate from submitted mesh identity.

    No supported collector schema supplies a mesh/animation backlink yet.
    Preserve unbound numerical candidates; refuse a forged true proof flag.
    """
    for name in ("pose_asset_binding_verified", "submitted_mesh_binding_verified"):
        if row.get(name, False) is not False:
            raise ValueError("unsupported asserted pose/submitted-mesh binding; selected label is insufficient")
    p = row.get("capture_provenance")
    if p is not None:
        if not isinstance(p, dict) or not isinstance(p.get("rig"), dict):
            raise ValueError("invalid capture provenance")
        for name in ("equipment_generation", "weapon_data", "persistence"):
            v = p.get(name)
            if isinstance(v, bool) or not isinstance(v, int) or v < (0 if name == "persistence" else 1) or v > (2**64-1 if name == "equipment_generation" else 2**32-1):
                raise ValueError("invalid capture provenance " + name)
        for name in ("soldier", "weak", "animation", "skeleton", "pose", "world_header", "world_matrices", "skin_matrices", "evaluated_matrices", "count"):
            v = p["rig"].get(name)
            if isinstance(v, bool) or not isinstance(v, int) or not 0 <= v <= 2**32-1:
                raise ValueError("invalid capture rig identity " + name)
        if p["rig"]["soldier"] != row.get("actor") or any(not p["rig"][n] for n in ("soldier", "weak", "animation", "skeleton", "pose", "count")) or not isinstance(p["rig"].get("native_ik"), bool):
            raise ValueError("capture provenance does not match sampled actor/rig")
    return {"status": "unbound_selected_asset_label", "selected_equipment_and_rig": p,
            "cohort": json.dumps(p, sort_keys=True, separators=(",", ":")),
            "submitted_mesh_binding_verified": False, "usable_as_verified_grasp": False,
            "meaning": "asset_name labels selected configuration; native matrices describe an actor rig, not a proven mesh/animation asset"}


def rig_identity(row, gameplay):
    roles = row.get("bone_roles")
    native = gameplay.get("rig", {})
    topology = [(bone.get("name"), bone.get("parent")) for bone in native.get("bones", [])]
    if roles is None and topology:
        names = {name for name, _ in topology}
        weapon_index = native.get("weapon_bone")
        roles = {
            "left_wrist": "LeftHand" if "LeftHand" in names else None,
            "right_wrist": "RightHand" if "RightHand" in names else None,
            "weapon_root": topology[weapon_index][0] if isinstance(weapon_index, int) and 0 <= weapon_index < len(topology) else None,
        }
    # An item-local role declaration is preferred over a possibly stale global rig snapshot.
    descriptor = {"bone_roles": roles, "topology_fingerprint": row.get("skeleton")} if row.get("bone_roles") else {"bone_roles": roles, "topology": topology}
    signature = hashlib.sha256(json.dumps(descriptor, sort_keys=True).encode()).hexdigest() if roles else "unknown"
    skeleton = row.get("skeleton", row.get("skeleton_identity", native.get("skeleton", "unknown")))
    return signature, roles, json.dumps(skeleton, sort_keys=True)


def capture_checks(document, rig, path, raw=False):
    failures = []
    if document.get("hooks_disabled") is False:
        failures.append("hooks_not_disabled")
    for key in (("source_changes", "packing_failures") if raw else ("source_changes", "packing_failures", "fallback_failures")):
        if rig.get(key, 0):
            failures.append(key)
    if rig.get("native_animation_written") is True:
        failures.append("native_animation_written")
    completion = path.parent / "completion.json"
    done = load_json(completion) if completion.exists() else None
    if done is not None:
        if done.get("bootstrap_exit") != 0 or done.get("game_exited") or done.get("new_crash_report"):
            failures.append("native_completion_failed")
        if done.get("game_responding") is False:
            failures.append("game_not_responding")
    return {
        "status": "failed" if failures else ("passed" if document.get("hooks_disabled") is True and done else "incomplete"),
        "failures": failures,
        "completion": {"path": str(completion), "sha256": digest(completion)} if done else None,
        "publication_warnings": {"fallback_failures": rig.get("fallback_failures", 0)} if raw else {},
        "meaning": "capture integrity only; does not verify a gameplay feature or headset feel",
    }


def audit_capture(path, bindings, policy):
    path = Path(path).resolve()
    document = load_json(path)
    if not isinstance(document, dict):
        raise ValueError("native trace must be an object")
    gameplay = document.get("gameplay", {})
    if not isinstance(gameplay, dict):
        raise ValueError("gameplay evidence must be an object")
    rig = gameplay.get("rig_publication", {})
    if not isinstance(rig, dict):
        raise ValueError("rig publication evidence must be an object")
    # New bounded per-weapon evidence replaces the last-256 debug ring, not adds to it.
    field = "weapon_profile_samples" if "weapon_profile_samples" in rig else "hand_evidence"
    rows = rig.get(field, [])
    if not isinstance(rows, list):
        raise ValueError("weapon evidence is not an array")
    capture = {"path": str(path), "sha256": digest(path), "evidence_field": field,
               "sample_count": len(rows), "collector": rig.get("weapon_profile_capture"),
               "checks": capture_checks(document, rig, path, field == "weapon_profile_samples"),
               "segments": [], "issues": []}
    segments = []
    previous_key = None
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            capture["issues"].append({"row": index, "reason": "sample must be an object"})
            previous_key = None
            continue
        try:
            asset = asset_identity(row, index, bindings)
            for key in ("weapon", "owner_generation", "space", "generation"):
                if not isinstance(row.get(key), int) or isinstance(row[key], bool) or row[key] < 0:
                    raise ValueError(f"missing or invalid {key}")
            attribution = pose_asset_provenance(row)
            signature, roles, skeleton = rig_identity(row, gameplay)
            units = row.get("units_per_meter", 1 if field == "hand_evidence" else None)
            if isinstance(units, bool) or not isinstance(units, (int, float)) or not math.isfinite(units) or units <= 0:
                raise ValueError("new raw capture requires finite positive units_per_meter")
            if field == "weapon_profile_samples":
                captured = row.get("captured_ms")
                if not isinstance(captured, int) or isinstance(captured, bool) or captured < 0:
                    raise ValueError("new raw capture requires captured_ms")
            sequence = row.get("capture_sequence", row["generation"])
            if not isinstance(sequence, int) or isinstance(sequence, bool) or sequence < 0:
                raise ValueError("invalid capture_sequence")
            key = (asset, row.get("actor"), row["owner_generation"], row["weapon"], row["space"], skeleton, signature, row.get("capture_episode"), units, attribution["cohort"])
        except (ValueError, TypeError, KeyError) as exc:
            capture["issues"].append({"row": index, "reason": str(exc)})
            previous_key = None
            continue
        if key != previous_key:
            segments.append({"asset_id": asset, "stable_id": "bc2:" + asset if asset else None,
                             "actor_local": row.get("actor"), "owner_generation": row["owner_generation"], "weapon": row["weapon"],
                             "space": row["space"], "skeleton_local": skeleton,
                             "capture_episode": row.get("capture_episode"), "pose_asset_attribution": attribution,
                             "units_per_meter": units,
                             "units_basis": "explicit" if "units_per_meter" in row else "historical_bc2_adapter_scale_1",
                             "rig_signature": signature, "bone_roles": roles,
                             "first_row": index, "last_row": index,
                             "counts": Counter(), "issues": [], "right": [], "left": [],
                             "seen_generations": set(), "last_generation": -1, "runtime_alignment": [], "captured_ms": []})
        previous_key = key
        segment = segments[-1]
        segment["last_row"] = index
        segment["counts"]["total"] += 1
        if not asset:
            segment["counts"]["unmapped"] += 1
        if sequence in segment["seen_generations"]:
            segment["counts"]["duplicate_generation"] += 1
            continue
        segment["seen_generations"].add(sequence)
        if sequence <= segment["last_generation"]:
            segment["counts"]["out_of_order_generation"] += 1
            continue
        segment["last_generation"] = sequence
        if isinstance(row.get("captured_ms"), (int, float)):
            segment["captured_ms"].append(row["captured_ms"])
        if row.get("attachment_pending") is not False:
            segment["counts"]["pending_or_unknown"] += 1
            continue
        try:
            weapon = matrix(row["native"], policy.rigid_tolerance)
            right = matrix(row["native_right_wrist"], policy.rigid_tolerance)
            left = matrix(row["native_left_wrist"], policy.rigid_tolerance)
            right_local = right @ np.linalg.inv(weapon)
            left_local = left @ np.linalg.inv(weapon)
            right_local[3, :3] /= units
            left_local[3, :3] /= units
            matrix(right_local, policy.rigid_tolerance)
            matrix(left_local, policy.rigid_tolerance)
        except (ValueError, TypeError, KeyError, np.linalg.LinAlgError) as exc:
            segment["counts"]["invalid_transform"] += 1
            segment["issues"].append({"row": index, "reason": str(exc)})
            continue
        segment["counts"]["settled_valid"] += 1
        segment["right"].append({"transform": right_local, "row": index, "captured_ms": row.get("captured_ms")})
        segment["left"].append({"transform": left_local, "row": index, "captured_ms": row.get("captured_ms")})
        arms = row.get("arms", [])
        if len(arms) >= 2 and arms[1].get("tracked") and "right_wrist_matrix" in row and "placed" in row:
            try:
                published = matrix(row["right_wrist_matrix"], policy.rigid_tolerance)
                placed = matrix(row["placed"], policy.rigid_tolerance)
                published_local = published @ np.linalg.inv(placed)
                published_local[3, :3] /= units
                position, angle = distance(right_local, published_local)
                segment["runtime_alignment"].append((position, angle))
            except (ValueError, TypeError, np.linalg.LinAlgError) as exc:
                segment["issues"].append({"row": index, "reason": "published transform: " + str(exc)})
    for segment in segments:
        del segment["seen_generations"]
        del segment["last_generation"]
        times = segment.pop("captured_ms")
        segment["observed_span_ms"] = max(times) - min(times) if len(times) >= 2 else None
        segment["counts"] = dict(segment["counts"])
        segment["right_wrist_in_weapon"] = stable_measurement(segment.pop("right"), policy)
        segment["left_wrist_in_weapon"] = stable_measurement(segment.pop("left"), policy)
        comparison = segment.pop("runtime_alignment")
        segment["published_comparison"] = {
            "samples": len(comparison),
            "max_position_difference_m": max((v[0] for v in comparison), default=None),
            "max_angle_difference_degrees": max((v[1] for v in comparison), default=None),
            "meaning": "diagnostic authored-versus-published attachment; not an acceptance gate",
        }
        for side in ("right", "left"):
            segment[side + "_wrist_in_weapon"]["pose_asset_binding_verified"] = False
            segment[side + "_wrist_in_weapon"]["usable_as_verified_grasp"] = False
        capture["segments"].append(segment)
    return capture


def combine_measurements(segments, key, policy):
    unresolved = [(capture, segment) for capture, segment in segments
                  if segment[key]["status"] == "ambiguous_or_unstable"
                  and capture["checks"]["status"] != "failed"]
    if unresolved:
        return {"status": "unresolved_pose_variation", "observations": 0,
                "ambiguous_segments": [{"capture": capture["path"], "first_row": segment["first_row"]}
                                       for capture, segment in unresolved]}
    measured = [(capture, segment, segment[key]) for capture, segment in segments
                if segment[key]["status"] == "candidate" and capture["checks"]["status"] != "failed"]
    if not measured:
        return {"status": "insufficient_or_unstable_evidence", "observations": 0}
    matrices = [matrix(value["matrix"], policy.rigid_tolerance) for _, _, value in measured]
    maximum_position = maximum_angle = 0.
    for i, a in enumerate(matrices):
        for b in matrices[i + 1:]:
            position, angle = distance(a, b)
            maximum_position = max(maximum_position, position)
            maximum_angle = max(maximum_angle, angle)
    consistent = maximum_position <= policy.position_tolerance_m and maximum_angle <= policy.angle_tolerance_degrees
    capture, segment, representative = max(measured, key=lambda value: value[2]["inlier_count"])
    result = {"status": "candidate" if consistent else "conflicting_stable_poses",
              "observations": len(measured), "max_between_segment_position_m": maximum_position,
              "max_between_segment_angle_degrees": maximum_angle}
    result["pose_asset_binding_verified"] = False
    result["usable_as_verified_grasp"] = False
    if consistent:
        result["matrix"] = representative["matrix"]
        result["provenance"] = {"capture": capture["path"], "sha256": capture["sha256"],
                                "evidence_field": capture["evidence_field"],
                                "row": representative["representative_row"],
                                "owner_generation": segment["owner_generation"], "weapon_local": segment["weapon"],
                                "pose_asset_attribution": segment["pose_asset_attribution"]}
    return result


def build_batch(paths, mapping=None, reviews=None, policy=None):
    policy = policy or Policy()
    if policy.min_samples < 3 or policy.position_tolerance_m <= 0 or policy.angle_tolerance_degrees <= 0 or not .5 < policy.inlier_fraction <= 1:
        raise ValueError("invalid stability policy")
    captures, errors = [], []
    for path in sorted({str(Path(p).resolve()) for p in paths}):
        try:
            captures.append(audit_capture(path, (mapping or {}).get(path, []), policy))
        except (ValueError, OSError, KeyError, TypeError) as exc:
            errors.append({"path": path, "reason": str(exc)})
    groups = defaultdict(list)
    for capture in captures:
        for segment in capture["segments"]:
            if segment["stable_id"]:
                groups[segment["stable_id"]].append((capture, segment))
    profiles = []
    for stable_id, segments in sorted(groups.items()):
        variants = defaultdict(list)
        for capture, segment in segments:
            variants[segment["rig_signature"]].append((capture, segment))
        features = {name: {"verification": "unverified"} for name in FEATURES}
        review = (reviews or {}).get(stable_id)
        if review:
            features.update(review["features"])
        profile = {"stable_id": stable_id, "asset_id": segments[0][1]["asset_id"],
                   "revision": 1, "status": "candidate_only", "runtime_enabled": False,
                   "pose_asset_binding_verified": False, "usable_as_verified_grasp": False,
                   "model_forward": None, "model_up": None, "features": features,
                   "review": review, "rig_variants": [],
                   "unmeasured": ["barrel_axes", "muzzle_origin", "support_contact_gate", "sight_hinge",
                                  "sight_interaction", "projectile_impacts", "controller_orientation_polish"]}
        for signature, variant in sorted(variants.items()):
            profile["rig_variants"].append({
                "rig_signature": signature, "bone_roles": variant[0][1]["bone_roles"],
                "segment_count": len(variant),
                "right_wrist_in_weapon": combine_measurements(variant, "right_wrist_in_weapon", policy),
                "left_wrist_in_weapon": combine_measurements(variant, "left_wrist_in_weapon", policy),
                "sources": [{"capture": capture["path"], "first_row": segment["first_row"],
                             "last_row": segment["last_row"], "actor_local": segment["actor_local"], "owner_generation": segment["owner_generation"],
                             "space": segment["space"], "weapon_local": segment["weapon"]} for capture, segment in variant]})
        profiles.append(profile)
    return {
        "schema": "fvr.weapon_profile_candidates", "schema_version": 1,
        "game": "bc2", "convention": {"layout": "row_major", "vectors": "row", "handedness": "left",
                                     "units": "metres", "relation": "native_wrist * inverse(native_weapon)"},
        "purpose": "offline candidate evidence; not a runtime configuration or acceptance generator",
        "policy": asdict(policy), "profiles": profiles, "captures": captures, "errors": errors,
        "unmapped_segments": sum(not s["asset_id"] for c in captures for s in c["segments"]),
    }


def render_audit(batch):
    lines = ["# Weapon profile batch audit", "",
             "Candidate measurements only. This tool does not enable gameplay features or infer headset acceptance.", "",
             "Asset names label the selected configuration. The sampled actor rig has no proven submitted mesh/animation backlink; numerical stability is not verified grasp evidence.", "",
             f"Captures: {len(batch['captures'])}; items: {len(batch['profiles'])}; "
             f"unmapped segments: {batch['unmapped_segments']}; read errors: {len(batch['errors'])}.", "",
             "| Asset | Rig variants | Right attachment | Left attachment | Feature review |",
             "| --- | ---: | --- | --- | --- |"]
    for profile in batch["profiles"]:
        status = lambda key: ", ".join(v[key]["status"] for v in profile["rig_variants"])
        features = ", ".join(f"{k}: {v['verification']}" for k, v in profile["features"].items())
        lines.append(f"| {profile['asset_id']} | {len(profile['rig_variants'])} | "
                     f"{status('right_wrist_in_weapon')} | {status('left_wrist_in_weapon')} | {features} |")
    lines.extend(["", "## Captures", ""])
    for capture in batch["captures"]:
        lines.append(f"- {capture['path']}: {capture['sample_count']} {capture['evidence_field']} rows; "
                     f"capture checks {capture['checks']['status']}.")
        if capture["collector"]:
            counters = capture["collector"]
            lines.append(f"  - Collector: {counters.get('groups', '?')} groups; "
                         f"{counters.get('observations', '?')} observations; "
                         f"{counters.get('invalid_samples', '?')} invalid; "
                         f"{counters.get('capacity_dropped', '?')} dropped for capacity.")
        if any(capture["checks"]["publication_warnings"].values()):
            lines.append(f"  - Publication warnings (raw capture remains separate): {capture['checks']['publication_warnings']}.")
        for segment in capture["segments"]:
            counts = segment["counts"]
            lines.append(f"  - Rows {segment['first_row']}–{segment['last_row']}, "
                         f"{segment['asset_id'] or 'UNMAPPED'}, owner {segment['owner_generation']}, "
                         f"local pointer {segment['weapon']}: {counts.get('settled_valid', 0)} settled valid; "
                         f"{counts.get('pending_or_unknown', 0)} pending; "
                         f"{counts.get('invalid_transform', 0)} invalid, "
                         f"{counts.get('duplicate_generation', 0)} duplicate, "
                         f"{counts.get('out_of_order_generation', 0)} out of order. "
                         f"Right {segment['right_wrist_in_weapon']['status']}; left {segment['left_wrist_in_weapon']['status']}.")
            for issue in segment["issues"]:
                lines.append(f"    - Rejected row {issue['row']}: {issue['reason']}.")
        for issue in capture["issues"]:
            lines.append(f"  - Rejected row {issue['row']}: {issue['reason']}.")
    for error in batch["errors"]:
        lines.append(f"- ERROR {error['path']}: {error['reason']}")
    lines.extend(["", "Authored hand attachments do not prove muzzle/barrel axes, sight mechanics, support contact gates,",
                  "projectile impacts, or headset feel. Review assertions, if supplied, retain their exact evidence",
                  "file hashes and scope; they do not automatically apply to a newly built mod or bind these sampled poses to the labelled mesh.", ""])
    return "\n".join(lines)


def strict_incomplete(batch):
    if batch["errors"] or batch["unmapped_segments"] or not batch["profiles"]:
        return True
    for capture in batch["captures"]:
        collector = capture["collector"] or {}
        if (capture["issues"] or capture["checks"]["status"] == "failed"
                or collector.get("capacity_dropped", 0) or collector.get("invalid_samples", 0)):
            return True
        for segment in capture["segments"]:
            if segment["issues"] or any(segment["counts"].get(key, 0) for key in
                    ("invalid_transform", "duplicate_generation", "out_of_order_generation")):
                return True
    return any(v[key]["status"] != "candidate"
               for profile in batch["profiles"] for v in profile["rig_variants"]
               for key in ("right_wrist_in_weapon", "left_wrist_in_weapon"))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", action="append", default=[], help="native-trace.json or its directory; repeatable")
    parser.add_argument("--reports-root", type=Path, help="discover immediate native-trace-*/native-trace.json captures")
    parser.add_argument("--mapping", type=Path, help="SHA-bound legacy capture pointer-to-asset mappings")
    parser.add_argument("--reviews", type=Path, help="explicit per-feature native/headset evidence assertions")
    parser.add_argument("--output", type=Path, required=True, help="new or existing output directory")
    parser.add_argument("--min-samples", type=int, default=8)
    parser.add_argument("--strict", action="store_true", help="nonzero exit for errors, unmapped identities or non-candidate measured variants")
    args = parser.parse_args(argv)
    paths = [Path(path) / "native-trace.json" if Path(path).is_dir() else Path(path) for path in args.trace]
    if args.reports_root:
        paths.extend(args.reports_root.glob("native-trace-*/native-trace.json"))
    if not paths:
        parser.error("at least one --trace or --reports-root with captures is required")
    try:
        batch = build_batch(paths, read_mapping(args.mapping), read_reviews(args.reviews), Policy(min_samples=args.min_samples))
    except (ValueError, OSError, KeyError, TypeError) as exc:
        parser.exit(2, f"weapon profile pipeline: {exc}\n")
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "candidates.json").write_text(json.dumps(batch, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    (args.output / "audit.md").write_text(render_audit(batch), encoding="utf-8")
    print(f"{len(batch['captures'])} captures, {len(batch['profiles'])} asset profiles, "
          f"{batch['unmapped_segments']} unmapped segments; {args.output / 'audit.md'}")
    incomplete = strict_incomplete(batch)
    return 1 if args.strict and incomplete else 0


if __name__ == "__main__":
    sys.exit(main())
