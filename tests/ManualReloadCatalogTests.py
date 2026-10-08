import copy
import importlib.util
import json
from pathlib import Path
import shutil
import uuid
import unittest
from unittest.mock import patch
import sys
from types import SimpleNamespace

TOOL = Path(__file__).resolve().parents[1] / 'tools' / 'build_manual_reload_catalog.py'
spec = importlib.util.spec_from_file_location('catalog', TOOL)
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
sys.path.insert(0, str(TOOL.parent))
import inspect_reload_animations as animations
import inspect_lmg_common as common


def fixture():
    fields = {name: dict(value=v, encoding='fixture', offset=i) for i, (name, v) in enumerate([
        ('FireLogic.ReloadType', 'rtMagazine'), ('FireLogic.FireLogicType', 'fltAutomaticFire'),
        ('FireLogic.ReloadTime', 3.2), ('Ammo.MagazineCapacity', 30)])}
    config = dict(schema='fvr.bc2.authored_weapon_configuration', schema_version=1, resolved_weapons=[dict(
        native_name='rifle', resource='weapon/rifle.dbx', resource_sha256='a'*64, instance_guid='guid1',
        weapon_class='wcAssault', fields=fields, weapon_states=[])])
    variants = [dict(sha256='b'*64, lods=[dict(lod=0, data_resource='mesh/rifle_lod0.res', data_sha256='c'*64,
        candidate_parts=[dict(bone_hash='12345678', candidate_name='jntWpn_3', sections=['magazine'],
                             minimum=[0, 0, 0], maximum=[1, 1, 1], raw_vertices=['must not export'])])])]
    inventory = dict(schema='fvr.bc2.installed_weapon_inventory', schema_version=1,
        asset_profiles=[dict(resource='mesh/rifle.res', geometry_variants=variants,
                             sources=[dict(archive='weapons/rifle.fbrb', index_sha256='d'*64)])],
        archives=[dict(archive='weapons/rifle.fbrb', index_sha256='d'*64, animations=[
            dict(name='animations/1p_Reload.res', kind='GrannyAnimation', bytes=123),
            dict(name='animations/1p_ChargingHandle.res', kind='GrannyAnimation', bytes=456),
            dict(name='animations/1p_Idle.res', kind='GrannyAnimation', bytes=789)])])
    bindings = dict(schema='fvr.bc2.authored_weapon_mesh_bindings', schema_version=1,
        inventory_sha256='e'*64, weapons=[dict(resource='weapon/rifle.dbx', resource_sha256='a'*64,
            instance_guid='guid1', states=[dict(index=0, meshes=[dict(status='authored_mesh_resolved',
            reference='mesh-guid', mesh_resource='mesh/rifle.res', geometry_status='candidate_inventory_match',
            geometry_variants=[dict(mesh_sha256='b'*64)])])])])
    plan = dict(schema='fvr.weapon_family_work_plan', schema_version=1, items=[dict(
        label='Fixture rifle', exact_mesh_names=['rifle.res'], family_proposal='detachable_magazine', action_variant='self_loading')])
    return dict(config=config, bindings=bindings, inventory=inventory, plan=plan)


class CatalogTests(unittest.TestCase):
    def test_hand_pose_requires_same_resource_hash_and_exact_source(self):
        f = fixture()
        clip = dict(archive='weapons/rifle.fbrb', archive_index_sha256='d'*64,
                    resource='animations/1p_Reload.res', resource_sha256='1'*64, status='decoded', tracks=[])
        batch = dict(schema='fvr.bc2.authored_animation_batch', schema_version=1, clips=[clip])
        pose = dict(schema='fvr.bc2.authored_hand_pose_batch.v1', clips=[dict(resource=clip['resource'],sha256='1'*64,
                    sources=[dict(archive=clip['archive'], index_sha256='d'*64)], status='authored_hands_decoded',
                    hands=dict(evaluation_status='static_authored_pose',all_required_controls_constant=True))])
        def occurrence():
            r = c.catalog(**f, animation_batches=[batch],hand_poses=pose)
            return next(a for a in r['meshes'][0]['animation_candidates'] if a['hints']==['reload'])['archives'][0]
        self.assertTrue(occurrence()['authored_hand_pose']['all_required_controls_constant'])
        pose['clips'][0]['sha256'] = '2'*64
        self.assertNotIn('authored_hand_pose',occurrence())
        pose['clips'][0]['sha256'] = '1'*64
        pose['clips'][0]['sources'][0]['index_sha256'] = '2'*64
        self.assertNotIn('authored_hand_pose',occurrence())

    def test_batch_extraction_uses_shared_decoder_and_quarantines_wrong_archive(self):
        f = fixture()
        calls=[]
        def reader(path,game_root):
            calls.append((path,game_root))
            return dict(clips=[dict(archive_relative='weapons/rifle.fbrb',archive='weapons/rifle.fbrb',
                archive_index_sha256='d'*64,resource=a['name'],resource_kind=a['kind'],resource_sha256='1'*64,
                status='decoded',tracks=[],raw_controls='must not export') for a in f['inventory']['archives'][0]['animations']])
        out = animations.run(Path(__file__).parent, f['inventory'], reader)
        self.assertEqual(out['summary']['clip_occurrences'],2)
        self.assertEqual(len(calls),1)
        self.assertNotIn('must not export',json.dumps(out))
        f['inventory']['archives'][0]['index_sha256']='e'*64
        out = animations.run(Path(__file__).parent,f['inventory'],reader)
        self.assertEqual(out['clips'],[])
        self.assertEqual(len(out['archive_gaps']),1)

    def test_batch_missing_occurrence_cannot_look_complete(self):
        out = animations.run(Path(__file__).parent,fixture()['inventory'],lambda *a,**k:dict(clips=[]))
        self.assertEqual(out['summary']['archive_gaps'],1)
        self.assertEqual(out['clips'],[])

    def test_common_all_classes_keeps_legacy_lmg_filter_and_archive_bound(self):
        selected=[];bound=[]
        entry=SimpleNamespace(flags=65536,name='Objects/Weapons/Handheld/one.dbx',size=20)
        document=SimpleNamespace(instances={str(i):SimpleNamespace(attributes={'type':'GameSharedResources.SoldierWeaponData'},
                     weapon_class=kind) for i,kind in enumerate(('wcAssault','wcLmg','wcUgl'))})
        class Archive:
            def __init__(self,path):
                bound.append(mesh.MAX_PAYLOAD)
                self.entries=[entry];self.index_sha256='d'*64;self.payload_sha256='e'*64;self.payload_size=100
            def read_selected(self,names):
                selected.extend(names);return {entry.name:b'fixture'}
        mesh=SimpleNamespace(MAX_PAYLOAD=512*1024*1024,Archive=Archive,sha=c.digest)
        config=SimpleNamespace(parse=lambda *a:document,Resolver=lambda d:None,
            effective_weapon=lambda r,d,i:dict(weapon_class=i.weapon_class))
        binding=SimpleNamespace(join=lambda *a:dict(summary={}))
        with patch.dict(sys.modules,inspect_bc2_mesh_asset=mesh,bc2_weapon_config_pipeline=config,bc2_weapon_mesh_bindings=binding):
            legacy=common.run(TOOL.parent,TOOL.parent,{})
            all_rows=common.run(TOOL.parent,TOOL.parent,{},all_weapons=True)
        self.assertEqual([w['weapon_class'] for w in legacy['configuration']['resolved_weapons']],['wcLmg'])
        self.assertEqual(len(all_rows['configuration']['resolved_weapons']),3)
        self.assertEqual(all_rows['schema'],'fvr.bc2.common_weapon_metadata.v1')
        self.assertEqual(bound,[640*1024*1024]*2)
        self.assertEqual(mesh.MAX_PAYLOAD,512*1024*1024)
        self.assertEqual(selected,[entry.name]*2)

    def test_sp_mp_identical_resources_remain_separate_origins(self):
        f = fixture()
        f['config'].update(archive='levels/sp_common.fbrb', index_sha256='1'*64)
        mp = copy.deepcopy(f)
        mp['config'].update(archive='levels/mp_common.fbrb', index_sha256='2'*64)
        common = dict(schema='fvr.bc2.lmg_common_metadata.v1', read_only=True, exported_assets=False,
                      configuration=mp['config'], mesh_bindings=mp['bindings'])
        result = c.catalog(**f, common_metadata=[common])
        self.assertEqual(len(result['configurations']), 2)
        self.assertEqual(len({w['id'] for w in result['configurations']}), 2)
        self.assertEqual(len(result['calibration_jobs']), 1)
        self.assertEqual(len(result['calibration_jobs'][0]['configurations']), 2)
        self.assertEqual(len(result['native_family_groups']), 1)

    def test_binding_cannot_borrow_different_archive_identity(self):
        f = fixture()
        f['config'].update(archive='levels/sp_common.fbrb', index_sha256='1'*64)
        f['bindings'].update(archive='levels/mp_common.fbrb', archive_index_sha256='1'*64)
        with self.assertRaises(ValueError): c.catalog(**f)
        f['bindings']['archive'] = f['config']['archive']
        f['bindings']['archive_index_sha256'] = '2'*64
        with self.assertRaises(ValueError): c.catalog(**f)

    def test_family_uses_dispatch_not_name_or_reload_duration(self):
        f = fixture()
        a = f['config']['resolved_weapons'][0]
        a['fields']['FireLogic.BoltAction.HoldBoltActionUntilFireRelease'] = dict(value=False)
        b = copy.deepcopy(a)
        b.update(native_name='different gun', instance_guid='guid2')
        b['fields']['FireLogic.ReloadTime']['value'] = 6.4
        f['config']['resolved_weapons'].append(b)
        r = c.catalog(**f)
        self.assertEqual(len(r['native_family_groups']), 1)
        self.assertEqual({w['timing_fields']['FireLogic.ReloadTime']['value']
                          for w in r['native_family_groups'][0]['configurations']}, {3.2, 6.4})
        b['fields']['FireLogic.BoltAction.HoldBoltActionUntilFireRelease']['value'] = True
        self.assertEqual(len(c.catalog(**f)['native_family_groups']), 2)

    def test_decoded_tracks_require_exact_archive_index_not_name_only(self):
        f = fixture()
        clip = dict(archive='weapons/rifle.fbrb', archive_index_sha256='d'*64,
                    resource='animations/1p_Reload.res', resource_sha256='1'*64, status='partial',
                    duration_seconds=3.2, tracks=[dict(name='jntWpn_4', payload_decoded=True,
                    motion_status='varying_controls_not_mechanism_proof', position_curve_type='D3')])
        batch = dict(schema='fvr.bc2.authored_animation_batch', schema_version=1, clips=[clip])
        r = c.catalog(**f, animation_batches=[batch])
        self.assertEqual(r['summary']['joined_decoded_animation_observations'], 1)
        a = next(a for a in r['meshes'][0]['animation_candidates'] if a['hints'] == ['reload'])
        self.assertFalse(a['decoded'])  # Partial remains partial, even with one decoded channel.
        self.assertEqual(a['track_names'], ['jntWpn_4'])
        self.assertEqual(a['mechanism_roles'], [])
        self.assertFalse(a['archives'][0]['authored_decode']['native_state_proof'])
        clip['archive_index_sha256'] = '2'*64
        r = c.catalog(**f, animation_batches=[batch])
        self.assertEqual(r['summary']['joined_decoded_animation_observations'], 0)
        self.assertEqual(len(r['unmatched_animation_observations']), 1)

    def test_decoded_payloads_are_metadata_only(self):
        clip = dict(archive='weapons/rifle.fbrb', archive_index_sha256='d'*64,
                    resource='animations/1p_Reload.res', resource_sha256='1'*64, status='decoded',
                    controls='DO NOT EXPORT CURVES', tracks=[dict(name='jntWpn_4',
                    payload_decoded=True, motion_status='constant_controls', vertices='DO NOT EXPORT GEOMETRY')])
        batch = dict(schema='fvr.bc2.authored_animation_batch', schema_version=1, clips=[clip])
        encoded = json.dumps(c.catalog(**fixture(), animation_batches=[batch]))
        self.assertNotIn('DO NOT EXPORT', encoded)
        with self.assertRaises(ValueError): c.catalog(**fixture(), animation_batches=[batch, batch])

    def test_exact_join_and_reusable_calibration_job(self):
        r = c.catalog(**fixture())
        self.assertEqual(r['configurations'][0]['meshes'][0]['join_status'], 'exact_resource_and_geometry_hash')
        self.assertEqual(len(r['calibration_jobs']), 1)
        self.assertEqual(len(r['calibration_jobs'][0]['configurations']), 1)
        self.assertFalse(r['runtime_enabled'] if 'runtime_enabled' in r else r['summary']['runtime_enabled'])

    def test_alias_name_is_not_configuration_identity(self):
        f = fixture()
        second = copy.deepcopy(f['config']['resolved_weapons'][0])
        second.update(resource='weapon/launcher.dbx', resource_sha256='f'*64, instance_guid='guid2', weapon_class='wcUgl')
        f['config']['resolved_weapons'].append(second)
        b = copy.deepcopy(f['bindings']['weapons'][0])
        b.update(resource=second['resource'], resource_sha256=second['resource_sha256'], instance_guid=second['instance_guid'])
        f['bindings']['weapons'].append(b)
        r = c.catalog(**f)
        self.assertEqual(len({w['id'] for w in r['configurations']}), 2)
        self.assertEqual(len(r['calibration_jobs'][0]['configurations']), 2)
        launcher = next(w for w in r['configurations'] if w['weapon_class'] == 'wcUgl')
        self.assertFalse(launcher['physical_family_proposals'][0]['applies_to_selected_mode'])

    def test_native_magazine_does_not_assert_detachable_feed(self):
        f = fixture()
        f['config']['resolved_weapons'][0]['weapon_class'] = 'wcLmg'
        f['plan']['items'][0]['family_proposal'] = 'belt_feed'
        r = c.catalog(**f)['configurations'][0]
        self.assertEqual(r['native_strategy'], 'pooled_magazine_transfer')
        self.assertEqual(r['physical_family_proposals'][0]['family'], 'belt_feed')
        self.assertEqual(r['physical_family_status'], 'unassigned_from_native_configuration')
        self.assertEqual(r['chamber_state'], 'unknown')

    def test_single_round_does_not_assert_tube(self):
        f = fixture()
        f['config']['resolved_weapons'][0]['fields']['FireLogic.ReloadType']['value'] = 'rtSingleBullet'
        f['plan']['items'] = []
        w = c.catalog(**f)['configurations'][0]
        self.assertEqual(w['native_strategy'], 'single_round_transfer')
        self.assertFalse(w['physical_family_proposals'])

    def test_changed_config_hash_does_not_borrow_binding(self):
        f = fixture()
        f['config']['resolved_weapons'][0]['resource_sha256'] = 'f'*64
        r = c.catalog(**f)
        self.assertEqual(r['configurations'][0]['meshes'], [])
        self.assertEqual(len(r['unresolved_configurations']), 1)

    def test_wrong_mesh_hash_cannot_join(self):
        f = fixture()
        f['bindings']['weapons'][0]['states'][0]['meshes'][0]['geometry_variants'][0]['mesh_sha256'] = 'f'*64
        r = c.catalog(**f)
        self.assertEqual(r['configurations'][0]['meshes'][0]['join_status'], 'geometry_unavailable_or_hash_mismatch')
        self.assertFalse(r['calibration_jobs'][0]['configurations'])

    def test_inventory_digest_and_archive_index_are_bound(self):
        with self.assertRaises(ValueError):
            c.catalog(**fixture(), inventory_hash='f'*64)
        f = fixture()
        f['inventory']['archives'][0]['index_sha256'] = 'f'*64
        with self.assertRaises(ValueError):
            c.catalog(**f)

    def test_duplicate_exact_config_or_mesh_is_rejected(self):
        for target, key in [('config', 'resolved_weapons'), ('inventory', 'asset_profiles')]:
            f = fixture()
            f[target][key].append(copy.deepcopy(f[target][key][0]))
            with self.assertRaises(ValueError):
                c.catalog(**f)

    def test_named_charge_clip_does_not_claim_motion_or_role(self):
        r = c.catalog(**fixture())
        clip = next(a for a in r['meshes'][0]['animation_candidates'] if 'charge' in a['hints'])
        self.assertFalse(clip['decoded'])
        self.assertEqual(clip['motion_status'], 'not_evaluated')
        self.assertEqual(clip['mechanism_roles'], [])
        self.assertEqual(r['meshes'][0]['geometry_variants'][0]['parts'][0]['role'], 'unassigned_weighted_part')

    def test_no_binary_or_vertex_payload_leaks(self):
        f = fixture()
        f['inventory']['asset_profiles'][0]['binary'] = 'private bytes'
        encoded = json.dumps(c.catalog(**f))
        for sentinel in ['must not export', 'private bytes', 'raw_vertices']:
            self.assertNotIn(sentinel, encoded)

    def test_orphan_meshes_still_get_jobs(self):
        f = fixture()
        f['bindings']['weapons'] = []
        r = c.catalog(**f)
        self.assertEqual(r['unbound_meshes'], ['mesh/rifle.res'])
        self.assertEqual(r['calibration_jobs'][0]['configuration_binding'], 'missing_from_current_configuration_snapshot')

    def test_missing_archive_remains_explicit(self):
        f = fixture()
        f['inventory']['archives'] = []
        mesh = c.catalog(**f)['meshes'][0]
        self.assertTrue(mesh['source_gaps'])
        self.assertFalse(mesh['animation_candidates'])

    def test_same_named_clips_retain_archive_provenance(self):
        f = fixture()
        a = copy.deepcopy(f['inventory']['archives'][0])
        a.update(archive='weapons/variant.fbrb', index_sha256='f'*64)
        a['animations'][0]['bytes'] = 999
        f['inventory']['archives'].append(a)
        f['inventory']['asset_profiles'][0]['sources'].append(dict(archive=a['archive'], index_sha256=a['index_sha256']))
        clips = c.catalog(**f)['meshes'][0]['animation_candidates']
        reload = next(a for a in clips if a['hints'] == ['reload'])
        self.assertEqual({a['bytes'] for a in reload['archives']}, {123, 999})

    def test_unknown_schema_paths_family_and_nonfinite_are_rejected(self):
        for bad in range(4):
            f = fixture()
            if bad == 0: f['config']['schema_version'] = 2
            if bad == 1: f['inventory']['asset_profiles'][0]['resource'] = '../foreign.res'
            if bad == 2: f['plan']['items'][0]['family_proposal'] = 'invented'
            if bad == 3: f['config']['resolved_weapons'][0]['fields']['FireLogic.ReloadTime']['value'] = float('nan')
            with self.assertRaises(ValueError): c.catalog(**f)

    def test_cli_uses_supplied_metadata_only(self):
        f = fixture()
        parent = Path(__file__).resolve().parents[1] / 'test-temp'
        root = parent / uuid.uuid4().hex
        root.mkdir(parents=True)
        try:
            inventory_bytes = json.dumps(f['inventory']).encode()
            f['bindings']['inventory_sha256'] = c.digest(inventory_bytes)
            argv = []
            for key, obj in f.items():
                p = root / (key + '.json')
                p.write_bytes(inventory_bytes if key == 'inventory' else json.dumps(obj).encode())
                argv += ['--' + key, str(p)]
            out = root / 'out.json'
            c.main(argv + ['--output', str(out)])
            result = json.loads(out.read_text())
            self.assertEqual(result['input_sha256']['inventory'], c.digest(inventory_bytes))
            self.assertFalse(result['exported_assets'])
        finally:
            if root.resolve().parent != parent.resolve():
                raise RuntimeError('Unexpected test cleanup path')
            shutil.rmtree(root)


if __name__ == '__main__':
    unittest.main()
