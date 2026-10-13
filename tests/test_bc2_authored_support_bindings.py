import copy
import json
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_authored_support_bindings as support
from bc2_support_batch import batch
from bc2_authored_grip_bindings import digest


def binding():
    r=dict(native_asset_name='SyntheticSupport',configured_mesh_path='Objects/Weapons/SyntheticMesh',
           rig_fingerprint='fnv1a64:0123456789abcdef',animation_sha256='1'*64,skeleton_sha256='2'*64,
           left_hand_in_weapon=[1,0,0,0,0,1,0,0,0,0,1,0,0,-.1,-.6,1],
           left_hand_status='static_authored_pose',right_hand_status='decoded_spline_candidate',
           state_count=1,state_index=0,weapon={'resource':'Synthetic.dbx'},animation_chain={'resource':'Synthetic.res'})
    r['binding_digest']=digest(r)
    return dict(schema='fvr.bc2.authored_reload_reference_bindings',runtime_admission=False,profiles=[r])


class SupportBindingTests(unittest.TestCase):
    def test_batch_uses_configuration_data_and_reports_gaps(self):
        d=binding();r=d['profiles'][0];r['weapon_class']='wcSmg'
        r['binding_digest']=digest({k:v for k,v in r.items() if k!='binding_digest'})
        dynamic=copy.deepcopy(r);dynamic['native_asset_name']='DynamicSupport'
        dynamic['left_hand_status']='decoded_spline_candidate'
        dynamic['binding_digest']=digest({k:v for k,v in dynamic.items() if k!='binding_digest'})
        d['profiles'].append(dynamic)
        result=batch([d,d], {'SyntheticSupport','MissingPistol'}, {'ExistingPump'})
        self.assertEqual(len(result['profiles']),1)
        self.assertEqual(result['profiles'][0]['support_aim_mode'],'primary_hand')
        rows={x['asset']:x for x in result['coverage']}
        self.assertEqual(rows['SyntheticSupport']['source_configurations'],1)
        self.assertEqual(rows['DynamicSupport']['status'],'deferred')
        self.assertEqual(rows['MissingPistol']['status'],'missing_authored_reference')
        self.assertEqual(rows['ExistingPump']['status'],'preserved_existing_contact')
        self.assertTrue(all(not x['native_reload_admitted'] for x in rows.values()))
        self.assertEqual(batch([d])['profiles'][0].get('support_aim_mode','two_point'),'two_point')
        # A malformed source is fatal, not a silently skipped weapon.
        d['profiles'][0]['left_hand_in_weapon'][12]+=.1
        with self.assertRaisesRegex(ValueError,'digest'):batch([d])

    def test_batch_retains_conflicting_variant_as_gap(self):
        d=binding();r=d['profiles'][0];r['weapon_class']='wcAssault'
        r['binding_digest']=digest({k:v for k,v in r.items() if k!='binding_digest'})
        second=copy.deepcopy(r);second['left_hand_in_weapon'][12]+=.1
        second['binding_digest']=digest({k:v for k,v in second.items() if k!='binding_digest'})
        d['profiles'].append(second)
        result=batch([d]);self.assertEqual(result['profiles'],[])
        self.assertIn('Conflicting',result['coverage'][0]['reason'])
        with self.assertRaises(ValueError):batch([d],{'SyntheticSupport'},{'SyntheticSupport'})

    def test_compiled_fleet_preserves_prior_contacts(self):
        root=Path(__file__).resolve().parents[1]/'profiles'
        d=json.loads((root/'support308/support-bindings.json').read_text())
        self.assertEqual((root/'support308/StaticSupports.generated.h').read_text(),support.header(d))
        self.assertEqual(len(d['profiles']),58)
        key=lambda r:(r['native_asset_name'],r['configured_mesh_path'],r['rig_fingerprint'])
        rows={key(r):r for r in d['profiles']}
        self.assertEqual(len(rows),len(d['profiles']))
        for prior in ('support259','support306'):
            for r in json.loads((root/prior/'support-bindings.json').read_text())['profiles']:
                current=rows[key(r)]
                for field in ('left_hand_in_weapon','animation_sha256','skeleton_sha256','support_contact_reference'):
                    self.assertEqual(r[field],current[field])
                self.assertEqual(r.get('support_aim_mode','two_point'),current.get('support_aim_mode','two_point'))
        self.assertEqual({r['native_asset_name'] for r in rows.values() if r.get('support_aim_mode')=='primary_hand'},
                         {'MP443_sp','MP443','M9','M9-3','MP412'})
        self.assertFalse(any(r['native_asset_name']=='SPAS12_sp' for r in rows.values()))

    def test_constant_left_does_not_promote_dynamic_right(self):
        d=support.derive(binding(),{'SyntheticSupport'})
        self.assertFalse(d['profiles'][0]['right_hand_retargeted'])
        self.assertEqual(d['profiles'][0]['right_hand_status_not_admitted'],'decoded_spline_candidate')
        self.assertFalse(d['runtime_admission'])
        self.assertIn('AuthoredSupportProfile',support.header(d))

    def test_tampered_proof_denied(self):
        d=binding();d['profiles'][0]['left_hand_in_weapon'][12]+=.1
        with self.assertRaisesRegex(ValueError,'digest'):support.derive(d,{'SyntheticSupport'})

    def test_nonconstant_left_denied_even_with_valid_digest(self):
        d=binding();r=d['profiles'][0];r['left_hand_status']='decoded_spline_candidate'
        r['binding_digest']=digest({k:v for k,v in r.items() if k!='binding_digest'})
        with self.assertRaisesRegex(ValueError,'constant'):support.derive(d,{'SyntheticSupport'})

    def test_configuration_and_ambiguity_denied(self):
        d=binding();r=d['profiles'][0];r['state_count']=2;r['binding_digest']=digest({k:v for k,v in r.items() if k!='binding_digest'})
        with self.assertRaises(ValueError):support.derive(d,{'SyntheticSupport'})
        d=binding();r=copy.deepcopy(d['profiles'][0]);r['left_hand_in_weapon'][12]+=.02
        r['binding_digest']=digest({k:v for k,v in r.items() if k!='binding_digest'});d['profiles'].append(r)
        with self.assertRaisesRegex(ValueError,'Conflicting'):support.derive(d,{'SyntheticSupport'})

    def test_multiple_configured_variants(self):
        d=binding();r=copy.deepcopy(d['profiles'][0]);r['configured_mesh_path']='Objects/Weapons/VariantMesh'
        r['binding_digest']=digest({k:v for k,v in r.items() if k!='binding_digest'});d['profiles'].append(r)
        self.assertEqual(len(support.derive(d,{'SyntheticSupport'})['profiles']),2)

    def test_unproved_asset_and_promoted_input_denied(self):
        with self.assertRaises(ValueError):support.derive(binding(),{'Unmeasured'})
        d=binding();d['runtime_admission']=True
        with self.assertRaises(ValueError):support.derive(d,{'SyntheticSupport'})

    def test_explicit_contact_enrollment_keeps_dynamic_right_unmodified(self):
        d=binding();r=d['profiles'][0];r['weapon_class']='wcLmg'
        r['binding_digest']=digest({k:v for k,v in r.items() if k!='binding_digest'})
        result=support.derive(d,{'SyntheticSupport'},enable_contact=True)
        p=result['profiles'][0]
        self.assertTrue(p['support_contact_reference'])
        self.assertFalse(p['right_hand_retargeted'])
        self.assertFalse(p['native_admission'])
        self.assertEqual(p['right_hand_status_not_admitted'],'decoded_spline_candidate')
        self.assertIn(',true}',support.header(result))
        self.assertNotIn('support_contact_reference',support.derive(d,{'SyntheticSupport'})['profiles'][0])

    def test_contact_requires_exact_class_and_constant_left(self):
        for klass in (None,'wcKnife','wcGrenade'):
            d=binding();r=d['profiles'][0];r['weapon_class']=klass
            r['binding_digest']=digest({k:v for k,v in r.items() if k!='binding_digest'})
            with self.assertRaises(ValueError):support.derive(d,{'SyntheticSupport'},enable_contact=True)
        for klass in ('wcSniper','wcShotgun'):
            d=binding();r=d['profiles'][0];r['weapon_class']=klass
            r['binding_digest']=digest({k:v for k,v in r.items() if k!='binding_digest'})
            result=support.derive(d,{'SyntheticSupport'},enable_contact=True)
            self.assertTrue(result['profiles'][0]['support_contact_reference'])
            self.assertFalse(result['profiles'][0]['native_admission'])
        d=binding();r=d['profiles'][0];r['weapon_class']='wcAssault';r['left_hand_status']='decoded_spline_candidate'
        r['binding_digest']=digest({k:v for k,v in r.items() if k!='binding_digest'})
        with self.assertRaisesRegex(ValueError,'constant'):support.derive(d,{'SyntheticSupport'},enable_contact=True)

    def test_primary_hand_aim_is_explicit_and_bound_to_measured_pistol(self):
        root=Path(__file__).resolve().parents[1]
        source=json.loads((root/'profiles/resource-enrollment295-pistol/reference-bindings.json').read_text())
        d=support.derive(source,{'MP443_sp'},enable_contact=True,primary_hand_assets={'MP443_sp'})
        row=d['profiles'][0]
        self.assertEqual(row['support_aim_mode'],'primary_hand')
        self.assertEqual(row['binding_digest'],digest({k:v for k,v in row.items() if k!='binding_digest'}))
        self.assertFalse(row['right_hand_retargeted'])
        saved=json.loads((root/'profiles/support306/support-bindings.json').read_text())
        self.assertEqual(saved,d)
        self.assertEqual((root/'profiles/support306/PistolSupport.generated.h').read_text(),support.header(d))
        for contact,assets in [(False,{'MP443_sp'}),(True,{'Foreign'})]:
            with self.assertRaises(ValueError):support.derive(source,{'MP443_sp'},enable_contact=contact,primary_hand_assets=assets)
        row['support_aim_mode']='unknown'
        with self.assertRaises(ValueError):support.header(d)

    def test_compiled_batch_matches_digested_reference_rows(self):
        root=Path(__file__).resolve().parents[1]/'profiles/support259'
        d=json.loads((root/'support-bindings.json').read_text())
        self.assertEqual((root/'StaticSupports.generated.h').read_text(),support.header(d))
        self.assertEqual(len(d['profiles']),6)
        self.assertEqual(sum(len(r['source_weapons']) for r in d['profiles']),12)
        self.assertEqual({r['native_asset_name'] for r in d['profiles']},
                         {'SCAR_sp','SCAR_sp_s','F2000_sp','XM8C','MG36','XM8 LMG'})
        for r in d['profiles']:
            self.assertEqual(r['binding_digest'],digest({k:v for k,v in r.items() if k!='binding_digest'}))
            self.assertTrue(r['support_contact_reference'])
            self.assertFalse(r['native_admission'])
            self.assertFalse(r['right_hand_retargeted'])
        self.assertEqual(d['deferred'][0]['native_asset_name'],'M416')

    def test_new_mesh_layout_preserves_existing_support_contacts(self):
        root=Path(__file__).resolve().parents[1]/'profiles'
        old=json.loads((root/'support308/support-bindings.json').read_text())
        new=json.loads((root/'support312/support-bindings.json').read_text())
        key=lambda p:(p['native_asset_name'],p['configured_mesh_path'])
        saved={key(p):p for p in new['profiles']}
        for row in old['profiles']:self.assertEqual(saved[key(row)],row)
        added=[p for p in new['profiles'] if key(p) not in {key(x) for x in old['profiles']}]
        self.assertEqual(len(added),1)
        row=added[0]
        self.assertEqual(row['native_asset_name'],'M1911')
        self.assertEqual(row['support_aim_mode'],'primary_hand')
        self.assertFalse(row['right_hand_retargeted']);self.assertFalse(row['native_admission'])
        self.assertEqual(row['animation_sha256'],'3ae1703a5ba229b92e279cc91542edb97a65fccbbc316e9f0389c1272430c04b')
        self.assertEqual((root/'support312/StaticSupports.generated.h').read_text(),support.header(new))


if __name__=='__main__':unittest.main()
