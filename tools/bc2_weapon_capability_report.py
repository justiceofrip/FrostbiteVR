"""Read-only scoped capability report from compiled registries and immutable packages.

No C++ parsing, inferred native permissions, process access or registry enrollment.
The optional probe source must be compiled with the audited build's private inputs.
Its receipt describes that probe, not an independently verified production binary.
"""
import argparse
import hashlib
import json
from pathlib import Path
from bc2_weapon_package_index import COMPONENTS

PROBE_PATH = Path(__file__).resolve().parents[1]/"src"/"games"/"bc2"/"Bc2WeaponCapabilityProbe.cpp"


HEADER_OPTIONS = ("BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER", "BC2_EXPERIMENTAL_MAGAZINE_HEADER",
                  "BC2_AUTHORED_GRIP_HEADER", "BC2_AUTHORED_SUPPORT_HEADER", "BC2_DRAW_CATALOG_HEADER")

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def cache_receipt(path):
    """Parse CMake's documented key:type=value records; never C++ source."""
    values={}
    for line in Path(path).read_text(encoding="utf-8-sig").splitlines():
        if not line or line.startswith(("#","//")) or "=" not in line or ":" not in line.split("=",1)[0]:
            continue
        key_type,value=line.split("=",1);key,_=key_type.split(":",1)
        if key in values:raise ValueError("duplicate CMake key: "+key)
        values[key]=value
    rows={}
    for key in HEADER_OPTIONS:
        value=values.get(key)
        if value is None:rows[key]={"status":"unknown","reason":"option_not_recorded"}
        elif not value:rows[key]={"status":"absent","reason":"header_not_configured"}
        else:
            p=Path(value)
            rows[key]={"status":"configured" if p.is_file() else "missing","path":value}
            if p.is_file():rows[key]["sha256"]=digest(p)
    return {"path":str(path),"sha256":digest(path),"headers":rows,
            "accepted_grip_baselines":values.get("BC2_AUTHORED_GRIP_ACCEPTED_BASELINES","unknown"),
            "binary_input_match":"unknown_without_binary_build_receipt"}

def status(value,reason):return {"status":value,"reason":reason}

def hexword(value,size):
    return isinstance(value,str) and len(value)==size and all(c in "0123456789abcdef" for c in value)

def report(index,receipt=None,cache=None):
    if index.get("schema")!="fvr.bc2.weapon-package-index.prototype.v1":raise ValueError("package schema")
    if index.get("native_admission_granted") is not False:raise ValueError("package must not grant admission")
    if receipt is not None and (receipt.get("schema")!="fvr.bc2.compiled-capabilities.v1" or receipt.get("runtime_enabled") is not False):
        raise ValueError("compiled receipt schema")
    if len(index.get("rows",[]))>256:raise ValueError("package count bound")
    if receipt is not None:
        if len(receipt.get("native_registrations",[]))>258 or len(receipt.get("configured_visibility",[]))>256:raise ValueError("compiled receipt count bound")
        for r in receipt.get("native_registrations",[]):
            if any(type(r.get(k)) is not bool for k in ("enabled","resolved","reviewed","magazine_ready")):raise ValueError("compiled registry flags")
        for r in receipt.get("configured_visibility",[]):
            if any(type(r.get(k)) is not bool for k in ("native_admitted","production_stow_admitted")):raise ValueError("compiled visibility flags")
            if "configuration_path" in r:
                if not isinstance(r["configuration_path"],str) or type(r.get("synthetic_snapshot_path_verified")) is not bool or r.get("native_configuration_capture_tested") is not False:
                    raise ValueError("compiled configuration capture is synthetic only")
                if any(type(r.get(k)) is not bool for k in ("hide_show_verified","input_suppression_verified")):raise ValueError("compiled proof flags")
    seen=set();rows=[]
    for p in index.get("rows",[]):
        pid=p["package_id"]
        if len(pid)!=64 or any(c not in "0123456789abcdef" for c in pid) or pid in seen:raise ValueError("package identity")
        seen.add(pid);identity=p["identity"];meshrefs=identity["meshes"]
        if not hexword(identity.get("configuration_sha256"),64) or not hexword(identity.get("rig"),16):raise ValueError("configuration digest/rig")
        guid=identity.get("instance_guid","")
        if not isinstance(guid,str) or len(guid)!=32 or any(c not in "0123456789ABCDEF" for c in guid):raise ValueError("configuration GUID")
        if not 1<=len(meshrefs)<=8 or any(len(x)!=3 or not isinstance(x[0],str) or not x[0] or not hexword(x[1],64) or not hexword(x[2],64) for x in meshrefs):raise ValueError("configured mesh evidence")
        actual=hashlib.sha256(json.dumps(identity,sort_keys=True,separators=(",",":")).encode()).hexdigest()
        if actual!=pid or p.get("runtime_enabled") is not False:raise ValueError("package digest/admission mismatch")
        # Package index includes exact mesh GUID and evidence digests. Runtime
        # configured palette uses paths, so this join proves only a mesh-set edge.
        paths=sorted(ref.rsplit("/",1)[0].lower() for ref,_,_ in meshrefs)
        if len(paths)!=len(set(paths)):raise ValueError("ambiguous configured mesh set")
        native=[r for r in (receipt or {}).get("native_registrations",[]) if r["asset"]==p["asset"]]
        resource=identity.get("configuration_resource","")
        exact=[r for r in native if resource.endswith(".dbx") and r["asset_path"].lower()==resource[:-4].lower()]
        configured=[r for r in (receipt or {}).get("configured_visibility",[]) if r["asset"]==p["asset"] and r.get("rig_fingerprint")==identity["rig"] and sorted(x.lower() for x in r["meshes"])==paths]
        if len(configured)>1:raise ValueError("ambiguous compiled visibility")
        mag=status("unknown","no_compiled_registry_receipt")
        if receipt is not None:
            if not exact:mag=status("unavailable" if resource else "unknown","no_exact_configuration_registration" if resource else "configuration_resource_missing")
            elif len(exact)>1:raise ValueError("ambiguous exact configuration registration")
            elif all(exact[0].get(k) is True for k in ("enabled","resolved","reviewed","magazine_ready")):
                mag=status("prerequisites_present","compiled_exact_path_has_reviewed_native_and_geometry_join;live_config_values_not_proven")
            else:mag=status("unavailable","disabled_unreviewed_or_geometry_missing;see_registration_rows")
        stow=status("unknown","no_matching_compiled_configured_palette")
        if configured:stow=status("adapter_profile_admitted" if configured[0]["production_stow_admitted"] else "unavailable",
                                 "compiled_exact_mesh_profile_gate;current_owner_receipts_not_tested" if configured[0]["production_stow_admitted"] else "production_stow_profile_not_admitted")
        path_binding=status("unknown","compiled_configuration_path_not_recorded")
        if configured and "configuration_path" in configured[0]:
            row=configured[0]
            matches=row["configuration_path"].lower()==resource[:-4].lower() and resource.endswith(".dbx")
            if not row["configuration_path"]:path_binding=status("unavailable","descriptor_configuration_backlink_absent")
            elif not matches:path_binding=status("unavailable","compiled_descriptor_configuration_path_mismatch")
            elif row["synthetic_snapshot_path_verified"]:path_binding=status("synthetic_binding_present","compiled_descriptor_to_synthetic_snapshot_only;native_capture_not_tested")
            else:path_binding=status("unavailable","synthetic_snapshot_path_not_bound")
            if not matches:stow=status("unknown","exact_configuration_path_not_bound_to_compiled_profile")
        components=p.get("components",{})
        if any(type(c.get("data_ready")) is not bool or c.get("native_admitted") is not False for c in components.values()):raise ValueError("component data is not native admission")
        rows.append({"package_id":pid,"asset":p["asset"],"identity":identity,
          "data_components":components,"missing_data_components":[k for k in COMPONENTS if k not in components or components[k]["data_ready"] is not True],
          "compiled_magazine_prerequisites":mag,"matching_configuration_registrations":exact,
          "same_label_registration_diagnostics":native,
          "production_stow":stow,"configuration_path_binding":path_binding,"native_configuration_capture_tested":False,"live_feed_commit":status("unknown","no_current_native_transaction_receipt"),
          "manual_action_cycle":status("unknown","data_or_policy_does_not_prove_production_cycle_consumer"),
          "chamber_fire":status("unavailable" if (receipt or {}).get("mechanisms",{}).get("chamber_known") is False else "unknown","native_chamber_observation_not_admitted"),
          "body_display":status("unknown","catalog_and_local_cache_not_bound_to_package"),
          "comfort":status("untested","no_package_scoped_headset_receipt"),
          "fully_playable_manual":False,"runtime_enabled":False})
    return {"schema":"fvr.bc2.weapon-capability-report.v1","scope":"supplied_immutable_packages_and_compiled_probe_only",
      "not_full_weapon_roster":True,"production_binary_match":"unknown","runtime_enabled":False,
      "cache_receipt":cache,"rows":rows,"excluded":index.get("excluded",[]),
      "limits":["Compiled probe is not a native live test or production binary attestation.",
                "Package data never enrolls a native adapter.","Compiled exact resource prerequisites are not live configuration/timing admission.",
                "No inferred pump, bolt, slide, chamber, holster or display completion."]}

def validate_outputs(args):
    inputs=[PROBE_PATH]
    inputs.extend(p for p in (args.package_index,args.registry_receipt,args.cmake_cache) if p)
    target=args.emit_probe or args.out
    if target and target.resolve() in {p.resolve() for p in inputs}:raise ValueError("output would overwrite input or canonical probe source")

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit-probe",type=Path)
    parser.add_argument("--package-index",type=Path)
    parser.add_argument("--registry-receipt",type=Path)
    parser.add_argument("--cmake-cache",type=Path)
    parser.add_argument("--out",type=Path)
    args=parser.parse_args(argv)
    if args.emit_probe:
        if any((args.package_index,args.registry_receipt,args.cmake_cache,args.out)):parser.error("--emit-probe cannot be combined with report inputs/options")
        try:
            validate_outputs(args);args.emit_probe.write_bytes(PROBE_PATH.read_bytes())
        except (OSError,ValueError) as e:parser.error(str(e))
        return
    if not args.package_index or not args.out:parser.error("--package-index and --out required")
    try:
        validate_outputs(args)
        index=json.loads(args.package_index.read_text(encoding="utf-8-sig"))
        receipt=json.loads(args.registry_receipt.read_text(encoding="utf-8-sig")) if args.registry_receipt else None
        result=report(index,receipt,cache_receipt(args.cmake_cache) if args.cmake_cache else None)
        result["inputs"]={"package_index":{"path":str(args.package_index),"sha256":digest(args.package_index)}}
        if args.registry_receipt:result["inputs"]["registry_receipt"]={"path":str(args.registry_receipt),"sha256":digest(args.registry_receipt)}
        args.out.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    except (OSError,ValueError,KeyError,TypeError,UnicodeError) as e:parser.error(str(e))
    print(json.dumps({"rows":len(result["rows"]),"runtime_enabled":False,"scope":result["scope"]}))

if __name__=="__main__":main()
