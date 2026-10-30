"""Exact configuration aliases for byte-identical body-display cache records.

Each alias owns a complete reviewed configuration profile and installed replay.
Only the private geometry record is shared. There is no asset-name fallback.
"""
import copy
import hashlib
import struct

import bc2_body_ammo_assets as ammo
import bc2_configured_body_assets as body
from bc2_asset_resource_identity import profiles_match
from bc2_weapon_animation_pipeline import Skeleton, canonical
from inspect_bc2_mesh_asset import bone_hash, sha


def require(value, message):
    if not value:
        raise ValueError(message)


def geometry_payload(record):
    """Exclude only the three identity strings; retain rig and exact section data."""
    at = 0
    for _ in range(3):
        require(at + 2 <= len(record), 'Truncated alias record')
        size = struct.unpack_from('<H', record, at)[0]
        require(0 < size <= 512 and at+2+size <= len(record), 'Alias identity bound')
        at += 2+size
    require(at+12 <= len(record), 'Truncated alias geometry')
    return record[at:]


def equal_content(a, b):
    fields = ('asset', 'mesh', 'rig_fingerprint', 'sections', 'inverse_bind', 'configured_meshes',
              'closed_clip_sha256', 'holster_from_weapon', 'grip', 'muzzle', 'closed_pose')
    require(all(a[k] == b[k] for k in fields), 'Alias display content differs')
    source = lambda r: sorted((s['mesh'], s['mesh_sha256'], s['lod_sha256']) for s in r['sources'])
    require(source(a) == source(b), 'Alias configured mesh content differs')


def add_alias(canonical_row, canonical_record, alias_row, alias_record):
    equal_content(canonical_row, alias_row)
    require(geometry_payload(canonical_record) == geometry_payload(alias_record), 'Alias geometry is not byte-equal')
    cfg = alias_row['authored_configuration']
    require(cfg['resource'] != canonical_row['authored_configuration']['resource'], 'Alias duplicates canonical configuration')
    aliases = canonical_row.setdefault('configured_aliases', [])
    require(not any(a['profile']['authored_configuration']['resource'] == cfg['resource'] for a in aliases), 'Duplicate exact alias')
    require(not alias_row.get('configured_aliases'), 'Nested aliases forbidden')
    aliases.append(dict(profile=copy.deepcopy(alias_row),
                        geometry_payload_sha256=hashlib.sha256(geometry_payload(alias_record)).hexdigest()))


def alias_resource_pins(row):
    """Yield archive/resource/hash triples for the normal portable resource wrapper."""
    for alias in row.get('configured_aliases', []):
        p = alias['profile']; cfg = p['authored_configuration']
        for ref in [cfg]+cfg['mesh_references']:
            yield p['configuration_archive'], ref['resource'], ref['sha256']
        for s in p['sources']:
            yield s['archive'], s['mesh']+'.res', s['mesh_sha256']
            yield s['archive'], s['mesh']+'_lod0_data.res', s['lod_sha256']
        yield p['closed_archive'], p['closed_clip'], p['closed_clip_sha256']


def verify_aliases(game, pool, reviewed, canonical_record, resource_content=False):
    """Rebuild every alias using its own exact configuration and closed source."""
    from bc2_firearm_cache_completion import (derive_reviewed_display, verify_configuration,
                                            reviewed_spec, reviewed_binding)
    aliases = reviewed.get('configured_aliases', [])
    require(len(aliases) <= 24, 'Alias count bound')
    seen = {reviewed['authored_configuration']['resource']}
    expected_payload = geometry_payload(canonical_record)
    sk_raw = pool(game/ammo.SKELETON_ARCHIVE).read_selected([ammo.SKELETON_RESOURCE])[ammo.SKELETON_RESOURCE]
    require(sha(sk_raw) == ammo.SKELETON_SHA, 'Alias skeleton differs')
    skeleton = Skeleton(sk_raw); known = {bone_hash(n):n for n in skeleton.names}
    inverse = {n:canonical(b['InverseWorldTransform']) for n,b in zip(skeleton.names,skeleton.bones)}
    for alias in aliases:
        p = alias['profile']; resource = p['authored_configuration']['resource']
        require(resource not in seen and not p.get('configured_aliases'), 'Duplicate/nested alias')
        seen.add(resource); equal_content(reviewed, p)
        require(alias['geometry_payload_sha256'] == hashlib.sha256(expected_payload).hexdigest(), 'Alias payload pin differs')
        if p.get('mod_authored_firearm_display'):
            row, record = derive_reviewed_display(game, pool, p, resource_content)
        else:
            index = verify_configuration(game, pool, p, resource_content)
            row, record = body.composite(game, pool, reviewed_spec(p), reviewed_binding(p),
                dict(configured_mesh_path=p['mesh']), skeleton, known, inverse, resource_content)
            row.update(configuration_archive=p['configuration_archive'], configuration_archive_index_sha256=index,
                       closed_archive=p['closed_archive'], closed_archive_index_sha256=pool(game/p['closed_archive']).index_sha256)
            require(profiles_match([row], [p], resource_content), 'Alias exact replay metadata differs')
        record = body.compact_cache_sections(ammo.MAGIC+struct.pack('<I', 1)+record, [row])[12:]
        require(geometry_payload(record) == expected_payload, 'Alias installed geometry differs')
    return copy.deepcopy(aliases)


def expand_equipment_rows(rows):
    """Emit one exact selector per configuration, sharing only a proven cache key."""
    result = []; seen = set()
    for row in rows:
        result.append(row)
        if row.get('configuration_path'):
            key = row['asset'], row['configuration_path']
            require(key not in seen, 'Duplicate body selector'); seen.add(key)
        for alias in row.get('configured_aliases', []):
            profile = alias['profile']; equal_content(row, profile)
            key = row['asset'], profile['configuration_path']
            require(key not in seen, 'Duplicate alias body selector'); seen.add(key)
            expanded = dict(profile, part=row['part'])
            result.append(expanded)
    return result
