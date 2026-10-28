"""Offline test-range availability planner; never mounts bundles or opens a game.

Reads saved typed configuration reports and bounded installed archive indexes.
Names, native classes and serialized dispatch groups are selection metadata, not
proof of physical mechanisms, player pickup routes, or enabled VR capabilities.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import uuid

from bc2_magazine_contact_batch import ArchivePool

MAX_INPUT = 8 * 1024 * 1024
MAX_TOTAL_INPUT = 32 * 1024 * 1024
MAX_OUTPUT = 8 * 1024 * 1024
MAX_DEFINITIONS = 4096
MAX_ARCHIVES = 128
DISPATCH_FIELDS = (
    'FireLogic.FireLogicType', 'FireLogic.ReloadType',
    'FireLogic.FireInputAction', 'FireLogic.ReloadInputAction',
    'FireLogic.BoltAction.HoldBoltActionUntilFireRelease',
    'FireLogic.BoltAction.HoldBoltActionUntilZoomRelease',
)
LIMITS = [
    'Only supplied typed reports are cataloged; absent definitions are unknown.',
    'Archive indexes are read; payloads and resource content hashes are not rechecked.',
    'Mount targets are the literal ordered package.mft section, not an inferred recursive expansion.',
    'A mounted definition candidate does not establish spawn, pickup, unlock, mesh closure, or live equipment availability.',
    'Conflicting or untyped overlay resources remain ambiguous; package precedence is not guessed.',
    'Native classes and dispatch families do not classify physical feed/action mechanisms.',
    'No native operation, VR capability, or headset acceptance is granted.',
]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()


def path_key(value):
    if (not isinstance(value, str) or not value or '\\' in value or ':' in value
            or value.startswith('/') or any(ord(c) < 32 or ord(c) == 127 for c in value)
            or any(p in ('', '.', '..') for p in value.split('/'))):
        raise ValueError('Canonical relative resource required')
    return value.casefold()


def sha(value):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-fA-F]{64}', value):
        raise ValueError('SHA-256 required')
    return value.lower()


def bundle(value):
    if not isinstance(value, str) or ':' not in value:
        raise ValueError('Namespaced bundle required')
    namespace, relative = value.split(':', 1)
    if namespace not in ('game', 'package') or not path_key(relative).endswith('.fbrb'):
        raise ValueError('Unsupported bundle')
    return namespace + ':' + path_key(relative)


def archive_bundle(value):
    value = path_key(value)
    for prefix, namespace in (('dist/win32/', 'game:'), ('package/', 'package:')):
        if value.startswith(prefix):
            return bundle(namespace + value[len(prefix):])
    raise ValueError('Configuration archive must be under Dist/win32 or Package')


def bundle_archive(value):
    namespace, relative = bundle(value).split(':', 1)
    return ('Dist/win32/' if namespace == 'game' else 'Package/') + relative


def parse_package(text):
    if len(text.encode()) > 2 * 1024 * 1024 or '\0' in text:
        raise ValueError('Package manifest bound')
    sections = {}
    current = None
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith('BundleSource '):
            current = bundle(line[len('BundleSource '):])
            if current in sections:
                raise ValueError('Duplicate manifest source')
            sections[current] = []
        elif line.startswith('BundleTarget '):
            if current is None:
                raise ValueError('Manifest target has no source')
            target = bundle(line[len('BundleTarget '):])
            if target in sections[current]:
                raise ValueError('Duplicate manifest target')
            sections[current].append(target)
        elif current is None and (line == 'Authorative' or re.fullmatch(r'Signature [0-9a-fA-F]+', line)):
            continue
        else:
            raise ValueError('Unknown package manifest statement')
    if not sections or len(sections) > 8192 or any(not t or len(t) > 32 for t in sections.values()):
        raise ValueError('Manifest section count or target bound')
    return sections


def parse_bundle_manifest(text):
    records = {}
    for raw in text.splitlines():
        if not raw.strip():
            continue
        parts = raw.split()
        if len(parts) != 3:
            raise ValueError('Bundle manifest must contain path GUID part-count')
        # Installed manifests prefix some logical audio bundles with one slash.
        # This is a logical namespace marker, never a filesystem absolute path.
        logical_name = parts[0][1:] if parts[0].startswith('/') else parts[0]
        name = path_key(logical_name)
        identifier = str(uuid.UUID(parts[1]))
        if name in records or not re.fullmatch(r'[1-9][0-9]*', parts[2]):
            raise ValueError('Duplicate bundle or invalid part count')
        count = int(parts[2])
        if count > 128:
            raise ValueError('Bundle part count bound')
        records[name] = {'guid': identifier, 'parts': count}
    if not records or len(records) > 8192:
        raise ValueError('Bundle manifest count bound')
    return records


def strict_json(data):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('Duplicate JSON key')
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=pairs,
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Nonfinite JSON')))


def load(path):
    if path.stat().st_size > MAX_INPUT:
        raise ValueError('Input exceeds 8 MiB')
    data = path.read_bytes()
    if len(data) > MAX_INPUT:
        raise ValueError('Input grew beyond bound')
    return data, {'path': str(path.resolve()), 'sha256': digest(data), 'bytes': len(data)}


def configuration_reports(document):
    schema = document.get('schema')
    if schema == 'fvr.bc2.campaign_weapon_configurations' and document.get('schema_version') == 1:
        reports = document['archives']
        errors = [{'scope': 'campaign_archive', **row} for row in document.get('errors', [])]
    elif schema in ('fvr.bc2.common_weapon_metadata.v1', 'fvr.bc2.lmg_common_metadata.v1'):
        reports, errors = [document['configuration']], []
    elif schema == 'fvr.bc2.authored_weapon_configuration':
        reports, errors = [document], []
    else:
        raise ValueError('Expected campaign or typed authored configuration report')
    if not isinstance(reports, list) or len(reports) > 32:
        raise ValueError('Configuration archive report bound')
    for report in reports:
        if report.get('schema') != 'fvr.bc2.authored_weapon_configuration' or report.get('schema_version') != 1:
            raise ValueError('Typed configuration report schema')
    return reports, errors


def collect(documents):
    definitions = {}
    origins = {}
    unresolved = []
    source_contents = {}
    for input_index, document in enumerate(documents):
        reports, errors = configuration_reports(document)
        unresolved.extend({'input': input_index, **row} for row in errors)
        for report in reports:
            owner = archive_bundle(report['archive'])
            index = sha(report['index_sha256'])
            origins.setdefault(owner, set()).add(index)
            for field in ('parse_errors', 'unresolved_weapons'):
                for row in report.get(field, []):
                    unresolved.append({'input': input_index, 'archive': owner, 'kind': field, 'detail': row})
            rows = report['resolved_weapons']
            if not isinstance(rows, list) or len(rows) > MAX_DEFINITIONS:
                raise ValueError('Configuration row bound')
            for row in rows:
                resource = path_key(row['resource'])
                if not resource.endswith('.dbx'):
                    raise ValueError('Configuration must be DBX')
                identifier = str(uuid.UUID(row['instance_guid']))
                identity = {'resource': resource, 'instance_guid': identifier,
                            'resource_sha256': sha(row['resource_sha256'])}
                source_key = owner, index, resource, identifier
                if source_contents.get(source_key, identity['resource_sha256']) != identity['resource_sha256']:
                    raise ValueError('Same indexed source has conflicting configuration content')
                source_contents[source_key] = identity['resource_sha256']
                native_name, native_class = row['native_name'], row['weapon_class']
                if not all(isinstance(v, str) and v and len(v) <= 256 for v in (native_name, native_class)):
                    raise ValueError('Native metadata label bound')
                fields = row.get('fields', {})
                dispatch = {name: fields[name]['value'] for name in DISPATCH_FIELDS if name in fields}
                family = {'fields': dispatch, 'missing_fields': [n for n in DISPATCH_FIELDS if n not in fields]}
                family_id = digest(canonical_json(family))[:24]
                key = digest(canonical_json(identity))
                definition = {'id': key, **identity, 'native_name': native_name, 'native_class': native_class,
                              'dispatch_family': family_id, 'dispatch': family,
                              'configuration_directory': resource.rsplit('/', 1)[0]}
                previous = definitions.get(key)
                if previous and any(previous[k] != definition[k] for k in definition):
                    raise ValueError('Same exact configuration has conflicting typed metadata')
                if previous is None:
                    definition['origins'] = []
                    definitions[key] = definition
                origin = {'bundle': owner, 'index_sha256': index}
                if origin not in definitions[key]['origins']:
                    definitions[key]['origins'].append(origin)
    if not definitions or len(definitions) > MAX_DEFINITIONS or len(origins) > MAX_ARCHIVES:
        raise ValueError('Definition or origin count bound')
    for row in definitions.values():
        row['origins'].sort(key=lambda origin: (origin['bundle'], origin['index_sha256']))
    return definitions, origins, unresolved


def read_indexes(game, targets, origins):
    """Read compressed index headers only. Never call read_selected."""
    if len(targets) > MAX_ARCHIVES:
        raise ValueError('Index archive count bound')
    mp = 'game:levels/mp_common/level-00.fbrb'
    pins = origins.get(mp, set())
    # Pool's existing MP-only 640 MiB declared-payload allowance is retained.
    pool = ArchivePool(game, bundle_archive(mp), next(iter(pins), '0' * 64), verify_common_index=False)
    result = {}
    for target in sorted(targets):
        relative = bundle_archive(target)
        path = (game / relative).resolve()
        if not path.is_relative_to(game):
            raise ValueError('Bundle target leaves installation')
        if not path.is_file():
            result[target] = {'status': 'missing'}
            continue
        try:
            archive = pool(path)
            entries = {}
            for entry in archive.entries:
                name = path_key(entry.name)
                if name in entries:
                    raise ValueError('Case-ambiguous archive resource')
                entries[name] = entry.flags
            result[target] = {'status': 'indexed', 'index_sha256': archive.index_sha256, 'entries': entries}
        except (OSError, ValueError) as exc:
            result[target] = {'status': 'unreadable_index', 'reason': str(exc)}
    return result


def map_source(value):
    if ':' in value:
        source = bundle(value)
    else:
        name = path_key(value)
        if not name.startswith('levels/'):
            name = 'levels/' + name
        if not name.endswith('/level-00.fbrb'):
            name += '/level-00.fbrb'
        source = bundle('game:' + name)
    if not re.fullmatch(r'game:levels/[^/]+/level-00\.fbrb', source):
        raise ValueError('Expected a level-00 map source')
    return source


def plan(documents, sections, bundle_records, indexes, *, maps=(), assets=(), native_classes=(),
         directories=(), families=()):
    definitions, origins, unresolved = collect(documents)
    available_maps = {s for s in sections if re.fullmatch(r'game:levels/[^/]+/level-00\.fbrb', s)}
    available_maps.update('game:' + name + '-00.fbrb' for name in bundle_records
                          if re.fullmatch(r'levels/[^/]+/level', name))
    selected_maps = {map_source(m) for m in maps} if maps else available_maps
    if not selected_maps or len(selected_maps) > 128 or selected_maps - available_maps:
        raise ValueError('Unknown map selection or map count bound')
    selected = [row for row in definitions.values()
                if (not assets or row['native_name'] in assets)
                and (not native_classes or row['native_class'] in native_classes)
                and (not directories or row['configuration_directory'] in {path_key(d) for d in directories})
                and (not families or row['dispatch_family'] in families)]
    if not selected:
        raise ValueError('No exact configurations match the selection')
    selected.sort(key=lambda row: (row['resource'], row['instance_guid'], row['resource_sha256']))
    family_groups = {}
    groups = {name: defaultdict(list) for name in ('native_name', 'native_class', 'configuration_directory')}
    for row in selected:
        family_groups[row['dispatch_family']] = row['dispatch']
        for field in groups:
            groups[field][row[field]].append(row['id'])
        row['installed_definition_evidence'] = []
        for origin in row['origins']:
            observed = indexes.get(origin['bundle'], {'status': 'not_inspected'})
            matched = (observed['status'] == 'indexed' and observed['index_sha256'] == origin['index_sha256']
                       and observed['entries'].get(row['resource']) == 65536)
            status = observed['status']
            if status == 'indexed':
                status = ('stale_index' if observed['index_sha256'] != origin['index_sha256']
                          else 'matched' if matched else 'deleted_or_missing_resource')
            row['installed_definition_evidence'].append({**origin, 'status': status, 'index_and_entry_match': matched})
        row['live_native_verified'] = False
    map_rows = []
    for source in sorted(selected_maps):
        targets = sections.get(source, [])
        candidates, ambiguous, uncovered = [], [], []
        unknown_indexes = [t for t in targets if indexes.get(t, {}).get('status') != 'indexed']
        untyped = [t for t in targets if t not in origins]
        needed = set()
        for row in selected:
            matching = [o['bundle'] for o in row['installed_definition_evidence']
                        if o['index_and_entry_match'] and o['bundle'] in targets]
            if not matching:
                uncovered.append(row['id'])
                needed.update(o['bundle'] for o in row['origins'] if o['bundle'] not in targets)
                continue
            active = [t for t in targets if indexes.get(t, {}).get('entries', {}).get(row['resource']) == 65536]
            deletions = [t for t in targets if indexes.get(t, {}).get('entries', {}).get(row['resource']) == 0]
            collisions = [t for t in active if t not in matching]
            if collisions or deletions or unknown_indexes:
                ambiguous.append({'definition': row['id'], 'matched_origins': matching,
                                  'unresolved_resource_owners': collisions, 'deletion_entries': deletions})
            else:
                candidates.append(row['id'])
        logical = source[len('game:'):-len('-00.fbrb')]
        map_rows.append({'source': source, 'targets': targets,
                         'listed_in_bundle_manifest': logical in bundle_records,
                         'mount_section_present': source in sections,
                         'mounted_definition_candidates': candidates,
                         'ambiguous_definition_candidates': ambiguous,
                         'uncovered_definitions': uncovered,
                         'needed_origin_candidates': sorted(needed),
                         'untyped_mounted_archives': untyped,
                         'all_definitions_cataloged': False,
                         'unavailable_target_indexes': unknown_indexes,
                         'live_native_verified': False})
    index_rows = [{'bundle': key, **{k: v for k, v in value.items() if k != 'entries'}}
                  for key, value in sorted(indexes.items())]
    return {'schema': 'fvr.bc2.test_range_catalog.v1', 'metadata_only': True,
            'live_native_verified': False, 'runtime_admission_granted': False,
            'selection': {'maps': sorted(selected_maps), 'assets': list(assets), 'native_classes': list(native_classes),
                          'directories': list(directories), 'dispatch_families': list(families)},
            'definitions': [{k: v for k, v in row.items() if k != 'dispatch'} for row in selected],
            'groups': {field: dict(sorted(values.items())) for field, values in groups.items()},
            'dispatch_families': family_groups, 'maps': map_rows, 'archive_indexes': index_rows,
            'unresolved_input_rows': unresolved, 'limits': LIMITS}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--report', type=Path, action='append', required=True)
    parser.add_argument('--package-mft', type=Path, required=True)
    parser.add_argument('--bundle-manifest', type=Path, required=True)
    parser.add_argument('--map', action='append', default=[])
    parser.add_argument('--asset', action='append', default=[])
    parser.add_argument('--native-class', action='append', default=[])
    parser.add_argument('--directory', action='append', default=[])
    parser.add_argument('--family', action='append', default=[], help='Exact dispatch-family ID from a prior report')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        game, output = args.game.resolve(strict=True), args.output.resolve()
        inputs = [p.resolve(strict=True) for p in args.report + [args.package_mft, args.bundle_manifest]]
        if not game.is_dir() or len(args.report) > 8 or len(set(inputs)) != len(inputs):
            raise ValueError('Game directory or unique report input count')
        if output in inputs or output.exists() or output.is_relative_to(game):
            raise ValueError('New output outside game installation required; inputs/outputs cannot be overwritten')
        if not output.parent.is_dir() or output.suffix.lower() != '.json':
            raise ValueError('Existing output directory and .json filename required')
        raw, receipts = [], []
        for path in inputs:
            data, receipt = load(path)
            raw.append(data)
            receipts.append(receipt)
        if sum(map(len, raw)) > MAX_TOTAL_INPUT:
            raise ValueError('Combined input bound')
        documents = [strict_json(data) for data in raw[:-2]]
        sections = parse_package(raw[-2].decode('ascii'))
        bundles = parse_bundle_manifest(raw[-1].decode('ascii'))
        _, origins, _ = collect(documents)
        selected_maps = {map_source(m) for m in args.map} if args.map else {
            s for s in sections if re.fullmatch(r'game:levels/[^/]+/level-00\.fbrb', s)}
        targets = set(origins) | {t for m in selected_maps for t in sections.get(m, [])}
        indexes = read_indexes(game, targets, origins)
        result = plan(documents, sections, bundles, indexes, maps=args.map, assets=args.asset,
                      native_classes=args.native_class, directories=args.directory, families=args.family)
        result['inputs'] = receipts
        data = json.dumps(result, indent=2, allow_nan=False).encode() + b'\n'
        if len(data) > MAX_OUTPUT:
            raise ValueError('Output exceeds 8 MiB; select fewer maps or configuration groups')
        with output.open('xb') as stream:
            stream.write(data)
        print(json.dumps({'output': str(output), 'bytes': len(data), 'definitions': len(result['definitions']),
                          'maps': len(result['maps']), 'unresolved_rows': len(result['unresolved_input_rows']),
                          'live_native_verified': False}))
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
