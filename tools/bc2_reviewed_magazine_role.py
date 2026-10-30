"""Explicit reviewed magazine role and shape policy, independent of grasp evidence.

Only the generic box-shape exclusion may be replaced by a bounded reviewed
envelope on the exact measured rigid part. The existing authored contact sampler
still qualifies the selected part. This module does not supply a hand anchor or
turn a semantic review into authored contact, native or controller proof.
"""
from __future__ import annotations
import math
from bc2_authored_grip_bindings import checked_hash, digest, path_key

SCHEMA = 'fvr.bc2.reviewed-magazine-role.v1'
SHAPE_REASON = 'outside explicit box-magazine shape candidate bounds'
FALSE_CLAIMS = ('stable_authored_contact_verified', 'native_role_verified',
                'stable_controller_grasp_verified', 'runtime_admitted', 'headset_tested')


def validate_review(review):
    if (review.get('schema') != SCHEMA or
            review.get('review_digest') != digest({k: v for k, v in review.items() if k != 'review_digest'}) or
            review.get('semantic_role') != 'detachable_magazine' or
            review.get('role_reviewed') is not True or
            any(review.get(k) is not False for k in FALSE_CLAIMS)):
        raise ValueError('Explicit semantic magazine review with false qualification claims required')
    for key in ('reviewer', 'review_basis', 'native_asset_name', 'magazine_bone', 'rig_fingerprint'):
        if not isinstance(review.get(key), str) or not review[key] or '\0' in review[key]:
            raise ValueError('Reviewed magazine role identity or basis missing')
    for key in ('binding_digest', 'mesh_sha256', 'lod_sha256', 'skeleton_sha256',
                'static_clip_sha256', 'clip_sha256', 'part_digest', 'visual_review_sha256'):
        checked_hash(review.get(key))
    checked_hash(review.get('weapon', {}).get('sha256'))
    for value in (review['weapon'].get('resource'), review.get('configured_mesh_path'), review.get('clip_resource')):
        path_key(value)
    if 'assembly_digest' in review:
        checked_hash(review['assembly_digest'])
    envelope = review.get('shape_envelope', {})
    keys = ('minimum_width_m', 'maximum_width_m', 'minimum_length_m', 'maximum_length_m')
    if set(envelope) != set(keys) or any(isinstance(envelope[k], bool) or
            not isinstance(envelope[k], (int, float)) or not math.isfinite(envelope[k]) for k in keys):
        raise ValueError('Reviewed magazine shape envelope must be finite and explicit')
    if (not .005 <= envelope['minimum_width_m'] <= envelope['maximum_width_m'] <= .1 or
            not .06 <= envelope['minimum_length_m'] <= envelope['maximum_length_m'] <= .4):
        raise ValueError('Reviewed magazine shape envelope exceeds bounded policy')
    return review


def index_reviews(reviews, assets):
    if len(reviews) > 128:
        raise ValueError('Magazine role review count bound')
    result = {}
    for review in reviews:
        validate_review(review)
        key = review['weapon']['resource']
        if key in result or review['native_asset_name'] not in assets:
            raise ValueError('Duplicate or unselected magazine role review')
        result[key] = review
    return result


def context(profile, clip, skeleton, clip_resource, mesh_sha, lod_sha, static_sha):
    """Exact identity fields for preparing a separate human/agent review."""
    return dict(native_asset_name=profile['native_asset_name'], weapon=profile['weapon'],
                configured_mesh_path=profile['configured_mesh_path'], binding_digest=profile['binding_digest'],
                mesh_sha256=mesh_sha, lod_sha256=lod_sha, skeleton_sha256=skeleton.sha256,
                rig_fingerprint=skeleton.fingerprint, static_clip_sha256=static_sha,
                clip_resource=clip_resource, clip_sha256=clip.sha256)


def verify_context(review, *args):
    validate_review(review)
    if any(review[k] != v for k, v in context(*args).items()):
        raise ValueError('Reviewed magazine role exact authored identity differs')


def part_digest(part):
    """Bind decoded vertices and rigid skin/index ownership, not just a box."""
    return digest({key: part[key] for key in ('points', 'sections', 'triangles', 'mixed_triangles')})


def shape_matches(review, extent, triangles):
    if (not isinstance(extent, list) or len(extent) != 3 or
            any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v <= 0 for v in extent) or
            type(triangles) is not int or triangles < 24):
        raise ValueError('Reviewed magazine extent/triangle evidence invalid')
    small, _, large = sorted(extent)
    envelope = review['shape_envelope']
    if (not envelope['minimum_width_m'] <= small <= envelope['maximum_width_m'] or
            not envelope['minimum_length_m'] <= large <= envelope['maximum_length_m'] or large <= 2 * small):
        raise ValueError('Exact magazine part is outside reviewed shape envelope')


def select_role(review, candidates, rejected, parts, assemblies):
    """Preserve every original non-shape rejection and all contact criteria."""
    import bc2_authored_magazine_geometry as g
    validate_review(review)
    bone = review['magazine_bone']
    matched = [row for row in [*candidates, *rejected] if row['bone'] == bone]
    if len(matched) != 1:
        raise ValueError('Reviewed magazine root absent or ambiguous')
    role = matched[0]
    if set(role.get('reasons', [])) - {SHAPE_REASON}:
        raise ValueError('Reviewed magazine role cannot override structural or motion rejection')
    part = parts[bone]
    if part['mixed_triangles'] or not 1 <= len(part['points']) <= 100000:
        raise ValueError('Reviewed magazine must have bounded fully rigid geometry')
    lo, hi = g.bounds(part['points'])
    extent = [b - a for a, b in zip(lo, hi)]
    shape_matches(review, extent, part['triangles'])
    if review['part_digest'] != part_digest(part):
        raise ValueError('Reviewed magazine exact measured surface changed')
    assembly = assemblies.get(bone)
    if review.get('assembly_digest') != (assembly['assembly_digest'] if assembly else None):
        raise ValueError('Reviewed magazine assembly changed')
    if role['extent_m'] != extent or role['triangles'] != part['triangles']:
        raise ValueError('Reviewed magazine role/part geometry differs')
    return dict({k: v for k, v in role.items() if k != 'reasons'},
                role='reviewed_semantic_detachable_magazine_candidate',
                ambiguity_resolution='exact_reviewed_semantic_root_and_shape',
                original_shape_rejection=role.get('reasons', []),
                measured_part_digest=part_digest(part),
                semantic_role_review=review)


def validate_export(profile):
    """Review selects the part; the ordinary export checks still prove the pair."""
    role = profile['role_evidence']
    review = validate_review(role['semantic_role_review'])
    expected = dict(native_asset_name=profile['native_asset_name'], weapon=profile['weapon'],
                    configured_mesh_path=profile['configured_mesh_path'], binding_digest=profile['grip_binding_digest'],
                    mesh_sha256=profile['mesh_sha256'], lod_sha256=profile['lod_sha256'],
                    skeleton_sha256=profile['skeleton_sha256'], rig_fingerprint=profile['rig_fingerprint'],
                    static_clip_sha256=profile['static_clip_sha256'], clip_resource=profile['reload_clip']['resource'],
                    clip_sha256=profile['reload_clip']['sha256'])
    if (any(review[k] != v for k, v in expected.items()) or
            review['magazine_bone'] != profile['bones']['magazine'] or
            review['magazine_bone'] != role['bone'] or
            review.get('assembly_digest') != profile.get('assembly', {}).get('assembly_digest') or
            role.get('measured_part_digest') != review['part_digest'] or
            set(role.get('original_shape_rejection', [])) - {SHAPE_REASON}):
        raise ValueError('Exported magazine role differs from exact reviewed source')
    measured = profile['geometry']['measured']
    if measured['bone_local_extent_m'] != role['extent_m'] or measured['part_triangle_count'] != role['triangles']:
        raise ValueError('Exported magazine measured extent/triangle evidence changed')
    shape_matches(review, measured['bone_local_extent_m'], measured['part_triangle_count'])
    if profile['geometry']['design'].get('authored_reload_grasp') is not True:
        raise ValueError('Reviewed semantic role still requires unchanged paired authored contact')
    return review
