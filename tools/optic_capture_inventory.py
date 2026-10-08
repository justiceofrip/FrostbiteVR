"""Read-only inventory of BC2 native optics evidence; never classifies an optic.

JSON goes to stdout unless --output explicitly names a NEW file. Exit 0 means
parsed, not optics-ready; malformed evidence exits 2. Legacy captures cannot
establish an exact asset/mode/ADS-phase association with their sampled draws.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re
import sys

MAX_JSON_BYTES = 64 * 1024 * 1024
MAX_ROWS = 16384
SHA256 = re.compile(r"^[0-9a-fA-F]{64}$")
SLOTS = {0: "VS:0", 1: "VS:1", 2: "PS:0", 3: "PS:1"}
OPTIC_FIELDS = {"AimingController", "FirstPersonCamera", "Hud", "RenderFov",
                "ZoomRenderFov", "MeshShaderSetNumberOverride"}


def uint(value, name, maximum=(1 << 64) - 1):
    if type(value) is not int or not 0 <= value <= maximum:
        raise ValueError(f"{name} must be an unsigned integer <= {maximum}")
    return value


def boolean(value, name):
    if type(value) is not bool:
        raise ValueError(f"{name} must be boolean")
    return value


def finite_vector(value, size, name):
    if (not isinstance(value, list) or len(value) != size or
            any(type(x) not in (int, float) or not math.isfinite(x) for x in value)):
        raise ValueError(f"{name} must contain {size} finite numbers")
    return value


def rows(value, name):
    if not isinstance(value, list) or len(value) > MAX_ROWS:
        raise ValueError(f"{name} must be an array with at most {MAX_ROWS} entries")
    return value


def object_value(value, name):
    if not isinstance(value, dict):
        raise ValueError(f"{name} must be an object")
    return value


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


class Evidence:
    def __init__(self):
        self.sources = []
        self.errors = []
        self.issues = []
        self.missing_files = []
        self.blobs = {}

    def read_json(self, path):
        path = Path(path)
        if not path.exists():
            self.missing_files.append(str(path))
            return None
        try:
            if path.stat().st_size > MAX_JSON_BYTES:
                raise ValueError("file exceeds 64 MiB audit limit")
            data = path.read_bytes()
            self.sources.append({"path": str(path), "bytes": len(data),
                                 "sha256": hashlib.sha256(data).hexdigest()})
            def bad_constant(value):
                raise ValueError(f"non-finite JSON constant {value}")
            result = json.loads(data.decode("utf-8-sig"), object_pairs_hook=unique_object,
                              parse_constant=bad_constant)
            if result is None:
                raise ValueError('JSON null is not an evidence schema')
            return result
        except (OSError, UnicodeError, ValueError, RecursionError) as exc:
            self.errors.append(f"{path}: {exc}")
            return None

    def blob(self, root, name, expected):
        path = root / name
        try:
            size = path.stat().st_size
            if size != expected:
                raise ValueError(f"size {size}, expected {expected}")
            data = path.read_bytes()
            if len(data) != expected:
                raise ValueError("file changed while reading")
            self.blobs[name] = hashlib.sha256(data).hexdigest()
            return True
        except (OSError, ValueError) as exc:
            self.issues.append(f"{name}: {exc}")
            return False

    def parse(self, label, callback, default):
        try:
            return callback()
        except (ValueError, KeyError, TypeError, OverflowError) as exc:
            self.errors.append(f"{label}: {exc}")
            return default


def pass_inventory(root, raw, enabled, ev):
    result = {"capture_state": "missing", "rows": 0, "valid_rows": 0,
              "complete_rows": 0, "sampled_combinations": [], "per_eye": [],
              "recorded_viewports": [], "rows_without_recorded_viewport": 0,
              "constant_slots": [], "native_draw_total": None,
              "native_repeated_draw_total": None,
              "pointer_identity_scope": "this capture only; not stable shaders/materials",
              "selection_contract": {
                  "basis": "reviewed legacy BC2 CapturePassBuffers source; not a probe-hash attestation",
                  "sampling": "first VS/PS/target/depth tuple per eye in the sampled frame",
                  "source_viewport_filter": [1920, 1080],
                  "source_capacity_per_eye": 512,
                  "recorded_viewport_in_legacy_json": False,
                  "sequence_is_not_a_draw_total": True}}
    if raw is None:
        return result
    values = rows(raw, "pass-buffer-evidence")
    result["rows"] = len(values)
    if enabled is False:
        result["capture_state"] = "disabled"
        if values:
            ev.errors.append("pass rows contradict manifest pass_evidence=false")
    elif not values:
        result["capture_state"] = "enabled_empty" if enabled is True else "empty_enablement_unknown"
        ev.issues.append("empty capture establishes no absence of optics")
    else:
        result["capture_state"] = "sampled" if enabled is True else "sampled_enablement_unknown"
    tuples = Counter()
    by_eye = {i: {"eye": i, "sampled_rows": 0, "complete_rows": 0,
                  "overflow_reported": 0, "max_sample_sequence": None} for i in (0, 1)}
    slots = {i: {"slot": i, "stage_slot": SLOTS[i], "advertised": 0,
                 "bound": 0, "done": 0, "verified_blobs": 0,
                 "pending_or_unavailable": 0} for i in SLOTS}
    viewports = Counter()
    seen = set()
    for index, value in enumerate(values):
        try:
            row = object_value(value, f"pass row {index}")
            eye = uint(row["eye"], "eye", 1)
            number = uint(row["pass"], "pass", 65535)
            if (eye, number) in seen:
                raise ValueError("duplicate eye/pass identity")
            seen.add((eye, number))
            sequence = uint(row["sequence"], "sequence")
            uint(row["kind"], "kind", 1)
            uint(row["count"], "sample vertex/index count")
            complete = boolean(row["complete"], "complete")
            overflow = uint(row["overflow"], "overflow")
            key = tuple(uint(row[k], k) for k in ("vs", "ps", "target", "depth"))
            buffers = rows(row["buffers"], "buffers")
            parsed = []
            buffer_ids = set()
            for b in buffers:
                b = object_value(b, "buffer")
                slot = uint(b["slot"], "slot", 3)
                if slot in buffer_ids:
                    raise ValueError("duplicate constant slot")
                buffer_ids.add(slot)
                source = uint(b["source"], "buffer source")
                size = uint(b["bytes"], "buffer bytes", 65536)
                done = boolean(b["done"], "buffer done")
                if (done and (not source or not size)) or (size and not source):
                    raise ValueError("buffer source/size/completion contradiction")
                parsed.append((slot, source, size, done))
            if buffer_ids != set(SLOTS):
                raise ValueError("legacy pass requires exactly VS0/VS1/PS0/PS1 descriptors")
            viewport = None
            if "width" in row or "height" in row:
                width = uint(row["width"], "width", 65536)
                height = uint(row["height"], "height", 65536)
                if not width or not height:
                    raise ValueError("viewport dimensions must be positive")
                viewport = (width, height)
        except (ValueError, KeyError, TypeError) as exc:
            ev.errors.append(f"pass row {index}: {exc}")
            continue
        result["valid_rows"] += 1
        result["complete_rows"] += int(complete)
        tuples[(eye, *key)] += 1
        stats = by_eye[eye]
        stats["sampled_rows"] += 1
        stats["complete_rows"] += int(complete)
        stats["overflow_reported"] = max(stats["overflow_reported"], overflow)
        stats["max_sample_sequence"] = max(stats["max_sample_sequence"] or 0, sequence)
        if viewport:
            viewports[viewport] += 1
        else:
            result["rows_without_recorded_viewport"] += 1
        if not complete:
            ev.issues.append(f"pass {eye}/{number}: GPU capture incomplete")
        for slot, source, size, done in parsed:
            stats = slots[slot]
            stats["advertised"] += 1
            stats["bound"] += int(bool(source))
            stats["done"] += int(done)
            if source:
                if done:
                    stats["verified_blobs"] += int(ev.blob(root, f"pass-{eye}-{number}-cb-{slot}.bin", size))
                else:
                    stats["pending_or_unavailable"] += 1
                    ev.issues.append(f"pass {eye}/{number} {SLOTS[slot]}: bound buffer not available")
    for eye, stats in by_eye.items():
        selected = {k: v for k, v in tuples.items() if k[0] == eye}
        stats["unique_sampled_combinations"] = len(selected)
        stats["repeated_sample_rows"] = sum(v - 1 for v in selected.values())
        if stats["repeated_sample_rows"]:
            ev.issues.append(f"eye {eye}: repeated tuples conflict with legacy first-tuple sampling contract")
        if stats["sampled_rows"] > 512:
            ev.issues.append(f"eye {eye}: exceeds reviewed legacy collector capacity")
        if stats["overflow_reported"]:
            ev.issues.append(f"eye {eye}: collector overflow {stats['overflow_reported']}")
    combinations = sorted({k[1:] for k in tuples})
    result["sampled_combinations"] = [dict(zip(("vs", "ps", "target", "depth"), k),
        eye_sample_rows=[tuples[(0, *k)], tuples[(1, *k)]]) for k in combinations]
    result["cross_eye_shared_combinations"] = sum(bool(tuples[(0, *k)] and tuples[(1, *k)]) for k in combinations)
    result["per_eye"] = list(by_eye.values())
    result["recorded_viewports"] = [{"width": k[0], "height": k[1], "sample_rows": v}
                                    for k, v in sorted(viewports.items())]
    result["constant_slots"] = list(slots.values())
    return result


def projection_inventory(raw):
    if raw is None:
        return {"available": False, "rows": 0}
    values = rows(raw, "projection-bindings")
    callsites, frames, gathers, matrices = set(), set(), Counter(), set()
    for row in values:
        object_value(row, "projection row")
        callsites.add(uint(row["callsite"], "callsite"))
        frames.add(uint(row["frame"], "frame"))
        for k in ("context", "address", "thread"):
            uint(row[k], k)
        gather = row["gather"]
        if type(gather) is not int or gather not in (-1, 0, 1):
            raise ValueError("invalid gather eye")
        gathers[gather] += 1
        matrices.add(tuple(finite_vector(row["projection"], 16, "projection")))
    return {"available": True, "rows": len(values), "callsites": sorted(callsites),
            "frames": sorted(frames), "gather_counts": dict(sorted(gathers.items())),
            "distinct_recorded_matrices": len(matrices), "optic_role": "unknown"}


def context_inventory(raw):
    if raw is None:
        return {"available": False, "contexts": 0}
    raw = object_value(raw, "camera contexts")
    failures = {k: uint(raw[k], k) for k in ("patched_scopes", "patch_failures", "restore_failures")}
    roles, callsites, valid, invalid = Counter(), set(), 0, 0
    values = rows(raw["contexts"], "contexts")
    for row in values:
        object_value(row, "camera context")
        eye = uint(row["eye"], "context eye", 1)
        role = uint(row["role"], "context role")
        roles[(eye, role)] += 1
        callsites.add(uint(row["callsite"], "context callsite"))
        for snapshot in rows(row["snapshots"], "snapshots"):
            object_value(snapshot, "snapshot")
            flag = snapshot["valid"]
            if type(flag) not in (bool, int) or flag not in (0, 1):
                raise ValueError("snapshot valid must be 0/1 or boolean")
            if flag:
                finite_vector(snapshot["values"], 36, "view/projection/position snapshot")
                valid += 1
            else:
                invalid += 1
    return {"available": True, "contexts": len(values), "valid_snapshots": valid,
            "invalid_snapshots": invalid, "callsites": sorted(callsites), **failures,
            "roles": [{"eye": k[0], "native_role": k[1], "contexts": v} for k, v in sorted(roles.items())],
            "optic_role": "unknown"}


def native_inventory(raw):
    result = {"available": False, "observed_assets": [], "world_color_targets": [],
              "asset_samples_are_not_linked_to_passes": True}
    if raw is None:
        return result
    raw = object_value(raw, "native-trace")
    result["available"] = True
    if "hooks_disabled" in raw:
        result["hooks_disabled"] = boolean(raw["hooks_disabled"], "hooks_disabled")
    result['state'] = raw.get('state')
    if result['state'] is not None and not isinstance(result['state'], str):
        raise ValueError('native state must be a string')
    targets = Counter()
    for row in rows(raw.get("world_color_captures", []), "world_color_captures"):
        object_value(row, "world color capture")
        if row.get("captured") is not True:
            continue
        target = object_value(row["target"], "world target")
        key = tuple(uint(target[k], k) for k in ("width", "height", "format", "samples"))
        targets[key] += 1
    result["world_color_targets"] = [dict(zip(("width", "height", "format", "samples"), k),
                                              capture_rows=v) for k, v in sorted(targets.items())]
    gameplay = object_value(raw.get("gameplay", {}), "gameplay")
    rig = object_value(gameplay.get("rig_publication", {}), "rig_publication")
    assets = Counter()
    for row in rows(rig.get("weapon_profile_samples", []), "weapon_profile_samples"):
        object_value(row, "weapon profile sample")
        name = row.get("asset_name")
        if not isinstance(name, str) or not name or len(name) > 1024:
            raise ValueError("weapon sample lacks exact asset_name")
        assets[name] += 1
    result["observed_assets"] = [{"asset_name": k, "sample_rows": v} for k, v in sorted(assets.items())]
    return result


def reflection_inventory(paths, ev):
    leads = []
    for path in paths:
        raw = ev.read_json(path)
        if raw is None:
            continue
        def parse():
            for item in rows(raw, "reflection"):
                object_value(item, "reflection type")
                name = item.get("name")
                if not isinstance(name, str):
                    raise ValueError("reflection type lacks name")
                for field in rows(item["fields"], "reflection fields"):
                    object_value(field, "reflection field")
                    if field.get("name") in OPTIC_FIELDS:
                        for key in ("name", "offset", "type"):
                            if not isinstance(field.get(key), str) or not field[key]:
                                raise ValueError(f"reflection field lacks {key}")
                        leads.append({"source": str(path), "owner_type": name,
                                      **{k: field[k] for k in ("name", "offset", "type")},
                                      "meaning": "metadata lead only; live consumer/units/optic role unverified"})
        ev.parse(str(path), parse, None)
    return leads


def inventory(trace, reflection=()):
    trace = Path(trace).resolve()
    ev = Evidence()
    if not trace.is_dir():
        ev.errors.append(f"trace directory does not exist: {trace}")
    manifest = ev.read_json(trace / "manifest.json")
    manifest = ev.parse("manifest", lambda: object_value(manifest, "manifest"), {}) if manifest is not None else {}
    hashes = {}
    for key in ("game_sha256", "probe_sha256"):
        value = manifest.get(key)
        hashes[key] = value.lower() if isinstance(value, str) and SHA256.fullmatch(value) else None
        if value is not None and hashes[key] is None:
            ev.errors.append(f"manifest: invalid {key}")
    enabled = manifest.get("pass_evidence")
    if enabled is not None and type(enabled) is not bool:
        ev.errors.append("manifest pass_evidence must be boolean")
        enabled = None
    raw_pass = ev.read_json(trace / "pass-buffer-evidence.json")
    passes = ev.parse("pass-buffer-evidence", lambda: pass_inventory(trace, raw_pass, enabled, ev),
                      {"capture_state": "invalid", "rows": 0, "valid_rows": 0})
    projections = ev.parse("projection-bindings", lambda: projection_inventory(
        ev.read_json(trace / "projection-bindings.json")), {"available": False})
    contexts = ev.parse("camera-context-evidence", lambda: context_inventory(
        ev.read_json(trace / "camera-context-evidence.json")), {"available": False})
    native = ev.parse("native-trace", lambda: native_inventory(
        ev.read_json(trace / "native-trace.json")), {"available": False})
    tracked = ev.read_json(trace / "tracked-camera-evidence.json")
    tracked_result = {"available": False}
    if tracked is not None:
        def tracked_parse():
            object_value(tracked, "tracked-camera-evidence")
            frame = uint(tracked["native_frame"], "native_frame")
            mask = uint(tracked["captured_mask"], "captured_mask", 63)
            steps = rows(tracked["steps"], "steps")
            if steps != ["applied", "before_draw", "after_draw"]:
                raise ValueError("unknown camera evidence steps")
            return {"available": True, "native_frame": frame, "captured_mask": mask,
                    "steps": steps, "matrix_blobs_audited": False, "optic_role": "unknown"}
        tracked_result = ev.parse("tracked-camera-evidence", tracked_parse, tracked_result)
    reflections = reflection_inventory(reflection, ev)
    if contexts.get("patch_failures") or contexts.get("restore_failures"):
        ev.issues.append("camera context correction/restoration failure recorded")
    if native.get("hooks_disabled") is False:
        ev.issues.append("native trace does not confirm stopped hooks")
    if not manifest:
        ev.issues.append("capture manifest unavailable")
    if not hashes["game_sha256"] or not hashes["probe_sha256"]:
        ev.issues.append("exact game/probe build identity unavailable")
    if not passes.get("valid_rows") and enabled is not False:
        ev.issues.append("no valid pass samples")
    if not native.get("available"):
        ev.issues.append("native completion/asset evidence unavailable")
    missing = ["exact_asset_and_attachment_identity_linked_to_pass_frame",
               "native_optic_mode_identity", "hip_or_ads_phase_and_transition_state",
               "per_draw_frame_owner_and_equipment_generation",
               "shader_resource_texture_bindings", "material_identity_and_asset_link",
               "blend_depth_stencil_state", "sampler_mip_and_postprocess_state",
               "reticle_hud_draw_identity", "native_ads_acknowledgement",
               "complete_draw_timeline_and_all_viewport_coverage"]
    if not passes.get("recorded_viewports") or passes.get("rows_without_recorded_viewport"):
        missing.append("per_sample_viewport_dimensions")
    if not projections.get("available") or not projections.get("rows"):
        missing.append("native_projection_evidence")
    if not hashes["game_sha256"]:
        missing.append("exact_game_build")
    if not hashes["probe_sha256"]:
        missing.append("exact_collector_build")
    status = "invalid" if ev.errors else "partial" if ev.issues else "valid_inventory"
    blob_manifest = "\n".join(f"{k}:{v}" for k, v in sorted(ev.blobs.items()))
    return {"schema": "fvr.optic_capture_inventory.v1", "trace": str(trace),
            "status": status, "classification": "unknown", "runtime_enabled": False,
            "comparison_key": {**hashes, "asset_name": None, "attachment_configuration": None,
                               "native_optic_mode": None, "phase": None},
            "comparison_key_reason": "legacy schemas do not link exact optic asset/mode/ADS phase to pass samples",
            "pass_inventory": passes, "projection_evidence": projections,
            "camera_context_evidence": contexts, "tracked_camera_evidence": tracked_result,
            "native_evidence": native, "reflection_leads": reflections,
            "phase_comparison": {"ready": False, "missing_fields": missing,
                                 "cross_capture_pointer_matching_allowed": False},
            "constant_blob_provenance": {"verified_files": len(ev.blobs),
                "manifest_sha256": hashlib.sha256(blob_manifest.encode()).hexdigest() if ev.blobs else None,
                "manifest_definition": "sorted relative_filename:sha256 lines, UTF-8, no trailing newline"},
            "sources": ev.sources, "missing_files": ev.missing_files,
            "capture_issues": ev.issues, "validation_errors": ev.errors}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, action="append", required=True)
    parser.add_argument("--reflection", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, help="write only this NEW JSON file; never overwrite evidence")
    args = parser.parse_args(argv)
    reports = [inventory(path, args.reflection) for path in args.trace]
    result = {"schema": "fvr.optic_capture_inventory_batch.v1", "captures": reports,
              "phase_comparison_ready": False,
              "note": "Read-only evidence inventory; valid inventory is not optic classification or readiness."}
    payload = json.dumps(result, indent=2, allow_nan=False) + "\n"
    if args.output:
        try:
            with args.output.open("x", encoding="utf-8") as handle:
                handle.write(payload)
        except OSError as exc:
            parser.error(f"cannot create output without overwriting: {exc}")
    else:
        print(payload, end="")
    return 2 if any(r["validation_errors"] for r in reports) else 0


if __name__ == "__main__":
    sys.exit(main())

