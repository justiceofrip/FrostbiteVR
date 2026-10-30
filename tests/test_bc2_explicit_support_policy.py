"""Generic authoring policy checks; no installed game data required."""
import copy
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_bc2_authored_pose_bounds import support_fixture, curve
from bc2_authored_grip_bindings import digest
from bc2_authored_support_bindings import derive, derive_bounded, derive_explicit_contact_policy, header


def repin(document):
    for row in document['profiles']:
        row['binding_digest'] = digest({k:v for k,v in row.items() if k != 'binding_digest'})
    return dict(schema='fvr.bc2.explicit_support_contact_policy', schema_version=1,
                mode='fixed_authored_handsikpose_initial', reference_time_seconds=0.0,
                source_curve_claim='none', support_aim_mode='two_point', native_admission=False,
                input_binding_document_digest=digest(document),
                source_binding_digests=[r['binding_digest'] for r in document['profiles']])


def fixture():
    clip, skeleton, document = support_fixture()
    old = clip.tracks['LeftHand']
    clip.tracks['LeftHand'] = (curve(3, [(.2,0,0), (.24,0,0)]), old[1], old[2])
    skeleton.metadata = lambda: {'bones': [dict(name=n, parent_index=p) for n,p in zip(skeleton.names,skeleton.parents)]}
    row = document['profiles'][0]
    receipt = lambda resource, kind: dict(resource=resource, sha256='3'*64, type=kind,
                                         instance_guid='00000000-0000-0000-0000-000000000001')
    row.update(weapon=receipt('Synthetic.dbx','GameSharedResources.SoldierWeaponData'),
               runtime_accepted=False, active_native_mesh_binding=False, weapon_bone='jntWpn_1',
               rig_topology=skeleton.metadata()['bones'],
               animation_source=dict(archive='Dist/clip.fbrb', archive_index_sha256='4'*64, kind='GrannyAnimation'),
               skeleton_source=dict(archive='Dist/config.fbrb', archive_index_sha256='5'*64, kind='GrannyModel'),
               animation_chain=dict(tree=receipt('Tree.dbx','Animation.SpecificAnimTreeData'),
                                    animation_asset=receipt('Animations/HandsIkPose.dbx','Animation.AnimationAsset'),
                                    skeleton_asset=receipt('Skeleton.dbx','Animation.SkeletonAsset'),
                                    animation_resource='Animations/HandsIkPose.res', skeleton_resource='Skeleton.res'))
    document.update(schema_version=1, archive='Dist/config.fbrb', archive_index_sha256='5'*64)
    return clip, skeleton, document, repin(document)


class ExplicitContactPolicyTests(unittest.TestCase):
    def run_policy(self, data, **kwargs):
        clip, skeleton, document, policy = data
        return derive_explicit_contact_policy(document, {'Synthetic'}, {(clip.sha256,skeleton.sha256):(clip,skeleton)},
                                              policy, enable_contact=kwargs.get('enable_contact', True))

    def test_fixed_contact_preserves_source_failure_and_original_gates(self):
        data = fixture(); clip, skeleton, document, _ = data
        original = copy.deepcopy(document)
        with self.assertRaisesRegex(ValueError, 'constant'):
            derive(document, {'Synthetic'}, enable_contact=True)
        with self.assertRaisesRegex(ValueError, 'exceeds'):
            derive_bounded(document, {'Synthetic'}, {(clip.sha256,skeleton.sha256):(clip,skeleton)},
                           translation_m=.00025, rotation_rad=math.radians(.05), enable_contact=True)
        result = self.run_policy(data); row = result['profiles'][0]
        self.assertEqual(document, original)
        self.assertEqual(row['left_hand_status'], 'mod_authored_fixed_contact')
        self.assertEqual(row['source_left_hand_status'], 'decoded_spline_candidate')
        self.assertGreater(row['source_reference_envelope']['translation_bound_m'], .039)
        self.assertFalse(row['right_hand_retargeted']); self.assertFalse(row['native_admission'])
        self.assertEqual(row['left_hand_in_weapon'], clip.evaluate(skeleton,0,['LeftHand'])['weapon_relative']['LeftHand'])
        self.assertEqual(row['binding_digest'], digest({k:v for k,v in row.items() if k != 'binding_digest'}))
        self.assertIn(',true}', header(result))

    def test_opt_in_document_and_complete_configuration_pins_required(self):
        with self.assertRaises(ValueError): self.run_policy(fixture(), enable_contact=False)
        for field, value in [('input_binding_document_digest','0'*64), ('source_binding_digests',[]),
                             ('reference_time_seconds',.01), ('reference_time_seconds',False),
                             ('source_curve_claim','static'), ('native_admission',True)]:
            data = fixture(); data[3][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError): self.run_policy(data)
        data = fixture(); data[2]['profiles'][0]['weapon']['sha256'] = '7'*64
        with self.assertRaisesRegex(ValueError,'document'): self.run_policy(data)

    def test_resealed_invalid_sources_still_fail(self):
        for field, value in [('state_count',2), ('state_index',1), ('runtime_accepted',True),
                             ('active_native_mesh_binding',True), ('weapon_class','wcGrenade'),
                             ('weapon_bone','Other'), ('rig_topology',[])]:
            clip,skeleton,document,_ = fixture(); document['profiles'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.run_policy((clip,skeleton,document,repin(document)))
        clip,skeleton,document,_ = fixture(); document['profiles'][0]['left_hand_in_weapon'][12] += .02
        with self.assertRaisesRegex(ValueError,'reproduce'):
            self.run_policy((clip,skeleton,document,repin(document)))
        data = fixture(); data[1].fingerprint = 'fnv1a64:ffffffffffffffff'
        with self.assertRaisesRegex(ValueError,'identity'): self.run_policy(data)

    def test_same_name_distinct_meshes_compile_as_distinct_keys(self):
        clip,skeleton,document,_ = fixture()
        other = copy.deepcopy(document['profiles'][0]); other['configured_mesh_path'] = 'Objects/OtherMesh'
        document['profiles'].append(other)
        result = self.run_policy((clip,skeleton,document,repin(document)))
        self.assertEqual(len(result['profiles']),2)
        self.assertEqual({r['configured_mesh_path'] for r in result['profiles']}, {'Objects/Synthetic','Objects/OtherMesh'})


if __name__ == '__main__':
    unittest.main()
