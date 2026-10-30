"""Exact MP443_sp readiness preparation; no chamber or capability admission.

prepare reads two hash-pinned authored resources and four reflected metadata
tables from disk. observe is a bounded, read-only three-copy survey for root to
run separately; its stage is an operator label, never an event/shot receipt.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))
from capture_reload_state import Image, Inspector
from capture_reload_server import capture, chain, discover, require_same_identity
from read_bc2 import Process
from inspect_bc2_mesh_asset import Archive
from bc2_weapon_config_pipeline import Resolver, effective_weapon, named, parse, scalar

EXE_SHA = "3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258"
ASSET = "MP443_sp"
ASSET_PATH = "Objects/Weapons/Handheld/RU_hg_MP443Grach/SP_hg_MP443Grach"
PROFILE = ROOT / "profiles/resource-enrollment295-pistol/selected-jobs.json"
STAGES = ("tactical-before", "tactical-after", "empty-before", "empty-after")
TYPE_NAMES = ("FireLogicData", "AmmoConfigData", "BoltActionData", "WeaponStateData")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def image_for(executable):
    image = Image(executable)
    require(sha(image.data) == EXE_SHA, "Executable differs from reviewed SingleFire image")
    return image


def locations(image, pattern):
    """Bounded offline references, never a process-memory scan."""
    found = []
    for rva, _, raw, size, _ in image.sections:
        data = image.data[raw:raw + size]
        start = 0
        while True:
            at = data.find(pattern, start)
            if at < 0:
                break
            found.append(image.base + rva + at)
            require(len(found) <= 256, "Too many metadata candidates")
            start = at + 1
    return found


def reflected_metadata(image, name):
    # Same metadata/table layout consumed by capture_reload_state.Inspector.info.
    # TypeInfo instances are initialized at runtime; do not reinterpret code
    # references as constructed TypeInfo objects in the executable file.
    strings = locations(image, name.encode("ascii") + b"\0")
    require(len(strings) == 1, "Missing or ambiguous type name: " + name)
    candidates = []
    for address in locations(image, struct.pack("<I", strings[0])):
        try:
            raw = image.read(address - image.base, 28)
            flags, size = struct.unpack_from("<HH", raw, 4)
            count = raw[13]
            table = struct.unpack_from("<I", raw, 24)[0]
            if address % 4 or flags != 0x29 or not 0 < count <= 64 or not 0 < size <= 1024:
                continue
            records = image.read(table - image.base, count * 24)
            fields = []
            for n in range(count):
                text_address, _, _, _, offset, _ = struct.unpack_from("<6I", records, n * 24)
                label = bytearray()
                for k in range(192):
                    ch = image.read(text_address - image.base + k, 1)[0]
                    if ch == 0:
                        break
                    require(32 <= ch < 127, "Invalid metadata field name")
                    label.append(ch)
                else:
                    raise ValueError("Unterminated field name")
                require(label and offset < size, "Field outside reflected type")
                fields.append({"name": label.decode("ascii"), "type_relative_offset": offset})
            require(len({field["name"] for field in fields}) == count, "Duplicate field names")
            candidates.append(dict(name=name, metadata_rva=address-image.base,
                metadata_sha256=sha(raw), table_rva=table-image.base,
                table_sha256=sha(records), size=size, fields=fields))
        except (ValueError, struct.error):
            continue
    require(len(candidates) == 1, "Missing or ambiguous struct metadata: " + name)
    return candidates[0]


def preparation(game):
    image = image_for(game / "BFBC2Game.exe")
    profile_bytes = PROFILE.read_bytes()
    rows = [row for row in json.loads(profile_bytes)["descriptors"]
            if row["configuration"]["assetName"] == ASSET]
    require(len(rows) == 1, "Exact MP443_sp descriptor missing or ambiguous")
    row = rows[0]
    archive_path = Path("Dist/win32/levels/sp_common/level-00.fbrb")
    archive = Archive(game / archive_path)
    origin = next(origin for origin in row["origins"] if origin["archive"] == archive_path.as_posix())
    require(archive.index_sha256 == origin["index_sha256"], "Archive index differs")
    names = sorted({row["identity"][key]["resource"] for key in ("weapon", "firing", "function")})
    contents = archive.read_selected(names)
    for key in ("weapon", "firing", "function"):
        identity = row["identity"][key]
        require(sha(contents[identity["resource"]]) == identity["sha256"], "Authored resource differs: " + key)
    documents = [parse(name, data) for name, data in contents.items()]
    resolver = Resolver(documents)
    document = next(doc for doc in documents if doc.resource == row["identity"]["weapon"]["resource"])
    weapon = document.instances[row["identity"]["weapon"]["guid"]]
    authored = effective_weapon(resolver, document, weapon)
    require(authored["native_name"] == ASSET and not authored["missing_fields"], "Exact authored fields unavailable")
    firing_doc, firing = resolver.resolve(document, named(weapon, "WeaponFiring").attributes.get("ref"), "GameSharedResources.WeaponFiringData")
    _, function = resolver.resolve(firing_doc, named(firing, "PrimaryFire").attributes.get("ref"), "GameSharedResources.FiringFunctionData")
    logic = named(function, "FireLogic")
    extra = {name: scalar(named(logic, name), kind) for name, kind in (
        ("PreFireDelay", "float32"), ("ZoomBeforeFireDelay", "float32"),
        ("WaitForTriggerRelease", "bool"), ("WaitForZoomRelease", "bool"),
        ("HoldReloadUntilFireRelease", "bool"), ("HoldReloadUntilZoomRelease", "bool"))}
    readiness = []
    for index, state in enumerate(named(weapon, "WeaponStates").children):
        item = named(state, "ReadinessStates")
        readiness.append(dict(index=index, fields={child.attributes["name"]: scalar(child, "bool") for child in item.children}))
    metadata = [reflected_metadata(image, name) for name in TYPE_NAMES]
    return dict(schema="chat16.pistol_readiness_preparation.v1", executable_sha256=EXE_SHA,
        reader_sha256={path.name: sha(path.read_bytes()) for path in (
            Path(__file__), ROOT / "tools/capture_reload_state.py",
            ROOT / "tools/capture_reload_server.py", ROOT / "tools/read_bc2.py",
            ROOT / "tools/inspect_bc2_mesh_asset.py", ROOT / "tools/bc2_weapon_config_pipeline.py")},
        descriptor_profile_sha256=sha(profile_bytes), authored_identity=row["identity"],
        archive=str(archive_path), archive_index_sha256=archive.index_sha256,
        authored_fields=authored["fields"], additional_fire_logic=extra,
        weapon_states=authored["weapon_states"], authored_readiness_states=readiness,
        reflected_metadata=metadata, game_process_opened=False,
        chamber_knowledge="Unknown", slide_ready_admitted=False, native_admission_changed=False,
        findings=[
            "ReadinessStates is authored Relaxed/Alert metadata; it is not an observed chamber or slide state.",
            "Zero bolt fields and 17 loaded capacity do not establish a separate chamber.",
            "WaitForTriggerRelease is authored true; repeat-trigger behavior remains native, not a slide completion receipt.",
            "The four inspected reflected structs expose no chamber/slide field; this is not proof that no such runtime state exists elsewhere.",
            "WeaponActionGate already distinguishes occupied tactical feed from empty feed; Slide has no gesture mapping or BC2 chamber binding."],
        next_observation=dict(command="py -3.12 -B pistol_readiness_probe.py observe --exe <exact BFBC2Game.exe> --pid <root-owned PID> --stage <stage> --output <new JSON>",
            stages=list(STAGES), samples_maximum=16, seconds_maximum=3.75,
            purpose="Compare existing two client and server firing observations around separately witnessed feed events.",
            limits="Stage labels do not prove events; external reads are not atomic, firing/empty suppression/chamber/slide acceptance remains unqualified."))


def selected_sample(inspect, binding):
    started = time.perf_counter_ns()
    server = capture(inspect, binding)
    require(server["asset_name"] == ASSET and server["asset_path"] == ASSET_PATH, "Selected weapon is not exact MP443_sp")
    owner = server["client_owner"]
    weapon = inspect.weapon(owner["selected_weapon"], owner["selected_slot"])
    require(weapon["asset_name"] == ASSET and weapon["asset_path"] == ASSET_PATH, "Client exact configuration differs")
    # Read already reflected fields; do not introduce a native offset or state.
    logic, ammo = weapon["fire_logic"], weapon["ammo_config"]
    require(logic["FireLogicType"]["value"] == 0 and logic["ReloadType"]["value"] == 1,
        "Selected firing family differs")
    require(logic["ReloadTime"] == 3.75 and logic["ReloadThreshold"] == .5
        and logic["ReloadDelay"] == logic["PostReloadSequenceTime"] == 0,
        "Selected reload timing differs")
    bolt = logic["BoltAction"]
    require(bolt["BoltActionTime"] == bolt["BoltActionDelay"] == 0
        and not bolt["HoldBoltActionUntilFireRelease"] and not bolt["HoldBoltActionUntilZoomRelease"],
        "Selected bolt configuration differs")
    require(ammo["MagazineCapacity"] == 17 and not ammo["AutoReplenishMagazine"],
        "Selected ammunition configuration differs")
    client = {hex(branch): inspect.state(weapon, branch) for branch in (0x3c, 0x40)}
    require(client["0x3c"]["address"] != client["0x40"]["address"], "Client firing branches alias")
    identity = {key: value for key, value in server.items()
                if key not in ("state", "identity_coherent", "atomic_native_snapshot", "authority_proven")}
    require_same_identity(identity, chain(inspect, binding), "pistol_three_copy_read",
        "Client/server ownership changed around three-copy observation", started, time.perf_counter_ns())
    return dict(observed_ns=started, identity=identity, server=server, client=client,
        chamber_knowledge="Unknown", slide_closed=None, fire_permission=None)


def observe(args):
    image = image_for(args.exe)
    report = dict(schema="chat16.pistol_readiness_observation.v1", executable_sha256=EXE_SHA,
        stage_label=args.stage, stage_is_event_receipt=False, samples=[], rejected=[],
        read_only=True, native_calls=False, process_writes=False, input_or_focus_changes=False,
        atomic_native_snapshot=False, chamber_knowledge="Unknown", native_admitted=False)
    process = Process(args.pid, expected_path=args.exe)
    try:
        inspect = Inspector(process, image)
        binding = discover(image, process)
        first_identity = None
        for sequence in range(args.samples):
            try:
                sample = selected_sample(inspect, binding)
                if first_identity is not None:
                    require_same_identity(first_identity, sample["identity"], "pistol_observation_lifetime",
                        "Selected ownership changed during bounded survey", report["samples"][0]["observed_ns"], sample["observed_ns"])
                first_identity = sample["identity"]
                report["samples"].append(dict(sequence=sequence, **sample))
            except (ValueError, OSError, KeyError) as exc:
                rejection = dict(sequence=sequence, reason=str(exc))
                if hasattr(exc, "identity_read_failure"):
                    rejection["identity_read_failure"] = exc.identity_read_failure
                report["rejected"].append(rejection)
                break  # Keep the first failure; do not replace it with a later pass.
            if sequence + 1 < args.samples:
                time.sleep(args.interval_ms / 1000)
    finally:
        process.close()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="mode", required=True)
    prepare = commands.add_parser("prepare")
    prepare.add_argument("--game", type=Path, required=True)
    live = commands.add_parser("observe")
    live.add_argument("--exe", type=Path, required=True)
    live.add_argument("--pid", type=int, required=True)
    live.add_argument("--stage", choices=STAGES, required=True)
    live.add_argument("--samples", type=int, default=8)
    live.add_argument("--interval-ms", type=int, default=100)
    for command in (prepare, live):
        command.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output exists; preserve earlier evidence")
    if args.mode == "observe" and not (1 <= args.samples <= 16 and 20 <= args.interval_ms <= 250):
        parser.error("Bounded observation: 1..16 samples, interval 20..250 ms")
    result = preparation(args.game) if args.mode == "prepare" else observe(args)
    encoded = json.dumps(result, indent=2, allow_nan=False) + "\n"
    require(len(encoded.encode("utf-8")) <= 256 * 1024, "Report exceeds bound")
    with args.output.open("x", encoding="utf-8") as output:
        output.write(encoded)
    print(json.dumps(dict(output=str(args.output), mode=args.mode, chamber_knowledge="Unknown")))
    return 1 if result.get("rejected") else 0


if __name__ == "__main__":
    raise SystemExit(main())
