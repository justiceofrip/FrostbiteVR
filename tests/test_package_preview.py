import importlib.util
import json
from pathlib import Path
import struct
import shutil
import subprocess
import sys
import uuid
import unittest
from unittest.mock import patch
import zipfile

spec=importlib.util.spec_from_file_location('pack',Path(__file__).resolve().parents[1]/'tools/package_preview.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
BODY_RUNTIME=('tools/bc2_authored_grip_bindings.py', 'tools/bc2_authored_magazine_geometry.py', 'tools/bc2_body_ammo_assets.py', 'tools/bc2_body_equipment_assets.py', 'tools/bc2_granny_curves.py', 'tools/bc2_granny_resource.py', 'tools/bc2_mesh_geometry.py', 'tools/bc2_weapon_animation_pipeline.py', 'tools/bc2_weapon_asset_pipeline.py', 'tools/bc2_weapon_config_pipeline.py', 'tools/bc2_weapon_mesh_bindings.py', 'tools/inspect_bc2_mesh_asset.py', 'tools/Prepare-BodyAmmoAssets.ps1', 'config/body-ammo-assets.json', 'licenses/Norbyte-LSLib-MIT.txt')

def image(machine=0x14c,dependency=None):
    data=bytearray(1024);data[:2]=b'MZ';struct.pack_into('<I',data,60,128);data[128:132]=b'PE\0\0'
    struct.pack_into('<HH',data,132,machine,1);struct.pack_into('<H',data,148,224);struct.pack_into('<H',data,152,0x10b)
    struct.pack_into('<IIII',data,384,512,4096,512,512)
    if dependency:
        struct.pack_into('<II',data,256,4096,40);struct.pack_into('<IIIII',data,512,0,0,0,4160,0)
        name=dependency.encode()+b'\0';data[576:576+len(name)]=name
    return bytes(data)

class Packaging(unittest.TestCase):
    def test_runtime_body_helper_closure_and_private_exclusion(self):
        files=p.collect_source(self.root)
        runtime={n:files[n] for n in p.RUNTIME}
        p.validate_python_helpers(runtime)
        for name in BODY_RUNTIME:self.assertIn(name,runtime)
        for name in ('build/body-ammo-assets.fvrprop','private/NativeFixture.h','config/generated-geometry.json'):
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'private cache')
        files=p.collect_source(self.root)
        runtime={n:files[n] for n in p.RUNTIME}
        self.assertFalse(any(n.endswith('.fvrprop') or n.startswith('private/') or 'Fixture' in n or 'generated-geometry' in n for n in runtime))
        for name,machine in p.BINARIES.items():
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(image(machine))
        destination=self.temp_dir/'body-preview'
        with patch.object(p,'LOADER_SHA256',p.sha(image(0x8664))):p.stage(self.root,destination,'body-preview')
        clean=destination/'preview'
        with zipfile.ZipFile(destination/'body-preview-singleplayer-preview.zip') as archive:
            names=archive.namelist()
            self.assertFalse(any(n.endswith('.fvrprop') or n.startswith('private/') or 'Fixture' in n or 'generated-geometry' in n for n in names))
            for name in BODY_RUNTIME:self.assertIn(name,names)
        code='import sys;sys.path.insert(0,sys.argv[1]);import bc2_body_ammo_assets,bc2_body_equipment_assets'
        result=subprocess.run([sys.executable,'-I','-S','-B','-c',code,str(clean/'tools')],cwd=self.temp_dir,capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_missing_runtime_asset_decoder_is_rejected_before_staging(self):
        source=p.collect_source(self.root)
        runtime={name:source[name] for name in p.RUNTIME}
        runtime.pop('tools/bc2_granny_curves.py')
        with self.assertRaisesRegex(ValueError,'bc2_granny_curves'):
            p.validate_python_helpers(runtime)
        runtime={name:source[name] for name in p.RUNTIME}
        runtime['tools/bc2_granny_curves.py']+=b'\nimport omitted_asset_dependency\n'
        with self.assertRaisesRegex(ValueError,'omitted_asset_dependency'):
            p.validate_python_helpers(runtime)


    def setUp(self):
        self.temp_root=Path(__file__).resolve().parents[1]/'test-temp'
        self.temp_root.mkdir(exist_ok=True)
        self.temp_dir=self.temp_root/('package-'+uuid.uuid4().hex);self.temp_dir.mkdir()
        self.root=self.temp_dir/'repo';self.root.mkdir()
        for name in p.EXACT:
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'public\n')
        # Exercise real distributed helper bytes, not placeholder scripts that
        # could hide an import inside main() or a developer-only installation.
        for name in ('tools/game_window.py','tools/read_bc2.py','tools/bc2_weapon_asset_pipeline.py',
                     'tools/bc2_mesh_geometry.py','tools/inspect_bc2_mesh_asset.py','tools/bc2_weapon_config_pipeline.py','tools/bc2_weapon_mesh_bindings.py','tools/audit_pump_hold.py','tools/inspect_lmg_common.py','tools/build_lmg_jobs.py','tools/capture_lmg_native.py','tools/capture_reload_state.py','tools/capture_reload_server.py',
                     'tools/bc2_granny_resource.py','tools/bc2_granny_curves.py','tools/bc2_weapon_animation_pipeline.py','tools/bc2_authored_mechanism_geometry.py',
                     'tools/build_manual_reload_catalog.py','tools/inspect_reload_animations.py','tools/bc2_authored_grip_bindings.py','tools/bc2_authored_hand_pose_batch.py','tools/bc2_authored_magazine_geometry.py','tools/bc2_magazine_contact_batch.py','tools/bc2_magazine_descriptor_manifest.py','tools/bc2_magazine_registry_header.py',
                     'tools/bc2_authored_sight_geometry.py','tools/bc2_authored_optic_catalog.py',
                     'tools/audit_pump_capture.py','tools/report_empty_step.py','tools/bc2_magazine_pipeline_coverage.py'):
            (self.root/name).write_bytes((Path(__file__).resolve().parents[1]/name).read_bytes())
        for name in BODY_RUNTIME:
            if name.endswith('.py'):
                (self.root/name).write_bytes((Path(__file__).resolve().parents[1]/name).read_bytes())
        for name in ('src/game.cpp','include/game.h','tests/GameTests.cpp','third_party/library/license.txt'):
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'code\n')
    def tearDown(self):
        assert self.temp_dir.resolve().is_relative_to(self.temp_root.resolve())
        shutil.rmtree(self.temp_dir)
    def test_private_and_game_files_excluded(self):
        for name in ('reports/secret.json','config/local.json','src/local.pdb','src/password.json','BFBC2Game.exe','runtime/private.dll','tests/trace.bin','test-temp/native-capture.cpp','.github/private.yml'):
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'private')
        source=p.collect_source(self.root)
        self.assertIn('src/game.cpp',source)
        self.assertFalse(any(b==b'private' for b in source.values()))
        self.assertIn('/test-temp/',source['.gitignore'].decode().splitlines())
    def test_empty_monitor_audit_and_optional_cmake_ship_without_private_fixtures(self):
        for name in ('tools/audit_empty_fire.py', 'tools/report_empty_step.py',
                     'cmake/ManualEmptyRegistryCoverage.cmake'):
            (self.root/name).write_bytes((Path(__file__).resolve().parents[1]/name).read_bytes())
        private=self.root/'private-inputs/empty-registry/FixtureRegistry.h'
        private.parent.mkdir(parents=True);private.write_text('private fixture')
        destination=self.temp_dir/'empty-monitor-source'
        p.stage(self.root,destination,'empty-monitor-test',source_only=True)
        source=destination/'source'
        self.assertTrue((source/'cmake/ManualEmptyRegistryCoverage.cmake').is_file())
        self.assertFalse((source/'private-inputs').exists())
        for name in ('audit_empty_fire.py','report_empty_step.py'):
            self.assertNotIn('tools/'+name,p.RUNTIME)
            completed=subprocess.run([sys.executable,'-E','-S','-B',str(source/'tools'/name),'--help'],
                                     cwd=self.temp_dir,capture_output=True,text=True,timeout=15)
            self.assertEqual(completed.returncode,0,completed.stderr)
    def test_offline_diagnostics_export_without_private_inputs(self):
        for name in ('private/FixtureRegistry.h','private/MeasuredGeometry.h','build/body-ammo-assets.fvrprop'):
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'private fixture data')
        destination=self.temp_dir/'offline-tools'
        p.stage(self.root,destination,'offline-tools',source_only=True)
        exported=destination/'source'
        for name in ('audit_pump_capture.py','report_empty_step.py','bc2_magazine_pipeline_coverage.py'):
            self.assertNotIn('tools/'+name,p.RUNTIME)
            completed=subprocess.run([sys.executable,'-E','-S','-B',str(exported/'tools'/name),'--help'],
                                     cwd=self.temp_dir,capture_output=True,text=True,timeout=15)
            self.assertEqual(completed.returncode,0,completed.stderr)
        self.assertFalse((exported/'private').exists())
        self.assertFalse((exported/'build').exists())
    def test_weapon_pipeline_is_source_only_and_dependencies_are_complete(self):
        source=p.collect_source(self.root)
        expected=('tools/bc2_weapon_asset_pipeline.py','tools/bc2_mesh_geometry.py','tools/inspect_bc2_mesh_asset.py',
                  'profiles/weapon-family-work-plan.json','profiles/runtime-weapon-scope-20261003.json',
                  'tests/test_bc2_weapon_asset_pipeline.py','WEAPON-PIPELINE.md')
        for name in expected:
            self.assertIn(name,source)
            self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        self.assertEqual(source['WEAPON-PIPELINE.md'],source['release/WEAPON-PIPELINE.md'])
        self.assertNotIn('installed-weapon-inventory.json',source)
        self.assertNotIn('weapon-interaction-blueprints.json',source)
    def test_exported_weapon_pipeline_imports_without_developer_path(self):
        destination=self.temp_dir/'pipeline-source'
        p.stage(self.root,destination,'pipeline-test',source_only=True)
        helper=destination/'source/tools/bc2_weapon_asset_pipeline.py'
        completed=subprocess.run([sys.executable,'-E','-S','-B',str(helper),'--help'],cwd=self.temp_dir,
                                 capture_output=True,text=True,timeout=15)
        self.assertEqual(completed.returncode,0,completed.stderr)
        self.assertIn('--config-archive',completed.stdout)
        self.assertIn('--runtime-snapshot',completed.stdout)
        decoder=destination/'source/tools/bc2_weapon_config_pipeline.py'
        completed=subprocess.run([sys.executable,'-E','-S','-B',str(decoder),'--help'],cwd=self.temp_dir,
                                 capture_output=True,text=True,timeout=15)
        self.assertEqual(completed.returncode,0,completed.stderr)
        self.assertIn('--archive',completed.stdout)
        self.assertIn('tools/bc2_weapon_config_pipeline.py',p.RUNTIME)
        mesh_join=destination/'source/tools/bc2_weapon_mesh_bindings.py'
        completed=subprocess.run([sys.executable,'-E','-S','-B',str(mesh_join),'--help'],cwd=self.temp_dir,
                                 capture_output=True,text=True,timeout=15)
        self.assertEqual(completed.returncode,0,completed.stderr)
        self.assertIn('--inventory',completed.stdout)
        self.assertIn('tools/bc2_weapon_mesh_bindings.py',p.RUNTIME)
        audit=destination/'source/tools/audit_pump_hold.py'
        completed=subprocess.run([sys.executable,'-E','-S','-B',str(audit),'--help'],cwd=self.temp_dir,
                                 capture_output=True,text=True,timeout=15)
        self.assertEqual(completed.returncode,0,completed.stderr)
        self.assertIn('--receiver',completed.stdout)
        self.assertNotIn('tools/audit_pump_hold.py',p.RUNTIME)
    def test_lmg_tools_source_only_without_generated_game_metadata(self):
        for name in ('mp-lmg-metadata.json','lmg-binding-jobs.json','reports/lmg-native.json'):
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('private evidence')
        source=p.collect_source(self.root)
        for name in ('tools/inspect_lmg_common.py','tools/build_lmg_jobs.py','tools/capture_lmg_native.py',
                     'tools/capture_reload_state.py','tools/capture_reload_server.py','tests/test_lmg_jobs.py','release/LMG-BINDING-JOBS.md'):
            self.assertIn(name,source);self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        self.assertNotIn('mp-lmg-metadata.json',source);self.assertNotIn('lmg-binding-jobs.json',source)
        self.assertNotIn('reports/lmg-native.json',source)
        observer=source['tools/capture_lmg_native.py'].decode()
        self.assertIn("p.add_argument('--executable',type=Path,required=True)",observer)
        self.assertIn('expected_path=executable',observer)

    def test_manual_reload_catalog_is_portable_and_private_results_stay_out(self):
        expected=('tools/build_manual_reload_catalog.py','tools/inspect_reload_animations.py',
                  'tests/ManualReloadCatalogTests.py','profiles/manual-reload-catalog.schema.json',
                  'release/MANUAL-RELOAD-CATALOG.md')
        for name in ('manual-reload-catalog.json','installed-reload-animation-metadata.json','mp-common-all-metadata.json'):
            (self.root/name).write_text('private extracted metadata')
        source=p.collect_source(self.root)
        for name in expected:
            self.assertIn(name,source);self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        self.assertFalse(any(b'private extracted metadata' in data for data in source.values()))
        destination=self.temp_dir/'reload-catalog-source'
        p.stage(self.root,destination,'reload-catalog-test',source_only=True)
        for tool in ('build_manual_reload_catalog.py','inspect_reload_animations.py','inspect_lmg_common.py'):
            completed=subprocess.run([sys.executable,'-E','-S','-B',str(destination/'source/tools'/tool),'--help'],
                                     cwd=self.temp_dir,capture_output=True,text=True,timeout=15)
            self.assertEqual(completed.returncode,0,completed.stderr)

    def test_authored_animation_reader_is_portable_source_only(self):
        source=p.collect_source(self.root)
        expected=('tools/bc2_granny_resource.py','tools/bc2_granny_curves.py','tools/bc2_weapon_animation_pipeline.py',
                  'tests/test_bc2_weapon_animation.py','release/WEAPON-ANIMATION-PIPELINE.md','licenses/Norbyte-LSLib-MIT.txt')
        for name in expected:
            self.assertIn(name,source);self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        for name in ('installed-replay.json','installed-batch-metadata.json','archive-inspection.json'):
            (self.root/name).write_text('private derived game data')
        self.assertFalse(any(name in p.collect_source(self.root) for name in ('installed-replay.json','installed-batch-metadata.json','archive-inspection.json')))
        destination=self.temp_dir/'animation-source'
        p.stage(self.root,destination,'animation-test',source_only=True)
        completed=subprocess.run([sys.executable,'-E','-S','-B',str(destination/'source/tools/bc2_weapon_animation_pipeline.py'),'--help'],
                                 cwd=self.temp_dir,capture_output=True,text=True,timeout=15)
        self.assertEqual(completed.returncode,0,completed.stderr)
        self.assertIn('--skeleton-resource',completed.stdout);self.assertIn('--game-root',completed.stdout)

    def test_authored_grip_generator_is_source_only_and_portable(self):
        source=p.collect_source(self.root)
        for name in ('tools/bc2_authored_grip_bindings.py','tests/test_bc2_authored_grip_bindings.py','release/AUTHORED-GRIP-BINDINGS.md'):
            self.assertIn(name,source);self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        generated=self.root/'generated';generated.mkdir()
        (generated/'Bc2AuthoredGrips.generated.h').write_text('private derived game data')
        (self.root/'installed-bindings.json').write_text('private derived game data')
        source=p.collect_source(self.root)
        self.assertNotIn('generated/Bc2AuthoredGrips.generated.h',source);self.assertNotIn('installed-bindings.json',source)
        destination=self.temp_dir/'authored-grip-source'
        p.stage(self.root,destination,'authored-grip-test',source_only=True)
        completed=subprocess.run([sys.executable,'-E','-S','-B',str(destination/'source/tools/bc2_authored_grip_bindings.py'),'--help'],
                                 cwd=self.temp_dir,capture_output=True,text=True,timeout=15)
        self.assertEqual(completed.returncode,0,completed.stderr);self.assertIn('--hand-poses',completed.stdout);self.assertIn('--asset',completed.stdout)

    def test_hand_pose_batch_is_portable_without_private_inputs(self):
        source=p.collect_source(self.root)
        for name in ('tools/bc2_authored_hand_pose_batch.py','tests/test_bc2_authored_hand_pose_batch.py'):
            self.assertIn(name,source);self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        (self.root/'authored-hand-pose-batch.json').write_text('private derived data')
        self.assertNotIn('authored-hand-pose-batch.json',p.collect_source(self.root))
        destination=self.temp_dir/'hand-pose-source'
        p.stage(self.root,destination,'hand-pose-test',source_only=True)
        completed=subprocess.run([sys.executable,'-E','-S','-B',str(destination/'source/tools/bc2_authored_hand_pose_batch.py'),'--help'],
                                 cwd=self.temp_dir,capture_output=True,text=True,timeout=15)
        self.assertEqual(completed.returncode,0,completed.stderr)
        self.assertIn('--skeleton-resource',completed.stdout)

    def test_authored_magazine_geometry_is_source_only_and_portable(self):
        expected=('tools/bc2_authored_magazine_geometry.py','tests/test_bc2_authored_magazine_geometry.py','release/AUTHORED-MAGAZINE-GEOMETRY.md')
        for name in expected:
            self.assertIn(name,p.collect_source(self.root));self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        generated=self.root/'generated';generated.mkdir()
        (generated/'Bc2ExperimentalMagazineGeometry.h').write_text('private generated geometry')
        (self.root/'experimental-magazine-geometry.json').write_text('private generated geometry')
        self.assertFalse(any(b'private generated geometry' in data for data in p.collect_source(self.root).values()))
        destination=self.temp_dir/'magazine-geometry-source'
        p.stage(self.root,destination,'magazine-geometry-test',source_only=True)
        completed=subprocess.run([sys.executable,'-E','-S','-B',str(destination/'source/tools/bc2_authored_magazine_geometry.py'),'--help'],
                                 cwd=self.temp_dir,capture_output=True,text=True,timeout=15)
        self.assertEqual(completed.returncode,0,completed.stderr)
        self.assertIn('--grip-bindings',completed.stdout);self.assertIn('--header-asset',completed.stdout)

    def test_pistol_precision_shotgun_metadata_is_source_only(self):
        source=p.collect_source(self.root)
        for name in ('tests/test_bc2_paired_role.py','release/PISTOL-PRECISION-SHOTGUN-MECHANISMS.md','release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json'):
            self.assertIn(name,source);self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        for name in ('mechanisms.json','generated/Bc2PrecisionMagazineCandidates.h','previews/M9-parts.png'):
            target=self.root/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(b'private mechanism geometry')
        self.assertFalse(any(data==b'private mechanism geometry' for data in p.collect_source(self.root).values()))

    def test_authored_lmg_mechanisms_are_source_only(self):
        expected=('tools/bc2_authored_mechanism_geometry.py','tests/test_bc2_authored_mechanism_geometry.py',
                  'release/LMG-AUTHORED-MECHANISMS.md','release/LMG-MECHANISM-COVERAGE.json')
        for name in expected:
            self.assertIn(name,p.collect_source(self.root));self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        (self.root/'mechanisms.json').write_text('private derived poses')
        self.assertNotIn('mechanisms.json',p.collect_source(self.root))
        destination=self.temp_dir/'lmg-mechanism-source'
        p.stage(self.root,destination,'lmg-mechanism-test',source_only=True)
        completed=subprocess.run([sys.executable,'-E','-S','-B',str(destination/'source/tools/bc2_authored_mechanism_geometry.py'),'--help'],
                                 cwd=self.temp_dir,capture_output=True,text=True,timeout=15)
        self.assertEqual(completed.returncode,0,completed.stderr)
        self.assertIn('--metadata',completed.stdout);self.assertIn('--asset',completed.stdout)

    def test_drum_contact_followup_is_source_only(self):
        source=p.collect_source(self.root)
        for name in ('tests/test_bc2_drum_grasp.py','release/DRUM-CONTACT-FOLLOWUP.md'):
            self.assertIn(name,source);self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        for name in ('private-candidates.json','generated/Bc2DrumContactCandidates.h','previews/drum.png'):
            target=self.root/name;target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(b'private drum geometry')
        self.assertFalse(any(data==b'private drum geometry' for data in p.collect_source(self.root).values()))

    def test_magazine_contact_batch_is_source_only(self):
        expected=('tools/bc2_magazine_contact_batch.py','tests/test_bc2_magazine_contact_batch.py','release/MAGAZINE-CONTACT-BATCH.md','release/CONTACT-COVERAGE.json')
        for name in expected:
            self.assertIn(name,p.collect_source(self.root));self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        destination=self.temp_dir/'contact-batch-source'
        p.stage(self.root,destination,'contact-batch-test',source_only=True)
        completed=subprocess.run([sys.executable,'-E','-S','-B',str(destination/'source/tools/bc2_magazine_contact_batch.py'),'--help'],
                                 cwd=self.temp_dir,capture_output=True,text=True,timeout=15)
        self.assertEqual(completed.returncode,0,completed.stderr)
        self.assertIn('--asset',completed.stdout);self.assertIn('--defer',completed.stdout)

    def test_magazine_descriptor_jobs_are_source_only(self):
        expected=('tools/bc2_magazine_descriptor_manifest.py','tests/test_bc2_magazine_descriptor_manifest.py','release/MAGAZINE-DESCRIPTOR-JOBS.md')
        for name in expected:
            self.assertIn(name,p.collect_source(self.root));self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        (self.root/'installed-descriptor-jobs.json').write_text('private descriptor data')
        (self.root/'reviewed-baseline-references.json').write_text('private descriptor data')
        self.assertFalse(any(b'private descriptor data' in data for data in p.collect_source(self.root).values()))
        destination=self.temp_dir/'descriptor-source'
        p.stage(self.root,destination,'descriptor-test',source_only=True)
        completed=subprocess.run([sys.executable,'-E','-S','-B',str(destination/'source/tools/bc2_magazine_descriptor_manifest.py'),'--help'],
                                 cwd=self.temp_dir,capture_output=True,text=True,timeout=15)
        self.assertEqual(completed.returncode,0,completed.stderr)
        self.assertIn('--reviewed-baselines',completed.stdout)

    def test_registry_tool_export_is_source_only(self):
        expected=('tools/bc2_magazine_registry_header.py','tests/test_bc2_magazine_registry_header.py','release/MAGAZINE-REGISTRY.md')
        for name in expected:
            self.assertIn(name,p.collect_source(self.root));self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        destination=self.temp_dir/'registry-source'
        p.stage(self.root,destination,'registry-test',source_only=True)
        completed=subprocess.run([sys.executable,'-E','-S','-B',str(destination/'source/tools/bc2_magazine_registry_header.py'),'--help'],
                                 cwd=self.temp_dir,capture_output=True,text=True,timeout=15)
        self.assertEqual(completed.returncode,0,completed.stderr)
        self.assertIn('--reviewed',completed.stdout)

    def test_sight_and_optic_tools_export_without_private_inputs(self):
        expected=('tools/bc2_authored_sight_geometry.py','tests/test_bc2_authored_sight_geometry.py',
                  'tools/bc2_authored_optic_catalog.py','tests/test_bc2_authored_optic_catalog.py')
        for name in expected:
            self.assertIn(name,p.collect_source(self.root));self.assertEqual(name in p.RUNTIME,name in BODY_RUNTIME)
        for name in ('private/AekSightProfile.h','sp-catalog.json','mp-catalog.json','installed-sight-measurements.json'):
            target=self.root/name;target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(b'private installed optic data')
        self.assertFalse(any(b'private installed optic data' in data for data in p.collect_source(self.root).values()))
        destination=self.temp_dir/'sight-optic-source'
        p.stage(self.root,destination,'sight-optic-test',source_only=True)
        for tool in ('bc2_authored_sight_geometry.py','bc2_authored_optic_catalog.py'):
            completed=subprocess.run([sys.executable,'-E','-S','-B',str(destination/'source/tools'/tool),'--help'],
                                     cwd=self.temp_dir,capture_output=True,text=True,timeout=15)
            self.assertEqual(completed.returncode,0,completed.stderr)
            self.assertIn('--game',completed.stdout)

    def test_same_inputs_identical_archives_and_hashes(self):
        a=self.temp_dir/'a';b=self.temp_dir/'b'
        first=p.stage(self.root,a,'preview.1',source_only=True);second=p.stage(self.root,b,'preview.1',source_only=True)
        self.assertEqual(first,second);self.assertEqual((a/'preview.1-source.zip').read_bytes(),(b/'preview.1-source.zip').read_bytes())
        loaded=json.loads((a/'source/source-manifest.json').read_text());self.assertEqual([x['path']for x in loaded['files']],sorted(x['path']for x in loaded['files']))
    def test_refuse_existing_output_and_unsafe_version(self):
        with self.assertRaises(ValueError):p.stage(self.root,self.root,'preview',source_only=True)
        with self.assertRaises(ValueError):p.stage(self.root,self.temp_dir/'out','../escape',source_only=True)
        for name in ('../secret','C:/secret','a\\b'):
            with self.assertRaises(ValueError):p.safe_relative(name)
    def test_overlay_only_selected_paths(self):
        overlay=self.temp_dir/'overlay';(overlay/'src').mkdir(parents=True)
        (overlay/'src/game.cpp').write_bytes(b'updated');(overlay/'local.json').write_bytes(b'secret')
        source=p.collect_source(self.root,overlay);self.assertEqual(source['src/game.cpp'],b'updated');self.assertNotIn('local.json',source)
    def test_missing_prerequisite_rejected(self):
        (self.root/'LICENSE').unlink()
        with self.assertRaises(FileNotFoundError):p.collect_source(self.root)
    def test_pe_architecture_imports_and_bad_image(self):
        self.assertEqual(p.pe_info(image())['machine'],'0x14c')
        self.assertEqual(p.pe_info(image(0x8664,'KERNEL32.dll'))['imports'],['kernel32.dll'])
        with self.assertRaises(ValueError):p.pe_info(image(dependency='private_runtime.dll'))
        with self.assertRaises(ValueError):p.pe_info(b'not a PE')
    def test_attestation_ties_exact_source_and_binary_bytes(self):
        binaries={'one.exe':b'one'};source=b'source'
        e={'schema':1,'source_manifest_sha256':p.sha(source),'binary_sha256':{'one.exe':p.sha(b'one')},'test_counts':{'x86':1,'x64':1}}
        self.assertTrue(p.verify_build(e,source,binaries));self.assertFalse(p.verify_build(None,source,binaries))
        for change in ('source','binary','suite'):
            bad=json.loads(json.dumps(e))
            if change=='source':bad['source_manifest_sha256']='wrong'
            if change=='binary':bad['binary_sha256']['one.exe']='wrong'
            if change=='suite':bad['test_counts'].pop('x86')
            with self.assertRaises(ValueError):p.verify_build(bad,source,binaries)
    def test_source_roundtrip_keeps_manifest_identity(self):
        first=self.temp_dir/'first';second=self.temp_dir/'second'
        a=p.stage(self.root,first,'preview',source_only=True);b=p.stage(first/'source',second,'preview',source_only=True)
        self.assertEqual(a['source_manifest_sha256'],b['source_manifest_sha256'])
    def test_full_package_excludes_fake_runtime_and_checks_architecture(self):
        for name,machine in p.BINARIES.items():
            target=self.root/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(image(machine))
        (self.root/'build/x64/FvrTestOpenXr.dll').write_bytes(b'never distribute')
        saved=p.LOADER_SHA256;p.LOADER_SHA256=p.sha(image(0x8664))
        try:
            output=self.temp_dir/'full';result=p.stage(self.root,output,'preview')
            self.assertFalse(result['build_verified']);self.assertFalse((output/'preview/build/x64/FvrTestOpenXr.dll').exists())
            self.assertEqual(len(json.loads((output/'preview/binary-manifest.json').read_text())['files']),4)
            with zipfile.ZipFile(output/'preview-source.zip') as source_zip:
                self.assertEqual(source_zip.read('ARCHITECTURE.md'),source_zip.read('release/ARCHITECTURE.md'))
                for name in ('HEADSET-TEST-CARD.md','PRERELEASE-DRAFT.md'):
                    self.assertEqual(source_zip.read(name),source_zip.read('release/'+name))
                self.assertIn(b'/test-temp/',source_zip.read('.gitignore'))
                for name in ('.github/workflows/build.yml','.github/ISSUE_TEMPLATE/bug_report.yml'):
                    self.assertEqual(source_zip.read(name),(self.root/name).read_bytes())
            with zipfile.ZipFile(output/'preview-singleplayer-preview.zip') as runtime_zip:
                self.assertEqual(runtime_zip.read('HEADSET-TEST-CARD.md'),(self.root/'release/HEADSET-TEST-CARD.md').read_bytes())
                self.assertNotIn('PRERELEASE-DRAFT.md',runtime_zip.namelist())
                self.assertNotIn('release/PRERELEASE-DRAFT.md',runtime_zip.namelist())
                self.assertNotIn('ARCHITECTURE.md',runtime_zip.namelist())
                self.assertNotIn('release/ARCHITECTURE.md',runtime_zip.namelist())
                self.assertNotIn('.gitignore',runtime_zip.namelist())
                self.assertFalse(any(name.startswith('.github/') for name in runtime_zip.namelist()))
            self.assertTrue(result['helper_preflight']['helper_ready'])
            self.assertFalse(result['helper_preflight']['process_opened'])
            extracted=self.temp_dir/'extracted preview with spaces'
            with zipfile.ZipFile(output/'preview-singleplayer-preview.zip') as zipped:zipped.extractall(extracted)
            report=p.preflight_python_helpers(extracted);self.assertTrue(report['helper_ready'])
            # Reproduce the original missing transitive import in an actual ZIP
            # extraction. No global Python environment may satisfy this import.
            (extracted/'tools/read_bc2.py').unlink()
            with self.assertRaisesRegex(ValueError,'preflight failed'):p.preflight_python_helpers(extracted)
            with self.assertRaises(ValueError):p.stage(self.root,self.temp_dir/'strict','preview',require_verified=True)
            (self.root/'build/x86/BC2NativeProbe.dll').write_bytes(image(0x8664))
            with self.assertRaises(ValueError):p.stage(self.root,self.temp_dir/'wrong','preview')
        finally:p.LOADER_SHA256=saved

    def test_dependency_closure_finds_import_inside_main(self):
        files={'tools/game_window.py':b'def main():\n    from read_bc2 import Process\n'}
        with self.assertRaisesRegex(ValueError,'read_bc2'):p.validate_python_helpers(files)
        files['tools/read_bc2.py']=b'import ctypes\nimport json\n'
        p.validate_python_helpers(files)
        files['tools/read_bc2.py']=b'import another_missing_local_module\n'
        with self.assertRaisesRegex(ValueError,'another_missing_local_module'):p.validate_python_helpers(files)

    def test_packaged_preflight_resolves_its_own_config_from_other_cwd(self):
        output=self.temp_dir/'portable package';p.stage(self.root,output,'preview',source_only=True)
        tree=output/'source';game=self.temp_dir/'different drive style installation with spaces'
        config=tree/'config/local.json';config.write_text(json.dumps({'game_path':str(game)}),encoding='utf-8-sig')
        command=[sys.executable,'-E','-S',str(tree/'tools/game_window.py'),'--preflight']
        run=subprocess.run(command,cwd=self.temp_dir,capture_output=True,text=True,check=True)
        report=json.loads(run.stdout);self.assertEqual(report['expected_executable'],str(game/'BFBC2Game.exe'))
        self.assertTrue(report['game_configured']);self.assertFalse(report['process_opened'])
        config.write_text(json.dumps({'game_path':'relative/game'}))
        with self.assertRaisesRegex(ValueError,'absolute installation'):p.preflight_python_helpers(tree)

    def test_preflight_rejects_window_operations_before_process_access(self):
        output=self.temp_dir/'isolated';p.stage(self.root,output,'preview',source_only=True)
        helper=output/'source/tools/game_window.py'
        for extra in (['--pid','123'],['--activate'],['--left-monitor'],['--capture','ignored.png']):
            result=subprocess.run([sys.executable,'-E','-S',str(helper),'--preflight']+extra,capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0);self.assertIn('must be used alone',result.stderr)

    def test_explicit_report_root_keeps_custom_session_containment(self):
        spec=importlib.util.spec_from_file_location('window_helper',self.root/'tools/game_window.py')
        helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
        custom=self.temp_dir/'custom session';custom.mkdir()
        self.assertEqual(helper.report_path(custom/'window-startup.json',custom),custom/'window-startup.json')
        for escaped in (custom,custom/'..'/'outside.json',self.temp_dir/'custom session other'/'window.json'):
            with self.assertRaises(ValueError):helper.report_path(escaped,custom)
        with self.assertRaises(ValueError):helper.report_path(custom/'window.json')

    def test_wrong_process_identity_closes_read_only_handle(self):
        spec=importlib.util.spec_from_file_location('packaged_reader',self.root/'tools/read_bc2.py')
        reader=importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)
        expected=str(self.temp_dir/'game'/'BFBC2Game.exe')
        class FakeKernel:
            closed=[];opened=[]
            def OpenProcess(self,mask,inherit,pid):self.opened.append((mask,inherit,pid));return 123
            def QueryFullProcessImageNameW(self,handle,flags,name,size):name.value=str(self_root/'wrong'/'BFBC2Game.exe');return 1
            def CloseHandle(self,handle):self.closed.append(handle)
        self_root=self.temp_dir;fake=FakeKernel()
        with patch.object(reader,'k',fake):
            with self.assertRaisesRegex(RuntimeError,'Wrong game process'):reader.Process(999,expected_path=expected)
        self.assertEqual(fake.opened,[(0x410,False,999)]);self.assertEqual(fake.closed,[123])
        self.assertTrue(reader.same_executable(expected.upper(),expected));self.assertFalse(reader.same_executable(expected+'x',expected))

if __name__=='__main__':unittest.main()
