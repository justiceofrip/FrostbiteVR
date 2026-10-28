"""Select exact campaign configurations and prepare bounded, nonmutating bench jobs.

This is a private development tool. It reads saved metadata, never opens BC2,
mounts a bundle, changes ammunition, or calls a native function. Execution is a
separate explicit PowerShell operation. A name/class match never grants support.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import uuid

# The bundled embeddable Python intentionally omits the script directory.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bc2_test_range_catalog import configuration_reports, strict_json, path_key, map_source
from bc2_magazine_descriptor_manifest import digest
from bc2_magazine_registry_header import FAMILY, SINGLE_FIRE_FAMILY, AUTOMATIC_STOCK_BOLT_FAMILY
from bc2_authored_grip_bindings import matrix as authored_matrix

LIMIT = 8 * 1024 * 1024
SCHEMA = 'fvr.bc2.campaign_bench_plan.v1'
CONFIG = 'fvr.bc2.campaign_bench_config.v1'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read(path):
    path = Path(path).resolve(strict=True)
    if not path.is_file() or path.stat().st_size > LIMIT:
        raise ValueError('Bounded JSON file required: ' + str(path))
    data = path.read_bytes()
    if len(data) > LIMIT:
        raise ValueError('Input grew beyond limit')
    return strict_json(data.decode('utf-8-sig')), {'path': str(path), 'bytes': len(data), 'sha256': sha(data)}


def check_pin(pin):
    path = Path(pin['path']).resolve(strict=True)
    if path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError('Pinned metadata/source exceeds bound')
    data = path.read_bytes()
    if sha(data) != pin['sha256'] or ('bytes' in pin and len(data) != pin['bytes']):
        raise ValueError('Pinned input changed: ' + str(path))
    return path


def exact(resource, guid, content):
    if not re.fullmatch('[a-f0-9]{64}', content):
        raise ValueError('Exact configuration content hash required')
    return (path_key(resource), str(uuid.UUID(guid)), content)


def enrollment_rows(documents):
    rows = {}
    for document in documents:
        if document.get('schema') != 'fvr.bc2.exact-resource-visibility-body-enrollment.v1':
            raise ValueError('Exact resource/visibility/body enrollment required')
        for p in document['profiles']:
            identity = p['identity']['weapon']
            key = exact(identity['resource'], identity['guid'], identity['sha256'])
            # This document also contains visibility/body data. Only its typed
            # magazine descriptor is reload metadata; body enrollment alone is
            # never a magazine route. The compiler still owns family admission.
            family = p.get('family', document.get('family'))
            if p.get('magazine_part') and (p.get('key') != 'bc2:magazine-descriptor:' + digest(p['identity'])
                             or p['descriptor_digest'] != digest(p['configuration'])
                             or any(p['identity'].get(f) != p['configuration'].get(f) for f in ('assetName', 'assetPath'))
                             or path_key(p['identity']['assetPath'] + '.dbx') != key[0]):
                raise ValueError('Magazine descriptor identity/configuration differs')
            magazine = bool(p.get('magazine_part') and family in
                            (FAMILY, SINGLE_FIRE_FAMILY, AUTOMATIC_STOCK_BOLT_FAMILY))
            row = {'family': family,
                   'descriptor_digest': p['descriptor_digest'], 'native_id': p['native_id'],
                   'asset_path': p['identity']['assetPath'],
                   'configured_meshes': p['configured_meshes'], 'body_part': p.get('body_part'),
                   'magazine_metadata': magazine, 'magazine_part': p.get('magazine_part'),
                   'rig_fingerprint': rig_key(p['rig_fingerprint']) if p.get('rig_fingerprint') else None,
                   'required_archives': p.get('required_archives', []),
                   'compiled_enrollment_verified': False, 'native_tested': False}
            if key in rows and rows[key] != row:
                raise ValueError('Conflicting exact enrollment')
            rows[key] = row
    return rows


def rig_key(value):
    if type(value) is int and 0 < value < 2**64:
        return format(value, '016x')
    if isinstance(value, str) and re.fullmatch(r'(?:fnv1a64:)?[a-fA-F0-9]{16}', value):
        return value.removeprefix('fnv1a64:').lower()
    raise ValueError('Exact rig fingerprint required')


def feature_rows(body_documents=(), support_documents=()):
    """Join feature-specific saved metadata, never native/compiled admission.

    Unconfigured legacy props cannot be assigned to an exact weapon by name.
    Support's source_weapons supplies its own exact identity, even for weapons
    with no reload descriptor. Its mesh/rig must also agree with other supplied
    exact-configuration records before combined readiness can be reported.
    """
    bodies, supports = {}, {}
    for document in body_documents:
        if document.get('schema') != 'fvr.bc2.body_ammo_assets' or document.get('version') != 1:
            raise ValueError('Body asset catalog schema mismatch')
        for p in document['profiles']:
            if not p.get('configuration_path'):
                continue
            i = p['authored_configuration']
            key = exact(i['resource'], i['instance_guid'], i['sha256'])
            meshes = sorted({path_key(m) for m in p['configured_meshes']})
            if (path_key(p['configuration_path'] + '.dbx') != key[0] or not meshes
                    or path_key(p['mesh']) not in meshes or p.get('display_only') is not True
                    or p.get('display_anchor_not_native_grip_calibration') is not True):
                raise ValueError('Exact configured display-only body record required')
            anchor = p.get('holster_from_weapon')
            has_anchor = anchor is not None and anchor != []
            if has_anchor:
                # holster_transform produces a finite rigid affine basis, not
                # a scale-bearing animation transform or native grip pose.
                authored_matrix(anchor)
            row = {'part': p['part'], 'configured_meshes': meshes,
                   'rig_fingerprint': rig_key(p['rig_fingerprint']),
                   'display_only': True, 'native_grip_calibration': False,
                   'holster_anchor_metadata': has_anchor}
            if key in bodies and bodies[key] != row:
                raise ValueError('Conflicting exact body metadata')
            bodies[key] = row
    for document in support_documents:
        if (document.get('schema') != 'fvr.bc2.authored_support_bindings'
                or document.get('schema_version') != 1):
            raise ValueError('Authored support binding schema mismatch')
        for p in document['profiles']:
            if p.get('support_contact_reference') is not True:
                continue
            if (not re.fullmatch('[a-f0-9]{64}', p['binding_digest'])
                    or digest({k: v for k, v in p.items() if k != 'binding_digest'}) != p['binding_digest']
                    or p.get('left_hand_status') not in ('static_authored_pose', 'bounded_decoded_reference')
                    or len(p.get('left_hand_in_weapon', [])) != 16):
                raise ValueError('Authored support reference metadata required')
            authored_matrix(p['left_hand_in_weapon'])
            row = {'binding_digest': p['binding_digest'],
                   'configured_mesh_path': path_key(p['configured_mesh_path']),
                   'rig_fingerprint': rig_key(p['rig_fingerprint']),
                   'left_hand_status': p['left_hand_status']}
            for i in p['source_weapons']:
                key = exact(i['resource'], i['instance_guid'], i['sha256'])
                matches = supports.setdefault(key, [])
                if row not in matches:
                    matches.append(row)
    return bodies, supports


def feature_readiness(mechanism, enrollment, body, supports):
    missing = 'missing_from_supplied_metadata'
    expected = [p for p in (enrollment, body) if p]
    compatible = [p for p in supports if all(
        p['configured_mesh_path'] in {path_key(m) for m in e['configured_meshes']}
        and (not e.get('rig_fingerprint') or p['rig_fingerprint'] == e['rig_fingerprint'])
        for e in expected)]
    body_matches = not (body and enrollment) or (
        body['configured_meshes'] == sorted({path_key(m) for m in enrollment['configured_meshes']})
        and body['part'] == enrollment['body_part']
        and (not enrollment['rig_fingerprint'] or body['rig_fingerprint'] == enrollment['rig_fingerprint']))
    body_status = ('incompatible_metadata' if not body_matches else 'configured_metadata') if body else missing
    return {
        'reload': {'status': 'configured_metadata' if mechanism else missing, 'mechanism': mechanism},
        'support': {'status': ('configured_metadata' if len(compatible) == 1 else
                               'ambiguous_metadata' if compatible else 'incompatible_metadata') if supports else missing,
                    'exact_bindings': supports, 'compatible_binding_count': len(compatible)},
        'body_display': {'status': body_status, 'record': body},
        'holster': {'status': body_status if body and body['holster_anchor_metadata'] else missing,
                    'scope': 'configured_body_and_display_anchor_only'},
    }


def typed_rows(documents):
    """Retain complete typed fields for the existing read-only resource scanner."""
    rows = {}
    for document in documents:
        reports, _ = configuration_reports(document)
        for report in reports:
            for row in report['resolved_weapons']:
                key = exact(row['resource'], row['instance_guid'], row['resource_sha256'])
                previous = rows.get(key)
                if previous and previous['fields'] != row['fields']:
                    raise ValueError('Conflicting exact typed firing fields')
                rows[key] = row
    return rows


def plan(catalog, enrollments, config, selected_map, selections=(), typed=None, evidence=(), runtime_sha=None,
         *, body_documents=(), support_documents=()):
    if catalog.get('schema') != 'fvr.bc2.test_range_catalog.v1' or config.get('schema') != CONFIG:
        raise ValueError('Bench/catalog schema mismatch')
    if len(catalog['definitions']) > 4096:
        raise ValueError('Definition count exceeds bound')
    source = map_source(selected_map)
    maps = [m for m in catalog['maps'] if m['source'] == source]
    if len(maps) != 1:
        raise ValueError('Map must occur exactly once in the supplied catalog')
    mount = maps[0]
    configured = enrollment_rows(enrollments)
    bodies, support = feature_rows(body_documents, support_documents)
    accepted = {}
    for item in evidence:
        if (item.get('schema') != 'fvr.bc2.campaign_bench.acceptance.v1'
                or item.get('independent_native_review') is not True
                or item.get('accepted') is not True or not item.get('accepted_features')):
            raise ValueError('Explicit independently reviewed native acceptance required')
        identity = item['configuration']
        key = exact(identity['resource'], identity['instance_guid'], identity['resource_sha256'])
        if key in accepted:
            raise ValueError('Duplicate native acceptance identity')
        if not set(item['accepted_features']) <= {'magazine', 'shell', 'pump', 'bolt', 'support', 'holster', 'fire'}:
            raise ValueError('Unsupported acceptance scope')
        accepted[key] = item
    known = {}
    for row in config.get('known_configurations', []):
        key = exact(row['resource'], row['instance_guid'], row['resource_sha256'])
        if key in known or row['mechanism'] not in config['mechanisms']:
            raise ValueError('Duplicate or unsupported exact mechanism metadata')
        known[key] = row
    available = set(mount['mounted_definition_candidates'])
    ambiguity = {a['definition']: a for a in mount['ambiguous_definition_candidates']}
    if len(ambiguity) != len(mount['ambiguous_definition_candidates']):
        raise ValueError('Duplicate ambiguous availability identity')
    ambiguous = set(ambiguity)
    uncovered = set(mount['uncovered_definitions'])
    ids = [d['id'] for d in catalog['definitions']]
    if len(set(ids)) != len(ids) or (available & ambiguous or available & uncovered or ambiguous & uncovered):
        raise ValueError('Duplicate/overlapping availability identities')
    if available | ambiguous | uncovered != set(ids):
        raise ValueError('Availability must partition every supplied definition')
    rows = []
    for definition in catalog['definitions']:
        key = exact(definition['resource'], definition['instance_guid'], definition['resource_sha256'])
        # Recompute the catalog identity, rather than trusting a substituted ID.
        identity = dict(resource=key[0], instance_guid=key[1], resource_sha256=key[2])
        if sha(json.dumps(identity, sort_keys=True, separators=(',', ':')).encode()) != definition['id']:
            raise ValueError('Catalog identity digest differs')
        enrollment = configured.get(key)
        named_source = known.get(key)
        mechanism = (named_source['mechanism'] if named_source else
                     'magazine' if enrollment and enrollment['magazine_metadata'] else None)
        features = feature_readiness(mechanism, enrollment, bodies.get(key), support.get(key, []))
        proof = accepted.get(key)
        current_proof = bool(proof and runtime_sha and proof['runtime']['sha256'] == runtime_sha)
        availability = ('mounted_candidate' if definition['id'] in available else
                        'ambiguous_overlay' if definition['id'] in ambiguous else 'not_cataloged_for_map')
        rows.append({**identity, 'id': definition['id'], 'name': definition['native_name'],
                     'native_class': definition['native_class'], 'availability': availability,
                     'availability_evidence': ambiguity.get(definition['id']),
                     'coverage': 'configured_metadata' if enrollment or named_source or bodies.get(key) or support.get(key) else 'unsupported_by_supplied_enrollments',
                     'enrollment': enrollment, 'native_tested': current_proof, 'runtime_enabled': False,
                     'accepted_features': proof['accepted_features'] if current_proof else [],
                     'historical_native_evidence': proof if proof and not current_proof else None,
                     'mechanism': mechanism, 'feature_readiness': features,
                     'manual_magazine_metadata_eligible': mechanism == 'magazine',
                     'combined_metadata_complete': all(f['status'] == 'configured_metadata' for f in features.values()),
                     'metadata_gaps': [name for name, f in features.items() if f['status'] != 'configured_metadata'],
                     'configured_source': named_source, 'historical_evidence_is_current_acceptance': False,
                     'world_pickup_spawn_supported': False,
                     'dispatch': catalog['dispatch_families'][definition['dispatch_family']]})
    chosen = []
    for selection in selections:
        matches = [r for r in rows if r['id'] == selection or r['resource'] == selection.lower()]
        if len(matches) != 1:
            raise ValueError('Select one exact ID (resource path is ambiguous or absent): ' + selection)
        if matches[0] in chosen:
            raise ValueError('Duplicate loadout selection')
        chosen.append(matches[0])
    if len(chosen) > min(2, config['limits']['loadout_items']):
        raise ValueError('Bench uses at most the two existing native inventory slots')
    selectors, missing = [], []
    required = ('Ammo.MagazineCapacity', 'FireLogic.FireLogicType', 'FireLogic.ReloadType',
                'FireLogic.BoltAction.BoltActionDelay', 'FireLogic.BoltAction.BoltActionTime',
                'FireLogic.BoltAction.HoldBoltActionUntilFireRelease',
                'FireLogic.BoltAction.HoldBoltActionUntilZoomRelease')
    for row in chosen:
        key = exact(row['resource'], row['instance_guid'], row['resource_sha256'])
        original = (typed or {}).get(key)
        absent = [f for f in required if not original or f not in original['fields']]
        if absent:
            missing.append({'id': row['id'], 'missing_fields': absent})
            continue
        selectors.append({'name': row['name'], 'path': row['resource'][:-4],
                          'configuration': {'resource_sha256': row['resource_sha256'],
                                            'fields': original['fields'], 'missing_fields': []},
                          'measured_family_geometry': bool(row['enrollment'] and row['enrollment']['magazine_metadata'])})
    return {'schema': SCHEMA, 'planning_only': True, 'process_opened': False,
            'native_calls': False, 'bundle_mounts_changed': False, 'runtime_admission_granted': False,
            'map': source, 'map_live_verified': False, 'catalog_complete': False,
            'rows': rows, 'loadout': [r['id'] for r in chosen],
            'selector_document': {'schema': 'fvr.bc2.authored_resource_selectors.v1', 'selectors': selectors},
            'selector_gaps': missing, 'unresolved_input_rows': catalog.get('unresolved_input_rows', []),
            'asset_budget': {'maximum_bytes': min(config['limits']['body_cache_bytes'], 16777216),
                             'maximum_records': min(config['limits']['body_cache_records'], 64),
                             'assets_extracted': False, 'cache_verified': False,
                             'selected_body_parts': sorted({part for r in chosen for part in
                                 [r['enrollment']['body_part'] if r['enrollment'] else None,
                                  (r['feature_readiness']['body_display']['record'] or {}).get('part')] if part})},
            'mechanism_sequences': config['mechanisms'], 'limits': config['limitations'],
            'historical_or_current_native_results_imported': bool(evidence),
            'instructions': ['Use a fresh native process on the selected existing map.',
                             'Run the existing read-only scanner with selector_document.',
                             'Validate the pinned provisioning binding before Observe/Equip/Restore.',
                             'Do not call provisioning while a mod session is active.',
                             'A completed provision job is not manual reload acceptance.']}


def decode_provision_variants(text):
    pattern = re.compile(r'^\{(\d+),0x([0-9a-f]+)ull,(\d+)ull,(true|false),\{"([^"]+)","([^"]+)"', re.M)
    variants = [{'variant': int(m[1]), 'descriptor_profile_id': format(int(m[2], 16), 'x'),
                 'native_profile_id': format(int(m[3]), 'x') if m[4] == 'true' else None,
                 'name': m[5], 'path': m[6]} for m in pattern.finditer(text)]
    if not variants or len(variants) > 64 or len({v['variant'] for v in variants}) != len(variants):
        raise ValueError('Exact compiled variant table could not be decoded')
    return variants


def provision_binding(build_path, header_path):
    """Bind the existing, tested 426 positional ABI; never infer a variant number."""
    build, receipt = read(build_path)
    if (build.get('schema') != 'fvr.bc2.nambu-provision426.build.v1' or build.get('architecture') != 'x86'
            or build.get('service_changed') is not False or build.get('offsets_added') is not False
            or {t['architecture'] for t in build['tests'] if t.get('passed') is True} != {'x86', 'x64'}):
        raise ValueError('Tested unchanged native provisioning ABI receipt required')
    prepared_path = Path(build_path).resolve().parent.parent / 'prepared-inputs.json'
    prepared, prepared_pin = read(prepared_path)
    if prepared_pin['sha256'] != build['prepared_inputs_sha256']:
        raise ValueError('Provisioning compilation inputs changed')
    for p in prepared['files']:
        check_pin(p)
    header_path = Path(header_path).resolve(strict=True)
    match = [p for p in prepared['files'] if Path(p['path']).resolve() == header_path]
    if len(match) != 1 or header_path.name != 'Bc2ExactProvisionVariants.h':
        raise ValueError('Variant header must belong to the tested compilation receipt')
    variants = decode_provision_variants(header_path.read_text())
    binaries = {}
    for p in build['binaries']:
        path = check_pin(p)
        if path.name == 'ProvisionRunner.exe':
            binaries['runner'] = p
        elif path.name == 'BC2ProvisionHarness426.dll':
            binaries['dll'] = p
    if set(binaries) != {'runner', 'dll'}:
        raise ValueError('Expected existing provisioning runner and harness')
    return {'build': receipt, 'prepared_inputs': prepared_pin, 'source_pins': prepared['files'],
            'header': match[0], 'variants': variants, **binaries}


def write_new(path, value):
    path = Path(path).resolve()
    # Private tooling only. Shipped installer/runtime path rules are independent.
    if path.drive.upper() != 'G:' or path.exists() or not path.parent.is_dir() or path.suffix.lower() != '.json':
        raise ValueError('New .json file in an existing G: output folder required')
    data = json.dumps(value, indent=2, allow_nan=False).encode() + b'\n'
    if len(data) > LIMIT:
        raise ValueError('Bench output exceeds 8 MiB')
    with path.open('xb') as stream:
        stream.write(data)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--map', required=True)
    parser.add_argument('--config', type=Path, default=Path(__file__).resolve().parents[1] / 'config/campaign-bench.json')
    parser.add_argument('--enrollment', type=Path, action='append', default=[])
    parser.add_argument('--typed-report', type=Path, action='append', default=[])
    parser.add_argument('--select', action='append', default=[])
    parser.add_argument('--runtime-receipt', type=Path)
    parser.add_argument('--native-evidence', type=Path, action='append', default=[],
                        help='Explicit independent native acceptance; completion alone never grants support')
    parser.add_argument('--provision-build', type=Path)
    parser.add_argument('--provision-header', type=Path)
    parser.add_argument('--selector-output', type=Path, help='Optional separate scanner selectors; no game access')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.output.exists() or args.output.drive.upper() != 'G:':
            raise ValueError('Use a new private G: output file')
        if args.selector_output and (args.selector_output.exists() or args.selector_output.drive.upper() != 'G:'
                                     or args.selector_output.resolve() == args.output.resolve()):
            raise ValueError('Separate new G: selector output required')
        catalog, catalog_pin = read(args.catalog)
        config, config_pin = read(args.config)
        root = Path(__file__).resolve().parents[1]
        paths = args.enrollment or [root / p for p in config['enrollment_documents']]
        body_paths = [root / path_key(p) for p in config.get('body_asset_documents', [])]
        support_paths = [root / path_key(p) for p in config.get('support_binding_documents', [])]
        if (len(paths) > 16 or len(body_paths) > 4 or len(support_paths) > 16
                or len(args.typed_report) > 8 or len(args.native_evidence) > 32):
            raise ValueError('Too many input documents')
        enrollments, pins = [], [catalog_pin, config_pin]
        typed_documents = []
        for path in paths:
            value, pin = read(path); enrollments.append(value); pins.append(pin)
        body_documents, support_documents = [], []
        for paths, documents in ((body_paths, body_documents), (support_paths, support_documents)):
            for path in paths:
                if not path.resolve(strict=True).is_relative_to(root):
                    raise ValueError('Feature metadata must remain inside the repository')
                value, pin = read(path); documents.append(value); pins.append(pin)
        for row in config.get('known_configurations', []):
            for field in ('source', 'historical_evidence'):
                relative = path_key(row[field])
                source = (root / relative).resolve(strict=True)
                if not source.is_relative_to(root) or source.stat().st_size > LIMIT:
                    raise ValueError('Bounded repository metadata source required')
                data = source.read_bytes()
                pins.append({'path': str(source), 'bytes': len(data), 'sha256': sha(data)})
        for path in args.typed_report:
            value, pin = read(path); typed_documents.append(value); pins.append(pin)
        if sum(p['bytes'] for p in pins) > 32 * 1024 * 1024:
            raise ValueError('Combined metadata exceeds 32 MiB')
        native = []
        runtime_pin = None
        if args.runtime_receipt:
            _, runtime_pin = read(args.runtime_receipt); pins.append(runtime_pin)
        for path in args.native_evidence:
            value, pin = read(path); pins.append(pin)
            # Review is explicit and scoped. This is an evidence join, not a new
            # independent native callback auditor or a trust-in-driver shortcut.
            for field in ('runtime', 'native_report', 'driver_completion', 'independent_audit'):
                check_pin(value[field]); pins.append(value[field])
            native.append(value)
        result = plan(catalog, enrollments, config, args.map, args.select, typed_rows(typed_documents),
                      native, runtime_pin['sha256'] if runtime_pin else None,
                      body_documents=body_documents, support_documents=support_documents)
        if bool(args.provision_build) != bool(args.provision_header):
            raise ValueError('Provide both provisioning build receipt and its compiled variant header')
        result['provisioning'] = provision_binding(args.provision_build, args.provision_header) if args.provision_build else None
        result['inputs'] = pins
        result['runtime'] = runtime_pin
        result['planner'] = {'path': str(Path(__file__).resolve()), 'sha256': sha(Path(__file__).read_bytes())}
        if args.selector_output and (result['selector_gaps'] or not result['selector_document']['selectors']):
            raise ValueError('Complete typed fields for every selected configuration required for scanner output')
        # Catalog provenance identifies the installed manifest; never write a
        # bench plan/selector into that game tree even though it is on G:.
        for item in catalog.get('inputs', []):
            source_path = Path(item['path'])
            if source_path.name.lower() == 'package.mft' and source_path.parent.name.lower() == 'win32':
                game_root = source_path.parent.parent.parent.resolve()
                for output in (args.output, args.selector_output):
                    if output and output.resolve().is_relative_to(game_root):
                        raise ValueError('Output cannot be inside the game installation')
        write_new(args.output, result)
        if args.selector_output:
            write_new(args.selector_output, result['selector_document'])
        print(json.dumps({'output': str(args.output), 'rows': len(result['rows']),
                          'selected': len(result['loadout']), 'planning_only': True, 'native_calls': False}))
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
