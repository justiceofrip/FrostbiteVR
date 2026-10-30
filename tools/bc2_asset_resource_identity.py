"""Exact resource identity for local cache preparation across archive layouts.

This is not native capability admission. Archive indexes remain provenance;
all requested resource bytes must match the reviewed catalog before parsing.
"""
import hashlib
from pathlib import Path, PurePosixPath
import re


def archive_key(value):
    value = str(value).replace('\\', '/')
    path = PurePosixPath(value)
    if not value or path.is_absolute() or ':' in value or '..' in path.parts:
        raise ValueError('Invalid catalog archive path')
    return value.casefold()


def catalog_resource_pins(rows, skeleton_archive, skeleton_resource, skeleton_sha):
    pins = {}

    def add(archive, resource, digest):
        if not isinstance(resource, str) or not resource or not isinstance(digest, str) or not re.fullmatch('[a-f0-9]{64}', digest):
            raise ValueError('Exact catalog resource pin required')
        key = (archive_key(archive), resource)
        if key in pins and pins[key] != digest:
            raise ValueError('Conflicting catalog resource pins')
        pins[key] = digest
        if len(pins) > 1024:
            raise ValueError('Catalog resource pin bound')

    add(skeleton_archive, skeleton_resource, skeleton_sha)
    for row in rows:
        if row.get('configured_aliases'):
            from bc2_firearm_body_aliases import alias_resource_pins
            for archive,resource,digest in alias_resource_pins(row):add(archive,resource,digest)
        if row.get('mod_authored_magazine'):
            for label in ('mesh', 'lod', 'skeleton', 'static', 'reload'):
                ref = row['mod_policy_source'][label]
                add(ref['archive'], ref['resource'], ref['sha256'])
        elif row.get('rigid_assembly'):
            for label in ('mesh', 'lod', 'skeleton', 'static', 'reload'):
                ref = row['assembly_source'][label]
                add(ref['archive'], ref['resource'], ref['sha256'])
        elif row.get('configuration_path'):
            cfg = row['authored_configuration']
            for ref in [cfg] + cfg['mesh_references']:
                add(row['configuration_archive'], ref['resource'], ref['sha256'])
            for ref in row['sources']:
                add(ref['archive'], ref['mesh'] + '.res', ref['mesh_sha256'])
                add(ref['archive'], ref['mesh'] + '_lod0_data.res', ref['lod_sha256'])
            add(row['closed_archive'], row['closed_clip'], row['closed_clip_sha256'])
        else:
            add(row['archive'], row['mesh'] + '.res', row['mesh_sha256'])
            add(row['archive'], row['mesh'] + '_lod0_data.res', row['lod_sha256'])
            if row.get('display_only'):
                add(row['archive'], row['closed_clip'], row['closed_clip_sha256'])
    return pins


class VerifiedResources:
    def __init__(self, game, pool, pins):
        self.game = Path(game).resolve()
        self.pool = pool
        self.pins = pins
        self.wrapped = set()

    def __call__(self, path):
        source = self.pool(path)
        key = archive_key(Path(path).resolve().relative_to(self.game).as_posix())
        if key not in self.wrapped:
            original = source.read_selected

            def read_selected(names):
                names = list(names)
                expected = {}
                for name in names:
                    pin = self.pins.get((key, name))
                    if pin is None:
                        raise ValueError('Resource not pinned by reviewed catalog: ' + name)
                    expected[name] = pin
                blobs = original(names)
                if set(blobs) != set(expected):
                    raise ValueError('Selected catalog resource set differs')
                for name, raw in blobs.items():
                    if hashlib.sha256(raw).hexdigest() != expected[name]:
                        raise ValueError('Catalog resource content differs: ' + name)
                return blobs

            source.read_selected = read_selected
            self.wrapped.add(key)
        return source


def content_profile(row):
    """Only declared container provenance may vary; no recursive hash removal."""
    def provenance(value, fields):
        result = dict(value)
        for field in fields:
            if field in result:
                if not isinstance(result[field], str) or not re.fullmatch('[a-f0-9]{64}', result[field]):
                    raise ValueError('Invalid archive index provenance')
                result[field] = None
        return result

    result = provenance(row, ('archive_index_sha256', 'configuration_archive_index_sha256', 'closed_archive_index_sha256'))
    if 'sources' in row:
        result['sources'] = [provenance(source, ('archive_index_sha256',)) for source in row['sources']]
    if 'assembly_source' in row:
        result['assembly_source'] = dict(row['assembly_source'])
        for label in ('mesh', 'lod', 'skeleton', 'static', 'reload'):
            if label in row['assembly_source']:
                result['assembly_source'][label] = provenance(row['assembly_source'][label], ('archive_index_sha256',))
    if 'mod_policy_source' in row:
        result['mod_policy_source'] = dict(row['mod_policy_source'])
        for label in ('mesh', 'lod', 'skeleton', 'static', 'reload'):
            if label in row['mod_policy_source']:
                result['mod_policy_source'][label] = provenance(row['mod_policy_source'][label], ('archive_index_sha256',))
    if 'configured_aliases' in row:
        result['configured_aliases']=[]
        for alias in row['configured_aliases']:
            if alias['profile'].get('configured_aliases'):raise ValueError('Nested configuration alias')
            result['configured_aliases'].append({**alias,'profile':content_profile(alias['profile'])})
    return result


def profiles_match(actual, reviewed, resource_content=False):
    if not resource_content:
        return actual == reviewed
    return len(actual) == len(reviewed) and all(content_profile(a) == content_profile(b) for a, b in zip(actual, reviewed))
