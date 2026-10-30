"""Prepare one exact magazine enrollment batch; root executes installed reads.

No runtime source, installation, native capture, or global archive limits change.
All output is private preparation data. A nonzero exit retains precise residuals.
"""
from __future__ import annotations
import argparse
import contextlib
import copy
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
import bc2_authored_grip_bindings as grip
import bc2_authored_magazine_geometry as geometry
import bc2_body_ammo_assets as ammo
import bc2_configured_body_assets as body
import bc2_magazine_assembly_cache as assembly
import bc2_magazine_registry_header as registry
import bc2_resource_enrollment as enrollment
import bc2_reviewed_magazine_contact as contact
import bc2_visibility_descriptors as visibility
from bc2_asset_resource_identity import profiles_match
from bc2_magazine_contact_batch import ArchivePool, readers
from bc2_weapon_animation_pipeline import Skeleton, Clip, canonical
from inspect_bc2_mesh_asset import bone_hash


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def read_pin(pin, limit=64 * 1024 * 1024):
    path = Path(pin['path'])
    if not path.is_absolute():
        path = ROOT / path
    require(path.stat().st_size <= limit, 'Pinned input size bound: ' + str(path))
    raw = path.read_bytes()
    require(sha(raw) == pin['sha256'], 'Pinned input changed: ' + str(path))
    return raw


def write_json(directory, name, value):
    (directory / name).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def one(rows, predicate, message):
    matches = [r for r in rows if predicate(r)]
    require(len(matches) == 1, message)
    return matches[0]


def checked_digest(row, field):
    require(row[field] == grip.digest({k: v for k, v in row.items() if k != field}), 'Content digest differs: ' + field)


@contextlib.contextmanager
def shared_readers(pool):
    previous = visibility.Archive
    try:
        visibility.Archive = pool
        with readers(pool):
            yield
    finally:
        visibility.Archive = previous


def validate_inputs(plan, docs):
    assets = set(plan['assets'])
    require(len(assets) == len(plan['assets']) and len(assets) <= 64, 'Unique bounded assets required')
    by_asset = {}
    for pin in plan['configurations']:
        asset = pin['asset']
        binding = one(docs['bindings']['profiles'], lambda p: p['native_asset_name'] == asset, 'Exact binding: ' + asset)
        checked_digest(binding, 'binding_digest')
        weapon = binding['weapon']
        require((weapon['resource'], weapon['instance_guid'].lower(), weapon['sha256']) ==
                (pin['configuration_resource'], pin['configuration_guid'].lower(), pin['configuration_sha256']), 'Configuration pin: ' + asset)
        job = one(docs['jobs']['descriptors'], lambda j: j['identity']['assetName'] == asset, 'Exact job: ' + asset)
        identity = job['identity']['weapon']
        require((identity['resource'], identity['guid'].lower(), identity['sha256']) ==
                (weapon['resource'], weapon['instance_guid'].lower(), weapon['sha256']), 'Job/configuration identity: ' + asset)
        configured = one(docs['configured']['resolved_weapons'], lambda p: p['native_name'] == asset, 'Exact configured resource: ' + asset)
        require((configured['resource'], configured['instance_guid'].lower(), configured['resource_sha256']) ==
                (weapon['resource'], weapon['instance_guid'].lower(), weapon['sha256']), 'Configured resource identity: ' + asset)
        one(docs['meshes']['weapons'], lambda p: geometry.mesh_weapon_matches(p, binding), 'Exact mesh binding: ' + asset)
        by_asset[asset] = binding
    require(set(by_asset) == assets, 'Requested configuration set differs')
    require(len(docs['catalog']['profiles']) == plan['preserve_catalog_rows'], 'Baseline catalog row count changed')
    require(len(docs['jobs']['descriptors']) == len(assets), 'Jobs include additional assets')
    return by_asset


def joined_contact_reviews(plan, docs, bindings):
    """Verify an explicit cross-configuration content join, never rename a review."""
    reviews = copy.deepcopy(docs['contacts'])
    receipts = []
    for join in plan['contact_content_joins']:
        source = bindings[join['source_asset']]
        target = bindings[join['target_asset']]
        original = one(reviews, lambda r: r['review_digest'] == join['source_review_digest'], 'Source contact review absent')
        contact.validate_review(original)
        require(original['weapon'] == source['weapon'] and original['binding_digest'] == source['binding_digest'], 'Source contact binding differs')
        require(source['binding_digest'] == join['source_binding_digest'] and target['binding_digest'] == join['target_binding_digest'], 'Contact join binding pins changed')
        for field in ('meshes', 'configured_mesh_path', 'animation_chain', 'animation_sha256', 'skeleton_sha256', 'rig_fingerprint', 'rig_topology'):
            require(source[field] == target[field], 'Contact content join differs: ' + field)
        proof = one(docs['existing_geometry']['profiles'], lambda p: p['native_asset_name'] == source['native_asset_name'], 'Source geometry absent')
        checked_digest(proof, 'profile_digest')
        require(proof['profile_digest'] == join['source_geometry_digest'], 'Source geometry pin differs')
        for field, expected in join['equal_content'].items():
            observed = proof['reload_clip']['sha256'] if field == 'reload_clip_sha256' else proof[field]
            require(observed == expected, 'Source geometry content differs: ' + field)
        reviewed = copy.deepcopy(join['target_review'])
        contact.validate_review(reviewed)
        require(reviewed['weapon'] == target['weapon'] and reviewed['binding_digest'] == target['binding_digest'] and reviewed['native_asset_name'] == target['native_asset_name'], 'Target contact identity differs')
        for field in ('mesh_sha256', 'lod_sha256', 'skeleton_sha256', 'rig_fingerprint', 'clip_resource', 'clip_sha256', 'magazine_bone', 'frame_seconds', 'pose_digest', 'render_receipt_sha256', 'contact_sheet_sha256'):
            require(reviewed[field] == original[field], 'Joined contact changes reviewed content: ' + field)
        require(reviewed['content_identity_join'] == {k: v for k, v in join.items() if k != 'target_review'}, 'Target contact provenance differs')
        reviews.append(reviewed)
        receipts.append({**reviewed['content_identity_join'], 'target_review_digest': reviewed['review_digest'],
                         'validation': 'exact cached content join; installed clip and same-frame pose rechecked by shared geometry derivation',
                         'new_visual_review': False, 'new_native_evidence': False})
    return reviews, receipts


def source_pin(archive, index, resource, digest):
    return dict(archive=archive, archive_index_sha256=index, resource=resource, sha256=digest)


def cache_sources(profile, binding, meshes):
    selected = one(binding['meshes'], lambda m: m['configured_mesh_path'] == profile['configured_mesh_path'], 'Selected mesh ambiguous')
    matches = [m for w in meshes['weapons'] if geometry.mesh_weapon_matches(w, binding)
               for s in w['states'] if s['index'] == binding['state_index']
               for m in s['meshes'] if m.get('mesh_resource') == selected['mesh_resource']]
    mesh = one(matches, lambda m: True, 'Exact cache mesh source absent')
    source = min((s for s in mesh['geometry_sources'] if s['geometry_status'] == 'derived'), key=lambda s: s['archive'])
    lod = one(selected['geometry']['lods'], lambda l: l['lod'] == 0, 'Exact LOD0 absent')
    skeleton, static, reload = binding['skeleton_source'], binding['animation_source'], profile['reload_clip']
    return dict(profile=profile,
        mesh=source_pin(source['archive'], source['index_sha256'], selected['mesh_resource'], profile['mesh_sha256']),
        lod=source_pin(source['archive'], source['index_sha256'], lod['data_resource'], profile['lod_sha256']),
        skeleton=source_pin(skeleton['archive'], skeleton['archive_index_sha256'], binding['animation_chain']['skeleton_resource'], profile['skeleton_sha256']),
        static=source_pin(static['archive'], static['archive_index_sha256'], binding['animation_chain']['animation_resource'], profile['static_clip_sha256']),
        reload=source_pin(reload['archive'], reload['archive_index_sha256'], reload['resource'], reload['sha256']))


def load_resource(game, pool, pin):
    archive = pool(game / pin['archive'])
    require(archive.index_sha256 == pin['archive_index_sha256'], 'Source archive changed: ' + pin['archive'])
    raw = archive.read_selected([pin['resource']])[pin['resource']]
    require(sha(raw) == pin['sha256'], 'Source resource changed: ' + pin['resource'])
    return raw


def run(args, plan, output, receipt):
    docs = {name: json.loads(read_pin(pin)) for name, pin in plan['sources'].items() if name != 'trace'}
    read_pin(plan['sources']['trace'])
    bindings = validate_inputs(plan, docs)
    require(docs['trace_evidence']['native_rig_proof'] == plan['sources']['trace'], 'Saved trace provenance differs')
    require(docs['native_review_evidence']['descriptor_jobs_sha256'] == plan['sources']['full_jobs']['sha256'], 'Native review jobs evidence differs')
    for job in docs['jobs']['descriptors']:
        require(job == one(docs['full_jobs']['descriptors'], lambda j: j['key'] == job['key'], 'Consolidated exact job absent'), 'Selected job differs from reviewed consolidated job')
    contacts, joins = joined_contact_reviews(plan, docs, bindings)
    write_json(output, 'contact-content-joins.json', joins)
    write_json(output, 'exact-contact-reviews.json', contacts)
    jobs = docs['jobs']
    reviews = [one(docs['native_reviews'], lambda r: r['key'] == job['key'] and r['descriptor_digest'] == job['descriptor_digest'], 'Exact native review missing') for job in jobs['descriptors']]
    require(all(r['evidence_sha256'] == plan['sources']['native_review_evidence']['sha256'] for r in reviews), 'Native review evidence differs')
    registry.build(jobs, reviews)
    write_json(output, 'exact-native-reviews.json', reviews)
    write_json(output, 'selected-jobs.json', jobs)

    configured = docs['configured']
    pool = ArchivePool(args.game, configured['archive'], configured['index_sha256'])
    assets = set(plan['assets'])
    preserved = {p['native_asset_name']: p for p in docs['existing_geometry']['profiles'] if p['native_asset_name'] in plan['preserve_geometry_assets']}
    for asset, profile in preserved.items():
        checked_digest(profile, 'profile_digest')
        require(profile['grip_binding_digest'] == bindings[asset]['binding_digest'], 'Preserved geometry binding differs')
    require({a: p['profile_digest'] for a, p in preserved.items()} == plan['preserve_geometry_digests'], 'Preserved geometry digest differs')
    measured = copy.deepcopy(docs.get('prior_geometry', {'schema': 'fvr.bc2.authored_magazine_geometry', 'schema_version': 1, 'profiles': [], 'gaps': [], 'runtime_admission': False}))
    if args.reuse_geometry:
        require(args.reuse_geometry_sha256 is not None, 'Reused geometry requires explicit SHA256')
        measured = json.loads(read_pin({'path': str(args.reuse_geometry.resolve()), 'sha256': args.reuse_geometry_sha256}))
    for profile in measured['profiles']:
        checked_digest(profile, 'profile_digest')
        require(profile['native_asset_name'] in assets, 'Reused geometry includes unrequested asset')
        require(profile['weapon'] == bindings[profile['native_asset_name']]['weapon'] and profile['grip_binding_digest'] == bindings[profile['native_asset_name']]['binding_digest'], 'Reused geometry identity differs')
    current = {p['native_asset_name']: p for p in measured['profiles']}
    current.update(preserved)
    remaining = assets - set(current)
    unchanged = {g['native_asset_name'] for g in measured['gaps']} - {j['target_asset'] for j in plan['contact_content_joins']}
    for gap in measured['gaps']:
        if gap['native_asset_name'] in remaining & unchanged:
            receipt['residuals'].append({'stage': 'geometry', **gap, 'unchanged_failure_not_reexecuted': True,
                                        'needed_contract': plan['residual_contracts'].get(gap['native_asset_name'])})
    remaining -= unchanged
    with shared_readers(pool):
        if remaining:
            fresh = geometry.derive(args.game, docs['bindings'], docs['meshes'], remaining,
                paired_grasp_assets=remaining, rigid_assembly_assets=remaining, sampled_path_assets=remaining,
                reviewed_contacts=[r for r in contacts if r['native_asset_name'] in remaining])
            write_json(output, 'fresh-geometry-candidates.json', fresh)
            current.update({p['native_asset_name']: p for p in fresh['profiles']})
            for gap in fresh['gaps']:
                receipt['residuals'].append({'stage': 'geometry', **gap})
        measured = {**measured, 'profiles': [current[a] for a in plan['assets'] if a in current],
                    'gaps': [r for r in receipt['residuals'] if r['stage'] == 'geometry']}
        write_json(output, 'magazine-geometry.json', measured)
        specs, excluded = visibility.specs_from_configuration(args.game, configured,
            classes=tuple(sorted({w['weapon_class'] for w in configured['resolved_weapons']})), inventory=docs['inventory'])
        # Match the exact binding representation used by preserved body rows.
        # Visibility and newly composed bodies then carry the same full identity.
        for spec in specs:
            configuration = spec['authored_configuration']
            weapon = bindings[spec['asset']]['weapon']
            require(configuration['instance_guid'].lower() == weapon['instance_guid'].lower(),
                    'Visibility/binding configuration GUID differs')
            configuration['instance_guid'] = weapon['instance_guid']
            configuration['type'] = weapon['type']
        vis = visibility.derive(args.game, specs, Path(plan['sources']['trace']['path']))
        vis['configuration_excluded'] = excluded
        write_json(output, 'visibility-specs.json', specs)
        write_json(output, 'visibility.json', vis)
        for gap in excluded + vis['excluded']:
            receipt['residuals'].append({'stage': 'visibility', **gap})
        require(vis['native_rig_proof']['sha256'] == plan['sources']['trace']['sha256'], 'Saved native trace changed during extraction')
        require({r['asset'] for r in vis['rows']} == assets and not excluded and not vis['excluded'], 'Exact complete visibility set unresolved')
        (output / 'Visibility.exact5.generated.h').write_text(visibility.header(vis), encoding='utf-8')
        missing_vis = {**vis, 'rows': [r for r in vis['rows'] if r['asset'] in plan['missing_visibility_assets']]}
        (output / 'Visibility.additions.generated.h').write_text(visibility.header(missing_vis), encoding='utf-8')
        usable = set(current)
        require(usable, 'No exact usable geometry')
        (output / 'Geometry.exact.generated.h').write_text(geometry.cpp_header(measured, usable), encoding='utf-8')
        if usable - set(preserved):
            (output / 'Geometry.additions.generated.h').write_text(geometry.cpp_header(measured, usable - set(preserved)), encoding='utf-8')
        added_rows, added_records = [], []
        append_assets = [a for a in plan['append_assets'] if a in usable]
        for asset in append_assets:
            profile, binding = current[asset], bindings[asset]
            source = cache_sources(profile, binding, docs['meshes'])
            blobs = {name: load_resource(args.game, pool, pin) for name, pin in source.items() if name != 'profile'}
            skeleton = Skeleton(blobs['skeleton'])
            row, packed = assembly.derive_cache(blobs['mesh'], blobs['lod'], skeleton, Clip(blobs['static']), Clip(blobs['reload']), profile)
            row['assembly_source'] = source
            added_rows.append(row)
            added_records.append(packed[12:])
            spec = one(specs, lambda s: s['asset'] == asset, 'Exact visibility spec absent')
            known = {bone_hash(n): n for n in skeleton.names}
            require(len(known) == len(skeleton.names), 'Skeleton hash collision')
            inverse = {n: canonical(b['InverseWorldTransform']) for n, b in zip(skeleton.names, skeleton.bones)}
            row, record = body.composite(args.game, pool, spec, binding, profile, skeleton, known, inverse)
            row.update(configuration_archive=configured['archive'], configuration_archive_index_sha256=configured['index_sha256'],
                       closed_archive=binding['animation_source']['archive'], closed_archive_index_sha256=binding['animation_source']['archive_index_sha256'])
            added_rows.append(row)
            added_records.append(record)
        receipt['selected_resource_cache'] = {'archives': len(pool.archives), 'bytes': pool.bytes, 'limit_bytes': 256 * 1024 * 1024}
    # Release the bounded selected-resource cache before rebuilding the baseline.
    del pool
    baseline = docs['catalog']['profiles']
    rows = baseline + added_rows
    require(len(rows) <= 64 and len(rows) == len(baseline) + 2 * len(append_assets), 'Catalog count bound')
    require(len({(r['asset'], r['mesh'], r['part']) for r in rows}) == len(rows), 'Duplicate catalog asset/mesh/part')
    catalog = {**docs['catalog'], 'profiles': rows}
    write_json(output, 'body-ammo-assets.candidate.json', catalog)
    subset = {**catalog, 'profiles': [r for r in rows if r['asset'] in usable]}
    write_json(output, 'exact-catalog.json', subset)
    usable_jobs = {**jobs, 'descriptors': [j for j in jobs['descriptors'] if j['identity']['assetName'] in usable]}
    usable_keys = {j['key'] for j in usable_jobs['descriptors']}
    usable_reviews = [r for r in reviews if r['key'] in usable_keys]
    usable_vis = {**vis, 'rows': [r for r in vis['rows'] if r['asset'] in usable]}
    joined, exact_header = enrollment.join(usable_jobs, usable_reviews, measured, usable_vis, subset)
    joined['preparation_scope'] = {'requested_assets': plan['assets'], 'prepared_assets': sorted(usable),
                                 'residuals': receipt['residuals'], 'new_native_or_headset_acceptance': False}
    write_json(output, 'enrollment.json', joined)
    new_jobs = {**jobs, 'descriptors': [j for j in jobs['descriptors'] if j['identity']['assetName'] in append_assets]}
    new_keys = {j['key'] for j in new_jobs['descriptors']}
    new_header, _ = registry.build(new_jobs, [r for r in reviews if r['key'] in new_keys])
    (output / 'Registry.exact.generated.h').write_text(exact_header, encoding='utf-8')
    (output / 'Registry.additions.generated.h').write_text(new_header, encoding='utf-8')
    (output / 'BodyAmmoAssetData.generated.h').write_text(ammo.catalog_header(rows), encoding='utf-8')
    rebuilt_rows, baseline_payload = body.derive_catalog(args.game, baseline, resource_content=True)
    require(profiles_match(rebuilt_rows, baseline, True), 'Baseline resource-content identities differ')
    compacted = body.compact_cache_sections(baseline_payload, baseline)
    payload = ammo.MAGIC + struct.pack('<I', len(rows)) + compacted[12:] + b''.join(added_records)
    receipt['cache'] = {'preserved_rows': len(baseline), 'appended_rows': len(added_rows), 'total_rows': len(rows),
        'baseline_bytes': len(baseline_payload), 'baseline_sha256': sha(baseline_payload),
        'lossless_compacted_baseline_bytes': len(compacted), 'added_record_bytes': sum(map(len, added_records)),
        'candidate_bytes': len(payload), 'maximum_bytes': ammo.MAX_CACHE, 'maximum_rows': 64,
        'indexed_hashes_and_all_metadata_preserved': True, 'remaining_bytes': ammo.MAX_CACHE - len(payload)}
    require(len(payload) <= ammo.MAX_CACHE, 'Cache bound: {} bytes exceeds {} by {}'.format(len(payload), ammo.MAX_CACHE, len(payload) - ammo.MAX_CACHE))
    require(rows[:len(baseline)] == baseline, 'Baseline catalog metadata modified')
    (output / 'body-ammo.fvrprop').write_bytes(payload)
    write_json(output, 'body-ammo-assets.json', catalog)
    receipt['cache']['sha256'] = sha(payload)
    receipt['prepared_assets'] = sorted(usable)
    receipt['prepared_append_assets'] = append_assets
    receipt['requested_assets'] = plan['assets']
    receipt['complete'] = usable == assets and not receipt['residuals']
    receipt['prepared_outputs_usable'] = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--review-input', type=Path, default=Path(__file__).with_name('review-input.json'))
    parser.add_argument('--reuse-geometry', type=Path)
    parser.add_argument('--reuse-geometry-sha256')
    args = parser.parse_args()
    args.game = args.game.resolve()
    output = args.output.resolve()
    require(not output.is_relative_to(args.game), 'Output must not be inside installation')
    require(not output.exists() or not any(output.iterdir()), 'Use an empty output directory to preserve earlier failures')
    output.mkdir(parents=True, exist_ok=True)
    plan = json.loads(args.review_input.read_text(encoding='utf-8'))
    receipt = {'schema': 'fvr.bc2.magazine-enrollment-preparation.v1', 'complete': False,
               'evidence_level': 'exact cached review joins and installed authored content extraction',
               'native_execution': False, 'new_native_capture': False, 'headset_acceptance': False,
               'installation_changed': False, 'residuals': [],
               'review_input_sha256': sha(args.review_input.read_bytes()), 'script_sha256': sha(Path(__file__).read_bytes())}
    try:
        run(args, plan, output, receipt)
    except (ValueError, KeyError, TypeError, OSError, IndexError) as error:
        receipt['residuals'].append({'stage': 'batch', 'exception': type(error).__name__, 'reason': str(error)})
    write_json(output, 'batch-receipt.json', receipt)
    print(json.dumps(receipt, allow_nan=False))
    return 0 if receipt['complete'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
