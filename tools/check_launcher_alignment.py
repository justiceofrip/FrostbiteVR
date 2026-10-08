"""Read-only native launcher alignment/support validation, with source hashes."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys

import numpy as np
import weapon_profile_pipeline as profiles

EXPECTED_ASSETS = ["XM8_sp_s", "40mmgl", "XM8_sp_s"]


def vector_angle(a, b):
    a, b = np.asarray(a, dtype=float), np.asarray(b, dtype=float)
    if not np.isfinite(a).all() or not np.isfinite(b).all() or min(np.linalg.norm(a), np.linalg.norm(b)) < 1e-8:
        raise ValueError("invalid direction vector")
    return math.degrees(math.acos(float(np.clip(np.dot(a, b) / np.linalg.norm(a) / np.linalg.norm(b), -1, 1))))


def source(path):
    path = Path(path).resolve()
    return {"path": str(path), "sha256": profiles.digest(path)}


def native_direction(row):
    # BC2's native aim heading differs by pi from canonical RenderView heading.
    yaw = float(row["native_yaw"]) - math.pi
    pitch = float(row["native_pitch"])
    if not math.isfinite(yaw) or not math.isfinite(pitch):
        raise ValueError("nonfinite native aim")
    return np.array([math.sin(yaw) * math.cos(pitch), math.sin(pitch), math.cos(yaw) * math.cos(pitch)])


def check(folder, receiver=None, support=False, sight_start_secondary=False):
    folder = Path(folder).resolve()
    path = folder / "native-trace.json"
    document = profiles.load_json(path)
    done_path = folder / "completion.json"
    done = profiles.load_json(done_path)
    gameplay = document["gameplay"]
    rig = gameplay["rig_publication"]
    failures = []

    def require(okay, reason):
        if not okay:
            failures.append(reason)

    require(document.get("state") == "observed" and document.get("hooks_disabled") is True, "native hooks did not retire")
    require(done.get("bootstrap_exit") == 0 and done.get("game_responding") is True
            and not done.get("game_exited") and not done.get("new_crash_report"), "native completion failed")
    for key in ("source_changes", "packing_failures", "fallback_failures"):
        require(rig.get(key) == 0, key + " must be zero")
    require(rig.get("native_animation_written") is False, "native animation source was modified")
    stream = document.get("native_stream", {})
    for key in ("camera_restore_failures", "gpu_failure_stage", "gpu_failure_code"):
        require(stream.get(key) == 0, "native stream " + key)
    raw = rig.get("weapon_profile_samples", [])
    require(bool(raw), "named raw weapon samples are missing")
    identities = {}
    for row in raw:
        name = profiles.asset_identity(row, 0, [])
        key = (row["weapon"], row["owner_generation"], row["space"])
        identity = (name, row.get("units_per_meter"))
        if key in identities and identities[key] != identity:
            raise ValueError("capture-local pointer has ambiguous asset or units")
        identities[key] = identity

    def identity(row):
        key = (row["weapon"], row["owner_generation"], row["space"])
        if key not in identities:
            raise ValueError("hand sample cannot be identified from same-capture named raw evidence")
        asset, units = identities[key]
        if not asset or isinstance(units, bool) or not isinstance(units, (int, float)) or not math.isfinite(units) or units <= 0:
            raise ValueError("invalid named asset or units")
        return asset, units

    hand_rows = rig.get("hand_evidence", [])
    episodes = []
    for row in hand_rows:
        asset, units = identity(row)
        key = (asset, row["weapon"], row["owner_generation"], row["space"])
        if not episodes or episodes[-1]["key"] != key:
            episodes.append({"key": key, "asset": asset, "units": units, "rows": []})
        episodes[-1]["rows"].append(row)
    sequence = [episode["asset"] for episode in episodes]
    expected_assets = ["40mmgl", "XM8_sp_s", "40mmgl"] if sight_start_secondary else EXPECTED_ASSETS
    require(sequence == expected_assets, "hand evidence does not cover exact " + " -> ".join(expected_assets) + " sequence")

    mode_rows = gameplay.get("weapon_mode_records", [])
    require(gameplay.get("weapon_mode_binding_verified") is True, "native mode binding was not verified")
    require(gameplay.get("weapon_mode_requests") == 2 and gameplay.get("weapon_mode_acknowledgements") == 2
            and gameplay.get("weapon_mode_rejections") == 0 and len(mode_rows) == 2, "mode request/ack coverage is not two complete transitions")
    expected_actions = [36, 33] if sight_start_secondary else [33, 36]
    require([record.get("action") for record in mode_rows] == expected_actions,
            "native action order disagrees with the explicit fixture start mode")
    if len(episodes) == 3 and len(mode_rows) == 2:
        for index, record in enumerate(mode_rows):
            require(record.get("from") == episodes[index]["key"][1]
                    and record.get("target") == episodes[index + 1]["key"][1]
                    and not record.get("cancelled")
                    and record.get("ack_ms", -1) >= record.get("ms", 0), "mode record disagrees with observed equipped assets")
    require(gameplay.get("next_weapon_commands") == 0 and gameplay.get("previous_weapon_commands") == 0,
            "mode fixture used ordinary primary cycling")

    aim_records = gameplay.get("view_basis_evidence", [])
    require(bool(aim_records), "native aim records are absent")
    native_directions = [native_direction(row) for row in aim_records]
    static_native_aim = bool(aim_records) and all(
        abs(math.remainder(row["native_yaw"] - row["body_yaw"], math.tau)) < 1e-4
        and abs(row["native_pitch"]) < 1e-4 for row in aim_records)
    if not support:
        require(static_native_aim, "identity-controller fixture is not static native aim")
    item_aim_coverage = {asset: sum(r.get("weapon") == pointer and r.get("space") == space for r in aim_records)
                         for asset, pointer, _, space in [episode["key"] for episode in episodes]}
    require(item_aim_coverage.get("40mmgl", 0) > 0, "no item-and-space-tagged native aim record covers launcher mode")

    observations = []
    for episode in episodes:
        settled = [row for row in episode["rows"] if row.get("attachment_pending") is False]
        require(len(settled) >= 8, f"{episode['asset']} episode has fewer than eight settled published poses")
        positions, rotations, barrel_angles, native_angles = [], [], [], []
        for row in settled:
            native = profiles.matrix(row["native"])
            placed = profiles.matrix(row["placed"])
            authored = profiles.matrix(row["native_right_wrist"]) @ np.linalg.inv(native)
            published = profiles.matrix(row["right_wrist_matrix"]) @ np.linalg.inv(placed)
            authored[3, :3] /= episode["units"]
            published[3, :3] /= episode["units"]
            position, rotation = profiles.distance(authored, published)
            positions.append(position)
            rotations.append(rotation)
            if not support:
                eye = profiles.matrix(row["eye_base"])
                barrel_angles.append(vector_angle(-placed[2, :3], eye[2, :3]))
                # Static native aim is independently sampled. No interpolation or
                # invented association with unmatched moving frames is permitted.
                matching = [native_direction(r) for r in aim_records if r.get("weapon") == row["weapon"] and r.get("space") == row["space"]]
                reference = matching if matching else native_directions
                if reference:
                    native_angles.append(max(vector_angle(-placed[2, :3], direction) for direction in reference))
        record = {
            "asset_name": episode["asset"], "weapon_local": episode["key"][1],
            "total_samples": len(episode["rows"]), "settled_samples": len(settled),
            "max_authored_grip_position_error_m": max(positions, default=None),
            "max_authored_grip_rotation_error_degrees": max(rotations, default=None),
            "max_barrel_vs_eye_forward_degrees": max(barrel_angles, default=None),
            "max_barrel_vs_native_aim_degrees": max(native_angles, default=None)}
        observations.append(record)
        require(bool(positions) and max(positions) < .001, f"{episode['asset']} wrist attachment exceeds 1 mm")
        require(bool(rotations) and max(rotations) < .1, f"{episode['asset']} wrist rotation differs from authored grip")
        if not support:
            require(bool(barrel_angles) and max(barrel_angles) < .05, f"{episode['asset']} barrel differs from shared eye/body forward")
            require(bool(native_angles) and max(native_angles) < .05, f"{episode['asset']} barrel differs from static native aim")

    sight = gameplay.get("sight_flip")
    sight_result = None
    if sight and sight.get("requested"):
        require(sight.get("requests") == 2 and sight.get("commits") == 2,
                "physical sight flip did not request and commit both transitions")
        require(sight.get("cancellations") == 0, "physical sight flip cancelled during the expected two-transition fixture")
        sight_result = {key: sight.get(key) for key in ("requested", "requests", "commits", "cancellations", "headset_verified")}
        sight_result["request_records"] = [row for row in sight.get("records", []) if row.get("request") or row.get("committed")]
    support_result = None
    if support:
        contact = gameplay["two_hand_support"]
        require(contact.get("requested") and contact.get("grabs", 0) > 0
                and contact.get("releases", 0) > 0 and contact.get("held_samples", 0) > 0, "support was not exercised")
        require(contact.get("preservation_failures") == 0, "support modified unrelated input")
        require(contact.get("max_aim_residual_radians", float("inf")) < .0001, "assisted aim differs from native aim")
        launcher = [(row, units) for row in hand_rows for asset, units in [identity(row)] if asset == "40mmgl"]
        attached, free, snaps = [], [], []
        for row, units in launcher:
            if not row["arms"][0]["tracked"] or row.get("sight_attached", False):
                # Sight attachment has its own fixed-palm audit; it is not a
                # free hand or a support-handle attachment.
                continue
            placed = profiles.matrix(row["placed"])
            expected = profiles.matrix(row["native_left_wrist"]) @ np.linalg.inv(profiles.matrix(row["native"])) @ placed
            resolved = np.asarray(row["arms"][0]["resolved"], dtype=float)
            local = row["local_grips"][0]
            raw_wrist = np.array([local[0] * units, local[1] * units, -local[2] * units, 1.]) @ profiles.matrix(row["eye_base"])
            raw_error = float(np.linalg.norm(raw_wrist[:3] - resolved)) / units
            if row.get("support_attached"):
                attached.append(float(np.linalg.norm(expected[3, :3] - resolved)) / units)
                snaps.append(raw_error)
            else:
                free.append(raw_error)
        require(len(attached) >= 8 and len(free) >= 8, "launcher support needs attached and independent free-hand coverage")
        require(bool(attached) and max(attached) < .001, "launcher hand missed authored support attachment")
        require(bool(free) and max(free) < .001, "free launcher hand did not follow independent controller")
        require(bool(snaps) and max(snaps) > .005, "support attachment was not tested with more than 5 mm controller-to-authored displacement")
        support_result = {"launcher_attached_samples": len(attached), "launcher_free_samples": len(free),
                          "max_authored_support_error_m": max(attached, default=None),
                          "max_free_hand_error_m": max(free, default=None),
                          "max_visible_seating_distance_m": max(snaps, default=None),
                          "max_native_aim_residual_radians": contact.get("max_aim_residual_radians"),
                          "rendered_dynamic_aim_temporal_match_tested": False}

    fire = gameplay.get("fire_origin_observation", {})
    require(fire.get("client_origin_writes") == 0 and fire.get("server_origin_writes") == 0,
            "no-fire launcher alignment check unexpectedly wrote firing origins")
    sources = {"native_trace": source(path), "completion": source(done_path)}
    if (folder / "manifest.json").exists():
        sources["native_manifest"] = source(folder / "manifest.json")
    delivery = None
    if receiver:
        receiver_path = Path(receiver).resolve() / "result.json"
        delivery = profiles.load_json(receiver_path)
        sources["receiver_result"] = source(receiver_path)
        require(delivery.get("consumed_pairs") == 240 and stream.get("consumed") == 240, "bounded stereo pair coverage failed")
        require(delivery.get("native_tracking_transport_verified") and delivery.get("gpu_busy") == 0,
                "tracking/GPU transport failed")
        require(delivery.get("async_timeouts") == 0, "native receiver timed out")
        require(delivery.get("sight_start_secondary", False) is sight_start_secondary,
                "receiver start mode disagrees with the explicit checker option")
        if sight_start_secondary:
            require(delivery.get("sight_gesture_fixture") is True,
                    "launcher-first capture did not use the physical sight fixture")
    return {"passed": not failures, "failures": failures, "headset_tested": False,
            "launcher_projectile_origin_verified": False, "projectile_impacts_tested": False,
            "visible_sight_identity_verified": False, "sources": sources, "asset_sequence": sequence,
            "sight_start_secondary": sight_start_secondary,
            "mode_requests": gameplay.get("weapon_mode_requests"),
            "mode_acknowledgements": gameplay.get("weapon_mode_acknowledgements"),
            "mode_records": mode_rows, "static_native_aim_observed": static_native_aim,
            "native_aim_item_coverage": item_aim_coverage, "episodes": observations,
            "support": support_result, "sight_flip": sight_result,
            "delivery": {"consumed_pairs": delivery.get("consumed_pairs"), "async_timeouts": delivery.get("async_timeouts")}
                        if delivery else None}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--receiver", type=Path)
    parser.add_argument("--support", action="store_true", help="verify GL support/free hand; static barrel test must be separate")
    parser.add_argument("--sight-start-secondary", action="store_true",
                        help="require exact launcher -> XM8 -> launcher physical sight sequence")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        result = check(args.native, args.receiver, args.support, args.sight_start_secondary)
    except (ValueError, TypeError, KeyError, OSError, np.linalg.LinAlgError) as exc:
        result = {"passed": False, "failures": [str(exc)], "headset_tested": False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf8")
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
