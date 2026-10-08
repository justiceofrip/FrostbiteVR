import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest
import hashlib
import json
from types import SimpleNamespace
from contextlib import redirect_stderr
import io
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))

SPEC=importlib.util.spec_from_file_location("capability_report",Path(__file__).resolve().parents[1]/"tools"/"bc2_weapon_capability_report.py")
m=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(m)

def index():
    out={"schema":"fvr.bc2.weapon-package-index.prototype.v1","native_admission_granted":False,
      "rows":[{"package_id":"a"*64,"asset":"AEK971_sp","identity":{"configuration_sha256":"b"*64,"instance_guid":"C"*32,
       "meshes":[["objects/aek_mesh/GUID","d"*64,"e"*64]],"rig":"a7f219a1426216ab","configuration_resource":"objects/aek.dbx"},
       "components":{"visibility":{"data_ready":True,"native_admitted":False}},"missing_components":["feed_geometry"],"runtime_enabled":False}],"excluded":[]}
    rehash(out);return out

def rehash(i):
    for p in i["rows"]:p["package_id"]=hashlib.sha256(json.dumps(p["identity"],sort_keys=True,separators=(",",":")).encode()).hexdigest()

def receipt():
    return {"schema":"fvr.bc2.compiled-capabilities.v1","runtime_enabled":False,
      "native_registrations":[{"id":1,"asset":"AEK971_sp","asset_path":"Objects/AEK","enabled":True,"resolved":True,"reviewed":True,"magazine_ready":True}],
      "configured_visibility":[{"asset":"AEK971_sp","meshes":["Objects/AEK_Mesh"],"rig_fingerprint":"a7f219a1426216ab","native_admitted":False,"production_stow_admitted":False}],
      "mechanisms":{"chamber_known":False,"manual_cycle_integration":"unknown"}}

class Tests(unittest.TestCase):
    def test_aek_magazine_does_not_imply_stow(self):
        r=m.report(index(),receipt())["rows"][0]
        self.assertEqual(r["compiled_magazine_prerequisites"]["status"],"prerequisites_present")
        self.assertEqual(r["production_stow"]["status"],"unavailable")
        self.assertFalse(r["fully_playable_manual"]);self.assertFalse(r["runtime_enabled"])
    def test_missing_probe_is_unknown(self):
        r=m.report(index())["rows"][0]
        self.assertEqual(r["compiled_magazine_prerequisites"]["status"],"unknown")
    def test_disabled_and_missing_geometry_never_ready(self):
        for field in ("enabled","resolved","reviewed","magazine_ready"):
            with self.subTest(field=field):
                r=receipt();r["native_registrations"][0][field]=False
                self.assertEqual(m.report(index(),r)["rows"][0]["compiled_magazine_prerequisites"]["status"],"unavailable")
    def test_exact_attachment_variant_required(self):
        i=index();i["rows"][0]["identity"]["meshes"].append(["objects/scope/GUID","f"*64,"1"*64])
        rehash(i)
        self.assertEqual(m.report(i,receipt())["rows"][0]["production_stow"]["status"],"unknown")
    def test_unknown_chamber_and_action_not_inferred(self):
        r=m.report(index(),receipt())["rows"][0]
        self.assertEqual(r["chamber_fire"]["status"],"unavailable")
        self.assertEqual(r["manual_action_cycle"]["status"],"unknown")
        self.assertEqual(r["live_feed_commit"]["status"],"unknown")
    def test_generated_geometry_does_not_enroll_native(self):
        r=receipt();r["native_registrations"]=[]
        self.assertEqual(m.report(index(),r)["rows"][0]["compiled_magazine_prerequisites"]["status"],"unavailable")
    def test_duplicate_packages_and_palette_rejected(self):
        i=index();i["rows"].append(copy.deepcopy(i["rows"][0]))
        with self.assertRaises(ValueError):m.report(i,receipt())
        r=receipt();r["configured_visibility"]*=2
        with self.assertRaises(ValueError):m.report(index(),r)
    def test_data_admission_claim_rejected(self):
        i=index();i["native_admission_granted"]=True
        with self.assertRaises(ValueError):m.report(i,receipt())
    def test_cmake_absent_missing_and_unknown(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]) as d:
            p=Path(d)/"CMakeCache.txt";p.write_text("BC2_EXPERIMENTAL_MAGAZINE_HEADER:FILEPATH=\nBC2_AUTHORED_GRIP_HEADER:FILEPATH="+str(Path(d)/"missing.h")+"\n")
            h=m.cache_receipt(p)["headers"]
            self.assertEqual(h["BC2_EXPERIMENTAL_MAGAZINE_HEADER"]["status"],"absent")
            self.assertEqual(h["BC2_AUTHORED_GRIP_HEADER"]["status"],"missing")
            self.assertEqual(h["BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER"]["status"],"unknown")
    def test_cache_hashes_actual_header(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]) as d:
            h=Path(d)/"data.h";h.write_text("immutable metadata")
            p=Path(d)/"CMakeCache.txt";p.write_text("BC2_EXPERIMENTAL_MAGAZINE_HEADER:FILEPATH="+str(h)+"\n")
            r=m.cache_receipt(p)["headers"]["BC2_EXPERIMENTAL_MAGAZINE_HEADER"]
            self.assertEqual(r["sha256"],m.digest(h));self.assertEqual(r["status"],"configured")
    def test_report_scope_not_full_roster(self):
        r=m.report(index(),receipt());self.assertTrue(r["not_full_weapon_roster"])
        self.assertEqual(r["production_binary_match"],"unknown")
        self.assertEqual(r["rows"][0]["comfort"]["status"],"untested")
    def test_same_label_other_configuration_cannot_borrow(self):
        i=index();i["rows"][0]["identity"]["configuration_resource"]="objects/aek_other.dbx";rehash(i)
        self.assertEqual(m.report(i,receipt())["rows"][0]["compiled_magazine_prerequisites"]["status"],"unavailable")
    def test_forged_identity_or_component_permission_rejected(self):
        i=index();i["rows"][0]["package_id"]="a"*64
        with self.assertRaises(ValueError):m.report(i,receipt())
    def test_malformed_compiled_flag_and_digest_rejected(self):
        r=receipt();r["configured_visibility"][0]["production_stow_admitted"]="false"
        with self.assertRaises(ValueError):m.report(index(),r)
        i=index();i["rows"][0]["identity"]["configuration_sha256"]="not-a-digest";rehash(i)
        with self.assertRaises(ValueError):m.report(i,receipt())
        i=index();i["rows"][0]["components"]["visibility"]["native_admitted"]=True
        with self.assertRaises(ValueError):m.report(i,receipt())

    def test_exact_duplicate_native_configuration_rejected(self):
        r=receipt();r["native_registrations"]*=2
        with self.assertRaises(ValueError):m.report(index(),r)
    def test_outputs_cannot_overwrite_inputs_or_probe(self):
        p=Path("input.json")
        for target in (p,m.PROBE_PATH):
            args=SimpleNamespace(package_index=p,registry_receipt=None,cmake_cache=None,emit_probe=None,out=target)
            with self.assertRaises(ValueError):m.validate_outputs(args)
        args=SimpleNamespace(package_index=None,registry_receipt=None,cmake_cache=None,emit_probe=m.PROBE_PATH,out=None)
        with self.assertRaises(ValueError):m.validate_outputs(args)
    def test_emit_mode_rejects_ignored_report_arguments(self):
        with redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as e:m.main(["--emit-probe","copy.cpp","--registry-receipt","receipt.json"])
        self.assertEqual(e.exception.code,2)
    def test_cli_malformed_utf8_reports_error(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]) as d:
            p=Path(d)/"input.json";p.write_bytes(b"\xff")
            with redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as e:m.main(["--package-index",str(p),"--out",str(Path(d)/"out.json")])
            self.assertEqual(e.exception.code,2)
    def test_wrong_rig_cannot_borrow_stow_and_false_component_missing(self):
        i=index();i["rows"][0]["identity"]["rig"]="0000000000000001";rehash(i)
        self.assertEqual(m.report(i,receipt())["rows"][0]["production_stow"]["status"],"unknown")
        i=index();i["rows"][0]["components"]["visibility"]["data_ready"]=False
        i["rows"][0]["missing_components"]=[]
        self.assertIn("visibility",m.report(i,receipt())["rows"][0]["missing_data_components"])

    def test_configuration_binding_is_explicitly_synthetic(self):
        r=receipt();row=r['configured_visibility'][0]
        row.update(configuration_path='Objects/AEK',synthetic_snapshot_path_verified=True,native_configuration_capture_tested=False,hide_show_verified=False,input_suppression_verified=False)
        out=m.report(index(),r)['rows'][0]
        self.assertEqual(out['configuration_path_binding']['status'],'synthetic_binding_present')
        self.assertFalse(out['native_configuration_capture_tested']);self.assertFalse(out['fully_playable_manual'])
    def test_wrong_configuration_cannot_borrow_stow(self):
        r=receipt();row=r['configured_visibility'][0]
        row.update(configuration_path='Objects/OTHER',synthetic_snapshot_path_verified=True,native_configuration_capture_tested=False,hide_show_verified=True,input_suppression_verified=True,production_stow_admitted=True)
        self.assertEqual(m.report(index(),r)['rows'][0]['production_stow']['status'],'unknown')
    def test_compiled_receipt_cannot_claim_live_configuration(self):
        r=receipt();r['configured_visibility'][0].update(configuration_path='Objects/AEK',synthetic_snapshot_path_verified=True,native_configuration_capture_tested=True,hide_show_verified=False,input_suppression_verified=False)
        with self.assertRaises(ValueError):m.report(index(),r)

if __name__=="__main__":unittest.main()
