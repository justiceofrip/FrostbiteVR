import copy
import importlib.util
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

path=Path(__file__).resolve().parents[1]/'tools/bc2_magazine_descriptor_manifest.py'
spec=importlib.util.spec_from_file_location('manifest',path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def row(name='Rifle',variant='Base',origin='sp'):
    values={'FireLogic.FireLogicType':'fltAutomaticFire','FireLogic.ReloadType':'rtMagazine',
        'FireLogic.FireInputAction':'EiaFire','FireLogic.ReloadInputAction':'EiaReload',
        'FireLogic.ReloadLogic':'rlWeaponSwitchCancelsUnfinishedReload',
        'Ammo.MagazineCapacity':30,'Ammo.NumberOfMagazines':4,
        'FireLogic.ReloadDelay':0.,'FireLogic.ReloadTime':3.2,'FireLogic.ReloadThreshold':.75,
        'FireLogic.PostReloadSequenceTime':0.,'FireLogic.BoltAction.BoltActionDelay':0.,
        'FireLogic.BoltAction.BoltActionTime':0.,'FireLogic.BoltAction.HoldBoltActionUntilFireRelease':False,
        'FireLogic.BoltAction.HoldBoltActionUntilZoomRelease':False}
    return {'native_name':name,'weapon_class':'wcAssault','resource':f'Objects/Weapons/{variant}.dbx',
        'resource_sha256':'a'*64,'instance_guid':'00000000-0000-0000-0000-000000000001',
        'firing_resource':'Objects/Weapons/Firing.dbx','firing_sha256':'b'*64,
        'firing_guid':'00000000-0000-0000-0000-000000000002','function_resource':'Objects/Weapons/Firing.dbx',
        'function_sha256':'b'*64,'function_guid':'00000000-0000-0000-0000-000000000003',
        'origin_archive':f'Dist/win32/levels/{origin}_common/level-00.fbrb','origin_index_sha256':'c'*64,
        'fields':{k:{'value':v}for k,v in values.items()}}

def catalog(*rows):return {'schema':'fvr.bc2.manual_reload_catalog','schema_version':1,'configurations':list(rows)}
def baseline(d):return {'key':d['key'],'descriptor_digest':d['descriptor_digest'],'registry_source_sha256':'d'*64,'descriptor_source_sha256':'e'*64}

class ManifestTests(unittest.TestCase):
    def test_optic_paths_not_collapsed(self):
        d=m.build(catalog(row(variant='Base'),row(variant='Scope'),row(variant='Kobra')))
        self.assertEqual(len(d['descriptors']),3);self.assertEqual(len({x['key']for x in d['descriptors']}),3)
        self.assertTrue(all(x['configuration']['assetName']=='Rifle' and not x['runtime_enabled'] for x in d['descriptors']))
    def test_exact_sp_mp_content_keeps_origins(self):
        d=m.build(catalog(row(origin='sp'),row(origin='mp')))['descriptors'];self.assertEqual(len(d),1);self.assertEqual(len(d[0]['origins']),2)
    def test_same_path_different_content_is_ambiguous(self):
        a=row();b=row();b['resource_sha256']='f'*64
        ds=m.build(catalog(a,b))['descriptors'];self.assertEqual(len(ds),2)
        self.assertTrue(all('conflicting_content_for_exact_native_path'in d['deferred_reasons'] for d in ds))
    def test_exact_identity_disagrees(self):
        a=row();b=row();b['fields']['Ammo.MagazineCapacity']['value']=32
        d=m.build(catalog(a,b))['descriptors'][0];self.assertFalse(d['same_reviewed_dispatch_shape']);self.assertIn('conflicting_content_for_exact_identity',d['deferred_reasons'])
    def test_unknown_dispatch_never_gets_guessed_numeric(self):
        a=row();a['fields']['FireLogic.FireLogicType']['value']='fltBurstFire'
        d=m.build(catalog(a))['descriptors'][0];self.assertFalse(d['same_reviewed_dispatch_shape']);self.assertEqual(d['configuration']['timing'],[])
        self.assertNotIn('fireLogicType',d['configuration']['values'])
        with self.assertRaises(ValueError):m.build(catalog(a),[baseline(d)])
    def test_unknown_bolt_shape_cannot_reference_reviewed_baseline(self):
        a=row();a['fields']['FireLogic.BoltAction.BoltActionTime']['value']=1.;a['fields']['FireLogic.BoltAction.HoldBoltActionUntilFireRelease']['value']=True
        d=m.build(catalog(a))['descriptors'][0];self.assertIn('authored_bolt_fields_require_shared_dispatch_review',d['deferred_reasons'])
        with self.assertRaises(ValueError):m.build(catalog(a),[baseline(d)])
    def test_existing_baseline_does_not_enable_any_runtime(self):
        c=catalog(row());d=m.build(c)['descriptors'][0];out=m.build(c,[baseline(d)])
        self.assertEqual(out['summary']['existing_baselines'],1);self.assertFalse(out['runtime_enabled'])
        self.assertFalse(out['descriptors'][0]['runtime_enabled']);self.assertEqual(out['descriptors'][0]['configuration']['admission'],'Candidate')
    def test_wrong_baseline_digest_rejected(self):
        c=catalog(row());b=baseline(m.build(c)['descriptors'][0]);b['descriptor_digest']='f'*64
        with self.assertRaises(ValueError):m.build(c,[b])
    def test_duplicate_baseline_rejected(self):
        c=catalog(row());b=baseline(m.build(c)['descriptors'][0])
        with self.assertRaises(ValueError):m.build(c,[b,b])
    def test_gadget_class_is_not_a_magazine_profile(self):
        a=row();a['weapon_class']='wcGrenadeLauncher';self.assertEqual(m.build(catalog(a))['descriptors'],[])
    def test_float32_words_keep_authored_bits(self):
        d=m.build(catalog(row()))['descriptors'][0];self.assertEqual(d['configuration']['values']['reloadTime'],m.f32(3.2))
        self.assertEqual(d['configuration']['timing'][2],{'offset':0x18,'expected':0x404ccccd,'expected_bits':'404ccccd','comparison':'Bits'})
    def test_magazine_lmg_data_is_generic_not_belt_admission(self):
        a=row();a['weapon_class']='wcLmg';a['fields']['Ammo.MagazineCapacity']['value']=100
        a['fields']['FireLogic.ReloadTime']['value']=6.4;a['fields']['FireLogic.ReloadThreshold']['value']=.58
        d=m.build(catalog(a))['descriptors'][0]
        self.assertEqual(d['configuration']['values']['baseCapacity'],100);self.assertGreater(d['proposed_completion_deadline_ns'],7100000000)
        self.assertTrue(d['same_reviewed_dispatch_shape']);self.assertFalse(d['runtime_enabled']);self.assertIn('independent_grip_and_magazine_geometry',d['jobs'])
    def test_large_completion_allowance_is_deferred_not_clamped(self):
        a=row();a['fields']['FireLogic.ReloadTime']['value']=9.9
        d=m.build(catalog(a))['descriptors'][0];self.assertIn('completion_allowance_exceeds_existing_bound',d['deferred_reasons']);self.assertGreater(d['proposed_completion_deadline_ns'],10000000000)
    def test_missing_field_has_explicit_rejection(self):
        a=row();del a['fields']['Ammo.MagazineCapacity'];d=m.build(catalog(a));self.assertEqual(len(d['rejected']),1);self.assertEqual(d['descriptors'],[])
    def test_invalid_identity_is_rejected(self):
        for key,value in [('resource','../x.dbx'),('resource','C:/x.dbx'),('resource_sha256','bad'),('instance_guid','bad')]:
            a=row();a[key]=value;self.assertEqual(len(m.build(catalog(a))['rejected']),1)
    def test_boolean_not_accepted_as_capacity(self):
        a=row();a['fields']['Ammo.MagazineCapacity']['value']=True;self.assertEqual(len(m.build(catalog(a))['rejected']),1)
    def test_nonfinite_or_unknown_logic_is_rejected(self):
        a=row();a['fields']['FireLogic.ReloadTime']['value']=float('nan');self.assertEqual(len(m.build(catalog(a))['rejected']),1)
        a=row();a['fields']['FireLogic.ReloadLogic']['value']='rlUnknown';d=m.build(catalog(a))['descriptors'][0]
        self.assertEqual(d['configuration']['timing'],[]);self.assertFalse(d['same_reviewed_dispatch_shape'])
    def test_input_order_does_not_change_manifest(self):
        rows=[row(variant='A'),row(variant='B'),row(variant='A',origin='mp')]
        self.assertEqual(m.build(catalog(*rows)),m.build(catalog(*reversed(rows))))
    def test_bounded_catalog_schema(self):
        with self.assertRaises(ValueError):m.build({'configurations':[]})
        with self.assertRaises(ValueError):m.build(catalog(*([row()]*2049)))
        with self.assertRaises(ValueError):m.build(catalog(row()),{})
    def test_cli_hashes_exact_consumed_bytes_if_source_changes(self):
        temp_root=Path(__file__).resolve().parents[1]/'test-temp';temp_root.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=temp_root) as folder:
            self.assertTrue(Path(folder).resolve().is_relative_to(temp_root.resolve()))
            source=Path(folder)/'catalog.json';output=Path(folder)/'output.json'
            consumed=json.dumps(catalog(row())).encode();source.write_bytes(consumed)
            original=m.build
            def changed_after_read(document,baselines):
                source.write_text('changed after bounded read')
                return original(document,baselines)
            with patch.object(sys,'argv',['tool','--catalog',str(source),'--output',str(output)]),patch.object(m,'build',changed_after_read):m.main()
            result=json.loads(output.read_text());self.assertEqual(result['summary']['descriptors'],1)
            self.assertEqual(result['input_sha256']['catalog'],hashlib.sha256(consumed).hexdigest())
            self.assertNotEqual(result['input_sha256']['catalog'],hashlib.sha256(source.read_bytes()).hexdigest())

if __name__=='__main__':unittest.main()
