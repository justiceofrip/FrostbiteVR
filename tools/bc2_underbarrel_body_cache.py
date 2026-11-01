"""Append measured underbarrel supplies to the reviewed264 private BC2PROP1 cache.

Run only after StorageBudget admission. Read selected installed resources through
the existing bounded pool; preserve every old catalog row and cache record byte.
Generated geometry stays under reports and must never be committed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

# The installed embedded Python has an isolated search path; use this checkout.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import bc2_body_ammo_assets as ammo
import bc2_configured_body_assets as body
import bc2_firearm_cache_completion as completion
import bc2_feed_supply_cache as feed
import bc2_underbarrel_reload_geometry as reload_geometry
from bc2_magazine_contact_batch import ArchivePool

ROOT = Path(__file__).resolve().parents[1]
PROFILES = 'profiles/weapons71-underbarrel-reload'
BASELINE = 'reports/coordinator-20261030-wiring54/feed-cache01'
BASELINE_CACHE_SHA = '7e3d16f5bcc0af74ff7ab3afe2740197b1b4b9ac18dc9f61416325861af8f49e'
BASELINE_CATALOG_SHA = 'cce44a5b1506afd3fc8f82496a06b5aa1fa8a9fe956c3b5d8d2525c4dfafecff'
BASELINE_ROWS = 264
MAX_ADDITIONS = 18
require = completion.require
cache_key = completion.cache_key
read = completion.read


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def source_jobs(root=ROOT):
    """Join generated runtime keys to exact measured family and archive tuples."""
    root = Path(root).resolve()
    directory = root/PROFILES
    geometry = read(directory/'geometry.json')
    require(geometry['schema'] == 'fvr.bc2.underbarrel_reload_geometry.v1', 'Underbarrel geometry schema')
    models = []
    jobs = {}
    pins = []
    configuration_keys = {}
    for folder in ('an94', 'm16', 'xm8'):
        source_path = directory/folder/'source.json'
        measured_path = directory/folder/'measurements.json'
        source, measured = read(source_path), read(measured_path)
        model = reload_geometry.model_profile(source, measured)
        models.append(model)
        pins.extend((feed.pin(source_path, root), feed.pin(measured_path, root)))
        # The portable inputs retain the pinned report71/72 measurement packets.
        for pin in source['provenance']:
            feed.read_pin(root, pin)
        chain = source['chain']
        require(chain['skeleton_sha256'] == ammo.SKELETON_SHA, 'Underbarrel skeleton changed')
        variants = {v['configuration']: v for v in source['variants']}
        selectors = {s['configurationPath']: s for s in model['selectors']}
        for selector in model['selectors']:
            path = selector['configurationPath']
            require(path not in configuration_keys, 'Duplicate underbarrel configuration')
            configuration_keys[path] = [
                (selector['asset'], selector['mesh'], part, selector['rigFingerprint'])
                for part in model['supply_parts']]
        for asset in sorted({s['asset'] for s in model['selectors']}):
            # Keep smoke/shotgun/grenade identity distinct even for identical mesh bytes.
            configuration = next(p for p in sorted(selectors) if selectors[p]['asset'] == asset)
            variant, selector = variants[configuration], selectors[configuration]
            for part in model['supply_parts']:
                job = dict(asset=asset, mesh=selector['mesh'], part=part,
                           rig_fingerprint=selector['rigFingerprint'], archive=variant['archive'],
                           archive_index_sha256=variant['index_sha256'], mesh_sha256=chain['mesh_sha256'],
                           lod_sha256=chain['lod_sha256'])
                key = cache_key(job)
                require(key not in jobs, 'Duplicate underbarrel supply key')
                jobs[key] = dict(profile=job, bone_index=source['skeleton_bones'][part]['index'],
                                 triangles=measured['parts'][part]['visible_triangles'])
    models.sort(key=lambda m: m['label'])
    require(models == geometry['models'], 'Generated geometry differs from measured inputs')
    require((directory/'FixedContainer.generated.h').read_text(encoding='utf-8') ==
            reload_geometry.cpp_header(models), 'Generated header differs from measured inputs')
    require(len(configuration_keys) == 40 and len(jobs) == MAX_ADDITIONS,
            'Expected40 measured configurations and18 exact supply keys')
    require({key for keys in configuration_keys.values() for key in keys} == set(jobs),
            'Runtime supply cache closure differs')
    pins.extend(feed.pin(directory/name, root) for name in ('geometry.json', 'FixedContainer.generated.h'))
    return list(jobs.values()), configuration_keys, pins


def append_catalog_bytes(original, additions):
    """Keep all bytes preceding the original profile-array terminator intact."""
    catalog = json.loads(original)
    newline = b'\r\n' if b'\r\n' in original else b'\n'
    suffix = newline+b'  ]'+newline+b'}'+newline
    require(original.endswith(suffix), 'Reviewed catalog terminator differs')
    added = json.dumps(additions, indent=2, allow_nan=False).splitlines()[1:-1]
    added = newline.join(('  '+line).encode('utf-8') for line in added)
    result = original[:-len(suffix)]+b','+newline+added+suffix
    require(json.loads(result) == dict(catalog, profiles=catalog['profiles']+additions),
            'Append-only catalog serialization differs')
    return result


def append_cache(baseline_rows, baseline, additions, records):
    require(0 < len(additions) == len(records) <= MAX_ADDITIONS, 'Underbarrel addition bound')
    completion.record_slices(baseline, baseline_rows)
    rows = baseline_rows+additions
    require(len(rows) <= 512 and len({cache_key(r) for r in rows}) == len(rows), 'Duplicate/excessive cache key')
    for row, record in zip(additions, records):
        feed.validate_supply(row, ammo.MAGIC+struct.pack('<I', 1)+record)
    payload = ammo.MAGIC+struct.pack('<I', len(rows))+baseline[12:]+b''.join(records)
    require(len(payload) <= ammo.MAX_CACHE, 'Merged underbarrel cache exceeds64MiB')
    require(payload[12:len(baseline)] == baseline[12:], 'Baseline cache records changed')
    completion.record_slices(payload, rows)
    return rows, payload


def extract(root, game, output, enroll=False):
    root, game, output = root.resolve(), game.resolve(), output.resolve()
    require(output.is_relative_to(root/'reports') and not output.is_relative_to(game),
            'Private cache output must stay in worktree reports, outside installation')
    require(not output.exists() or all(p.name == 'storage-admission.json' for p in output.iterdir()),
            'Preserve prior private cache output')
    original = (root/BASELINE/'body-ammo-assets.json').read_bytes()
    baseline = (root/BASELINE/'body-ammo.fvrprop').read_bytes()
    require(sha(original) == BASELINE_CATALOG_SHA and sha(baseline) == BASELINE_CACHE_SHA,
            'Reviewed264 catalog/cache identity changed')
    baseline_rows = json.loads(original)['profiles']
    require(len(baseline_rows) == BASELINE_ROWS, 'Reviewed264 row count changed')
    completion.record_slices(baseline, baseline_rows)
    if enroll:
        require((root/'config/body-ammo-assets.json').read_bytes() == original,
                'Current catalog differs from reviewed264 baseline')
    jobs, configurations, pins = source_jobs(root)
    first = jobs[0]['profile']
    pool = ArchivePool(game, first['archive'], first['archive_index_sha256'])
    additions, records = [], []
    with completion.shared_readers(pool):
        raw = pool(game/ammo.SKELETON_ARCHIVE).read_selected([ammo.SKELETON_RESOURCE])[ammo.SKELETON_RESOURCE]
        require(sha(raw) == ammo.SKELETON_SHA, 'Installed skeleton changed')
        skeleton = ammo.Skeleton(raw)
        for job in jobs:
            expected = job['profile']
            require(skeleton.names[job['bone_index']] == expected['part'], 'Supply bone index changed')
            print(json.dumps(dict(stage='underbarrel_supply', asset=expected['asset'],
                                  mesh=expected['mesh'], part=expected['part'])), flush=True)
            rows, packed = ammo.derive(game, [tuple(expected[k] for k in ('asset', 'archive', 'mesh', 'part'))])
            row = rows[0]
            require(all(row[k] == v for k, v in expected.items()), 'Installed exact supply content changed')
            require(sum(s['part_triangles'] for s in row['sections']) == job['triangles'],
                    'Installed supply triangles differ from measured packet')
            packed = body.compact_cache_sections(packed, rows)
            feed.validate_supply(row, packed)
            additions.append(row)
            records.append(packed[12:])
    rows, payload = append_cache(baseline_rows, baseline, additions, records)
    catalog_bytes = append_catalog_bytes(original, additions)
    header = ammo.catalog_header(rows)
    # Recheck source pins immediately before publishing the completed result.
    for pin in pins:
        feed.read_pin(root, pin)
    output.mkdir(parents=True, exist_ok=True)
    (output/'body-ammo.fvrprop').write_bytes(payload)
    if enroll:
        (root/'config/body-ammo-assets.json').write_bytes(catalog_bytes)
        (root/'src/games/bc2/Bc2BodyAmmoAssetProfiles.h').write_text(header, encoding='utf-8', newline='\n')
    receipt = dict(schema='fvr.bc2.underbarrel_body_cache.v1',
                   cache=feed.pin(output/'body-ammo.fvrprop', root), cache_bytes=len(payload),
                   parts=len(rows), added_parts=len(additions), added_bytes=len(payload)-len(baseline),
                   prior_parts=BASELINE_ROWS, prior_cache_sha256=BASELINE_CACHE_SHA,
                   prior_catalog_sha256=BASELINE_CATALOG_SHA, prior_records_byte_exact=True,
                   prior_catalog_rows_byte_exact=True, measured_configurations=len(configurations),
                   family_parts={model: sum(1 for j in jobs if model in j['profile']['mesh'])
                                 for model in ('AN94Abakan', 'M16A2', 'XM8')},
                   selected_resource_bytes=pool.bytes, selected_archives=len(pool.archives),
                   source_pins=pins, catalog_sha256=sha(catalog_bytes),
                   header_sha256=sha(header.encode()), metadata_enrolled=enroll,
                   installed_geometry_verified=True, native_execution=False, headset_acceptance=False)
    completion.write(output/'extraction-receipt.json', receipt)
    print(json.dumps({k: v for k, v in receipt.items() if k != 'source_pins'}))
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--enroll', action='store_true', help='Publish append-only catalog and generated header')
    args = parser.parse_args()
    extract(args.root, args.game, args.output, args.enroll)


if __name__ == '__main__':
    main()
