"""Offline comparison of captured weapon bone poses; no process access or writes.

Generic bone names are reported exactly as captured. Pose changes never establish
a sight/handle role, a native mode relationship, or permission to edit animation.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
import math
from pathlib import Path
import sys

import numpy as np
import weapon_profile_pipeline as profiles


def hidden_matrix(value, tolerance=.005):
    """Validate the declared collapsed leaf without attempting a rigid inverse."""
    try:
        result = np.asarray(value, dtype=float).reshape(4, 4)
    except (ValueError, TypeError) as exc:
        raise ValueError("hidden matrix must contain 16 numeric values") from exc
    if not np.isfinite(result).all():
        raise ValueError("hidden matrix is non-finite")
    if not np.allclose(result[:, 3], [0, 0, 0, 1], atol=tolerance, rtol=0):
        raise ValueError("hidden matrix is not affine")
    scale = np.linalg.svd(result[:3, :3], compute_uv=False)
    if max(scale) > .001 or min(scale) <= 0 or np.linalg.det(result[:3, :3]) <= 0:
        raise ValueError("hidden leaf is not a finite positive collapsed transform")
    return result


def snapshot(row, policy):
    nodes = row.get("native_weapon_bones")
    if not isinstance(nodes, list) or not nodes:
        raise ValueError("missing native_weapon_bones")
    if row.get("weapon_bones_complete") is not True or row.get("weapon_bones_dropped", 0) != 0:
        raise ValueError("weapon bone snapshot is incomplete")
    root_name = row.get("bone_roles", {}).get("weapon_root")
    if not isinstance(root_name, str) or not root_name:
        raise ValueError("missing weapon root role")
    weapon = profiles.matrix(row.get("native"), policy.rigid_tolerance)
    units = row.get("units_per_meter")
    if isinstance(units, bool) or not isinstance(units, (int, float)) or not math.isfinite(units) or units <= 0:
        raise ValueError("finite positive units_per_meter is required")
    by_name = {}
    for node in nodes:
        if not isinstance(node, dict):
            raise ValueError("weapon bone is not an object")
        name = node.get("name")
        parent = node.get("parent_name")
        if not isinstance(name, str) or not name or len(name) > 127:
            raise ValueError("invalid weapon bone name")
        if name in by_name:
            raise ValueError("duplicate weapon bone name")
        if parent is not None and (not isinstance(parent, str) or not parent):
            raise ValueError("invalid parent_name")
        if not isinstance(node.get("hidden"), bool):
            raise ValueError("bone hidden state must be explicit")
        by_name[name] = node
    if root_name not in by_name:
        raise ValueError("weapon root is absent from inclusive snapshot")
    for name, node in by_name.items():
        if name == root_name:
            if node["hidden"]:
                raise ValueError("weapon root cannot be hidden")
            if node["parent_name"] in by_name:
                raise ValueError("weapon root parent must be outside the captured subtree")
            continue
        visited = set()
        at = name
        while at != root_name:
            if at in visited:
                raise ValueError("weapon hierarchy contains a cycle")
            visited.add(at)
            if at not in by_name:
                raise ValueError("bone does not descend from captured root")
            at = by_name[at]["parent_name"]
            if at is None:
                raise ValueError("bone does not descend from captured root")
    inverse_weapon = np.linalg.inv(weapon)
    result = {}
    for name, node in by_name.items():
        if node["hidden"]:
            if any(other["parent_name"] == name for other in by_name.values()):
                raise ValueError("hidden bone must be a leaf")
            hidden_matrix(node.get("native"), policy.rigid_tolerance)
            result[name] = {"parent_name": node["parent_name"], "hidden": True, "transform": None}
            continue
        value = profiles.matrix(node.get("native"), policy.rigid_tolerance)
        if name == root_name and not np.allclose(value, weapon, atol=1e-5, rtol=0):
            raise ValueError("inclusive root matrix disagrees with native weapon matrix")
        relative = value @ inverse_weapon
        relative[3, :3] /= units
        profiles.matrix(relative, policy.rigid_tolerance)
        result[name] = {"parent_name": node["parent_name"], "hidden": False, "transform": relative}
    return root_name, result


def episode_measurements(rows, segment, policy):
    counts = Counter()
    issues = []
    bone_rows = {}
    topology = None
    root_name = None
    last_sequence = -1
    for index in range(segment["first_row"], segment["last_row"] + 1):
        row = rows[index]
        counts["rows"] += 1
        sequence = row.get("capture_sequence", row.get("generation", -1))
        if sequence <= last_sequence:
            counts["rejected_sequence"] += 1
            issues.append({"row": index, "reason": "duplicate or out-of-order capture sequence"})
            continue
        last_sequence = sequence
        if row.get("attachment_pending") is not False:
            counts["pending"] += 1
            continue
        try:
            captured_root, bones = snapshot(row, policy)
            current_topology = sorted((name, value["parent_name"]) for name, value in bones.items())
            if topology is None:
                topology = current_topology
                root_name = captured_root
            elif topology != current_topology or root_name != captured_root:
                raise ValueError("bone topology changed inside one capture episode")
            counts["settled_complete"] += 1
            for name, value in bones.items():
                entry = bone_rows.setdefault(name, {"parent_name": value["parent_name"], "visible": [], "hidden_rows": [], "hidden_times": []})
                if value["hidden"]:
                    entry["hidden_rows"].append(index)
                    entry["hidden_times"].append(row["captured_ms"])
                else:
                    entry["visible"].append({"transform": value["transform"], "row": index,
                                             "captured_ms": row["captured_ms"]})
        except (ValueError, TypeError, KeyError, np.linalg.LinAlgError) as exc:
            counts["rejected_snapshots"] += 1
            issues.append({"row": index, "reason": str(exc)})
    measurements = {}
    for name, value in sorted(bone_rows.items()):
        visible = profiles.stable_measurement(value["visible"], policy)
        hidden_count = len(value["hidden_rows"])
        hidden_span = max(value["hidden_times"]) - min(value["hidden_times"]) if hidden_count else None
        longest_hidden = consecutive = 0
        previous_hidden = None
        for index in value["hidden_rows"]:
            consecutive = consecutive + 1 if previous_hidden is not None and index == previous_hidden + 1 else 1
            previous_hidden = index
            longest_hidden = max(longest_hidden, consecutive)
        hidden_stable = hidden_count >= policy.min_samples and longest_hidden >= policy.min_samples and hidden_span >= policy.min_stable_span_ms
        state = ("mixed_visibility" if hidden_count and value["visible"] else
                 "hidden" if hidden_stable else
                 "insufficient_hidden_samples" if hidden_count else "visible")
        measurements[name] = {
            "semantic_role": None, "parent_name": value["parent_name"],
            "visibility": state, "hidden_rows": value["hidden_rows"],
            "hidden_span_ms": hidden_span, "longest_consecutive_hidden": longest_hidden,
            "visible_pose": visible}
    return {
        "asset_name": segment["asset_id"], "stable_id": segment["stable_id"],
        "capture_episode": segment["capture_episode"], "actor_local": segment["actor_local"],
        "pose_asset_attribution": segment["pose_asset_attribution"],
        "pose_asset_binding_verified": False, "usable_as_verified_grasp": False,
        "owner_generation": segment["owner_generation"], "space": segment["space"],
        "skeleton": segment["skeleton_local"], "rig_signature": segment["rig_signature"],
        "first_row": segment["first_row"], "last_row": segment["last_row"],
        "root_name": root_name, "topology": topology,
        "counts": dict(counts), "issues": issues, "bones": measurements}


def comparable(a, b):
    if a["asset_name"] is None or b["asset_name"] is None:
        return "asset identity is unresolved"
    for key in ("actor_local", "owner_generation", "space", "skeleton", "rig_signature", "root_name"):
        if a[key] != b[key]:
            return f"{key} differs"
    if not a["topology"] or a["topology"] != b["topology"]:
        return "named parent topology differs or is unavailable"
    return None


def pose_difference(a, b, policy):
    if a["visibility"] == "hidden" and b["visibility"] == "hidden":
        return {"status": "both_hidden", "changed": False}
    if {a["visibility"], b["visibility"]} == {"hidden", "visible"}:
        visible = b if b["visibility"] == "visible" else a
        if visible["visible_pose"]["status"] == "candidate":
            return {"status": "visibility_change", "changed": True,
                    "from_visibility": a["visibility"], "to_visibility": b["visibility"]}
    if a["visibility"] != "visible" or b["visibility"] != "visible":
        return {"status": "unresolved_visibility", "changed": None}
    if a["visible_pose"]["status"] != "candidate" or b["visible_pose"]["status"] != "candidate":
        return {"status": "insufficient_or_unstable_pose", "changed": None}
    first = profiles.matrix(a["visible_pose"]["matrix"], policy.rigid_tolerance)
    second = profiles.matrix(b["visible_pose"]["matrix"], policy.rigid_tolerance)
    position, angle = profiles.distance(first, second)
    return {"status": "measured", "changed": not profiles.near(first, second, policy),
            "position_change_m": position, "angle_change_degrees": angle,
            "position_delta_m": (second[3, :3] - first[3, :3]).tolist(),
            "from_row": a["visible_pose"]["representative_row"],
            "to_row": b["visible_pose"]["representative_row"]}


def transition(a, b, policy):
    result = {"from_asset": a["asset_name"], "to_asset": b["asset_name"],
              "from_episode": a["capture_episode"], "to_episode": b["capture_episode"],
              "status": "candidate_observation", "mechanism_verified": False,
              "bone_changes": []}
    reason = comparable(a, b)
    if reason:
        result.update(status="not_comparable", reason=reason)
        return result
    for name in sorted(a["bones"].keys() & b["bones"].keys()):
        comparison = pose_difference(a["bones"][name], b["bones"][name], policy)
        result["bone_changes"].append({"name": name, "semantic_role": None, **comparison})
    return result


def hinge_candidate(closed, opened, policy, repeatable=True, minimum_angle_degrees=5.):
    """Fit an unnamed stationary pivot line to an observed repeatable rigid change.

    For row vectors the relative root-space motion is inverse(closed) * opened.
    A stationary point p satisfies (I-R)^T p = t. The minimum-norm solution picks
    the point on the otherwise unbounded pivot line closest to the root origin.
    """
    result = {"status": "unverified", "semantic_role": None, "mechanism_verified": False,
              "coordinate_space": "weapon_root_metres", "minimum_rotation_degrees": minimum_angle_degrees}
    if not repeatable:
        return {**result, "status": "nonrepeatable"}
    if closed.get("status") != "candidate" or opened.get("status") != "candidate":
        return {**result, "status": "insufficient_stable_pose"}
    first = profiles.matrix(closed["matrix"], policy.rigid_tolerance)
    second = profiles.matrix(opened["matrix"], policy.rigid_tolerance)
    delta = np.linalg.inv(first) @ second
    # Only accepted near-rigid roundoff is projected for the linear fit. Source
    # and representative matrices remain untouched in the artifact.
    u, _, vh = np.linalg.svd(delta[:3, :3])
    rotation = u @ vh
    if np.linalg.det(rotation) <= 0:
        return {**result, "status": "improper_relative_rotation"}
    radians = math.acos(float(np.clip((np.trace(rotation) - 1) / 2, -1, 1)))
    angle = math.degrees(radians)
    result["rotation_degrees"] = angle
    result["relative_translation_m"] = delta[3, :3].tolist()
    result["observed_position_spread_m"] = (
        closed.get("inlier_max_position_deviation_m", 0.) + opened.get("inlier_max_position_deviation_m", 0.))
    result["observed_angle_spread_degrees"] = (
        closed.get("inlier_max_angle_deviation_degrees", 0.) + opened.get("inlier_max_angle_deviation_degrees", 0.))
    result["fit_basis_adjustment_max"] = float(np.max(np.abs(rotation - delta[:3, :3])))
    if angle < minimum_angle_degrees:
        return {**result, "status": "insufficient_rotation",
                "reason": "Near-zero or translation-only changes do not constrain a useful pivot line."}
    system = (np.eye(3) - rotation).T
    _, singular, basis = np.linalg.svd(system)
    axis = basis[-1].copy()
    skew = np.array([rotation[1, 2] - rotation[2, 1],
                     rotation[2, 0] - rotation[0, 2],
                     rotation[0, 1] - rotation[1, 0]])
    if np.linalg.norm(skew) > 1e-8:
        if np.dot(axis, skew) < 0:
            axis *= -1
    elif axis[int(np.argmax(np.abs(axis)))] < 0:
        axis *= -1
    point, _, rank, _ = np.linalg.lstsq(system, delta[3, :3], rcond=1e-6)
    residual = system @ point - delta[3, :3]
    residual_length = float(np.linalg.norm(residual))
    result["stationary_fit_residual_m"] = residual_length
    result["rank"] = int(rank)
    result["nonzero_singular_values"] = singular[:2].tolist()
    result["translation_to_pivot_sensitivity"] = 1 / float(singular[1]) if singular[1] > 1e-9 else None
    result["uncertainty_note"] = (
        "Observed spread and geometric sensitivity only; not a confidence interval. "
        "The point along the axis is unconstrained; no mesh, sight identity or interaction bounds are established.")
    if rank != 2:
        return {**result, "status": "ill_conditioned"}
    if residual_length > policy.position_tolerance_m:
        return {**result, "status": "non_hinge_rigid_change",
                "reason": "Translation along the rotation axis leaves no stationary hinge line within tolerance."}
    return {**result, "status": "pivot_line_candidate", "axis_direction": axis.tolist(),
            "point_on_axis_m": point.tolist()}


def round_trip(a, b, c, policy):
    result = {"asset_sequence": [a["asset_name"], b["asset_name"], c["asset_name"]],
              "episodes": [a["capture_episode"], b["capture_episode"], c["capture_episode"]],
              "status": "candidate_observation", "mechanism_verified": False, "repeatable_changes": [],
              "unresolved_bones": [], "nonreturning_bones": []}
    reason = comparable(a, b) or comparable(a, c)
    if reason:
        result.update(status="not_comparable", reason=reason)
        return result
    for name in sorted(a["bones"].keys() & b["bones"].keys() & c["bones"].keys()):
        first = pose_difference(a["bones"][name], b["bones"][name], policy)
        returned = pose_difference(a["bones"][name], c["bones"][name], policy)
        if first["changed"] is None or returned["changed"] is None:
            result["unresolved_bones"].append(name)
        elif returned["changed"]:
            result["nonreturning_bones"].append(name)
        elif first["changed"]:
            change = {"name": name, "semantic_role": None,
                      "outbound": first, "return_to_initial": returned}
            if first["status"] == "measured":
                change["hinge_candidate"] = hinge_candidate(
                    a["bones"][name]["visible_pose"], b["bones"][name]["visible_pose"], policy)
            result["repeatable_changes"].append(change)
    return result


def audit_capture(path, policy):
    path = Path(path).resolve()
    base = profiles.audit_capture(path, [], policy)
    raw = profiles.load_json(path).get("gameplay", {}).get("rig_publication", {}).get("weapon_profile_samples")
    if not isinstance(raw, list):
        raise ValueError("mechanism analysis requires named weapon_profile_samples")
    episodes = [episode_measurements(raw, segment, policy) for segment in base["segments"]]
    transitions = [transition(a, b, policy) for a, b in zip(episodes, episodes[1:])
                   if a["asset_name"] != b["asset_name"]]
    round_trips = [round_trip(a, b, c, policy) for a, b, c in zip(episodes, episodes[1:], episodes[2:])
                   if a["asset_name"] == c["asset_name"] and a["asset_name"] != b["asset_name"]]
    return {"path": base["path"], "sha256": base["sha256"], "capture_checks": base["checks"],
            "collector": base["collector"], "identity_issues": base["issues"],
            "base_rejections": [{"first_row": s["first_row"], "issues": s["issues"], "counts": s["counts"]}
                                for s in base["segments"]],
            "episodes": episodes, "transitions": transitions, "round_trips": round_trips}


def build_batch(paths, policy=None):
    policy = policy or profiles.Policy()
    captures, errors = [], []
    for path in sorted({str(Path(value).resolve()) for value in paths}):
        try:
            captures.append(audit_capture(path, policy))
        except (ValueError, TypeError, KeyError, OSError, np.linalg.LinAlgError) as exc:
            errors.append({"path": path, "reason": str(exc)})
    return {"schema": "fvr.weapon_mechanism_candidates", "schema_version": 1,
            "runtime_enabled": False, "mechanism_verified": False,
            "convention": "canonical row-vector LH; bone_world * inverse(weapon_world); local translations in metres",
            "limitations": ["Generic bone names have no inferred semantic role.",
                            "Different equipped assets do not establish a native underbarrel mode relationship.",
                            "Pose changes do not identify a sight, hinge, handle or interactable region.",
                            "No transform is installed or written back to animation."],
            "captures": captures, "errors": errors}


def strict_incomplete(batch):
    if batch["errors"] or not batch["captures"]:
        return True
    for capture in batch["captures"]:
        if capture["identity_issues"] or capture["capture_checks"]["status"] == "failed":
            return True
        if any((capture["collector"] or {}).get(key, 0) for key in ("invalid_samples", "capacity_dropped")):
            return True
        for rejection in capture["base_rejections"]:
            if rejection["issues"] or any(rejection["counts"].get(key, 0) for key in
                                         ("invalid_transform", "duplicate_generation", "out_of_order_generation")):
                return True
        if not capture["episodes"] or not capture["round_trips"]:
            return True
        for episode in capture["episodes"]:
            if episode["issues"] or not episode["bones"]:
                return True
            for value in episode["bones"].values():
                if value["visibility"] == "hidden":
                    continue
                if value["visibility"] != "visible" or value["visible_pose"]["status"] != "candidate":
                    return True
        if any(t["status"] == "not_comparable"
               or any(change["changed"] is None for change in t["bone_changes"])
               for t in capture["transitions"]):
            return True
        if any(t["status"] == "not_comparable" or t["unresolved_bones"] or t["nonreturning_bones"] for t in capture["round_trips"]):
            return True
    return False


def render_audit(batch):
    lines = ["# Weapon mechanism pose audit", "",
             "Observed candidates only. No sight, handle, hinge or native weapon-mode binding is established.", ""]
    for capture in batch["captures"]:
        lines.extend([f"## {Path(capture['path']).parent.name}", "",
                      f"Capture checks: {capture['capture_checks']['status']}. Episodes: {len(capture['episodes'])}.", ""])
        for episode in capture["episodes"]:
            lines.append(f"- {episode['asset_name']}, episode {episode['capture_episode']}, rows "
                         f"{episode['first_row']}–{episode['last_row']}: "
                         f"{episode['counts'].get('settled_complete', 0)} settled snapshots; "
                         f"{episode['counts'].get('rejected_snapshots', 0)} rejected.")
            for issue in episode["issues"]:
                lines.append(f"  - Row {issue['row']}: {issue['reason']}.")
        for trip in capture["round_trips"]:
            lines.extend(["", "### " + " → ".join(str(x) for x in trip["asset_sequence"]), ""])
            if trip["status"] == "not_comparable":
                lines.append("Not comparable: " + trip["reason"] + ".")
                continue
            lines.append("| Bone name (role unknown) | Outbound change | Return consistency |")
            lines.append("| --- | --- | --- |")
            for change in trip["repeatable_changes"]:
                delta = change["outbound"]
                description = (f"{delta['position_change_m'] * 1000:.3f} mm / {delta['angle_change_degrees']:.3f} degrees"
                               if delta["status"] == "measured" else delta["status"])
                lines.append(f"| {change['name']} | {description} | Within audit tolerance |")
                hinge = change.get("hinge_candidate")
                if hinge:
                    lines.append(f"| {change['name']} unnamed pivot analysis | {hinge['status']} | "
                                 f"Fit residual {hinge.get('stationary_fit_residual_m', 'unconstrained')} m |")
            if not trip["repeatable_changes"]:
                lines.append("No repeatable changed bone pose was established.")
            if trip["unresolved_bones"]:
                lines.append("Insufficient/unstable bones: " + ", ".join(trip["unresolved_bones"]) + ".")
            if trip["nonreturning_bones"]:
                lines.append("Bones that did not return: " + ", ".join(trip["nonreturning_bones"]) + ".")
    for error in batch["errors"]:
        lines.append(f"- ERROR {error['path']}: {error['reason']}")
    lines.extend(["", "A repeatable equipped-asset difference is a discovery lead. It is not a verified physical interaction.",
                  "Native ownership, semantic role, interaction bounds and restoration still need separate evidence.", ""])
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--strict", action="store_true", help="require complete stable comparable return-trip evidence")
    args = parser.parse_args(argv)
    paths = [Path(p) / "native-trace.json" if Path(p).is_dir() else Path(p) for p in args.trace]
    batch = build_batch(paths)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "candidates.json").write_text(json.dumps(batch, indent=2, allow_nan=False) + "\n", encoding="utf8")
    (args.output / "audit.md").write_text(render_audit(batch), encoding="utf8")
    print(f"{len(batch['captures'])} captures; {sum(len(c['round_trips']) for c in batch['captures'])} return trips; "
          f"{args.output / 'audit.md'}")
    return 1 if args.strict and strict_incomplete(batch) else 0


if __name__ == "__main__":
    sys.exit(main())
