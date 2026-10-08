import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_authored_support_bindings as support
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


if __name__=='__main__':unittest.main()
