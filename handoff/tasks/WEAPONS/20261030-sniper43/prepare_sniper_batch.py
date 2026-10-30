"""Prepare every cached sniper configuration without native or geometry admission.

Reads existing immutable cache only. Writes this task directory only. No build,
test runner, game access, process access, Git, or source mutation is performed.
"""
from __future__ import annotations

import collections
import hashlib
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[3]
WORKSPACE = ROOT.parents[1]
CACHE = WORKSPACE / "bc2vr-recovery"
sys.path.insert(0, str(ROOT / "tools"))
import bc2_magazine_descriptor_manifest as descriptor
import bc2_singlefire_magazine_proof as singlefire
import bc2_boltaction_magazine_proof as boltaction
import bc2_magazine_registry_header as registry


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(name, data):
    target = OUT / name
    if target.resolve().parent != OUT:
        raise ValueError("Output escaped task directory")
    target.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def main():
    candidate = CACHE / "pistol-sniper-shotgun-mechanisms-candidate"
    paths = {
        "catalog": CACHE / "manual-reload-catalog-20261003-candidate/manual-reload-catalog.json",
        "selected": candidate / "selected-configurations.json",
        "mechanisms": candidate / "batch/mechanisms.json",
        "bindings": candidate / "batch/reference-bindings.json",
        "meshes": candidate / "batch/mesh-bindings.json",
        "geometry": candidate / "geometry-final.json",
        "singlefire_proof": ROOT / "profiles/singlefire238/static-proof.json",
        "singlefire_instructions": ROOT / "profiles/singlefire238/instruction-evidence.json",
        "familybatch36": ROOT / "handoff/tasks/CHAT14-16/20261030-familybatch36/input-plan.json",
        "descriptor_tool": ROOT / "tools/bc2_magazine_descriptor_manifest.py",
        "singlefire_tool": ROOT / "tools/bc2_singlefire_magazine_proof.py",
        "registry_tool": ROOT / "tools/bc2_magazine_registry_header.py",
        "native_profile": ROOT / "src/games/bc2/Bc2MagazineNativeProfile.h",
        "boltaction_tool": ROOT / "tools/bc2_boltaction_magazine_proof.py",
    }
    proof_root = CACHE / "automatic-magazine-stock-bolt-20261003-candidate/proof"
    for name, expected_hash in boltaction.EXPECTED_PROOF["cached_evidence_sha256"].items():
        path = proof_root / name
        if sha(path) != expected_hash:
            raise ValueError("Cached disassembly proof changed: " + name)
        paths["bolt_evidence_" + name] = path
    sources = {key: {"path": str(path), "sha256": sha(path)} for key, path in paths.items()}
    data = {key: json.loads(path.read_text(encoding="utf-8")) for key, path in paths.items() if path.suffix == ".json"}
    catalog = data["catalog"]
    catalog["configurations"] = [r for r in catalog["configurations"] if r["weapon_class"] == "wcSniper"]
    jobs = descriptor.build(catalog)
    if sources["singlefire_instructions"]["sha256"] != singlefire.EXPECTED_PROOF["instruction_evidence_sha256"]:
        raise ValueError("SingleFire instruction evidence changed")
    jobs = singlefire.apply(jobs, data["singlefire_proof"])
    jobs = boltaction.apply(jobs, boltaction.EXPECTED_PROOF)
    jobs["input_sha256"] = {"catalog": sources["catalog"]["sha256"], "singlefire_magazine_proof": sources["singlefire_proof"]["sha256"]}
    jobs["input_sha256"]["boltaction_magazine_proof"] = boltaction.PROOF_DIGEST
    jobs["scope"] = "All exact cached wcSniper configurations; no name-based admission or omitted variants"
    selected = [r for r in data["selected"]["rows"] if r["weapon_class"] == "wcSniper"]
    expected = {(r["resource"], r["resource_sha256"], r["instance_guid"].lower()) for r in selected}
    actual = {(r["identity"]["weapon"]["resource"], r["identity"]["weapon"]["sha256"], r["identity"]["weapon"]["guid"]) for r in jobs["descriptors"]}
    if expected != actual or len(actual) != 28 or jobs["rejected"]:
        raise ValueError("Exact cached sniper coverage differs")

    def pointer(source, collection, index, **extra):
        return {"source": source, "pointer": f"/{collection}/{index}", **extra}

    rows = []
    batches = []
    for index, row in enumerate(jobs["descriptors"]):
        ident = row["identity"]
        weapon = ident["weapon"]
        matches = lambda w: w["resource"] == weapon["resource"] and w["sha256"] == weapon["sha256"] and w["instance_guid"].lower() == weapon["guid"]
        bindings = [(i, r) for i, r in enumerate(data["bindings"]["profiles"]) if matches(r["weapon"])]
        mechanisms = [(i, r) for i, r in enumerate(data["mechanisms"]["mechanisms"]) if matches(r["weapon_configuration"])]
        meshes = [(i, r) for i, r in enumerate(data["meshes"]["weapons"]) if r["resource"] == weapon["resource"] and r["resource_sha256"] == weapon["sha256"] and r["instance_guid"].lower() == weapon["guid"]]
        if len(bindings) != 1 or len(mechanisms) != 1 or len(meshes) != 1:
            raise ValueError("Expected one exact cached state for " + weapon["resource"])
        binding = bindings[0][1]
        mechanism = mechanisms[0][1]
        if mechanism["binding_digest"] != binding["binding_digest"]:
            raise ValueError("Mechanism/reference binding differs")
        geometry = [(i, r) for i, r in enumerate(data["geometry"]["profiles"]) if r["grip_binding_digest"] == binding["binding_digest"]]
        if len(geometry) > 1:
            raise ValueError("Ambiguous exact strict geometry")
        gaps = [(i, r) for i, r in enumerate(data["geometry"]["gaps"]) if r["native_asset_name"] == ident["assetName"]]
        logic = row["authored_symbols"]["FireLogic.FireLogicType"]
        v = row["configuration"]["values"]
        if logic == "fltSingleFireWithBoltAction":
            group = "bolt_action_magazine_with_persistent_debt"
            proposed_family = "bc2.bolt-action-magazine-reload.v1"
        elif logic == "fltSingleFire" and any(v[k] for k in ("boltDelay", "boltTime", "holdBoltUntilFireRelease", "holdBoltUntilZoomRelease")):
            group = "singlefire_authored_bolt_fields"
            proposed_family = "bc2.singlefire-magazine-inactive-stock-bolt.v1"
        elif logic == "fltSingleFire":
            group = "singlefire_zero_bolt"
            proposed_family = "bc2.singlefire-magazine-zero-bolt.v1"
        elif logic == "fltAutomaticFire":
            group = "automatic_zero_bolt"
            proposed_family = "bc2.automatic-magazine-zero-bolt.v1"
        else:
            raise ValueError("Unclassified exact dispatch")
        measured = []
        for part_index, part in enumerate(mechanism["parts"]):
            if part["motion"]["kind"] == "static":
                continue
            measured.append({"source": "mechanisms", "pointer": f"/mechanisms/{mechanisms[0][0]}/parts/{part_index}",
                "bone": part["bone"], "motion_kind": part["motion"]["kind"],
                "translation_m": part["motion"]["max_translation_m"], "rotation_rad": part["motion"]["max_rotation_rad"],
                "has_authored_contact": part.get("contact") is not None,
                "semantic_role_admitted": False})
        geometry_work = "reuse_exact_strict_candidate_then_native_mesh_contact_join" if geometry else (
            "review_complete_paired_role_from_exact_rigid_parts" if ident["assetName"] == "M24" else
            "derive_reviewed_reverse_authored_withdrawal_path" if ident["assetName"] == "GOL" else
            "review_exact_authored_contact_frames_without_relaxing_stability")
        record = {"job_id": f"sniper43-{index:02d}", "identity": ident,
            "descriptor": {"path": str(OUT / "descriptor-jobs.json"), "pointer": f"/descriptors/{index}", "key": row["key"], "digest": row["descriptor_digest"]},
            "dispatch_group": group, "proposed_family": proposed_family,
            "candidate_dispatch_supported": row["same_reviewed_dispatch_shape"],
            "deferred_reasons": row["deferred_reasons"],
            "references": [pointer("bindings", "profiles", i, binding_digest=r["binding_digest"]) for i, r in bindings],
            "mechanisms": [pointer("mechanisms", "mechanisms", i, digest=r["digest"]) for i, r in mechanisms],
            "meshes": [pointer("meshes", "weapons", i) for i, _ in meshes],
            "strict_geometry": [pointer("geometry", "profiles", i, profile_digest=r["profile_digest"]) for i, r in geometry],
            "cached_asset_level_gaps": [pointer("geometry", "gaps", i, reason=r["reason"]) for i, r in gaps],
            "configured_mesh": binding["configured_mesh_path"], "rig_fingerprint": binding["rig_fingerprint"],
            "reload_clip": mechanism["source"], "moving_part_inputs": measured,
            "geometry_work": geometry_work, "native_admission": False, "runtime_enabled": False}
        rows.append(record)
        stages = ["exact_descriptor_extraction"]
        if not row["same_reviewed_dispatch_shape"]:
            stages += ["exact_shared_native_instruction_dispatch_review", "independent_native_family_review"]
        else:
            stages += ["independent_native_family_review"]
        stages += [geometry_work, "native_three_copy_ammunition_resource_join"]
        if group == "bolt_action_magazine_with_persistent_debt":
            stages += ["typed_reload_pause_resume_and_ammo_receipt_rebase", "exact_bolt_calibration_and_selected_native_cycle_join"]
        stages += ["immutable_exact_key_digest_registry_enrollment"]
        batches.append({"job_id": record["job_id"], "configuration": ident["weapon"], "asset": ident["assetName"],
            "inputs": {"path": str(OUT / "geometry-inputs.json"), "pointer": f"/configurations/{index}"},
            "proposed_family": proposed_family, "stages_before_tests": stages,
            "manual_operations": ["remove_original", "return_original", "discard_original", "seat_replacement", "partial_reload", "empty_reload", "retain_resource_on_tracking_or_equipment_interruption"],
            "owed_bolt_policy": "retain_same_cycle_and_shot_through_every_ammo_transaction" if group == "bolt_action_magazine_with_persistent_debt" else "no_manual_bolt_or_chamber_grant_from_authored_fields",
            "ready_to_enable": False})
    counts = collections.Counter(r["dispatch_group"] for r in rows)
    summary = {"catalog_origin_rows": len(catalog["configurations"]), "exact_configurations": len(rows),
        "native_asset_names": len({r["identity"]["assetName"] for r in rows}), "dispatch_groups": dict(counts),
        "exact_reference_bindings": len(rows), "exact_mechanism_inputs": len(rows), "exact_mesh_inputs": len(rows),
        "strict_geometry_configs": sum(bool(r["strict_geometry"]) for r in rows),
        "geometry_work_configs": sum(not r["strict_geometry"] for r in rows),
        "supported_candidate_dispatch_configs": sum(r["candidate_dispatch_supported"] for r in rows),
        "native_enabled_configs": 0, "tests_run": False, "builds_run": False, "game_access": False}
    write("descriptor-jobs.json", jobs)
    write("static-proof.json", boltaction.EXPECTED_PROOF)
    header, header_summary = registry.build(jobs)
    (OUT / "DisabledSniperRegistry.h").write_text(header, encoding="utf-8")
    write("geometry-inputs.json", {"schema": "fvr.bc2.sniper43.exact_geometry_inputs.v1", "sources": sources, "summary": summary, "configurations": rows})
    write("batch-jobs.json", {"schema": "fvr.bc2.sniper43.manual_reload_batch.v1", "summary": summary,
        "runtime_enabled": False, "jobs": batches,
        "minimum_ammo_conservation": {"remove": "L/R -> 0/R; original carries exactly L", "return": "0/R -> L/R; consume exact original once", "refill": "0/R -> min(C,R)/(R-min(C,R)); original must be explicitly discarded", "bolt": "no extra chamber round, no ammo change on stroke, same cycle/shot remain owed until exact completion"}})
    write("receipt.json", {"schema": "fvr.bc2.sniper43.preparation_receipt.v1", "summary": summary,
        "outputs": {name: sha(OUT / name) for name in ("descriptor-jobs.json", "geometry-inputs.json", "batch-jobs.json", "static-proof.json", "DisabledSniperRegistry.h", "IMPLEMENTATION.txt", "pause-rebase-interface.json", "HANDOFF.txt", "prepare_sniper_batch.py")},
        "registry_summary": header_summary,
        "source_files_modified": ["tools/bc2_magazine_descriptor_manifest.py", "tools/bc2_boltaction_magazine_proof.py", "tools/bc2_magazine_registry_header.py", "src/games/bc2/Bc2MagazineNativeProfile.h"],
        "evidence_level": "offline exact cached metadata preparation and source/static-disassembly review only"})
    print(json.dumps(summary, sort_keys=True))


if __name__ == "__main__":
    main()
