"""Compile PRIVATE feed/slide registries from reviewed, pinned authored evidence.

This is an offline join, never an archive extractor or a native qualification.
Geometry/rig fields are JSON-pointer references to hashed existing pipeline
outputs. Role names and VR policy are explicit annotations. Native descriptor
reviews and mechanism admissions are separate, exact-digest inputs. Rejected
rows and missing admissions remain visible in the receipt; family coverage is
never reported as enabled production coverage.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import uuid

import bc2_magazine_descriptor_manifest as descriptor
import bc2_magazine_registry_header as native_registry

SCHEMA = 'fvr.bc2.mechanism_registry_annotations.v1'
ADMISSIONS = 'fvr.bc2.mechanism_admission_reviews.v1'
CLASSES = {'feed': ('wcLmg', 'AutomaticMagazineZeroBoltTransfer'),
           'pistol_slide': ('wcSmg', 'SingleFireMagazineZeroBolt')}
FEED_FLOATS = ('completeFraction', 'releaseFraction', 'pathToleranceMeters',
               'orientationToleranceRadians', 'maxStepMeters', 'maxStepRadians')
FEED_TIMES = ('endpointDwellNs', 'maximumGestureNs', 'maxInputGapNs')
SLIDE_FLOATS = ('sourceDisplacementMeters', 'strokeMeters', 'frontToleranceMeters',
                'rearToleranceMeters', 'hysteresisMeters', 'contactRadiusMeters',
                'lateralToleranceMeters', 'rotationToleranceRadians', 'maxStepMeters')
SLIDE_TIMES = ('endpointDwellNs', 'maximumStrokeNs')
SLIDE_MATRICES = ('weaponFromStaticSlide', 'slideFromAuthoredWrist')
SLIDE_VECTORS = ('rearward', 'partBoundsMin', 'partBoundsMax', 'ring3Part', 'nearestVertexPart')
SLIDE_DOUBLES = ('minimumDisplacementMeters', 'maximumDisplacementMeters',
                 'authoredLandmarkSeconds', 'ring3VertexDistanceMeters')
FEED_MATRICES = ('weaponFromHinge', 'hingeFromOpeningContact',
                 'hingeFromClosingContact', 'weaponFromCoverReference')
SOURCE_HASHES = ('meshSha256', 'lodSha256', 'skeletonSha256', 'clipSha256')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def exact_fields(value, names, context):
    require(isinstance(value, dict), context + ': expected object')
    missing, extra = set(names) - set(value), set(value) - set(names)
    require(not missing and not extra,
            f'{context}: missing {sorted(missing)}; unknown {sorted(extra)}')


def integer(value, low, high, context):
    require(type(value) is int and low <= value <= high, context + ': integer bound')
    return value


def finite(value, low=-1e6, high=1e6):
    require(type(value) in (int, float) and math.isfinite(value) and low <= value <= high,
            'Nonfinite or out-of-bounds measurement')
    return value


def matrix(value):
    require(isinstance(value, list) and len(value) == 16, 'Expected 16-value rigid transform')
    value = [descriptor.f32(finite(x)) for x in value]
    require(all(abs(value[i]) < 1e-5 for i in (3, 7, 11)) and abs(value[15] - 1) < 1e-5,
            'Invalid row-vector affine transform')
    rows = [value[4*i:4*i+3] for i in range(3)]
    require(all(abs(sum(a*b for a, b in zip(rows[i], rows[j])) - (i == j)) < 1e-4
                for i in range(3) for j in range(3)), 'Nonrigid transform')
    a, b, c = rows
    det = (a[0]*(b[1]*c[2]-b[2]*c[1]) - a[1]*(b[0]*c[2]-b[2]*c[0])
           + a[2]*(b[0]*c[1]-b[1]*c[0]))
    require(abs(det - 1) < .0003, 'Reflected transform is not a proper rigid frame')
    return value


def vector(value):
    require(isinstance(value, list) and len(value) == 3, 'Expected three-vector')
    return [descriptor.f32(finite(x)) for x in value]


def fingerprint(value):
    if isinstance(value, str):
        require(re.fullmatch(r'(?:fnv1a64:|0x)?[0-9a-f]{16}', value) is not None,
                'Invalid rig fingerprint')
        value = int(value.removeprefix('fnv1a64:').removeprefix('0x'), 16)
    return integer(value, 1, 2**64-1, 'rigFingerprint')


def pointer(document, path):
    require(isinstance(path, str) and (path == '' or path.startswith('/')),
            'Expected JSON pointer')
    for token in path.split('/')[1:]:
        require(re.search(r'~(?![01])', token) is None, 'Invalid JSON pointer escape')
        token = token.replace('~1', '/').replace('~0', '~')
        if isinstance(document, list):
            require(re.fullmatch(r'0|[1-9][0-9]*', token) is not None, 'Invalid array pointer')
            require(int(token) < len(document), 'Missing evidence array element')
            document = document[int(token)]
        else:
            require(isinstance(document, dict) and token in document, 'Missing evidence pointer: ' + path)
            document = document[token]
    return document


class Evidence:
    def __init__(self, pins, loaded):
        require(isinstance(pins, dict) and 0 < len(pins) <= 512, 'Evidence source count bound')
        self.pins, self.loaded = pins, loaded
        paths = set()
        for name, pin in pins.items():
            descriptor.text(name, 128)
            exact_fields(pin, ('path', 'sha256'), 'source pin')
            path = descriptor.text(pin['path'], 1024)
            require('\\' not in path and ':' not in path and not path.startswith('/')
                    and all(x not in ('', '.', '..') for x in path.split('/')), 'Noncanonical evidence path')
            require(path.casefold() not in paths, 'Duplicate evidence path')
            paths.add(path.casefold())
            require(name in loaded and descriptor.sha(pin['sha256']) == loaded[name][1],
                    'Missing or mismatched evidence source hash: ' + name)

    def ref(self, reference):
        exact_fields(reference, ('source', 'pointer'), 'measurement reference')
        name = reference['source']
        require(name in self.pins, 'Unknown evidence source')
        return pointer(self.loaded[name][0], reference['pointer'])

    def measured(self, value):
        # Structures may be assembled from multiple existing outputs. Every
        # leaf must still come from a pinned source, never an annotation literal.
        if isinstance(value, dict):
            if 'source' in value or 'pointer' in value:
                return self.ref(value)
            return {k: self.measured(v) for k, v in value.items()}
        if isinstance(value, list):
            return [self.measured(v) for v in value]
        raise ValueError('Measured fields require source/pointer references')

    def contexts(self, value):
        """Visit enclosing records, so a pointer cannot hide another config."""
        if isinstance(value, dict) and ('source' in value or 'pointer' in value):
            self.ref(value)
            document = self.loaded[value['source']][0]
            yield document
            prefix = ''
            for token in value['pointer'].split('/')[1:]:
                prefix += '/' + token
                yield pointer(document, prefix)
        elif isinstance(value, dict):
            for child in value.values(): yield from self.contexts(child)
        elif isinstance(value, list):
            for child in value: yield from self.contexts(child)


def config_identity(value):
    require(isinstance(value, dict), 'Missing exact authored configuration evidence')
    guid = value.get('guid', value.get('instance_guid'))
    return {'resource': descriptor.resource(value.get('resource')),
            'sha256': descriptor.sha(value.get('sha256')),
            'guid': str(uuid.UUID(guid))}


def check_geometry_identity(record, selection, weapon, required=False):
    """Join existing pipeline naming conventions, not weapon-specific labels."""
    if not isinstance(record, dict):
        require(not required, 'Geometry evidence must carry an exact identity record')
        return
    found = set()
    for name in ('weapon_configuration', 'weapon', 'configuration'):
        value = record.get(name)
        if isinstance(value, dict) and 'resource' in value:
            require(config_identity(value) == config_identity(weapon), 'Geometry/configuration evidence mismatch')
            found.add('configuration')
    for name in ('asset', 'asset_name', 'native_asset_name'):
        if name in record:
            require(record[name] == selection['asset'], 'Geometry asset evidence mismatch')
            found.add('asset')
    for name in ('configured_mesh', 'configured_mesh_path', 'meshPath', 'mesh'):
        if isinstance(record.get(name), str):
            require(record[name] == selection['meshPath'], 'Geometry mesh evidence mismatch')
            found.add('mesh')
    for name in ('rig_fingerprint', 'rigFingerprint', 'rig'):
        if name in record:
            require(fingerprint(record[name]) == selection['rigFingerprint'], 'Geometry rig evidence mismatch')
            found.add('rig')
    selector = record.get('selector')
    if isinstance(selector, dict):
        if 'asset_name' in selector:
            require(selector['asset_name'] == selection['asset'], 'Measured selector asset differs')
        if 'asset_path' in selector:
            require(selector['asset_path'] == selection['configurationPath'], 'Measured selector path differs')
    require(not required or found == {'configuration', 'asset', 'mesh', 'rig'},
            'Geometry evidence missing exact configuration/asset/mesh/rig identity')


def bone(value, parent=True):
    fields = ('bone', 'index', 'parent', 'parentIndex') if parent else ('bone', 'index')
    exact_fields(value, fields, 'reviewed bone role')
    descriptor.text(value['bone'], 128)
    integer(value['index'], 0, 4095, 'bone index')
    if parent:
        descriptor.text(value['parent'], 128)
        integer(value['parentIndex'], 0, 4095, 'parent bone index')
        require(value['index'] != value['parentIndex'], 'Self-parented bone role')


def validate_roles(kind, roles):
    names = ('root', 'pouch', 'cover', 'feed_chain') if kind == 'feed' else ('root', 'slide')
    exact_fields(roles, names, 'roles')
    bone(roles['root'], False)
    root = roles['root']
    children = [roles['pouch'], roles['cover']] if kind == 'feed' else [roles['slide']]
    if kind == 'feed':
        require(isinstance(roles['feed_chain'], list) and 1 <= len(roles['feed_chain']) <= 8,
                'Feed chain count bound')
        children += roles['feed_chain']
    seen_names, seen_indices = {root['bone']}, {root['index']}
    for child in children:
        bone(child)
        require(child['bone'] not in seen_names and child['index'] not in seen_indices,
                'Duplicate bone name/index in roles')
        seen_names.add(child['bone']); seen_indices.add(child['index'])
    direct = children[:2] + roles['feed_chain'][:1] if kind == 'feed' else children
    for child in direct:
        require((child['parent'], child['parentIndex']) == (root['bone'], root['index']),
                'Reviewed root topology differs')
    if kind == 'feed':
        for before, after in zip(roles['feed_chain'], roles['feed_chain'][1:]):
            require((after['parent'], after['parentIndex']) == (before['bone'], before['index']),
                    'Broken articulated feed chain')


def policy(kind, value):
    floats = FEED_FLOATS if kind == 'feed' else SLIDE_FLOATS
    times = FEED_TIMES if kind == 'feed' else SLIDE_TIMES
    meta = () if kind == 'feed' else ('name', 'sourceReference', 'revision')
    exact_fields(value, (*floats, *times, *meta), 'explicit interaction policy')
    out = dict(value)
    for field in floats:
        out[field] = descriptor.f32(finite(value[field], -1 if field == 'sourceDisplacementMeters' else 0, 10))
        require(field == 'sourceDisplacementMeters' or out[field] > 0, 'Zero policy threshold: ' + field)
    for field in times:
        integer(value[field], 1, 10000000000, field)
    if kind == 'feed':
        require(0 < out['releaseFraction'] < out['completeFraction'] <= 1, 'Feed fraction order')
        require(out['endpointDwellNs'] < out['maximumGestureNs'] and out['maxInputGapNs'] <= 100000000,
                'Feed policy time bounds')
        require(out['maxInputGapNs'] <= out['maximumGestureNs']
                and out['orientationToleranceRadians'] < math.pi/2 and out['maxStepRadians'] < math.pi,
                'Feed policy angular/time bounds')
    else:
        descriptor.text(value['name'], 256); descriptor.text(value['sourceReference'], 1024)
        integer(value['revision'], 1, 2**64-1, 'policy revision')
        require(5000000 <= out['endpointDwellNs'] <= 250000000
                and out['maximumStrokeNs'] >= 4*out['endpointDwellNs'], 'Slide policy time order')
        stroke = out['strokeMeters']
        require(.01 <= stroke <= descriptor.f32(.15)
                and all(out[k] <= stroke*.15 for k in ('frontToleranceMeters', 'rearToleranceMeters', 'hysteresisMeters'))
                and out['frontToleranceMeters'] + out['hysteresisMeters'] < stroke*.3
                and out['rearToleranceMeters'] + out['hysteresisMeters'] < stroke*.3
                and out['contactRadiusMeters'] <= descriptor.f32(.15)
                and out['lateralToleranceMeters'] <= descriptor.f32(.1)
                and out['rotationToleranceRadians'] <= .5 and out['maxStepMeters'] <= stroke*.5,
                'Slide stroke tolerance bound')
    return out


def geometry(kind, value, roles):
    out = dict(value)
    if kind == 'pistol_slide':
        exact_fields(value, (*SLIDE_MATRICES, *SLIDE_VECTORS, *SLIDE_DOUBLES, 'linearHelperSha256'), 'slide geometry')
        for field in SLIDE_MATRICES: out[field] = matrix(value[field])
        for field in SLIDE_VECTORS: out[field] = vector(value[field])
        for field in SLIDE_DOUBLES: finite(value[field], -1 if field == 'minimumDisplacementMeters' else 0, 100)
        descriptor.sha(value['linearHelperSha256'])
        require(abs(sum(x*x for x in out['rearward']) - 1) < 1e-5, 'Slide axis is not unit length')
        require(0 < value['maximumDisplacementMeters'] - value['minimumDisplacementMeters'] <= .5,
                'Invalid signed slide extrema')
        require(all(a < b for a, b in zip(out['partBoundsMin'], out['partBoundsMax'])), 'Invalid slide bounds')
        require(all(a <= p <= b for a, p, b in zip(out['partBoundsMin'], out['nearestVertexPart'], out['partBoundsMax'])),
                'Nearest slide vertex outside measured bounds')
        require(abs(math.dist(out['ring3Part'], out['nearestVertexPart']) - value['ring3VertexDistanceMeters']) < 1e-5,
                'Slide contact distance disagrees with measured points')
    else:
        optional = ('coverCompanions',) if 'coverCompanions' in value else ()
        exact_fields(value, (*SOURCE_HASHES, *FEED_MATRICES, 'coverTravelRadians', 'poses', *optional), 'feed geometry')
        for field in SOURCE_HASHES: descriptor.sha(value[field])
        for field in FEED_MATRICES: out[field] = matrix(value[field])
        out['coverTravelRadians'] = descriptor.f32(finite(value['coverTravelRadians'], -2*math.pi, 2*math.pi))
        require(abs(out['coverTravelRadians']) >= .001, 'Cover travel is degenerate')
        if 'coverCompanions' in value:
            companions = value['coverCompanions']
            require(isinstance(companions, list) and len(companions) <= 4, 'Cover companion count exceeds shared bound')
            existing = [roles['root'], roles['pouch'], roles['cover'], *roles['feed_chain']]
            out['coverCompanions'] = []
            for companion in companions:
                exact_fields(companion, ('bone', 'index', 'parent', 'parentIndex', 'weaponFromReference'), 'cover companion')
                role = {key: companion[key] for key in ('bone', 'index', 'parent', 'parentIndex')}
                bone(role)
                require(role['parent'] == roles['root']['bone'] and role['parentIndex'] == roles['root']['index'],
                        'Cover companion must be a measured root sibling')
                require(all(role['bone'] != other['bone'] and role['index'] != other['index'] for other in existing),
                        'Cover companion overlaps another part')
                existing.append(role)
                out['coverCompanions'].append({**role, 'weaponFromReference': matrix(companion['weaponFromReference'])})
        require(isinstance(value['poses'], list) and len(value['poses']) == 7, 'Seven explicit feed pose slots required')
        out['poses'] = []
        for pose in value['poses']:
            exact_fields(pose, ('authoredSeconds', 'weaponFromPouch', 'weaponFromFeedRoot',
                                'parentFromChainChild', 'sourceFromWrist'), 'feed pose')
            finite(pose['authoredSeconds'], 0, 15)
            require(isinstance(pose['parentFromChainChild'], list)
                    and len(pose['parentFromChainChild']) == len(roles['feed_chain']) - 1,
                    'Pose and role chain counts differ')
            out['poses'].append({**pose, 'weaponFromPouch': matrix(pose['weaponFromPouch']),
                'weaponFromFeedRoot': matrix(pose['weaponFromFeedRoot']),
                'sourceFromWrist': matrix(pose['sourceFromWrist']),
                'parentFromChainChild': [matrix(m) for m in pose['parentFromChainChild']]})
    return out


def mechanism(value):
    # The first registry supports only the reviewed coupled same-container
    # sequence. Missing latch, separate withdrawal and charge work stays absent.
    flags = {'cover': True, 'latch': False, 'container': True, 'charge': False,
             'containerNeedsOpen': True, 'feedNeedsOpen': True, 'detachNeedsUnseated': False,
             'sameContainerReplenishment': True, 'coupledContainerDetach': True}
    exact_fields(value, ('revision', 'maxAgeNs', 'contacts', 'ammunitionKind', *flags),
                 'feed mechanism requirements')
    integer(value['revision'], 1, 2**64-1, 'mechanism revision')
    integer(value['maxAgeNs'], 1, 100000000, 'mechanism maxAgeNs')
    integer(value['ammunitionKind'], 1, 2**32-1, 'ammunitionKind')
    require(all(value[k] is v for k, v in flags.items()), 'Unsupported feed sequence requirements')
    exact_fields(value['contacts'], ('Cover', 'Container', 'Feed'), 'feed contacts')
    for contact in value['contacts'].values():
        exact_fields(contact, ('role', 'radiusMeters', 'maxAngleRadians'), 'feed contact')
        integer(contact['role'], 1, 2**32-1, 'contact role')
        finite(contact['radiusMeters'], .0001, .5)
        finite(contact['maxAngleRadians'], .0001, math.pi)
    require(len({v['role'] for v in value['contacts'].values()}) == 3, 'Duplicate contact role IDs')
    return value


def resolve_row(row, job, evidence):
    fields = ('kind', 'descriptor_key', 'descriptor_digest', 'native_class', 'configuration', 'asset',
              'meshPath', 'rigFingerprint', 'roles', 'geometry', 'geometry_evidence', 'role_review',
              'reviewer', 'roles_reviewed', 'policy_reviewed', 'interaction_policy', 'enabled')
    if row.get('kind') == 'feed': fields += ('mechanism',)
    exact_fields(row, fields, 'annotation row')
    kind = row['kind']
    require(kind in CLASSES, 'Unknown mechanism kind')
    require(row['roles_reviewed'] is True and row['policy_reviewed'] is True, 'Missing explicit role/policy review')
    descriptor.text(row['reviewer'], 128)
    require(type(row['enabled']) is bool, 'Enabled must be explicit boolean')
    require(row['descriptor_digest'] == job['descriptor_digest'], 'Native descriptor digest differs')
    # BC2's authored magazine pistols, including MP443, use wcSmg. The
    # independently reviewed SingleFire dispatch below is the native boundary;
    # never relabel the source metadata as an invented wcPistol category.
    require(job['source_class'] == CLASSES[kind][0] and row['native_class'] == CLASSES[kind][1], 'Wrong supported native class')
    require(native_registry.eligible(job), 'Native descriptor prerequisites unresolved')
    v = job['configuration']['values']
    require(v['fireLogicType'] == (2 if kind == 'feed' else 0)
            and v['reloadDelay'] == 0 and v['postReloadTime'] == 0
            and all(v[k] == 0 for k in ('boltDelay', 'boltTime', 'holdBoltUntilFireRelease', 'holdBoltUntilZoomRelease')),
            'Wrong exact native dispatch class')
    require(config_identity(evidence.ref(row['configuration'])) == config_identity(job['identity']['weapon']),
            'Exact configuration resource/GUID/hash mismatch')
    require(evidence.ref(row['asset']) == job['identity']['assetName'], 'Exact asset name mismatch')
    mesh = evidence.ref(row['meshPath'])
    descriptor.resource(mesh + '.dbx')
    selection = {'asset': job['identity']['assetName'], 'configurationPath': job['identity']['assetPath'],
                 'configurationGuid': job['identity']['weapon']['guid'],
                 'configurationSha256': job['identity']['weapon']['sha256'],
                 'meshPath': mesh, 'rigFingerprint': fingerprint(evidence.ref(row['rigFingerprint']))}
    # Both references must resolve, even when only their pinned provenance is
    # emitted. Explicit annotations carry semantic review, never bone labels.
    anchor = evidence.ref(row['geometry_evidence']); evidence.ref(row['role_review'])
    check_geometry_identity(anchor, selection, job['identity']['weapon'], required=True)
    for field in ('roles', 'geometry', 'meshPath', 'rigFingerprint', 'role_review'):
        for context in evidence.contexts(row[field]):
            check_geometry_identity(context, selection, job['identity']['weapon'])
    roles = evidence.measured(row['roles']); validate_roles(kind, roles)
    geom = geometry(kind, evidence.measured(row['geometry']), roles)
    pol = policy(kind, row['interaction_policy'])
    if kind == 'pistol_slide':
        require(pol['sourceDisplacementMeters'] - pol['frontToleranceMeters'] - pol['hysteresisMeters'] >= geom['minimumDisplacementMeters']
                and pol['sourceDisplacementMeters'] + pol['strokeMeters'] + pol['rearToleranceMeters'] + pol['hysteresisMeters'] <= geom['maximumDisplacementMeters'],
                'VR policy exceeds measured signed rail extrema')
    else:
        require(abs(geom['coverTravelRadians']) < math.pi
                and pol['maxStepRadians'] < abs(geom['coverTravelRadians']), 'Unsupported cover gesture travel')
        for field in ('hingeFromOpeningContact', 'hingeFromClosingContact'):
            require(math.hypot(*geom[field][12:14]) > 2 * pol['pathToleranceMeters'],
                    'Cover contact lies inside hinge tolerance')
        def multiply(a, b):
            return [sum(a[i*4+k]*b[k*4+j] for k in range(4)) for i in range(4) for j in range(4)]
        def terminal(pose):
            frame = pose['weaponFromFeedRoot']
            for child in pose['parentFromChainChild']: frame = multiply(child, frame)
            return frame
        poses = geom['poses']
        moves = [(poses[1]['weaponFromPouch'], poses[2]['weaponFromPouch']),
                 (poses[2]['weaponFromPouch'], poses[4]['weaponFromPouch']),
                 (terminal(poses[4]), terminal(poses[6]))]
        for a, b in moves:
            distance = math.dist(a[12:15], b[12:15])
            cosine = (sum(a[i*4+j]*b[i*4+j] for i in range(3) for j in range(3)) - 1) / 2
            angle = math.acos(max(-1, min(1, cosine)))
            require((distance > 2*pol['pathToleranceMeters'] or angle > 2*pol['orientationToleranceRadians'])
                    and (distance > pol['maxStepMeters'] or angle > pol['maxStepRadians']),
                    'Feed source and destination cannot form distinct gesture regions')
    requirements = mechanism(row['mechanism']) if kind == 'feed' else None
    if requirements:
        require(pol['maxInputGapNs'] <= requirements['maxAgeNs'], 'Input gap exceeds registered evidence age')
    # Admission booleans are deliberately absent from the preimage they grant.
    payload = {'kind': kind, 'descriptor_key': row['descriptor_key'], 'descriptor_digest': row['descriptor_digest'],
               'native_class': row['native_class'], 'configuration': job['configuration'], 'selection': selection,
               'roles': roles, 'geometry': geom, 'interaction_policy': pol, 'mechanism': requirements,
               'role_review_sha256': evidence.pins[row['role_review']['source']]['sha256'],
               'geometry_evidence_sha256': evidence.pins[row['geometry_evidence']['source']]['sha256'],
               'evidence_references': {name: {**evidence.pins[row[name]['source']], 'pointer': row[name]['pointer']}
                                       for name in ('geometry_evidence', 'role_review')}}
    digest = descriptor.digest(payload)
    return {'payload': payload, 'profile_digest': digest, 'key': native_registry.stable_id(digest),
            'annotation_digest': descriptor.digest({k:v for k,v in row.items() if k != 'enabled'}),
            'annotation': row, 'job': job}


def literal(value):
    return 'std::bit_cast<float>(0x' + descriptor.bits(value) + 'u)'


def cpp_text(value):
    # ASCII JSON escapes are valid C++ string escapes for these bounded names.
    require(all(32 <= ord(c) < 127 for c in value), 'C++ identity strings must be printable ASCII')
    return json.dumps(value)


def emit_matrix(target, values):
    return [f'    {target}.values[{i//4}][{i%4}]={literal(x)};' for i, x in enumerate(values)]


def emit_native(index, job, reviewed):
    values = job['configuration']['values']
    words = ','.join('{0x%x,0x%08xu}' % (w['offset'], w['expected']) for w in job['configuration']['timing'])
    out = [f'inline constexpr std::array<ReloadTimingWord,6> MechanismTiming{index}{{{{{words}}}}};',
           f'inline constexpr ReloadConfigDescriptor MechanismNative{index}=[]{{', '    ReloadConfigDescriptor out{};']
    for name in ('assetName', 'assetPath'): out.append(f'    out.{name}={cpp_text(job["configuration"][name])};')
    for name in native_registry.ORDER:
        value = values[name]
        text = str(value).lower() if type(value) is bool else literal(value) if name in native_registry.ORDER[6:12] else str(value)
        out.append(f'    out.values.{name}={text};')
    out += [f'    out.timing=MechanismTiming{index};',
            '    out.admission=ReloadDescriptorAdmission::' + ('ReviewedNative' if reviewed else 'Candidate') + ';',
            '    return out;', '}();']
    return out


def emit_row(index, entry, evidence):
    p, row = entry['payload'], entry['annotation']
    kind, g, roles, pol = p['kind'], p['geometry'], p['roles'], p['interaction_policy']
    index = ('Feed' if kind == 'feed' else 'Slide') + str(index)
    prefix = f'Mechanism{index}'
    out = emit_native(index, entry['job'], entry['reviewed'])
    gtype = 'Bc2FeedGeometry' if kind == 'feed' else 'Bc2PistolSlideGeometry'
    out += [f'inline constexpr {gtype} {prefix}Geometry=[]{{', f'    {gtype} out{{}};']
    for name, value in p['selection'].items():
        if kind == 'feed' and name in ('configurationGuid', 'configurationSha256'): continue
        target = 'out.' + ('' if kind == 'feed' else 'selection.') + name
        out.append(f'    {target}=' + (str(value)+'ull' if type(value) is int else cpp_text(value)) + ';')
    if kind == 'pistol_slide':
        for name, value in {'evidencePath': evidence.pins[row['geometry_evidence']['source']]['path'],
                            'geometrySha256': evidence.pins[row['geometry_evidence']['source']]['sha256'],
                            'roleReviewPath': evidence.pins[row['role_review']['source']]['path'],
                            'linearHelperSha256': g['linearHelperSha256'],
                            'root': roles['root']['bone'], 'slide': roles['slide']['bone']}.items():
            out.append(f'    out.{name}={cpp_text(value)};')
        for role in ('root', 'slide'): out.append(f'    out.{role}Index={roles[role]["index"]};')
        for name in SLIDE_MATRICES: out += emit_matrix('out.'+name, g[name])
        for name in SLIDE_VECTORS: out.append(f'    out.{name}={{{",".join(literal(x) for x in g[name])}}};')
        for name in SLIDE_DOUBLES: out.append(f'    out.{name}={g[name]!r};')
        # Authored sample extrema/contact do not verify native/controller stops.
        out += ['    out.nativeClosedVerified=false;', '    out.nativeRetractedVerified=false;',
                '    out.stableControllerGraspVerified=false;']
    else:
        for name in SOURCE_HASHES: out.append(f'    out.{name}={cpp_text(g[name])};')
        for name in FEED_MATRICES: out += emit_matrix('out.'+name, g[name])
        out += [f'    out.coverTravelRadians={literal(g["coverTravelRadians"])};',
                f'    out.chainCount={len(roles["feed_chain"])};']
        for role in ('root', 'pouch', 'cover'):
            for name, value in roles[role].items():
                out.append(f'    out.{role}Bone.{name}=' + (cpp_text(value) if isinstance(value, str) else str(value)) + ';')
        for n, b in enumerate(roles['feed_chain']):
            for name, value in b.items(): out.append(f'    out.chain[{n}].{name}=' + (cpp_text(value) if isinstance(value, str) else str(value)) + ';')
        companions = g.get('coverCompanions', [])
        out.append(f'    out.coverCompanionCount={len(companions)};')
        for n, companion in enumerate(companions):
            for name in ('bone', 'index', 'parent', 'parentIndex'):
                value = companion[name]
                out.append(f'    out.coverCompanions[{n}].bone.{name}=' + (cpp_text(value) if isinstance(value, str) else str(value)) + ';')
            out += emit_matrix(f'out.coverCompanions[{n}].weaponFromReference', companion['weaponFromReference'])
        for n, pose in enumerate(g['poses']):
            target = f'out.poses[{n}]'
            out += [f'    {target}.authoredSeconds={pose["authoredSeconds"]!r};',
                    f'    {target}.chainCount={len(roles["feed_chain"])};']
            for name in ('weaponFromPouch', 'weaponFromFeedRoot', 'sourceFromWrist'): out += emit_matrix(target+'.'+name, pose[name])
            for m, frame in enumerate(pose['parentFromChainChild']): out += emit_matrix(f'{target}.parentFromChainChild[{m}]', frame)
    out += ['    return out;', '}();']
    ptype = 'Bc2FeedInteractionPolicy' if kind == 'feed' else 'Bc2PistolSlideInteractionPolicy'
    out += [f'inline constexpr {ptype} {prefix}Policy=[]{{', f'    {ptype} out{{}};']
    if kind == 'pistol_slide': out.append(f'    out.id=0x{entry["key"]:016x}ull;')
    for name, value in pol.items():
        text = cpp_text(value) if isinstance(value, str) else str(value)+'ull' if name == 'revision' else str(value)+'ll' if name.endswith('Ns') else literal(value)
        out.append(f'    out.{name}={text};')
    out += ['    return out;', '}();']
    rtype = 'Bc2FeedRegistration' if kind == 'feed' else 'Bc2PistolSlideRegistration'
    out += [f'inline constexpr {rtype} {prefix}Row=[]{{', f'    {rtype} out{{}};',
            f'    out.key=0x{entry["key"]:016x}ull;', f'    out.profileDigest="{entry["profile_digest"]}";']
    if kind == 'feed':
        out += [f'    out.geometry={prefix}Geometry;', f'    out.interactionPolicy={prefix}Policy;',
                f'    out.config=MechanismNative{index};', f'    out.nativeClass=Bc2FeedNativeClass::{p["native_class"]};']
        m = p['mechanism']
        out += [f'    out.mechanism.id=out.key;', f'    out.mechanism.revision={m["revision"]}ull;',
                '    out.mechanism.family=interaction::FeedFamily::Belt;',
                f'    out.mechanism.ammunitionKind={m["ammunitionKind"]}u;',
                f'    out.mechanism.maxAgeNs={m["maxAgeNs"]}ll;']
        for name in ('cover', 'latch', 'container', 'charge', 'containerNeedsOpen', 'feedNeedsOpen',
                     'detachNeedsUnseated', 'sameContainerReplenishment', 'coupledContainerDetach'):
            out.append(f'    out.mechanism.{name}={str(m[name]).lower()};')
        for part, contact in m['contacts'].items():
            target = f'out.mechanism.contacts[static_cast<unsigned>(interaction::FeedPart::{part})]'
            out += [f'    {target}.role={contact["role"]}u;', f'    {target}.radiusMeters={literal(contact["radiusMeters"])};',
                    f'    {target}.maxAngleRadians={literal(contact["maxAngleRadians"])};']
    else:
        out += [f'    out.geometry=&{prefix}Geometry;', f'    out.interaction=&{prefix}Policy;',
                f'    out.configuration=MechanismNative{index};',
                f'    out.nativeClass=Bc2PistolSlideNativeClass::{p["native_class"]};']
    out += [f'    out.enabled={str(entry["enabled"]).lower()};', f'    out.reviewed={str(entry["reviewed"]).lower()};',
            '    return out;', '}();']
    return out


def build(jobs, native_reviews, annotations, loaded_sources, admissions=None, *, header_kind=None):
    require(header_kind in (None, 'feed', 'pistol_slide'), 'Unknown header kind')
    require(annotations.get('schema') == SCHEMA, 'Expected mechanism annotations v1')
    exact_fields(annotations, ('schema', 'sources', 'profiles'), 'annotations')
    profiles = annotations['profiles']
    require(isinstance(profiles, list) and len(profiles) <= 256, 'Mechanism row bound')
    # Reuse the exact descriptor identity/digest, dispatch proof, collision,
    # timing and independently reviewed native-family machinery unchanged.
    native_registry.build(jobs, native_reviews)
    by_key = {j['key']: j for j in jobs['descriptors']}
    reviewed_native = {r['key'] for r in native_reviews}
    evidence = Evidence(annotations['sources'], loaded_sources)
    admissions = {'schema': ADMISSIONS, 'reviews': []} if admissions is None else admissions
    exact_fields(admissions, ('schema', 'reviews'), 'admissions')
    require(admissions['schema'] == ADMISSIONS and isinstance(admissions['reviews'], list)
            and len(admissions['reviews']) <= 256, 'Expected bounded mechanism admission reviews v1')
    grants = {}
    for review in admissions['reviews']:
        exact_fields(review, ('profile_digest', 'descriptor_key', 'descriptor_digest', 'native_class',
                              'physical_reviewed', 'native_reviewed', 'reviewer', 'evidence'), 'mechanism admission')
        digest = descriptor.sha(review['profile_digest'])
        require(digest not in grants, 'Duplicate mechanism admission digest')
        require(review['physical_reviewed'] is True and review['native_reviewed'] is True,
                'Admission requires separate explicit physical and native reviews')
        descriptor.text(review['reviewer'], 128)
        evidence.ref(review['evidence'])
        grants[digest] = review
    emitted, reports, matched = [], [], set()
    seen_paths, seen_keys, seen_ids = set(), set(), set()
    for index, row in enumerate(profiles):
        require(isinstance(row, dict), 'Invalid annotation row')
        key = descriptor.text(row.get('descriptor_key'), 256)
        require(key not in seen_keys, 'Duplicate descriptor annotation key')
        seen_keys.add(key)
        report = {'row': index, 'kind': row.get('kind'), 'descriptor_key': key, 'emitted': False,
                  'enabled': False, 'missing_prerequisites': []}
        try:
            require(key in by_key, 'Descriptor key not found')
            job = by_key[key]
            path = job['identity']['assetPath']
            require(path not in seen_paths, 'Duplicate exact configuration path')
            seen_paths.add(path)
            entry = resolve_row(row, job, evidence)
            require(entry['key'] not in seen_ids, 'Duplicate or truncated mechanism ID collision')
            seen_ids.add(entry['key'])
            review = grants.get(entry['profile_digest'])
            if review:
                require(all(review[k] == entry['payload'][k] for k in ('descriptor_key', 'descriptor_digest', 'native_class')),
                        'Mechanism admission does not match exact native descriptor/class')
                require(key in reviewed_native, 'Mechanism admission lacks independent native descriptor review')
                matched.add(entry['profile_digest'])
            entry['reviewed'] = bool(review)
            entry['enabled'] = row['enabled'] and entry['reviewed']
            require(not row['enabled'] or entry['reviewed'], 'Requested enablement lacks exact physical/native admission')
            report.update(profile_digest=entry['profile_digest'], mechanism_key=f'{entry["key"]:016x}',
                          descriptor_digest=job['descriptor_digest'], annotation_digest=entry['annotation_digest'],
                          emitted=True, enabled=entry['enabled'], reviewed=entry['reviewed'])
            if key not in reviewed_native: report['missing_prerequisites'].append('independent_exact_native_descriptor_review')
            if not review: report['missing_prerequisites'].append('exact_physical_and_native_mechanism_admission')
            emitted.append(entry)
        except (ValueError, TypeError, KeyError, AttributeError, OverflowError) as error:
            report['missing_prerequisites'].append(str(error))
        reports.append(report)
    require(matched == set(grants), 'Unmatched or rejected mechanism admission review')
    # Reject unsafe requests while retaining diagnostics. CLI writes the receipt
    # but never writes a header when any input row failed validation.
    valid = all(r['emitted'] for r in reports)
    lines = ['#pragma once', '// PRIVATE generated registry; authored data is not native qualification.',
             '#include <array>', '#include <bit>']
    # Optional runtime headers are included AFTER their public row declaration.
    # Standalone combined output may include both public headers itself.
    if header_kind in (None, 'feed'): lines += ['#include "Bc2FeedEquipmentProfile.h"']
    if header_kind in (None, 'pistol_slide'): lines += ['#include "Bc2PistolSlideProfile.h"']
    lines += ['namespace fvr::bc2::generated {']
    if valid:
        for i, entry in enumerate(emitted):
            if header_kind in (None, entry['payload']['kind']): lines += emit_row(i, entry, evidence)
    for kind, rtype, name in (('feed', 'Bc2FeedRegistration', 'GeneratedFeedProfiles'),
                              ('pistol_slide', 'Bc2PistolSlideRegistration', 'GeneratedPistolSlideProfiles')):
        if header_kind not in (None, kind): continue
        prefix = 'Feed' if kind == 'feed' else 'Slide'
        rows = [f'Mechanism{prefix}{i}Row' for i, e in enumerate(emitted) if e['payload']['kind'] == kind] if valid else []
        lines += [f'inline constexpr std::array<{rtype},{len(rows)}> {name}{{{{{",".join(rows)}}}}};']
    lines += ['} // namespace fvr::bc2::generated', '']
    receipt = {'schema': 'fvr.bc2.mechanism_registry_header.v1', 'valid': valid,
               'summary': {'requested_rows': len(profiles), 'emitted_rows': len(emitted) if valid else 0,
                           'enabled_rows': sum(e['enabled'] for e in emitted) if valid else 0,
                           'rejected_rows': sum(not r['emitted'] for r in reports)},
               'rows': reports, 'native_test_claim': False, 'headset_test_claim': False,
               'limits': ['Only exact reviewed admissions enable rows; authored data alone does not.',
                          'No archive extraction, native execution, controller qualification or family coverage is inferred.']}
    return '\n'.join(lines), receipt


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('jobs', 'reviewed', 'annotations', 'feed-header', 'pistol-header', 'receipt'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--admissions', type=Path)
    parser.add_argument('--evidence-root', type=Path,
                        help='Root for pinned source paths; defaults to annotations parent')
    args = parser.parse_args(argv)
    inputs, hashes = {}, {}
    for name in ('jobs', 'reviewed', 'annotations', 'admissions'):
        path = getattr(args, name)
        if path is not None: inputs[name], hashes[name] = descriptor.load(path)
    root = (args.evidence_root or args.annotations.parent).resolve()
    loaded = {}
    for name, pin in inputs['annotations']['sources'].items():
        path = (root / pin['path']).resolve()
        require(path.is_relative_to(root), 'Evidence source escapes evidence root')
        loaded[name] = descriptor.load(path)
    feed, receipt = build(inputs['jobs'], inputs['reviewed'], inputs['annotations'], loaded,
                          inputs.get('admissions'), header_kind='feed')
    pistol, _ = build(inputs['jobs'], inputs['reviewed'], inputs['annotations'], loaded,
                       inputs.get('admissions'), header_kind='pistol_slide')
    receipt['input_sha256'] = hashes
    receipt['source_sha256'] = {k: v[1] for k, v in loaded.items()}
    receipt['header_sha256'] = {kind: descriptor.hashlib.sha256(header.encode('utf-8')).hexdigest()
                               for kind, header in (('feed', feed), ('pistol_slide', pistol))} if receipt['valid'] else None
    outputs = [args.feed_header.resolve(), args.pistol_header.resolve(), args.receipt.resolve()]
    source_paths = [(root / pin['path']).resolve() for pin in inputs['annotations']['sources'].values()]
    input_paths = [p.resolve() for p in (args.jobs, args.reviewed, args.annotations, args.admissions) if p]
    require(len(set(outputs)) == len(outputs) and not set(outputs).intersection(input_paths + source_paths),
            'Input/output paths must be distinct')
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(json.dumps(receipt, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    if receipt['valid']:
        for path, header in ((args.feed_header, feed), (args.pistol_header, pistol)):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(header, encoding='utf-8')
    print(json.dumps(receipt['summary'], sort_keys=True))
    return 0 if receipt['valid'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
