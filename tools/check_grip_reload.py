"""Read-only BC2 support/reload and right trigger audit. Only --output is written.

A passing audit proves the recorded grip survived the Reload action and the
isolated visual index overlay behaved correctly. Native acceptance of a reload,
mesh trigger movement, and headset appearance need their own evidence.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

RELOAD = 8  # fvr::interaction::ActionPolicy.h
FIRE = 1
RELEASE_BUTTON = 5  # fvr::interaction::SupportRelease::Button
TRIGGER_MAX_RADIANS = (.12, .22, .12)
ROTATION_ANGLE_TOLERANCE = .002
LOCAL_TRANSLATION_CAP = .0005  # Fixture uses one native unit per metre.
ATTACHMENT_DRIFT_CAP = .005
EXPECTED_FINGERPRINT = "a7f219a1426216ab"


def integer(value, minimum=0):
    return isinstance(value, int) and not isinstance(value, bool) and value >= minimum


def number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"),
                      parse_constant=lambda x: (_ for _ in ()).throw(ValueError("Nonfinite JSON: " + x)))


def source(path):
    path = Path(path)
    return {"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def determinant(m):
    return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
            - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
            + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))


def matrix(value):
    if not isinstance(value, list):
        raise ValueError("Matrix must be an array")
    flat = [x for row in value for x in row] if len(value) == 4 and all(isinstance(x, list) and len(x) == 4 for x in value) else value
    if len(flat) != 16 or not all(number(x) for x in flat):
        raise ValueError("Matrix must contain exactly 16 finite numbers")
    m = [list(flat[n:n + 4]) for n in range(0, 16, 4)]
    if any(abs(m[n][3] - (1 if n == 3 else 0)) > 1e-5 for n in range(4)):
        raise ValueError("Non-affine matrix")
    error = max(abs(sum(m[r][k] * m[c][k] for k in range(3)) - (1 if r == c else 0))
                for r in range(3) for c in range(3))
    if error > .02 or abs(determinant(m) - 1) > .02:
        raise ValueError("Improper, scaled, singular, or non-rigid matrix")
    return m


def inverse(m):
    d = determinant(m)
    out = [[0.0] * 4 for _ in range(4)]
    out[3][3] = 1.0
    for r in range(3):
        for c in range(3):
            rows, cols = [i for i in range(3) if i != c], [i for i in range(3) if i != r]
            out[r][c] = ((-1) ** (r + c)) * (m[rows[0]][cols[0]] * m[rows[1]][cols[1]]
                          - m[rows[0]][cols[1]] * m[rows[1]][cols[0]]) / d
    for c in range(3):
        out[3][c] = -sum(m[3][k] * out[k][c] for k in range(3))
    return out


def multiply(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def point(value, transform):
    if not isinstance(value, list) or len(value) != 3 or not all(number(x) for x in value):
        raise ValueError("Invalid finite point")
    return [sum(value[k] * transform[k][c] for k in range(3)) + transform[3][c] for c in range(3)]


def rotation_angle(before, after):
    delta = multiply(after, inverse(before))
    return math.acos(max(-1.0, min(1.0, (sum(delta[n][n] for n in range(3)) - 1) / 2)))


def audit_documents(doc, completion, receiver, pairs):
    failures = []

    def require(ok, reason):
        if not ok and reason not in failures:
            failures.append(reason)

    def zero(container, key):
        require(integer(container.get(key)) and container[key] == 0, key + " is missing or nonzero")

    gameplay, rig, stream = doc["gameplay"], doc["gameplay"]["rig_publication"], doc["native_stream"]
    support, trigger = gameplay["two_hand_support"], rig["right_trigger"]
    require(doc.get("state") == "observed" and doc.get("hooks_disabled") is True, "Native hooks did not retire")
    require(completion.get("game_responding") is True and completion.get("game_exited") is False and
            completion.get("new_crash_report") is False, "Game did not complete cleanly")
    zero(completion, "bootstrap_exit")
    require(rig.get("native_animation_written") is False, "Native animation was written")
    for key in ("source_changes", "packing_failures", "fallback_failures"):
        zero(rig, key)
    for key in ("camera_restore_failures", "gpu_failure_stage", "gpu_failure_code"):
        zero(stream, key)
    for key in ("phase_failures", "mismatches"):
        if key in gameplay:
            zero(gameplay, key)
    zero(support, "preservation_failures")
    zero(trigger, "failures")
    require(receiver.get("support_reload_fixture") is True, "Receiver did not run the support/reload fixture")
    require(receiver.get("support_grip_requested") is True, "Receiver did not request support grip")
    require(receiver.get("shot_probe_requested") is False, "Visual-only trigger fixture also requested shots")
    require(receiver.get("native_tracking_transport_verified") in (True, 1), "Receiver transport was not verified")
    require(integer(receiver.get("consumed_pairs"), 1), "Receiver consumed no pairs")
    require(stream.get("consumed") == receiver.get("consumed_pairs"), "Native/receiver pair counts differ")
    require(integer(receiver.get("async_timeouts")), "Invalid asynchronous timeout counter")
    require(integer(receiver.get("controls_started_ms"), 1), "Missing receiver fixture start time")
    start = receiver["controls_started_ms"]
    require(support.get("requested") is True and integer(support.get("held_samples"), 1), "No held support poses")
    require(trigger.get("free_hand_binding_available") is True, "Right anatomical binding unavailable")
    require(trigger.get("empty_hands_enabled") is False and trigger.get("mesh_trigger_enabled") is False,
            "Unverified empty-hand or mesh-trigger feature was enabled")
    require(integer(trigger.get("poses"), 1), "No nonzero right trigger poses")
    fp = str(rig.get("hand_roles", {}).get("binding_fingerprint", "")).lower().removeprefix("fnv1a64:")
    require(fp == EXPECTED_FINGERPRINT, "Runtime hand rig fingerprint is unverified")

    rows = support["records"]
    if not isinstance(rows, list) or not 0 < len(rows) <= 192:
        raise ValueError("Support records must be a nonempty bounded ring")
    previous_ms, previous_generation = -1, -1
    for row in rows:
        for key in ("ms", "generation", "action_bits", "grasp_token", "release_reason", "weapon_kind"):
            if not integer(row[key]):
                raise ValueError("Invalid support integer " + key)
        if row["generation"] == 0:
            raise ValueError("Zero support generation")
        for key in ("contact", "holding", "engaged", "released"):
            if type(row[key]) is not bool:
                raise ValueError("Invalid support boolean " + key)
        if not number(row["squeeze"]) or not 0 <= row["squeeze"] <= 1 or not number(row["distance_m"]) or row["distance_m"] < 0:
            raise ValueError("Invalid support squeeze/distance")
        require(row["ms"] >= previous_ms and row["generation"] >= previous_generation,
                "Support ring is not chronological")
        previous_ms, previous_generation = row["ms"], row["generation"]
        require(not row["action_bits"] & FIRE, "Visual fixture dispatched Fire")
    reload_rows = [r for r in rows if r["action_bits"] & RELOAD]
    require(bool(reload_rows), "No Reload action reached the native adapter")
    require(all(5900 <= r["ms"] - start <= 6500 for r in reload_rows),
            "Reload timing does not match receiver fixture")
    held = [r for r in rows if 3500 <= r["ms"] - start < 9500 and r["holding"]]
    require(bool(held), "No grip held in the requested fixture window")
    token = held[0]["grasp_token"] if held else 0
    require(token > 0, "Held support token is zero")
    if held and reload_rows:
        first, last = held[0], held[-1]
        episode = [r for r in rows if first["ms"] <= r["ms"] <= last["ms"]]
        require(first["ms"] < reload_rows[0]["ms"] and last["ms"] > reload_rows[-1]["ms"] + 1000,
                "Grip lacks coverage before and after Reload")
        require(last["ms"] - start >= 9000, "Grip did not survive until close to intentional release")
        require(all(r["holding"] and not r["released"] and r["grasp_token"] == token and r["contact"] and
                    r["squeeze"] > .35 and r["release_reason"] == 0 for r in episode),
                "Held squeeze lost contact, released, or changed grasp token during the episode")
        require(all(r["holding"] and r["grasp_token"] == token and r["contact"] for r in reload_rows),
                "Reload interrupted the same held grip")
        require(all(b["ms"] - a["ms"] <= 500 for a, b in zip(episode, episode[1:])),
                "Support telemetry has a gap too large to establish continuity")
        require(sum(r["engaged"] for r in episode) <= 1, "Grip re-engaged during the same episode")
    else:
        episode = []
    intentional = [r for r in rows if r["released"] and 9500 <= r["ms"] - start <= 11000]
    require(bool(intentional), "Intentional fixture release was not observed")
    require(all(r["squeeze"] <= .35 and r["release_reason"] == RELEASE_BUTTON for r in intentional),
            "Final grip loss was not the requested squeeze release")
    held_releases = [r for r in rows if 3500 <= r["ms"] - start < 9500 and r["released"] and r["squeeze"] > .35]
    require(not held_releases, "Support released while fixture squeeze remained held")

    tr = trigger["records"]
    if not isinstance(tr, list) or not 0 < len(tr) <= 128:
        raise ValueError("Trigger records must be a nonempty bounded ring")
    previous_generation = -1
    active_count = neutral_count = 0
    max_angle_error = max_translation = max_neutral_error = max_rotation_change = 0.0
    active_generations = []
    indices_by_weapon = {}
    for row in tr:
        if not integer(row["generation"], 1) or not integer(row["weapon"], 1) or not number(row["trigger"]) or not 0 <= row["trigger"] <= 1:
            raise ValueError("Invalid right trigger identity/value")
        require(row["generation"] >= previous_generation, "Right trigger ring is not chronological")
        previous_generation = row["generation"]
        require(row.get("wrist_preserved") is True and row.get("other_branches_preserved") is True,
                "Right trigger changed wrist or another branch")
        joints = row["joints"]
        if not isinstance(joints, list) or len(joints) != 3:
            raise ValueError("Right index requires three recorded joints")
        indices = tuple(j["index"] for j in joints)
        if not all(integer(x, 1) for x in indices) or len(set(indices)) != 3:
            raise ValueError("Right index identities are invalid or duplicated")
        require(indices_by_weapon.setdefault(row["weapon"], indices) == indices, "Right index mapping changed")
        for joint, maximum in zip(joints, TRIGGER_MAX_RADIANS):
            before, after = matrix(joint["before"]), matrix(joint["after"])
            translation = max(abs(after[3][k] - before[3][k]) for k in range(3))
            max_translation = max(max_translation, translation)
            require(translation <= LOCAL_TRANSLATION_CAP, "Trigger changed local bone translation beyond float32 allowance")
            difference = max(abs(after[r][c] - before[r][c]) for r in range(4) for c in range(4))
            if row["trigger"] == 0:
                max_neutral_error = max(max_neutral_error, difference)
                require(difference == 0, "Released trigger retained a finger overlay")
            else:
                angle = rotation_angle(before, after)
                max_rotation_change = max(max_rotation_change, angle)
                error = abs(angle - maximum * row["trigger"])
                max_angle_error = max(max_angle_error, error)
                require(error <= ROTATION_ANGLE_TOLERANCE, "Trigger angle differs from bounded single-overlay amount")
        if row["trigger"] == 0:
            neutral_count += 1
        else:
            active_count += 1
            active_generations.append(row["generation"])
            require(abs(row["trigger"] - .65) < 1e-5, "Unexpected trigger value for visual-only fixture")
    require(active_count > 0 and neutral_count > 0 and max_rotation_change > .01, "Trigger lacks active/release visual coverage")
    require(any(r["trigger"] == 0 and active_generations and r["generation"] > max(active_generations) for r in tr),
            "No neutral trigger sample after activation")

    # pairs.jsonl interleaves delivered-pair rows with tracking/FOV metadata.
    for row in pairs:
        if not isinstance(row, dict) or ("tracking" not in row and "tracking_sample" not in row):
            raise ValueError("Unknown receiver pair-log record")
    pairs = [row for row in pairs if "tracking" in row]
    pair_generations = set()
    for pair in pairs:
        if not integer(pair.get("tracking"), 0x100000001):
            raise ValueError("Invalid receiver tracking generation")
        pair_generations.add(pair["tracking"] - 0x100000000)
    require(len(pairs) == receiver.get("consumed_pairs"), "Receiver pair log count differs from summary")
    require(bool(pair_generations.intersection(r["generation"] for r in rows)), "Receiver/native support generations do not overlap")
    require(bool(pair_generations.intersection(r["generation"] for r in tr)), "Receiver/native right-trigger generations do not overlap")

    hand_rows = rig.get("hand_evidence", [])
    tracking_count, native_points, rendered_points, clamped_count = 0, [], [], 0
    if held:
        for row in hand_rows:
            if not held[0]["generation"] <= row.get("generation", 0) <= held[-1]["generation"]:
                continue
            if not row.get("support_attached"):
                require(False, "Rendered hand lost its support attachment during the held episode")
                continue
            arms = row.get("arms", [])
            if len(arms) != 2:
                raise ValueError("Hand evidence must contain both tracked arms")
            require(all(a.get("tracked") is True for a in arms), "Hand evidence lost tracking during held fixture")
            tracking_count += 1
            if all(key in row for key in ("native_left_wrist", "native", "placed")):
                native = multiply(matrix(row["native_left_wrist"]), inverse(matrix(row["native"])))
                placed_inverse = inverse(matrix(row["placed"]))
                native_points.append(native[3][:3])
                rendered_points.append(point(arms[0]["resolved"], placed_inverse))
                if not number(arms[0].get("error")) or arms[0]["error"] > .005:
                    clamped_count += 1
    require(tracking_count > 0, "No independent tracked-hand evidence overlaps the held fixture")
    native_motion = max((math.dist(p, native_points[0]) for p in native_points), default=None)
    rendered_drift = max((math.dist(p, rendered_points[0]) for p in rendered_points), default=None)
    native_motion_observed = native_motion is not None and native_motion >= .005
    attachment_verified = bool(native_motion_observed and rendered_drift is not None and rendered_drift <= ATTACHMENT_DRIFT_CAP and not clamped_count)
    if native_motion_observed and not clamped_count:
        require(rendered_drift <= ATTACHMENT_DRIFT_CAP, "Rendered support attachment followed native hand animation")
    return {"passed": not failures, "failures": failures, "headset_tested": False,
            "native_reload_acceptance_verified": False, "reload_action_while_gripped_verified": bool(reload_rows and not held_releases and token),
            "receiver_delivery": {"consumed_pairs": receiver["consumed_pairs"], "recovered_async_timeouts": receiver["async_timeouts"]},
            "support": {"grasp_token": token, "records": len(rows), "held_records": len(held), "reload_records": len(reload_rows),
                        "held_squeeze_releases": len(held_releases), "intentional_release_records": len(intentional),
                        "fixture_relative_reload_ms": [r["ms"] - start for r in reload_rows],
                        "tracked_hand_samples": tracking_count, "tracking_source": "hand_evidence arms; support records have no explicit tracked flag"},
            "right_trigger": {"active_records": active_count, "neutral_records": neutral_count,
                              "max_rotation_radians": max_rotation_change, "max_single_overlay_angle_error": max_angle_error,
                              "max_local_translation_difference": max_translation, "max_neutral_difference": max_neutral_error},
            "attachment_observation": {"samples": len(native_points), "native_local_left_motion_m": native_motion,
                                       "rendered_weapon_local_left_drift_m": rendered_drift,
                                       "clamped_or_unknown_error_samples": clamped_count,
                                       "native_motion_observed": native_motion_observed, "attachment_stability_verified": attachment_verified},
            "limits": {"proper_rotation_gram_error": .02, "proper_rotation_det_error": .02,
                       "trigger_rotation_angle_error_radians": ROTATION_ANGLE_TOLERANCE,
                       "local_translation_float32_cap_m": LOCAL_TRANSLATION_CAP,
                       "rendered_attachment_drift_m": ATTACHMENT_DRIFT_CAP},
            "limitations": ["Reload action telemetry does not by itself prove the native weapon accepted or completed a reload.",
                            "Native wrist movement may include idle/recoil animation; without an independent reload-state trace it is not uniquely attributed to reload.",
                            "Parent-local float32 translations permit a fixed 0.5 mm envelope at the fixture's one-unit-per-metre scale.",
                            "No mesh trigger, fingertip collision, empty-hand mode, or headset appearance is verified."]}


def check(native, receiver):
    native, receiver = Path(native), Path(receiver)
    files = {"native_trace": native / "native-trace.json", "completion": native / "completion.json",
             "receiver_result": receiver / "result.json", "receiver_pairs": receiver / "pairs.jsonl"}
    docs = {key: read_json(path) for key, path in files.items() if key != "receiver_pairs"}
    pairs = [json.loads(line) for line in files["receiver_pairs"].read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    result = audit_documents(docs["native_trace"], docs["completion"], docs["receiver_result"], pairs)
    result["sources"] = {key: source(path) for key, path in files.items()}
    result["sources"]["checker"] = source(__file__)
    manifest = native / "manifest.json"
    if manifest.is_file():
        result["sources"]["native_manifest"] = source(manifest)
        value = read_json(manifest)
        result["probe_sha256_recorded_by_launcher"] = value.get("probe_sha256")
        probe = native / "BC2NativeProbe.dll"
        if probe.is_file():
            result["sources"]["captured_probe"] = source(probe)
            if str(value.get("probe_sha256", "")).lower() != result["sources"]["captured_probe"]["sha256"]:
                result["passed"] = False
                result["failures"].append("Captured probe hash differs from launcher manifest")
    return result


def join_reload_state(result, state_path, native, receiver):
    """Join the independently interpreted collector audit by source hashes.

    Do not reinterpret native offsets or enum states here. Verify its claimed
    counter samples against the raw collector and this run's held-token window.
    """
    from bisect import bisect_left
    state_path, native, receiver = Path(state_path), Path(native), Path(receiver)
    if not state_path.name.startswith("reload-state-"):
        raise ValueError("--reload-state requires a reload-state-<capture>.json collector")
    audit_path = state_path.with_name(state_path.name.replace("reload-state-", "reload-native-audit-", 1))
    raw, evidence = read_json(state_path), read_json(audit_path)
    if raw.get("schema") != "fvr.bc2.reload_state" or evidence.get("schema") != "fvr.bc2.reload_native_audit":
        raise ValueError("Unrecognized native reload evidence schema")
    if raw.get("schema_version") != 1 or evidence.get("schema_version") != 1:
        raise ValueError("Unrecognized native reload evidence version")
    if raw.get("read_only") is not True or any(raw.get(k) is not False for k in ("native_calls", "process_writes", "input_or_focus_changes")):
        raise ValueError("Reload collector was not strictly read-only")
    if evidence.get("status") != "native_reload_and_continuous_support_verified":
        raise ValueError("Independent native reload audit did not verify the hold")
    expected = {"observer": source(state_path), "native": source(native / "native-trace.json"),
                "receiver": source(receiver / "result.json")}
    for key, actual in expected.items():
        if evidence.get("sources", {}).get(key, {}).get("sha256", "").lower() != actual["sha256"]:
            raise ValueError("Independent reload audit source hash mismatch: " + key)
    doc, manifest = read_json(native / "native-trace.json"), read_json(native / "manifest.json")
    if raw.get("pid") != doc.get("pid") or evidence.get("pid") != doc.get("pid"):
        raise ValueError("Reload collector belongs to another process")
    game_hash = str(manifest.get("game_sha256", "")).lower()
    if not game_hash or str(raw.get("executable_sha256", "")).lower() != game_hash or str(evidence.get("executable_sha256", "")).lower() != game_hash:
        raise ValueError("Reload collector executable hash mismatch")
    rows = doc["gameplay"]["two_hand_support"]["records"]
    token = result["support"]["grasp_token"]
    held = [r for r in rows if r["holding"] and r["grasp_token"] == token]
    release = [r for r in rows if r["released"] and held and r["ms"] > held[0]["ms"]]
    if not held or not release:
        raise ValueError("No complete support episode for native reload join")
    begin, end = held[0]["ms"], release[0]["ms"]
    ordered = sorted(rows, key=lambda r: r["ms"])
    times = [r["ms"] for r in ordered]
    samples = {}
    for row in raw["samples"]:
        sequence = row["sequence"]
        if not integer(sequence) or sequence in samples:
            raise ValueError("Duplicate/invalid raw collector sequence")
        samples[sequence] = row
    branch_summaries = {}
    for branch, detail in evidence["branches"].items():
        changes = detail["ammo_changes"]
        if branch not in ("0x3c", "0x40") or len(changes) < 2:
            raise ValueError("Missing independently interpreted firing branch transfers")
        previous = None
        total = None
        transfers = []
        for change in changes:
            row = samples.get(change["sequence"])
            if not row or row.get("identity_coherent") is not True or row["tick_ms"] != change["tick_ms"]:
                raise ValueError("Reload change has no matching coherent raw sample")
            owner = row["owner"]
            for field in ("actor", "player", "weak", "inventory", "selected_slot", "selected_weapon"):
                if owner[field] not in evidence["owner_identity"][field]:
                    raise ValueError("Reload transfer owner mismatch: " + field)
            state = row["states"][branch]
            counts = [state["counter_7c"], state["counter_80"]]
            if counts != change["loaded_reserve"] or not all(integer(n) for n in counts):
                raise ValueError("Reload counter interpretation differs from raw sample")
            if state["address"] not in detail["firing_object_addresses"]:
                raise ValueError("Reload firing object changed")
            if total is None:
                total = sum(counts)
            if sum(counts) != total:
                raise ValueError("Ammo total was not conserved during native reload")
            if previous is not None:
                loaded, reserve = counts[0] - previous[0], counts[1] - previous[1]
                if loaded <= 0 or reserve != -loaded:
                    raise ValueError("Native reload change was not reserve-to-loaded transfer")
                tick = change["tick_ms"]
                if not begin <= tick < end:
                    raise ValueError("Ammo transfer occurred outside the same held grip")
                at = bisect_left(times, tick)
                bracket = ordered[max(0, at - 1):min(len(ordered), at + 1)]
                if not bracket or max(abs(r["ms"] - tick) for r in bracket) > 250 or not all(
                        r["holding"] and r["contact"] and r["squeeze"] > .35 and not r["released"] and r["grasp_token"] == token for r in bracket):
                    raise ValueError("Ammo transfer lacks same-token support samples around it")
                transfers.append({"tick_ms": tick, "loaded_reserve": counts, "transferred": loaded})
            previous = counts
        if changes[0]["loaded_reserve"] != detail["initial_loaded_reserve"] or previous != detail["final_loaded_reserve"] or total != detail["conserved_ammo_total"]:
            raise ValueError("Reload summary disagrees with its conserved transfers")
        # Enum meanings remain the independent audit's interpretation. Preserve
        # its explicit caveat about support ending before post-reload readiness.
        ready_after = [r["tick_ms"] for r in detail.get("state_transitions", [])
                       if r["tick_ms"] > transfers[-1]["tick_ms"] and r["state_current_previous_next"][0] == 2]
        branch_summaries[branch] = {"initial_loaded_reserve": changes[0]["loaded_reserve"],
            "final_loaded_reserve": previous, "conserved_total": total, "transfers": transfers,
            "held_through_final_transfer": True,
            "held_through_post_reload_ready": bool(ready_after and min(ready_after) < end)}
    if not branch_summaries:
        raise ValueError("No native reload branch evidence")
    result["native_reload_acceptance_verified"] = result["passed"]
    result["native_reload_evidence"] = {"collector": source(state_path), "independent_interpretation": source(audit_path),
        "source_hashes_verified": True, "grasp_token": token, "held_interval_ms": [begin, end],
        "accepted_samples": evidence.get("samples_accepted"), "rejected_samples": evidence.get("samples_rejected"),
        "branches": branch_summaries, "atomic_native_snapshot": raw.get("atomic_native_snapshot"),
        "physical_reload_interaction_verified": False, "manual_reload_enabled": False,
        "limitations": evidence.get("limitations", [])}
    return result


def self_test():
    """Persistable deterministic audit regression; no external evidence needed."""
    import copy

    def identity():
        return [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]

    def rotate(angle):
        m = identity()
        m[5] = m[10] = math.cos(angle)
        m[6], m[9] = math.sin(angle), -math.sin(angle)
        return m

    start = 100000
    rows, triggers, hands, pairs = [], [], [], []
    for elapsed in range(3500, 9500, 100):
        generation = elapsed // 100
        rows.append(dict(ms=start + elapsed, generation=generation, weapon_kind=2, release_reason=0,
            action_bits=RELOAD if 6000 <= elapsed < 6200 else 0, grasp_token=1, contact=True,
            distance_m=.1, squeeze=1., holding=True, engaged=elapsed == 3500, released=False))
        value = .65 if 4200 <= elapsed < 5500 else 0.
        joints = [dict(index=j + 5, before=identity(), after=rotate(value * limit)) for j, limit in enumerate(TRIGGER_MAX_RADIANS)]
        triggers.append(dict(generation=generation, weapon=55, trigger=value, wrist_preserved=True,
                             other_branches_preserved=True, joints=joints))
        pairs.append(dict(tracking=0x100000000 + generation))
        pairs.append(dict(tracking_sample=0x100000000 + generation, static_pose=1))
        wrist = identity()
        wrist[12] = .01 if elapsed >= 6200 else 0.
        hands.append(dict(generation=generation, support_attached=True, native_left_wrist=wrist,
            native=identity(), placed=identity(), arms=[dict(tracked=True, resolved=[0, 0, 0], error=0),
                                                       dict(tracked=True, resolved=[0, 0, 0], error=0)]))
    rows.append(dict(ms=start + 9500, generation=95, weapon_kind=2, release_reason=RELEASE_BUTTON,
        action_bits=0, grasp_token=0, contact=True, distance_m=.1, squeeze=0., holding=False, engaged=False, released=True))
    rig = dict(native_animation_written=False, source_changes=0, packing_failures=0, fallback_failures=0,
        hand_roles=dict(binding_fingerprint=EXPECTED_FINGERPRINT), hand_evidence=hands,
        right_trigger=dict(failures=0, poses=13, free_hand_binding_available=True,
                           empty_hands_enabled=False, mesh_trigger_enabled=False, records=triggers))
    doc = dict(state="observed", hooks_disabled=True,
        gameplay=dict(rig_publication=rig, phase_failures=0, mismatches=0,
                      two_hand_support=dict(requested=True, held_samples=60, preservation_failures=0, records=rows)),
        native_stream=dict(camera_restore_failures=0, gpu_failure_stage=0, gpu_failure_code=0, consumed=60))
    done = dict(game_responding=True, game_exited=False, new_crash_report=False, bootstrap_exit=0)
    receiver = dict(support_reload_fixture=True, support_grip_requested=True, shot_probe_requested=False,
        native_tracking_transport_verified=True, consumed_pairs=60, async_timeouts=1, controls_started_ms=start)
    baseline = audit_documents(doc, done, receiver, pairs)
    cases = []
    names = ("missing_reload", "release_while_held", "token_replacement", "invalid_final_release",
             "overlay_after_trigger_release", "reflected_matrix", "accidental_fire", "native_source_change",
             "double_trigger_overlay", "contact_loss", "different_receiver_clock", "different_tracking_generations",
             "missing_tracking_evidence", "other_branch_changed")
    for case, name in enumerate(names):
        d, c, r, p = copy.deepcopy((doc, done, receiver, pairs))
        support = d["gameplay"]["two_hand_support"]["records"]
        rig = d["gameplay"]["rig_publication"]
        trigger = rig["right_trigger"]
        if case == 0:
            for row in support:
                row["action_bits"] = 0
        if case == 1:
            support[28].update(holding=False, released=True, grasp_token=0, release_reason=3)
        if case == 2:
            support[30]["grasp_token"] = 2
        if case == 3:
            support[-1]["squeeze"] = 1
        if case == 4:
            trigger["records"][0]["joints"][0]["after"] = rotate(.1)
        if case == 5:
            trigger["records"][7]["joints"][0]["after"][0] = -1
        if case == 6:
            support[15]["action_bits"] = FIRE
        if case == 7:
            rig["source_changes"] = 1
        if case == 8:
            trigger["records"][7]["joints"][0]["after"] = rotate(.156)
        if case == 9:
            support[10]["contact"] = False
        if case == 10:
            r["controls_started_ms"] += 10000
        if case == 11:
            p = [dict(tracking=0x100000000 + 999999)] * 60
        if case == 12:
            rig["hand_evidence"] = []
        if case == 13:
            trigger["records"][7]["other_branches_preserved"] = False
        try:
            outcome = audit_documents(d, c, r, p)
            cases.append({"case": name, "rejected": not outcome["passed"], "reasons": outcome["failures"]})
        except ValueError as exc:
            cases.append({"case": name, "rejected": True, "reasons": [str(exc)]})
    return {"passed": baseline["passed"] and all(c["rejected"] for c in cases),
            "baseline_passed": baseline["passed"], "baseline_failures": baseline["failures"],
            "mutation_count": len(cases), "mutations": cases, "source": source(__file__),
            "game_process_accessed": False, "external_evidence_modified": False}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path)
    parser.add_argument("--receiver", type=Path)
    parser.add_argument("--reload-state", type=Path, help="Optional collector; requires adjacent reload-native-audit-<capture>.json")
    parser.add_argument("--self-test", action="store_true", help="Run deterministic audit regressions instead of reading captures")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.self_test:
        if args.native or args.receiver or args.reload_state:
            parser.error("--self-test does not accept capture inputs")
        if args.output.resolve() == Path(__file__).resolve():
            parser.error("--output must not overwrite the checker")
        result = self_test()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2, allow_nan=False))
        return 0 if result["passed"] else 1
    if not args.native or not args.receiver:
        parser.error("--native and --receiver are required unless --self-test is used")
    inputs = {args.native / name for name in ("native-trace.json", "completion.json", "manifest.json", "BC2NativeProbe.dll")}
    inputs.update(args.receiver / name for name in ("result.json", "pairs.jsonl"))
    inputs.add(Path(__file__))
    if args.reload_state:
        inputs.add(args.reload_state)
        inputs.add(args.reload_state.with_name(args.reload_state.name.replace("reload-state-", "reload-native-audit-", 1)))
    if args.output.resolve() in {p.resolve() for p in inputs}:
        parser.error("--output must not overwrite source evidence or the checker")
    try:
        result = check(args.native, args.receiver)
        if args.reload_state:
            result = join_reload_state(result, args.reload_state, args.native, args.receiver)
    except (KeyError, TypeError, ValueError, OSError, IndexError, OverflowError, ZeroDivisionError) as exc:
        result = {"passed": False, "failures": [type(exc).__name__ + ": " + str(exc)], "headset_tested": False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
