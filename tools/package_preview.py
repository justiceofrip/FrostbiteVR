"""Allowlisted, deterministic local preview staging. Never starts BC2/XR or publishes."""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import zipfile

LOADER_SHA256 = "a69729a3348bc4dcb9be126a49d594074e477be7312f65180b0d082bf100fa5f"
CODE = {".cpp", ".c", ".h", ".inc", ".def"}
EXACT = (
    "cmake/ManualEmptyRegistryCoverage.cmake",
    "cmake/Bc2NativeOperationCapability.cmake",
    "tools/bc2_native_operation_receipts.py", "tests/test_bc2_native_operation_receipts.py",
    "tools/audit_empty_fire.py", "tests/test_audit_empty_fire.py",
    "CMakeLists.txt", "Build.ps1", "LICENSE", "Start-NativeTrace.ps1",
    "Start-BC2VRSession.ps1", "Stop-BC2VRSession.ps1", "Setup-BC2VRPreview.ps1",
    "Start-BC2VRPreview.ps1", "config/example.json", "tools/game_window.py", "tools/read_bc2.py",
    "tools/package_preview.py", "tools/Build-SingleplayerPreview.ps1",
    "tests/test_package_preview.py", "tests/Test-PreviewLaunchers.ps1",
    "tools/bc2_weapon_asset_pipeline.py", "tools/bc2_mesh_geometry.py", "tools/inspect_bc2_mesh_asset.py",
    "tools/bc2_weapon_config_pipeline.py", "tests/test_bc2_weapon_config_pipeline.py",
    "tools/bc2_visibility_descriptors.py", "tests/test_bc2_visibility_descriptors.py",
    "tests/test_bc2_visibility_configuration_paths.py",
    "tests/test_bc2_native_operation_descriptor_data.py",
    "tools/bc2_weapon_package_index.py", "tests/test_bc2_weapon_package_index.py",
    "tools/bc2_weapon_component_closure.py", "tests/test_bc2_weapon_component_closure.py",
    "tools/bc2_weapon_capability_report.py", "tests/test_bc2_weapon_capability_report.py",
    "tools/bc2_mechanism_coverage.py", "tests/test_bc2_mechanism_coverage.py",
    "docs/WEAPON-SYSTEM-PIPELINE.md",
    "tools/bc2_authored_support_bindings.py", "tests/test_bc2_authored_support_bindings.py",
    "tools/bc2_weapon_mesh_bindings.py", "tests/test_bc2_weapon_mesh_bindings.py",
    "tools/bc2_weapon_draw_catalog.py", "tests/test_bc2_weapon_draw_catalog.py", "release/WEAPON-DRAW-CATALOG.md",
    "tools/bc2_granny_resource.py", "tools/bc2_granny_curves.py", "tools/bc2_weapon_animation_pipeline.py",
    "tests/test_bc2_weapon_animation.py", "release/WEAPON-ANIMATION-PIPELINE.md", "licenses/Norbyte-LSLib-MIT.txt",
    "tools/bc2_authored_sight_geometry.py", "tests/test_bc2_authored_sight_geometry.py",
    "tools/bc2_authored_optic_catalog.py", "tests/test_bc2_authored_optic_catalog.py",
    "tools/bc2_authored_mechanism_geometry.py", "tests/test_bc2_authored_mechanism_geometry.py",
    "release/LMG-AUTHORED-MECHANISMS.md", "release/LMG-MECHANISM-COVERAGE.json",
    "tests/test_bc2_drum_grasp.py", "release/DRUM-CONTACT-FOLLOWUP.md",
    "tools/bc2_authored_grip_bindings.py", "tests/test_bc2_authored_grip_bindings.py", "release/AUTHORED-GRIP-BINDINGS.md",
    "tools/bc2_authored_hand_pose_batch.py", "tests/test_bc2_authored_hand_pose_batch.py",
    "tools/bc2_authored_magazine_geometry.py", "tests/test_bc2_authored_magazine_geometry.py", "release/AUTHORED-MAGAZINE-GEOMETRY.md",
    "tools/bc2_magazine_contact_batch.py", "tests/test_bc2_magazine_contact_batch.py", "release/MAGAZINE-CONTACT-BATCH.md", "release/CONTACT-COVERAGE.json",
    "tools/bc2_magazine_registry_header.py", "tests/test_bc2_magazine_registry_header.py", "release/MAGAZINE-REGISTRY.md",
    "tools/bc2_magazine_descriptor_manifest.py", "tests/test_bc2_magazine_descriptor_manifest.py", "release/MAGAZINE-DESCRIPTOR-JOBS.md",
    "tools/bc2_automatic_stock_bolt_proof.py", "tests/test_stock_bolt_dispatch.py", "release/AUTOMATIC-STOCK-BOLT-DISPATCH.md",
    "tests/test_bc2_paired_role.py", "release/PISTOL-PRECISION-SHOTGUN-MECHANISMS.md", "release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json",
    "tools/bc2_body_equipment_assets.py", "tests/test_bc2_body_equipment_assets.py",
    "tools/bc2_body_ammo_assets.py", "tools/Prepare-BodyAmmoAssets.ps1", "config/body-ammo-assets.json", "tests/test_bc2_body_ammo_assets.py",
    "tools/audit_pump_hold.py", "tests/test_audit_pump_hold.py",
    "tools/audit_pump_capture.py", "tests/test_audit_pump_capture.py", "release/PUMP-CAPTURE-VALIDATOR.md",
    "tools/report_empty_step.py", "tests/test_report_empty_step.py", "release/EMPTY-STEP-REPORT.md",
    "tools/report_arming_empty.py", "tests/test_report_arming_empty.py",
    "tools/report_live_arming_zero.py", "tests/test_live_zero.py",
    "tools/bc2_magazine_pipeline_coverage.py", "tests/test_bc2_magazine_pipeline_coverage.py", "release/MAGAZINE-PIPELINE-COVERAGE.md",
    "tools/inspect_lmg_common.py", "tools/build_lmg_jobs.py", "tools/capture_lmg_native.py",
    "tools/build_manual_reload_catalog.py", "tools/inspect_reload_animations.py", "tests/ManualReloadCatalogTests.py",
    "profiles/manual-reload-catalog.schema.json", "release/MANUAL-RELOAD-CATALOG.md",
    "tools/capture_reload_state.py", "tools/capture_reload_server.py",
    "tests/test_lmg_jobs.py", "release/LMG-BINDING-JOBS.md",
    "profiles/weapon-family-work-plan.json", "profiles/runtime-weapon-scope-20261003.json",
    "tests/test_bc2_weapon_asset_pipeline.py", "release/WEAPON-PIPELINE.md",
    ".github/workflows/build.yml", ".github/ISSUE_TEMPLATE/bug_report.yml",
    "release/README.md", "release/FEATURES.json", "release/BUILDING.md", "release/ARCHITECTURE.md",
    "release/HEADSET-TEST-CARD.md", "release/PRERELEASE-DRAFT.md",
    "release/GP30-SIGHT-CONSUMER.md",
    "release/VEHICLE-RETICLE-PRODUCER.md",
    "release/THIRD_PARTY_NOTICES.md", "licenses/BFVR-MIT.txt",
    "licenses/MinHook-BSD.txt", "licenses/OpenXR-Apache-2.0.txt", "licenses/JsonCpp-MIT.txt",
)
RUNTIME = (
    'tools/bc2_authored_grip_bindings.py',
    'tools/bc2_authored_magazine_geometry.py',
    'tools/bc2_body_ammo_assets.py',
    'tools/bc2_body_equipment_assets.py',
    'tools/bc2_granny_curves.py',
    'tools/bc2_granny_resource.py',
    'tools/bc2_mesh_geometry.py',
    'tools/bc2_weapon_animation_pipeline.py',
    'tools/bc2_weapon_asset_pipeline.py',
    'tools/bc2_weapon_config_pipeline.py',
    'tools/bc2_weapon_mesh_bindings.py',
    'tools/inspect_bc2_mesh_asset.py',
    'tools/Prepare-BodyAmmoAssets.ps1',
    'config/body-ammo-assets.json',
    'licenses/Norbyte-LSLib-MIT.txt',
    "LICENSE", "Start-NativeTrace.ps1", "Start-BC2VRSession.ps1",
    "Stop-BC2VRSession.ps1", "Setup-BC2VRPreview.ps1", "Start-BC2VRPreview.ps1",
    "config/example.json", "tools/game_window.py", "tools/read_bc2.py", "licenses/BFVR-MIT.txt",
    "licenses/MinHook-BSD.txt", "licenses/OpenXR-Apache-2.0.txt", "licenses/JsonCpp-MIT.txt",
    "README.md", "FEATURES.json", "THIRD_PARTY_NOTICES.md", "BUILDING.md", "HEADSET-TEST-CARD.md",
)
BINARIES = {
    "build/x86/BC2NativeProbe.dll": 0x14C,
    "build/x86/BC2NativeTrace.exe": 0x14C,
    "build/x64/BC2XrHost.exe": 0x8664,
    "build/x64/runtime/openxr/win64/openxr_loader.dll": 0x8664,
}
SYSTEM_IMPORTS = {"advapi32.dll", "kernel32.dll", "user32.dll", "ole32.dll",
                  "d3d11.dll", "dxgi.dll", "d3dcompiler_47.dll"}
IGNORED = """/build/
/reports/
/runtime/
/dist/
/test-temp/
/config/local.json
**/__pycache__/
*.pyc
*.pdb
*.obj
*.exe
*.dll
*.zip
*.user
"""

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def encoded(value) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")

def safe_relative(value: str) -> str:
    path = Path(value)
    if path.is_absolute() or ".." in path.parts or "\\" in value or ":" in value:
        raise ValueError("Unsafe package path: " + value)
    return value

def read_inside(root: Path, name: str) -> bytes:
    path = root / safe_relative(name)
    if not path.resolve().is_relative_to(root.resolve()) or path.is_symlink():
        raise ValueError("Source path escapes package root: " + name)
    return path.read_bytes()

def collect_source(root: Path, overlay: Path | None = None) -> dict[str, bytes]:
    names = set(EXACT)
    for directory in ("include", "src", "tests", "third_party"):
        for base in (root, overlay):
            if base is None:
                continue
            for path in (base / directory).rglob("*"):
                if path.is_file() and (path.suffix in CODE or (directory == "third_party" and path.suffix == ".txt")):
                    names.add(path.relative_to(base).as_posix())
    files = {}
    for name in sorted(names):
        base = overlay if overlay and (overlay / name).is_file() else root
        files[name] = read_inside(base, name)
    for public in ("README.md", "FEATURES.json", "BUILDING.md", "THIRD_PARTY_NOTICES.md", "ARCHITECTURE.md",
                   "HEADSET-TEST-CARD.md", "PRERELEASE-DRAFT.md", "WEAPON-PIPELINE.md"):
        files[public] = files["release/" + public]
    files[".gitignore"] = IGNORED.encode()
    # Known dev reports/config are never even enumerated. C++ and notices stay
    # byte-exact; package manifests do not contain original absolute paths.
    return files

def manifest(files: dict[str, bytes], kind: str, version: str) -> dict:
    return {"schema": 1, "kind": kind, "version": version,
            "files": [{"path": n, "size": len(b), "sha256": sha(b)} for n, b in sorted(files.items())]}

def validate_python_helpers(files: dict[str, bytes]) -> None:
    # Audit every import, including imports inside main(). Keep optional capture
    # separate from the standard-library-only launcher path; do not accidentally
    # resolve omitted local modules from the developer's PYTHONPATH/site folders.
    standard = {"argparse", "ctypes", "json", "pathlib", "time", "struct", "os"}
    body_standard = {"__future__", "bisect", "collections", "copy", "dataclasses",
                     "hashlib", "math", "re", "statistics", "uuid", "zlib"}
    optional = {("tools/game_window.py", "PIL")}
    pending = ["tools/game_window.py"]; seen = set()
    if "tools/Prepare-BodyAmmoAssets.ps1" in files:
        pending.extend(("tools/bc2_body_ammo_assets.py", "tools/bc2_body_equipment_assets.py"))
        standard |= body_standard
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        if name not in files:
            raise ValueError("Missing packaged Python dependency: " + name)
        tree = ast.parse(files[name], filename=name)
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                modules = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom):
                if node.level:
                    raise ValueError("Unaudited relative Python import in " + name)
                modules = [node.module or ""]
            else:
                continue
            for module in modules:
                top = module.split('.')[0]
                if top in standard or (name, top) in optional:
                    continue
                dependency = "tools/" + top + ".py"
                if dependency not in files:
                    raise ValueError("Missing or unaudited packaged Python dependency: " + module)
                pending.append(dependency)

def preflight_python_helpers(root: Path) -> dict:
    command = [sys.executable, "-E", "-S", "-B", str(root / "tools/game_window.py"), "--preflight"]
    result = subprocess.run(command, cwd=root.parent, capture_output=True, text=True, timeout=15)
    if result.returncode:
        raise ValueError("Extracted window-helper preflight failed: " + result.stderr.strip())
    try:
        report = json.loads(result.stdout)
    except ValueError as error:
        raise ValueError("Extracted window-helper preflight did not return JSON") from error
    if report.get("helper_ready") is not True or report.get("process_opened") is not False or report.get("window_or_input_actions") is not False or report.get("dependency") != "tools/read_bc2.py":
        raise ValueError("Extracted window-helper preflight did not confirm the local read-only dependency")
    return {key: report[key] for key in ("helper_ready", "dependency", "process_opened", "window_or_input_actions")}

def pe_info(data: bytes) -> dict:
    if data[:2] != b"MZ" or len(data) < 256:
        raise ValueError("Not a PE image")
    pe = struct.unpack_from("<I", data, 60)[0]
    if data[pe:pe+4] != b"PE\0\0":
        raise ValueError("PE signature missing")
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic not in (0x10B, 0x20B) or count > 96:
        raise ValueError("Unsupported PE layout")
    directory = opt + (96 if magic == 0x10B else 112)
    sections = []
    for n in range(count):
        entry = opt + optional_size + n * 40
        virtual_size, virtual, raw_size, raw = struct.unpack_from("<IIII", data, entry + 8)
        sections.append((virtual, max(virtual_size, raw_size), raw, raw_size))
    def offset(rva: int, size: int = 1) -> int:
        for virtual, length, raw, raw_size in sections:
            if virtual <= rva and rva + size <= virtual + length and rva - virtual + size <= raw_size:
                at = raw + rva - virtual
                if at + size <= len(data):
                    return at
        raise ValueError("PE import data outside image")
    import_rva, import_size = struct.unpack_from("<II", data, directory + 8)
    imports = []
    if import_rva:
        for n in range(min(import_size // 20, 256)):
            row = struct.unpack_from("<IIIII", data, offset(import_rva + n * 20, 20))
            if not any(row):
                break
            at = offset(row[3]); end = data.find(b"\0", at, at + 256)
            if end < 0:
                raise ValueError("Malformed PE import name")
            name = data[at:end].decode("ascii").lower()
            if name not in SYSTEM_IMPORTS and not re.fullmatch(r"api-ms-win-[a-z0-9-]+\.dll",name):
                raise ValueError("Undeclared imported dependency: " + name)
            imports.append(name)
        else:
            raise ValueError("Unterminated PE import list")
    return {"machine": hex(machine), "imports": sorted(set(imports))}

def verify_build(evidence: dict | None, source_manifest: bytes, binaries: dict[str, bytes]) -> bool:
    if evidence is None:
        return False
    if evidence.get("schema") != 1 or evidence.get("source_manifest_sha256") != sha(source_manifest):
        raise ValueError("Build attestation does not match this exact source snapshot")
    if set(evidence.get("test_counts", {})) != {"x86", "x64"} or not all(isinstance(x, int) and x > 0 for x in evidence["test_counts"].values()):
        raise ValueError("Both architecture test counts are required")
    hashes = evidence.get("binary_sha256", {})
    if set(hashes) != set(binaries) or any(hashes[n].lower() != sha(b) for n, b in binaries.items()):
        raise ValueError("Binary bytes do not match the tested build attestation")
    return True

def write_tree(root: Path, files: dict[str, bytes]) -> None:
    root.mkdir(parents=True, exist_ok=False)
    for name, data in sorted(files.items()):
        path = root / safe_relative(name); path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("xb") as stream:
            stream.write(data)

def archive(path: Path, files: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_STORED) as output:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(safe_relative(name), date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system = 3; info.external_attr = 0o100644 << 16
            output.writestr(info, data)

def stage(root: Path, out: Path, version: str, overlay: Path | None = None,
          source_only: bool = False, evidence: dict | None = None,
          require_verified: bool = False) -> dict:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", version):
        raise ValueError("Version must be a simple filename-safe tag")
    root=root.resolve();out=out.resolve()
    if out.exists() or out == root or root.is_relative_to(out):
        raise ValueError("Output must be a new directory, not a source ancestor")
    for base in (root, overlay):
        if base and any(out.is_relative_to((base / part).resolve()) for part in ("include","src","tests","third_party")):
            raise ValueError("Output must not be inside an enumerated source directory")
    source=collect_source(root, overlay)
    validate_python_helpers({n:source[n] for n in RUNTIME})
    source_manifest=encoded(manifest(source, "source", version))
    source["source-manifest.json"]=source_manifest
    runtime={};binary_manifest=None;verified=False
    if not source_only:
        binaries={n:read_inside(root,n) for n in BINARIES}
        metadata={}
        for name, data in binaries.items():
            info=pe_info(data)
            if int(info["machine"],16)!=BINARIES[name]:
                raise ValueError("Wrong binary architecture: "+name)
            metadata[name]=info
        loader="build/x64/runtime/openxr/win64/openxr_loader.dll"
        if sha(binaries[loader])!=LOADER_SHA256:
            raise ValueError("OpenXR loader hash is not the pinned supplied version")
        verified=verify_build(evidence, source_manifest, binaries)
        if require_verified and not verified:
            raise ValueError("A matching tested-build attestation is required")
        runtime={n:source[n] for n in RUNTIME};runtime.update(binaries)
        binary_manifest=manifest(binaries,"binary",version)
        for record in binary_manifest["files"]:
            record.update(metadata[record["path"]])
        binary_manifest.update(build_verified=verified,source_manifest_sha256=sha(source_manifest),headset_verified=False)
        runtime["binary-manifest.json"]=encoded(binary_manifest)
        runtime["source-manifest.json"]=source_manifest
        if verified:
            # Explicit sanitized fields only, never raw paths or build reports.
            runtime["build-attestation.json"]=encoded({k:evidence[k] for k in ("schema","source_manifest_sha256","binary_sha256","test_counts")})
        runtime["package-manifest.json"]=encoded(manifest(runtime,"single-player-preview",version))
    # Re-read before committing files: sources and executables must not change
    # midway through a package snapshot. Version metadata cannot hide a race.
    if collect_source(root,overlay)!={n:b for n,b in source.items() if n!="source-manifest.json"}:
        raise ValueError("Source changed during staging; retry after integration finishes")
    if not source_only and any(read_inside(root,n)!=runtime[n] for n in BINARIES):
        raise ValueError("Binary changed during staging")
    out.mkdir(parents=True,exist_ok=False)
    write_tree(out/"source",source)
    helper_preflight=preflight_python_helpers(out/"source")
    if not source_only:
        write_tree(out/"preview",runtime)
        helper_preflight=preflight_python_helpers(out/"preview")
    archive(out/(version+"-source.zip"),source)
    if not source_only:
        archive(out/(version+"-singleplayer-preview.zip"),runtime)
    digest={p.name:sha(p.read_bytes())for p in sorted(out.glob("*.zip"))}
    (out/"SHA256SUMS.json").write_bytes(encoded(digest))
    result={"schema":1,"version":version,"source_files":len(source),"preview_files":len(runtime),"build_verified":verified,
            "headset_verified":False,"published":False,"helper_preflight":helper_preflight,"source_manifest_sha256":sha(source_manifest),"archives":digest}
    (out/"stage-result.json").write_bytes(encoded(result))
    return result

def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root",type=Path,required=True);parser.add_argument("--overlay",type=Path)
    parser.add_argument("--out",type=Path,required=True);parser.add_argument("--version",required=True)
    parser.add_argument("--source-only",action="store_true");parser.add_argument("--build-attestation",type=Path)
    parser.add_argument("--require-build-verified",action="store_true")
    args=parser.parse_args();evidence=json.loads(args.build_attestation.read_text(encoding="utf-8-sig"))if args.build_attestation else None
    print(json.dumps(stage(args.root,args.out,args.version,args.overlay,args.source_only,evidence,args.require_build_verified),indent=2))

if __name__=="__main__":
    main()
