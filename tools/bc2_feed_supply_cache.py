"""Append exact belt-feed pouch recipes and private geometry to a pinned cache.

Planning uses reviewed metadata only. Extraction reads five selected mesh/LOD
pairs and the pinned skeleton through the existing bounded archive pool. It never
launches BC2 or writes installed files; the private cache must not enter Git.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import struct

import bc2_body_ammo_assets as ammo
import bc2_configured_body_assets as body
import bc2_firearm_cache_completion as completion
from bc2_magazine_contact_batch import ArchivePool

SCHEMA = 'fvr.bc2.feed_supply_cache_plan.v1'
BASELINE_SHA = 'd0135f2f44f31322ac5e3971b82b26d665c22c48d9b6a39a04dd489f95b396aa'
BASELINE_ROWS = 252
MAX_ADDITIONS = 24
MAX_ROWS = BASELINE_ROWS + MAX_ADDITIONS
MAX_BYTES = ammo.MAX_CACHE
ROOT = Path(__file__).resolve().parents[1]
REVIEW = 'handoff/tasks/CHAT14/20261030-batch37/roles-geometry-policy.json'
SAMPLES = 'handoff/tasks/CHAT14/20261030-batch37/synchronized-samples.json'
GENERATED = 'profiles/mechanism-family41/Feed.generated.h'
BUILTIN = 'src/games/bc2/Bc2FeedEquipmentProfile.h'
BASELINE_CATALOG = 'reports/coordinator-20261030-cache48-enrollment01/body-ammo-assets.json'

require = completion.require
read = completion.read
write = completion.write
digest = completion.digest
cache_key = completion.cache_key


def pin(path, root):
    path = Path(path).resolve()
    return dict(path=path.relative_to(root.resolve()).as_posix(),
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def read_pin(root, ref):
    path = (root/ref['path']).resolve()
    require(path.is_relative_to(root.resolve()), 'Source pin outside worktree')
    raw = path.read_bytes()
    require(hashlib.sha256(raw).hexdigest() == ref['sha256'], 'Source pin changed: '+ref['path'])
    return raw


def registry_geometry(generated, builtin):
    """Read only explicit geometry initializers; fail closed on missing fields."""
    rows = []
    blocks = re.findall(r'inline constexpr Bc2FeedGeometry \w+=\[\]\{(.*?)return out;\s*\}\(\);', generated, re.S)
    for block in blocks:
        row = {}
        for field in ('asset', 'configurationPath', 'meshPath', 'meshSha256', 'lodSha256',
                      'skeletonSha256', 'pouchBone.bone'):
            values = re.findall(r'out\.'+re.escape(field)+r'="([^"]+)";', block)
            require(len(values) == 1, 'Missing/ambiguous generated feed field: '+field)
            row[field] = values[0]
        rigs = re.findall(r'out\.rigFingerprint=(\d+)ull;', block)
        require(len(rigs) == 1, 'Missing generated feed rig')
        row['rigFingerprint'] = int(rigs[0])
        rows.append(row)
    match = re.search(r'inline const Bc2M60FeedGeometry M60Geometry\{(.*?)\n\};', builtin, re.S)
    require(match is not None, 'Builtin M60 geometry missing')
    block = match.group(1)
    strings = re.findall(r'"([^"]*)"', block)
    rig = re.search(r',0x([0-9a-f]+)ull,', block)
    roles = re.search(r'\{"([^"]+)","",\d+,\d+\},\{"([^"]+)","[^"]+",\d+,\d+\}', block)
    require(len(strings) >= 7 and rig and roles, 'Builtin M60 geometry layout changed')
    rows.append(dict(zip(('asset', 'configurationPath', 'meshPath', 'meshSha256',
                          'lodSha256', 'skeletonSha256'), strings[:6]),
                     rigFingerprint=int(rig[1], 16), **{'pouchBone.bone': roles[2]}))
    require(len(rows) == 24 and len({r['configurationPath'] for r in rows}) == 24,
            'Expected24 exact registered feed configurations')
    return rows


def jobs_from_review(review, samples, registrations):
    require(review['schema'] == 'fvr.bc2.belt_family_measured_review.v1', 'Feed role review schema')
    configs = review['configurations']
    require(len(configs) == len(registrations) == 24, 'Feed configuration count')
    registered = {r['configurationPath']: r for r in registrations}
    require(len(registered) == 24, 'Duplicate feed registration')
    seen_configs = set()
    jobs = {}
    joins = []
    for config in configs:
        path = config['configuration']['resource']
        require(path.endswith('.dbx') and path[:-4] not in seen_configs, 'Duplicate/invalid feed configuration')
        seen_configs.add(path[:-4])
        require(path[:-4] in registered and config['roles_reviewed'] and config['policy_reviewed'],
                'Unreviewed/unregistered feed configuration')
        group = review['groups'][config['content_group']]
        sample = samples['groups'][config['content_group']]
        content = group['content']; pouch = group['roles']['pouch']['bone']
        require(content == sample['content'] and group['roles'] == sample['roles'], 'Feed source join differs')
        require(content['skeletonSha256'] == ammo.SKELETON_SHA, 'Feed skeleton differs')
        rig = int(config['rig_fingerprint'].split(':')[1], 16)
        require(config['rig_fingerprint'] == content['rigFingerprint'], 'Feed rig join differs')
        expected = dict(asset=config['asset'], configurationPath=path[:-4], meshPath=config['configured_mesh'],
                        meshSha256=content['meshSha256'], lodSha256=content['lodSha256'],
                        skeletonSha256=content['skeletonSha256'], rigFingerprint=rig,
                        **{'pouchBone.bone': pouch})
        require(registered[path[:-4]] == expected, 'Runtime/review feed geometry differs: '+path)
        source = sample['source']
        require(source['mesh_sha256'] == content['meshSha256'] and source['lod_sha256'] == content['lodSha256'],
                'Sample mesh content differs')
        require(any(pouch in s['bone_names'] and not s['name'].endswith('_ZOnly')
                    for s in source['skin_sections']), 'Pouch has no weighted surface')
        job = dict(asset=config['asset'], mesh=config['configured_mesh'], part=pouch, rig_fingerprint=rig,
                   archive=source['mesh_source']['archive'], archive_index_sha256=source['mesh_source']['archive_index_sha256'],
                   mesh_sha256=content['meshSha256'], lod_sha256=content['lodSha256'])
        key = cache_key(job)
        require(key not in jobs or jobs[key] == job, 'Conflicting content for exact feed cache key')
        jobs[key] = job
        joins.append(dict(configuration=copy.deepcopy(config['configuration']), cache_key=list(key)))
    require(seen_configs == set(registered), 'Feed configuration closure differs')
    return list(jobs.values()), joins


def prepare(root, baseline_cache, baseline_catalog=None):
    root = root.resolve()
    paths = dict(review=root/REVIEW, samples=root/SAMPLES, generated=root/GENERATED,
                 builtin=root/BUILTIN, baseline=baseline_catalog or root/BASELINE_CATALOG)
    sources = {name: pin(path, root) for name, path in paths.items()}
    rows = read(paths['baseline'])['profiles']
    payload = baseline_cache.read_bytes()
    require(len(rows) == BASELINE_ROWS and hashlib.sha256(payload).hexdigest() == BASELINE_SHA,
            'Reviewed252 baseline cache changed')
    completion.record_slices(payload, rows)
    require(len({cache_key(row) for row in rows}) == len(rows), 'Duplicate baseline cache key')
    registrations = registry_geometry(paths['generated'].read_text(encoding='utf-8'), paths['builtin'].read_text(encoding='utf-8'))
    jobs, joins = jobs_from_review(read(paths['review']), read(paths['samples']), registrations)
    existing = {cache_key(row) for row in rows}
    require(not any(cache_key(job) in existing for job in jobs), 'Baseline unexpectedly contains feed pouch')
    require(0 < len(jobs) <= MAX_ADDITIONS, 'Feed supply addition bound')
    plan = dict(schema=SCHEMA, sources=sources, baseline_cache=pin(baseline_cache, root),
                jobs=jobs, configuration_joins=joins,
                summary=dict(feed_configurations=len(joins), added_parts=len(jobs), baseline_parts=len(rows),
                             total_parts=len(rows)+len(jobs), baseline_bytes=len(payload),
                             mesh_content_groups=len({(j['mesh_sha256'], j['lod_sha256']) for j in jobs}),
                             maximum_cache_bytes=MAX_BYTES, requested_storage_bytes=128*1024*1024),
                native_execution=False, headset_acceptance=False)
    plan['plan_digest'] = digest(plan)
    return plan


def validate_supply(row, payload):
    """Independently count only rigid triangles owned by the exact pouch bone."""
    completion.record_slices(payload, [row])
    at = 12
    def string():
        nonlocal at
        size = struct.unpack_from('<H', payload, at)[0]
        at += 2+size
    for _ in range(3):
        string()
    at += 12
    for section in row['sections']:
        string()
        nv, ni, width, vo, io = struct.unpack_from('<5I', payload, at)
        at += 20
        vertices = payload[at:at+nv]; indices = payload[at+nv:at+nv+ni]; at += nv+ni
        require(width == 2 and io == 0 and ni == section['count']*2 and ni % 6 == 0,
                'Pouch index contract differs')
        ids = struct.unpack('<'+'H'*(ni//2), indices)
        pos_size = 8 if section['position'] == 'Half4' else 12
        owners = []; skins = bytearray(); positions = bytearray()
        for index in ids:
            offset = vo+index*section['stride']
            vertex = vertices[offset:offset+pos_size+8]
            require(len(vertex) == pos_size+8, 'Pouch vertex outside section')
            weights = vertex[pos_size+4:pos_size+8]
            require(sum(weights) == 255 and weights.count(255) == 1, 'Pouch vertex not rigid')
            owner = vertex[pos_size+weights.index(255)]
            require(owner < section['palette_size'], 'Pouch vertex outside palette')
            owners.append(owner); skins += vertex; positions += vertex[:pos_size]
        triangles = 0
        for offset in range(0, len(owners), 3):
            require(len(set(owners[offset:offset+3])) == 1, 'Pouch cross-part triangle')
            triangles += owners[offset] == section['part_palette_index']
        require(triangles == section['part_triangles'] and triangles > 0, 'Pouch triangle ownership differs')
        require(f'{ammo.fnv(skins):016x}' == section['vertex_skin_hash']
                and f'{ammo.fnv(positions):016x}' == section['position_hash'], 'Pouch section fingerprint differs')


def append_cache(baseline_rows, baseline, additions, records):
    require(len(additions) == len(records) and 0 < len(additions) <= MAX_ADDITIONS, 'Feed addition count')
    completion.record_slices(baseline, baseline_rows)
    rows = baseline_rows+additions
    require(len(rows) <= MAX_ROWS and len({cache_key(r) for r in rows}) == len(rows), 'Duplicate/excessive cache keys')
    for row, record in zip(additions, records):
        validate_supply(row, ammo.MAGIC+struct.pack('<I', 1)+record)
    full = ammo.MAGIC+struct.pack('<I', len(rows))+baseline[12:]+b''.join(records)
    require(len(full) <= MAX_BYTES, 'Complete feed cache exceeds64MiB')
    require(full[12:len(baseline)] == baseline[12:], 'Existing cache record bytes changed')
    return rows, full


def extract(root, manifest, game, output):
    root = root.resolve(); game = game.resolve(); output = output.resolve()
    plan = read(manifest)
    require(plan['schema'] == SCHEMA and plan['plan_digest'] == digest({k:v for k,v in plan.items() if k != 'plan_digest'}),
            'Feed plan digest differs')
    docs = {k: read_pin(root, ref) for k, ref in plan['sources'].items()}
    catalog = json.loads(docs['baseline']); baseline = read_pin(root, plan['baseline_cache'])
    require(hashlib.sha256(baseline).hexdigest() == BASELINE_SHA and len(catalog['profiles']) == BASELINE_ROWS,
            'Reviewed252 baseline differs')
    jobs, joins = jobs_from_review(json.loads(docs['review']), json.loads(docs['samples']),
                                  registry_geometry(docs['generated'].decode(), docs['builtin'].decode()))
    require(jobs == plan['jobs'] and joins == plan['configuration_joins'], 'Feed plan source replay differs')
    require(not output.is_relative_to(game) and output.is_relative_to(root/'reports'),
            'Private feed cache output must stay in worktree reports, outside game installation')
    require(not output.exists() or not any(output.iterdir()), 'Preserve prior extraction output')
    first = jobs[0]
    pool = ArchivePool(game, first['archive'], first['archive_index_sha256'])
    additions = []; records = []
    with completion.shared_readers(pool):
        for job in jobs:
            print(json.dumps(dict(stage='feed_pouch', asset=job['asset'], part=job['part'])), flush=True)
            fresh, packed = ammo.derive(game, [(job['asset'], job['archive'], job['mesh'], job['part'])])
            row = fresh[0]
            require(all(row[k] == v for k,v in job.items()), 'Exact feed supply content differs')
            packed = body.compact_cache_sections(packed, fresh)
            validate_supply(row, packed)
            additions.append(row); records.append(packed[12:])
    rows, full = append_cache(catalog['profiles'], baseline, additions, records)
    catalog = dict(catalog, profiles=rows)
    output.mkdir(parents=True, exist_ok=True)
    (output/'body-ammo.fvrprop').write_bytes(full)
    write(output/'body-ammo-assets.json', catalog)
    receipt = dict(schema='fvr.bc2.feed_supply_cache_receipt.v1', plan_digest=plan['plan_digest'],
                   baseline_cache=plan['baseline_cache'], baseline_records_byte_exact=True,
                   preserved_keys=len(rows)-len(additions), added_parts=len(additions), total_parts=len(rows),
                   cache_bytes=len(full), added_bytes=len(full)-len(baseline), selected_resource_bytes=pool.bytes,
                   configuration_joins=joins, cache_pin=pin(output/'body-ammo.fvrprop', root),
                   catalog_pin=pin(output/'body-ammo-assets.json', root),
                   additions=[dict(cache_key=list(cache_key(r)), triangles=sum(s['part_triangles'] for s in r['sections']),
                                   sections=len(r['sections'])) for r in additions],
                   native_execution=False, headset_acceptance=False)
    write(output/'extraction-receipt.json', receipt)
    print(json.dumps({k:v for k,v in receipt.items() if k not in ('configuration_joins', 'additions')}))
    return receipt


def belt_header(rows):
    sections = [(r, s) for r in rows for s in r['sections']]
    lines = ['#pragma once', '#include "fvr/graphics/RigidPropGeometry.h"', '#include <array>',
             'namespace fvr::bc2 {',
             '// Derived installed-resource fingerprints only. No mesh bytes or native draw admission.',
             'struct BeltPropSectionProfile {const char* asset;const char* mesh;const char* part;const char* section;graphics::RigidPropSection geometry;};',
             f'inline constexpr std::array<BeltPropSectionProfile,{len(sections)}> BeltPropSections'+'{{']
    for row, section in sections:
        values = ','.join(str(section[k]) for k in ('count', 'stride', 'palette_size', 'part_palette_index'))
        lines.append('{'+','.join(json.dumps(row[k]) for k in ('asset','mesh','part'))+','+json.dumps(section['section'])+',{'+values+
                     ',graphics::RigidPropPosition::'+section['position']+',0x'+section['vertex_skin_hash']+'ull,0x'+
                     section['position_hash']+'ull,'+str(section['part_triangles'])+'}},')
    return '\n'.join(lines+['}};', 'inline constexpr bool IndependentBeltPropNativeAdmitted=false;', '}'])+'\n'


def enroll(root, manifest, extraction):
    """Publish reproducible metadata only; leave private geometry under reports."""
    plan = read(manifest); receipt = read(extraction/'extraction-receipt.json')
    require(plan['schema'] == SCHEMA and plan['plan_digest'] == digest({k:v for k,v in plan.items() if k != 'plan_digest'}),
            'Feed plan digest differs')
    require(receipt['schema'] == 'fvr.bc2.feed_supply_cache_receipt.v1'
            and receipt['plan_digest'] == plan['plan_digest'] and receipt['baseline_records_byte_exact'],
            'Extraction/plan join differs')
    payload = read_pin(root, receipt['cache_pin'])
    original = read_pin(root, plan['baseline_cache'])
    require(hashlib.sha256(original).hexdigest() == BASELINE_SHA and payload[:8] == ammo.MAGIC
            and payload[12:len(original)] == original[12:], 'Existing cache record bytes changed')
    require(len(payload) == receipt['cache_bytes'] <= MAX_BYTES
            and struct.unpack_from('<I', payload, 8)[0] == receipt['total_parts'], 'Feed cache receipt differs')
    catalog = json.loads(read_pin(root, receipt['catalog_pin']))
    baseline = json.loads(read_pin(root, plan['sources']['baseline']))
    rows = catalog['profiles']; additions = rows[len(baseline['profiles']):]
    require(rows[:len(baseline['profiles'])] == baseline['profiles'], 'Baseline metadata changed')
    require([cache_key(r) for r in additions] == [cache_key(j) for j in plan['jobs']], 'Feed addition keys differ')
    require(len(rows) == receipt['total_parts'] and len(additions) == receipt['added_parts'], 'Feed receipt counts differ')
    write(root/'config/body-ammo-assets.json', catalog)
    (root/'src/games/bc2/Bc2BodyAmmoAssetProfiles.h').write_text(ammo.catalog_header(rows), encoding='utf-8')
    # Preserve the two legacy belt seed recipes and add each exact feed pouch.
    seeds = baseline['profiles'][:2]
    require([r['asset'] for r in seeds] == ['XM8_sp_s', 'SPAS12_sp'], 'Legacy belt seeds changed')
    (root/'src/games/bc2/Bc2BeltPropProfiles.h').write_text(belt_header(seeds+additions), encoding='utf-8')
    public = root/'profiles/weapons54-feed-cache'
    write(public/'additions-body-ammo-assets.json', dict(schema=catalog['schema'], version=catalog['version'], profiles=additions))
    write(public/'extraction-receipt.json', receipt)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('plan')
    p.add_argument('--baseline-cache', type=Path, required=True)
    p.add_argument('--baseline-catalog', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('extract')
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--game', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('enroll')
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--extraction', type=Path, required=True)
    args = parser.parse_args(argv)
    if args.command == 'plan':
        plan = prepare(args.root, args.baseline_cache, args.baseline_catalog)
        write(args.output, plan); print(json.dumps(plan['summary']))
    elif args.command == 'extract':
        extract(args.root, args.manifest, args.game, args.output)
    else:
        enroll(args.root, args.manifest, args.extraction)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
