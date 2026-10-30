"""Deterministic review-boundary checks; fixtures do not prove game behavior."""
import copy
import unittest
from types import SimpleNamespace
import bc2_reviewed_magazine_role as policy
from bc2_authored_grip_bindings import digest


def seal(review):
    review['review_digest'] = digest({k: v for k, v in review.items() if k != 'review_digest'})
    return review


def fixture():
    part = dict(points=[[0., 0., 0.], [.014, .125, .07]], sections=[], triangles=240, mixed_triangles=0)
    role = dict(bone='jntWpn_12', parent='jntWpn_1', child_count=1, extent_m=[.014, .125, .07],
                triangles=240, native_role_verified=False, reasons=[policy.SHAPE_REASON])
    review = dict(schema=policy.SCHEMA, native_asset_name='Example',
                  weapon=dict(resource='Weapons/Example.dbx', sha256='1'*64),
                  configured_mesh_path='Weapons/Example_Mesh', binding_digest='2'*64,
                  mesh_sha256='3'*64, lod_sha256='4'*64, skeleton_sha256='5'*64,
                  static_clip_sha256='6'*64, rig_fingerprint='fnv1a64:0123456789abcdef',
                  clip_resource='Animations/Example_Reload.res', clip_sha256='7'*64,
                  magazine_bone='jntWpn_12', semantic_role='detachable_magazine',
                  role_reviewed=True, reviewer='deterministic fixture', review_basis='synthetic boundary check',
                  visual_review_sha256='8'*64, part_digest=policy.part_digest(part), assembly_digest='9'*64,
                  shape_envelope=dict(minimum_width_m=.01, maximum_width_m=.025,
                                      minimum_length_m=.1, maximum_length_m=.15),
                  **{key: False for key in policy.FALSE_CLAIMS})
    return seal(review), part, role, {'jntWpn_12': {'assembly_digest': '9'*64}}


class ReviewedMagazineRoleTests(unittest.TestCase):
    def test_explicit_thin_assembly_keeps_original_rejection_and_false_claims(self):
        review, part, role, assemblies = fixture()
        chosen = policy.select_role(review, [], [role], {'jntWpn_12': part}, assemblies)
        self.assertEqual(chosen['bone'], 'jntWpn_12')
        self.assertEqual(chosen['original_shape_rejection'], [policy.SHAPE_REASON])
        self.assertFalse(chosen['native_role_verified'])
        self.assertNotIn('paired_reload_grasp', chosen)

    def test_no_structural_motion_or_static_failure_can_be_overridden(self):
        for reason in ('not a rigid direct leaf of the weapon root', 'cross-part weighted triangles',
                       'no substantial authored reload-position control displacement',
                       'closed authored part transform is not constant'):
            with self.subTest(reason=reason):
                review, part, role, assemblies = fixture()
                role['reasons'].append(reason)
                with self.assertRaisesRegex(ValueError, 'structural or motion'):
                    policy.select_role(review, [], [role], {'jntWpn_12': part}, assemblies)

    def test_exact_vertex_change_is_rejected_even_when_bounds_stay_the_same(self):
        review, part, role, assemblies = fixture()
        part['points'].append([.007, .06, .035])
        with self.assertRaisesRegex(ValueError, 'surface changed'):
            policy.select_role(review, [], [role], {'jntWpn_12': part}, assemblies)

    def test_assembly_and_rigid_skin_changes_are_rejected(self):
        review, part, role, assemblies = fixture()
        assemblies['jntWpn_12']['assembly_digest'] = 'a'*64
        with self.assertRaisesRegex(ValueError, 'assembly changed'):
            policy.select_role(review, [], [role], {'jntWpn_12': part}, assemblies)
        part['mixed_triangles'] = 1
        with self.assertRaisesRegex(ValueError, 'fully rigid'):
            policy.select_role(review, [], [role], {'jntWpn_12': part}, assemblies)

    def test_rehashed_false_qualification_or_overbroad_envelope_is_rejected(self):
        original, _, _, _ = fixture()
        for key in policy.FALSE_CLAIMS:
            with self.subTest(claim=key):
                changed = copy.deepcopy(original)
                changed[key] = True
                with self.assertRaises(ValueError):
                    policy.validate_review(seal(changed))
        changed = copy.deepcopy(original)
        changed['shape_envelope']['minimum_width_m'] = .001
        with self.assertRaisesRegex(ValueError, 'bounded policy'):
            policy.validate_review(seal(changed))

    def test_review_does_not_admit_a_part_outside_its_own_envelope(self):
        review, part, role, assemblies = fixture()
        review['shape_envelope']['minimum_width_m'] = .015
        with self.assertRaisesRegex(ValueError, 'outside reviewed'):
            policy.select_role(seal(review), [], [role], {'jntWpn_12': part}, assemblies)

    def test_duplicate_unselected_and_unreviewed_inputs_are_rejected(self):
        review, _, _, _ = fixture()
        for reviews, assets in (([review, review], {'Example'}), ([review], {'Other'})):
            with self.assertRaises(ValueError):
                policy.index_reviews(reviews, assets)
        review['role_reviewed'] = False
        with self.assertRaises(ValueError):
            policy.index_reviews([seal(review)], {'Example'})

    def test_exact_configuration_change_is_rejected(self):
        review, _, _, _ = fixture()
        binding = dict(native_asset_name='Example', weapon=review['weapon'],
                       configured_mesh_path=review['configured_mesh_path'], binding_digest='b'*64)
        with self.assertRaisesRegex(ValueError, 'identity differs'):
            policy.verify_context(review, binding, SimpleNamespace(sha256='7'*64),
                                  SimpleNamespace(sha256='5'*64, fingerprint=review['rig_fingerprint']),
                                  review['clip_resource'], '3'*64, '4'*64, '6'*64)

    def test_export_requires_independent_authored_pair(self):
        review, part, role, assemblies = fixture()
        chosen = policy.select_role(review, [], [role], {'jntWpn_12': part}, assemblies)
        profile = {k: review[k] for k in ('native_asset_name', 'weapon', 'configured_mesh_path',
                   'mesh_sha256', 'lod_sha256', 'skeleton_sha256', 'rig_fingerprint', 'static_clip_sha256')}
        profile.update(grip_binding_digest=review['binding_digest'],
                       reload_clip=dict(resource=review['clip_resource'], sha256=review['clip_sha256']),
                       role_evidence=chosen, bones=dict(magazine='jntWpn_12'), assembly=assemblies['jntWpn_12'],
                       geometry=dict(measured=dict(bone_local_extent_m=role['extent_m'], part_triangle_count=240),
                                     design=dict(authored_reload_grasp=False)))
        with self.assertRaisesRegex(ValueError, 'still requires unchanged paired'):
            policy.validate_export(profile)


if __name__ == '__main__':
    unittest.main()
