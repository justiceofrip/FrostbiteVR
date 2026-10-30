"""Synthetic contract tests only; these fixtures are never game enrollment data."""
import copy
import hashlib
import json
import unittest
from unittest.mock import patch

import bc2_magazine_descriptor_manifest as descriptor
import bc2_magazine_registry_header as native_registry
import bc2_mechanism_registry_header as exporter
import bc2_singlefire_magazine_proof as singlefire


IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def fixture(kind='pistol_slide', suffix=''):
    """A deliberately synthetic, exact-config pipeline/review fixture."""
    name = ('SyntheticFeed' if kind == 'feed' else 'SyntheticSlide') + suffix
    fields = {spec[0]: {'value': spec[1]} for spec in descriptor.ENUM_FIELDS.values()}
    for target, (field, value_kind) in descriptor.VALUE_FIELDS.items():
        value = False if value_kind == 'bool' else 0
        if target in ('baseCapacity', 'numberOfMagazines'): value = 2
        if target == 'reloadTime': value = 2.0
        if target == 'reloadThreshold': value = .5
        fields[field] = {'value': value}
    fields['FireLogic.ReloadLogic'] = {'value': 'rlWeaponSwitchCancelsUnfinishedReload'}
    if kind == 'pistol_slide': fields['FireLogic.FireLogicType']['value'] = 'fltSingleFire'
    row = {'native_name': name, 'resource': f'Fixture/{name}.dbx', 'resource_sha256': '1'*64,
           'instance_guid': '11111111-1111-1111-1111-111111111111',
           'firing_resource': f'Fixture/{name}Firing.dbx', 'firing_sha256': '2'*64,
           'firing_guid': '22222222-2222-2222-2222-222222222222',
           'function_resource': f'Fixture/{name}Function.dbx', 'function_sha256': '3'*64,
           'function_guid': '33333333-3333-3333-3333-333333333333',
           'origin_archive': 'synthetic.fixture', 'origin_index_sha256': '4'*64,
           'weapon_class': 'wcLmg' if kind == 'feed' else 'wcSmg', 'fields': fields}
    jobs = descriptor.build({'schema': 'fvr.bc2.manual_reload_catalog', 'schema_version': 1,
                             'configurations': [row]})
    if kind == 'pistol_slide': jobs = singlefire.apply(jobs, singlefire.EXPECTED_PROOF)
    job = jobs['descriptors'][0]
    review = {'key': job['key'], 'descriptor_digest': job['descriptor_digest'], 'evidence_sha256': '5'*64,
              'family_proof': native_registry.FAMILY if kind == 'feed' else native_registry.SINGLE_FIRE_FAMILY}
    if kind == 'pistol_slide': review['instruction_proof_digest'] = singlefire.PROOF_DIGEST
    roles = {'root': {'bone': 'syntheticRoot', 'index': 1}}
    def child(name, index, parent='syntheticRoot', parent_index=1):
        return {'bone': name, 'index': index, 'parent': parent, 'parentIndex': parent_index}
    if kind == 'pistol_slide':
        roles['slide'] = child('syntheticSlide', 2)
        geometry = {name: list(IDENTITY) for name in exporter.SLIDE_MATRICES}
        geometry.update(rearward=[0, 0, 1], partBoundsMin=[-.01]*3, partBoundsMax=[.01]*3,
                        ring3Part=[0, 0, 0], nearestVertexPart=[0, 0, 0], minimumDisplacementMeters=-.01,
                        maximumDisplacementMeters=.1, authoredLandmarkSeconds=1.0,
                        ring3VertexDistanceMeters=0, linearHelperSha256='6'*64)
        policy = dict(name='Synthetic policy', sourceReference='Synthetic same-frame wrist', revision=1,
                      sourceDisplacementMeters=0, strokeMeters=.06, frontToleranceMeters=.002,
                      rearToleranceMeters=.002, hysteresisMeters=.001, contactRadiusMeters=.03,
                      lateralToleranceMeters=.02, rotationToleranceRadians=.3, maxStepMeters=.02,
                      endpointDwellNs=30000000, maximumStrokeNs=8000000000)
    else:
        roles.update(pouch=child('syntheticPouch', 2), cover=child('syntheticCover', 3),
                     feed_chain=[child('syntheticFeed', 4), child('syntheticTip', 5, 'syntheticFeed', 4)])
        geometry = {name: list(IDENTITY) for name in exporter.FEED_MATRICES}
        geometry.update({name: '6'*64 for name in exporter.SOURCE_HASHES})
        geometry.update(coverTravelRadians=1.5, poses=[dict(authoredSeconds=float(i),
            weaponFromPouch=list(IDENTITY), weaponFromFeedRoot=list(IDENTITY),
            parentFromChainChild=[list(IDENTITY)], sourceFromWrist=list(IDENTITY)) for i in range(7)])
        for i, pose in enumerate(geometry['poses']):
            pose['weaponFromPouch'][12] = i * .2
            pose['weaponFromFeedRoot'][12] = i * .2
        geometry['hingeFromOpeningContact'][12] = .1
        geometry['hingeFromClosingContact'][12] = .1
        policy = dict(completeFraction=.95, releaseFraction=.1, pathToleranceMeters=.02,
                      orientationToleranceRadians=.3, maxStepMeters=.1, maxStepRadians=.3,
                      endpointDwellNs=30000000, maximumGestureNs=8000000000, maxInputGapNs=100000000)
    data = {'configuration': job['identity']['weapon'], 'asset': name, 'mesh': f'Fixture/{name}Mesh',
            'rig': 'fnv1a64:0123456789abcdef', 'roles': roles, 'geometry': geometry,
            'role_review': {'reviewer': 'synthetic reviewer', 'explicit_role_review': True},
            'native_review': {'evidence_level': 'synthetic_contract_fixture', 'reviewed': True}}
    ref = lambda pointer: {'source': 'fixture', 'pointer': pointer}
    annotation = dict(kind=kind, descriptor_key=job['key'], descriptor_digest=job['descriptor_digest'],
                      native_class=exporter.CLASSES[kind][1], configuration=ref('/configuration'),
                      asset=ref('/asset'), meshPath=ref('/mesh'), rigFingerprint=ref('/rig'), roles=ref('/roles'),
                      geometry=ref('/geometry'), geometry_evidence=ref(''), role_review=ref('/role_review'),
                      reviewer='synthetic reviewer', roles_reviewed=True, policy_reviewed=True,
                      interaction_policy=policy, enabled=False)
    if kind == 'feed':
        annotation['mechanism'] = dict(revision=1, maxAgeNs=100000000, ammunitionKind=1,
            cover=True, latch=False, container=True, charge=False, containerNeedsOpen=True,
            feedNeedsOpen=True, detachNeedsUnseated=False, sameContainerReplenishment=True,
            coupledContainerDetach=True, contacts={name: dict(role=i, radiusMeters=.04, maxAngleRadians=.5)
                                                  for i, name in enumerate(('Cover', 'Container', 'Feed'), 1)})
    annotations = {'schema': exporter.SCHEMA, 'sources': {'fixture': {'path': 'synthetic.json', 'sha256': ''}},
                   'profiles': [annotation]}
    result = [jobs, [review], annotations, {'fixture': (data, '')}]
    repin(result)
    return result


def repin(args):
    document = args[3]['fixture'][0]
    sha = hashlib.sha256(json.dumps(document, sort_keys=True).encode()).hexdigest()
    args[3]['fixture'] = (document, sha)
    args[2]['sources']['fixture']['sha256'] = sha


def admit(args):
    _, receipt = exporter.build(*args)
    row = args[2]['profiles'][0]
    grant = {key: row[key] for key in ('descriptor_key', 'descriptor_digest', 'native_class')}
    grant.update(profile_digest=receipt['rows'][0]['profile_digest'], physical_reviewed=True,
                 native_reviewed=True, reviewer='synthetic independent reviewer',
                 evidence={'source': 'fixture', 'pointer': '/native_review'})
    return {'schema': exporter.ADMISSIONS, 'reviews': [grant]}


class MechanismRegistryTests(unittest.TestCase):
    def rejected(self, args, text):
        _, receipt = exporter.build(*args)
        self.assertFalse(receipt['valid'])
        self.assertEqual(receipt['summary']['emitted_rows'], 0)
        self.assertIn(text, ' '.join(receipt['rows'][0]['missing_prerequisites']))

    def test_authored_candidates_never_grant_admission(self):
        for kind in exporter.CLASSES:
            args = fixture(kind)
            header, receipt = exporter.build(*args)
            self.assertTrue(receipt['valid'])
            self.assertEqual(receipt['summary']['emitted_rows'], 1)
            self.assertEqual(receipt['summary']['enabled_rows'], 0)
            self.assertIn('out.reviewed=false;', header)
            self.assertIn('ReloadDescriptorAdmission::Candidate', header)
            self.assertIn('exact_physical_and_native_mechanism_admission', receipt['rows'][0]['missing_prerequisites'])

    def test_exact_independent_reviews_enable_requested_rows(self):
        for kind in exporter.CLASSES:
            args = fixture(kind)
            grants = admit(args)
            args[2]['profiles'][0]['enabled'] = True
            header, receipt = exporter.build(*args, grants)
            self.assertEqual(receipt['summary']['enabled_rows'], 1)
            self.assertIn('out.reviewed=true;', header)
            self.assertNotEqual(receipt['rows'][0]['mechanism_key'], args[0]['descriptors'][0]['descriptor_digest'][:16])
            self.assertIn('out.profileDigest=', header)

    def test_enablement_without_physical_native_review_is_rejected(self):
        args = fixture(); args[2]['profiles'][0]['enabled'] = True
        self.rejected(args, 'lacks exact physical/native admission')

    def test_native_review_is_independent(self):
        args = fixture(); grants = admit(args); args[1] = []
        with self.assertRaisesRegex(ValueError, 'Unmatched or rejected'):
            exporter.build(*args, grants)

    def test_native_review_cannot_be_borrowed_after_policy_change(self):
        args = fixture(); grants = admit(args)
        args[2]['profiles'][0]['interaction_policy']['strokeMeters'] = .05
        with self.assertRaisesRegex(ValueError, 'Unmatched or rejected'):
            exporter.build(*args, grants)

    def test_wrong_config_hash_guid_or_resource_rejected(self):
        for field, value in (('sha256', '9'*64), ('guid', '99999999-9999-9999-9999-999999999999'),
                             ('resource', 'Fixture/Another.dbx')):
            args = fixture(); args[3]['fixture'][0]['configuration'] = dict(args[3]['fixture'][0]['configuration'])
            args[3]['fixture'][0]['configuration'][field] = value; repin(args)
            self.rejected(args, 'configuration resource/GUID/hash mismatch')

    def test_wrong_native_class_rejected(self):
        args = fixture(); args[2]['profiles'][0]['native_class'] = exporter.CLASSES['feed'][1]
        self.rejected(args, 'Wrong supported native class')

    def test_actual_pistol_source_class_kept(self):
        args = fixture()
        self.assertEqual(args[0]['descriptors'][0]['source_class'], 'wcSmg')
        _, receipt = exporter.build(*args)
        self.assertTrue(receipt['valid'])
        args[0]['descriptors'][0]['source_class'] = 'wcPistol'
        self.rejected(args, 'Wrong supported native class')

    def test_runtime_geometry_contracts_checked_before_emission(self):
        args = fixture(); args[3]['fixture'][0]['geometry']['ring3VertexDistanceMeters'] = .1; repin(args)
        self.rejected(args, 'Slide contact distance disagrees')
        args = fixture('feed'); args[3]['fixture'][0]['geometry']['poses'][2]['weaponFromPouch'] = list(IDENTITY); repin(args)
        args[3]['fixture'][0]['geometry']['poses'][1]['weaponFromPouch'] = list(IDENTITY); repin(args)
        self.rejected(args, 'distinct gesture regions')
        args = fixture('feed'); args[3]['fixture'][0]['geometry']['coverTravelRadians'] = -1.5; repin(args)
        _, receipt = exporter.build(*args)
        self.assertTrue(receipt['valid'])

    def test_cover_companions_emit_and_reject_overlap(self):
        args = fixture('feed')
        companion = dict(bone='syntheticSibling', index=6, parent='syntheticRoot', parentIndex=1,
                         weaponFromReference=list(IDENTITY))
        args[3]['fixture'][0]['geometry']['coverCompanions'] = [companion]; repin(args)
        header, receipt = exporter.build(*args)
        self.assertTrue(receipt['valid']); self.assertIn('out.coverCompanionCount=1;', header)
        self.assertIn('out.coverCompanions[0].bone.index=6;', header)
        companion['index'] = 2; repin(args)
        self.rejected(args, 'Cover companion overlaps')

    def test_missing_and_unknown_roles_rejected(self):
        for change in ('missing', 'unknown'):
            args = fixture(); roles = args[3]['fixture'][0]['roles']
            if change == 'missing': del roles['slide']
            else: roles['bolt'] = copy.deepcopy(roles['slide'])
            repin(args); self.rejected(args, 'roles: missing')

    def test_bad_rigid_transform_rejected(self):
        for value in (float('nan'), float('inf'), 2.0, -1.0):
            args = fixture(); args[3]['fixture'][0]['geometry']['weaponFromStaticSlide'][0] = value
            repin(args); self.rejected(args, 'transform' if math_isfinite(value) else 'Nonfinite')

    def test_out_of_rail_policy_rejected(self):
        args = fixture(); args[2]['profiles'][0]['interaction_policy']['sourceDisplacementMeters'] = .05
        self.rejected(args, 'measured signed rail extrema')

    def test_geometry_anchor_cannot_borrow_another_exact_config(self):
        args = fixture()
        args[3]['fixture'][0]['foreign'] = {'weapon': {**args[0]['descriptors'][0]['identity']['weapon'],
            'sha256': '9'*64}, 'asset': args[3]['fixture'][0]['asset'], 'mesh': args[3]['fixture'][0]['mesh'],
            'rig': args[3]['fixture'][0]['rig'], 'geometry': copy.deepcopy(args[3]['fixture'][0]['geometry'])}
        args[2]['profiles'][0]['geometry']['pointer'] = '/foreign/geometry'
        repin(args); self.rejected(args, 'Geometry/configuration evidence mismatch')

    def test_unknown_measured_native_claim_rejected(self):
        args = fixture(); args[3]['fixture'][0]['geometry']['nativeClosedVerified'] = True
        repin(args); self.rejected(args, 'unknown')

    def test_wrong_source_hash_and_duplicate_paths_rejected(self):
        args = fixture(); args[2]['sources']['fixture']['sha256'] = '0'*64
        with self.assertRaisesRegex(ValueError, 'source hash'): exporter.build(*args)
        args = fixture(); args[2]['sources']['second'] = copy.deepcopy(args[2]['sources']['fixture'])
        args[3]['second'] = args[3]['fixture']
        with self.assertRaisesRegex(ValueError, 'Duplicate evidence path'): exporter.build(*args)

    def test_duplicate_annotation_key_rejected(self):
        args = fixture(); args[2]['profiles'].append(copy.deepcopy(args[2]['profiles'][0]))
        with self.assertRaisesRegex(ValueError, 'Duplicate descriptor annotation key'): exporter.build(*args)

    def test_unreviewed_roles_and_inline_geometry_rejected(self):
        args = fixture(); args[2]['profiles'][0]['roles_reviewed'] = False
        self.rejected(args, 'Missing explicit role/policy review')
        args = fixture(); args[2]['profiles'][0]['geometry'] = args[3]['fixture'][0]['geometry']
        self.rejected(args, 'Measured fields require source/pointer')

    def test_broken_articulated_chain_rejected(self):
        args = fixture('feed'); args[3]['fixture'][0]['roles']['feed_chain'][1]['parentIndex'] = 99
        repin(args); self.rejected(args, 'Broken articulated feed chain')

    def test_unsupported_feed_semantics_rejected(self):
        args = fixture('feed'); args[2]['profiles'][0]['mechanism']['charge'] = True
        self.rejected(args, 'Unsupported feed sequence')

    def test_split_headers_have_only_own_family(self):
        for kind, own, other in (('feed', 'GeneratedFeedProfiles', 'GeneratedPistolSlideProfiles'),
                                  ('pistol_slide', 'GeneratedPistolSlideProfiles', 'GeneratedFeedProfiles')):
            header, receipt = exporter.build(*fixture(kind), header_kind=kind)
            self.assertTrue(receipt['valid']); self.assertIn(own, header); self.assertNotIn(other, header)

    def test_json_pointer_escaping_and_missing_values(self):
        self.assertEqual(exporter.pointer({'a/b': {'~x': [42]}}, '/a~1b/~0x/0'), 42)
        with self.assertRaisesRegex(ValueError, 'escape'): exporter.pointer({}, '/~2')
        args = fixture(); args[2]['profiles'][0]['configuration']['pointer'] = '/missing'
        self.rejected(args, 'Missing evidence pointer')


def math_isfinite(value):
    return value == value and abs(value) != float('inf')


if __name__ == '__main__':
    unittest.main()
