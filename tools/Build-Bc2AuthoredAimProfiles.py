"""Compile bounded, explicitly reviewed authored visual axes; reads metadata only.

The review is author input, never inferred from names, class, or muzzle origin.
Source game meshes/previews remain private; generated files contain metadata only.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                    ensure_ascii=True, allow_nan=False).encode()).hexdigest()


def read(path):
    if path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError("metadata size bound")
    return json.loads(path.read_text(encoding="utf-8"))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def append_reviewed_batch(path, sources, provenance, rows, muzzle):
    """Revalidate a frozen independent review; never discover new axis defaults."""
    packet = read(path)
    require(packet["schema"] == "fvr.bc2.authored_axis_batch_review.v1" and
            packet["review_level"] == "authored_geometry_reviewed" and
            packet["native_verified"] is False and packet["headset_accepted"] is False,
            "independent authored-only review required")
    require(len(packet["profiles"]) + len(packet["additional_configuration_bindings"]) <= 512 and
            len(packet["meshes"]) <= 128 and len(packet["poses"]) <= 128, "independent review bounds")
    packet_sha = sha(path)
    sources["independent_axis_review:" + packet_sha] = {"path": str(path), "sha256": packet_sha}
    data = {}
    for name, source in packet["sources"].items():
        source_path = Path(source["path"])
        require(sha(source_path) == source["sha256"], "independent source changed: " + name)
        data[name] = read(source_path)
    for pose in packet["poses"]:
        require(sha(Path(pose["geometry_image"]["path"])) == pose["geometry_image"]["sha256"],
                "independently reviewed image changed")
    meshes = {m["mesh_path"]: m for m in packet["meshes"]}
    require(len(meshes) == len(packet["meshes"]), "duplicate independent mesh")
    for mesh, m in meshes.items():
        require(mesh not in provenance and m["weapon_bone"] == "jntWpn_1" and
                m["coordinate_convention"] == packet["coordinate_convention"], "independent root convention")
        f, u = m["model_forward"], m["model_up"]
        require(all(len(v) == 3 and all(type(x) in (int, float) and math.isfinite(x) for x in v) and
                    abs(sum(x*x for x in v)-1) < 1e-4 for v in (f, u)) and
                abs(sum(x*y for x, y in zip(f, u))) < 1e-4, "independent axes")
        provenance[mesh] = {"review": m, "review_digest": digest(m),
                            "review_packet_sha256": packet_sha}
    seen = {(r["asset"], r["configuration_path"], r["mesh_path"]) for r in rows}
    omitted = []
    for p in packet["profiles"] + packet["additional_configuration_bindings"]:
        key = p["native_asset_name"], p["configuration_path"], p["configured_mesh_path"]
        require(key not in seen, "duplicate independent exact configuration")
        m = meshes[key[2]]
        require(all(p[k] == m[k] for k in ("mesh_sha256", "lod_sha256", "skeleton_sha256",
                "rig_fingerprint", "model_forward", "model_up")), "independent row/mesh identity")
        require(p["source_binding_refs"], "independent row missing source binding")
        raw = []
        for ref in p["source_binding_refs"]:
            b = data[ref["source_id"]]["profiles"][ref["profiles_index"]]
            require((b["native_asset_name"], b["weapon"]["resource"].removesuffix(".dbx"),
                     b["configured_mesh_path"]) == key and b["binding_digest"] == ref["binding_digest"] and
                    b["animation_sha256"] == ref["clip_sha256"] and
                    b["weapon"]["sha256"] == ref["configuration_sha256"] and
                    b["weapon"]["instance_guid"] == ref["configuration_guid"] and
                    b["state_count"] == 1 and b["weapon_bone"] == "jntWpn_1" and
                    b["coordinate_convention"] == packet["coordinate_convention"] and
                    b["skeleton_sha256"] == m["skeleton_sha256"] and b["rig_fingerprint"] == m["rig_fingerprint"],
                    "independent exact configuration/source binding")
            geometry = [v["geometry"] for v in b["meshes"] if v["configured_mesh_path"] == key[2]]
            require(len(geometry) == 1 and geometry[0]["mesh_sha256"] == m["mesh_sha256"] and
                    any(v["lod"] == 0 and v["data_sha256"] == m["lod_sha256"] for v in geometry[0]["lods"]),
                    "independent exact mesh/LOD content")
            raw.append(b)
        if "firing_evidence" in p:
            ref = p["firing_evidence"]
            firing = data[ref["source_id"]]["profiles"][ref["profiles_index"]]
            require(digest(firing) == ref["row_digest"] and firing["binding_digest"] == p["binding_digest"] and
                    (firing["native_asset_name"], firing["weapon"]["resource"].removesuffix(".dbx"),
                     firing["configured_mesh_path"]) == key and firing["weapon"] == raw[0]["weapon"] and
                    firing["mesh_sha256"] == m["mesh_sha256"] and
                    firing["initial_direction"]["raw_values"] == [0, 0, 1, 0], "independent exact typed direction")
        else:
            matches = [v for v in muzzle if (v["asset"], v["configuration_path"], v["mesh_path"]) == key]
            if not matches:
                omitted.append({"asset": key[0], "configuration_path": key[1], "mesh_path": key[2],
                                "reason": "Exact typed firing metadata unavailable; visual review alone not enrolled."})
                continue
            require(len(matches) == 1, "independent ambiguous firing configuration")
            v = matches[0]
            require(v["profile_digest"] == p["muzzle_metadata_join"]["profile_digest"] and
                    any(b["binding_digest"] in v["equivalent_source_binding_digests"] for b in raw) and
                    v["configuration_sha256"] == raw[0]["weapon"]["sha256"] and
                    v["configuration_guid"] == raw[0]["weapon"]["instance_guid"] and
                    v["firing"]["fields"]["InitialDirection"] == [0, 0, 1] and
                    v["origin"]["direction_claim"] == "none", "independent typed muzzle metadata join")
            firing = v["firing"]
        b = raw[0]
        axis = {"review_digest": digest(m), "review_packet_sha256": packet_sha,
                "reviewed_configuration_digest": digest(p), "mesh_sha256": m["mesh_sha256"],
                "lod_sha256": m["lod_sha256"], "source_binding_digest": b["binding_digest"],
                "configuration_sha256": b["weapon"]["sha256"], "firing_metadata_digest": digest(firing),
                "model_forward": m["model_forward"], "model_up": m["model_up"]}
        rows.append({"asset": key[0], "configuration_path": key[1], "mesh_path": key[2],
            "family": key[2].rsplit("/", 1)[-1], "source_binding_digest": b["binding_digest"],
            "configuration_sha256": b["weapon"]["sha256"], "configuration_guid": b["weapon"]["instance_guid"],
            "mesh_sha256": m["mesh_sha256"], "lod_sha256": m["lod_sha256"],
            "clip_sha256": b["animation_sha256"], "skeleton_sha256": b["skeleton_sha256"],
            "rig_fingerprint": b["rig_fingerprint"], "axis_evidence": axis,
            "axis_evidence_digest": digest(axis), "native_verified": False, "headset_accepted": False,
            "own_pose_geometry_gap_retained": p.get("own_pose_geometry_gap_retained", False)})
        seen.add(key)
    return omitted


def build(args):
    review = read(args.reviews)
    require(review["schema"] == "fvr.bc2.authored_aim_mesh_reviews.v1" and
            review["review_level"] == "authored_geometry_reviewed" and
            review["native_verified"] is False and review["headset_accepted"] is False,
            "explicit authored-only review required")
    reviews = review["meshes"]
    require(0 < len(reviews) <= 128, "review count bound")
    by_mesh = {r["mesh_path"]: r for r in reviews}
    require(len(by_mesh) == len(reviews), "duplicate mesh review")
    bindings = read(args.bindings)["profiles"]
    mechanisms = read(args.mechanisms)["mechanisms"]
    previews = read(args.source / "preview-receipt.json")["rows"]
    muzzle = read(args.muzzle)["profiles"]
    require(len(bindings) <= 512 and len(mechanisms) <= 512 and len(muzzle) <= 512,
            "source count bound")
    sources = {name: {"path": str(path), "sha256": sha(path)} for name, path in {
        "bindings": args.bindings, "mechanisms": args.mechanisms,
        "preview_receipt": args.source / "preview-receipt.json",
        "preview_renderer": args.source / "render_previews.py",
        "muzzle_enrollment": args.muzzle, "mesh_reviews": args.reviews}.items()}
    provenance = {}
    for mesh, r in by_mesh.items():
        f, u = r["model_forward"], r["model_up"]
        require(all(isinstance(v, list) and len(v) == 3 and
                    all(type(x) in (int, float) and math.isfinite(x) for x in v) and
                    abs(sum(x*x for x in v)-1) < 1e-4 for v in (f, u)) and
                abs(sum(x*y for x, y in zip(f, u))) < 1e-4, "review axes")
        require(sha(args.source / "previews" / r["preview"]) == r["preview_sha256"],
                "reviewed preview content changed")
        preview = next(p for p in previews if Path(p["parts_preview"]).name == r["preview"])
        record = next(m for m in mechanisms if m["digest"] == preview["record_digest"])
        require(record["configured_mesh"] == mesh and
                record["source"]["mesh_sha256"] == r["mesh_sha256"] and
                record["source"]["lod_sha256"] == r["lod_sha256"], "preview mesh binding")
        provenance[mesh] = {"review": r, "review_digest": digest(r),
            "mechanism_record_digest": record["digest"], "preview_binding_digest": record["binding_digest"],
            "preview_clip_sha256": record["source"]["sha256"],
            "skeleton_sha256": record["skeleton_sha256"],
            "omitted_preview_triangles": preview["omitted_triangles"]}
    rows, seen = [], set()
    for b in bindings:
        mesh = b["configured_mesh_path"]
        if mesh not in by_mesh:
            continue
        r = by_mesh[mesh]
        configuration = b["weapon"]["resource"].removesuffix(".dbx")
        key = b["native_asset_name"], configuration, mesh
        require(key not in seen, "duplicate exact configuration")
        seen.add(key)
        geometry = [m["geometry"] for m in b["meshes"] if m["configured_mesh_path"] == mesh]
        require(len(geometry) == 1 and geometry[0]["mesh_sha256"] == r["mesh_sha256"] and
                any(l["lod"] == 0 and l["data_sha256"] == r["lod_sha256"] for l in geometry[0]["lods"]),
                "exact reference mesh content")
        require(b["skeleton_sha256"] == provenance[mesh]["skeleton_sha256"] and
                b["coordinate_convention"] == review["coordinate_convention"] and
                b["state_count"] == 1, "source rig/convention/state")
        matches = [m for m in muzzle if (m["asset"], m["configuration_path"], m["mesh_path"]) == key]
        require(len(matches) == 1, "exact typed firing configuration missing")
        m = matches[0]
        require(b["binding_digest"] in m["equivalent_source_binding_digests"] and
                m["configuration_sha256"] == b["weapon"]["sha256"] and
                m["configuration_guid"] == b["weapon"]["instance_guid"] and
                m["rig_fingerprint"] == b["rig_fingerprint"], "configuration content binding")
        require(m["firing"]["fields"]["InitialDirection"] == [0, 0, 1], "typed firing axis disagreement")
        # The origin-only enrollment is corroborating typed source metadata,
        # never promoted into direction evidence. Review r is the axis authority.
        require(m["origin"]["direction_claim"] == "none", "unexpected origin scope")
        axis = {"review_digest": digest(r), "mesh_sha256": r["mesh_sha256"],
                "lod_sha256": r["lod_sha256"], "source_binding_digest": b["binding_digest"],
                "configuration_sha256": b["weapon"]["sha256"],
                "firing_metadata_digest": digest(m["firing"]),
                "model_forward": r["model_forward"], "model_up": r["model_up"]}
        rows.append({"asset": key[0], "configuration_path": configuration, "mesh_path": mesh,
            "family": r["family"], "source_binding_digest": b["binding_digest"],
            "configuration_sha256": b["weapon"]["sha256"], "configuration_guid": b["weapon"]["instance_guid"],
            "mesh_sha256": r["mesh_sha256"], "lod_sha256": r["lod_sha256"],
            "clip_sha256": b["animation_sha256"], "skeleton_sha256": b["skeleton_sha256"],
            "rig_fingerprint": b["rig_fingerprint"], "axis_evidence": axis,
            "axis_evidence_digest": digest(axis), "native_verified": False, "headset_accepted": False})
    require({r["mesh_path"] for r in rows} == set(by_mesh), "review with no bound configurations")
    omitted = []
    for batch in args.batch_review:
        omitted.extend(append_reviewed_batch(batch, sources, provenance, rows, muzzle))
    rows.sort(key=lambda p: (p["asset"], p["configuration_path"], p["mesh_path"]))
    enrolled = {(p["asset"], p["configuration_path"], p["mesh_path"]) for p in rows}
    remaining = [{k: p[k] for k in ("asset", "configuration_path", "mesh_path")} for p in muzzle
                 if (p["asset"], p["configuration_path"], p["mesh_path"]) not in enrolled]
    result = {"schema": "fvr.bc2.authored_aim_enrollment.v1", "scope": "authored visual orientation only",
        "native_verified": False, "headset_accepted": False, "sources": sources,
        "mesh_evidence": list(provenance.values()), "profiles": rows,
        "summary": {"configurations": len(rows), "asset_names": len({r["asset"] for r in rows}),
                    "mesh_families": len({r["mesh_path"] for r in rows}), "omitted_configurations": len(omitted),
                    "matching_origin_enrollment_configurations": len(muzzle)-len(remaining),
                    "unmatched_origin_enrollment_configurations": len(remaining)},
        "omitted_configurations": omitted,
        "unenrolled_origin_configurations": remaining,
        "gaps": ["All meshes outside the explicit reviewed set remain unenrolled; existing legacy/authored-grip alignment retains priority.",
                 "Native accuracy, live submitted mesh and headset alignment acceptance remain open."]}
    header = ['#pragma once', '#include "Bc2AuthoredAim.h"', '#include <array>',
              'namespace fvr::bc2::generated {',
              '// Authored visual axes only. See enrollment.json for exact per-configuration evidence.',
              f'inline constexpr std::array<AuthoredAimProfile,{len(rows)}> AuthoredAims{{{{']
    for p in rows:
        values = [p[k] for k in ("asset", "configuration_path", "mesh_path", "source_binding_digest",
                                 "mesh_sha256", "skeleton_sha256", "axis_evidence_digest")]
        vectors = ["math::Vec3{" + ",".join(f"{float(x):.1f}f" for x in p["axis_evidence"][k]) + "}"
                   for k in ("model_forward", "model_up")]
        header.append("    {" + ",".join(map(json.dumps, values)) + ",0x" +
                      p["rig_fingerprint"].split(":")[1] + "ull," + ",".join(vectors) + "},")
    header += ['}};', '} // namespace fvr::bc2::generated', '']
    outputs = {"Aim.generated.h": "\n".join(header), "enrollment.json": json.dumps(result, indent=2) + "\n"}
    args.output.mkdir(parents=True, exist_ok=True)
    for name, content in outputs.items():
        path = args.output / name
        if args.check:
            require(path.read_text(encoding="utf-8") == content, "generated output mismatch: " + name)
        else:
            path.write_text(content, encoding="utf-8", newline="\n")
    print(json.dumps(result["summary"], sort_keys=True))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Private LMG evidence directory (read only)")
    parser.add_argument("--reviews", type=Path, required=True)
    parser.add_argument("--muzzle", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--batch-review", type=Path, action="append", default=[], help="Frozen independent exact mesh/configuration review; repeat for disjoint batches")
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    arguments.bindings = arguments.source / "all" / "reference-bindings.json"
    arguments.mechanisms = arguments.source / "all" / "mechanisms.json"
    build(arguments)
