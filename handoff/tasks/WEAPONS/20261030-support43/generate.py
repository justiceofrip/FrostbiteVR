"""Exact support43 metadata derivation; no build, native access, or installation."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tools'))
import bc2_authored_grip_bindings as grip
from bc2_authored_archive_set import AuthoredArchiveSet
from bc2_authored_support_bindings import derive_explicit_contact_policy, header
from bc2_magazine_contact_batch import ArchivePool
from bc2_weapon_animation_pipeline import Clip, Skeleton
from bc2_weapon_config_pipeline import Resolver, guid, named, parse, scalar
from bc2_weapon_mesh_bindings import join, reference_resource

BASE = Path(__file__).resolve().parent
RECOVERY = Path('G:/Unity/VRMMO/bc2vr-recovery')
COMMON = 'Dist/win32/levels/mp_common/level-00.fbrb'
COMMON_HASH = '6ec10fba796c14d2a05f86d3079e819c38a4aaab3ec9205d7345bd2869554965'
M1_ARCHIVE = 'Dist/win32/async/weapon/m1garand-00.fbrb'
M1_INDEX = 'd0d031b25e2cc58e61c309d0ebb958394c175e4329c5468bd81a68f2325068d5'
M1_CLIP = 'Animations/Weapons/Handheld/M1garand/1P/HandsIkPose.res'
M1_CLIP_HASH = '5af5de7e8fdd5f45080553d97805575d8227488ac865d43d751fc7c6ff282d85'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf8')


class ExactSources:
    def __init__(self, game):
        self.game = game
        self.pool = ArchivePool(game, COMMON, COMMON_HASH)
        self.receipts = {}

    def archive(self, source):
        archive = self.pool(self.game / source['archive'])
        if archive.index_sha256 != source['archive_index_sha256']:
            raise ValueError('Source archive index changed')
        self.receipts[source['archive']] = archive.index_sha256
        return archive

    def one(self, source, resource, expected_hash, kind):
        archive = self.archive(source)
        entries = [e for e in archive.entries if e.flags == 65536 and e.name.casefold() == resource.casefold()]
        if len(entries) != 1 or entries[0].kind != kind:
            raise ValueError('Missing or ambiguous exact resource/type')
        raw = archive.read_selected([entries[0].name])[entries[0].name]
        if sha(raw) != expected_hash:
            raise ValueError('Exact resource content changed')
        return raw

    def closure(self, sources, weapon_receipts, mesh_receipts, tree_receipts=()):
        archive_set = AuthoredArchiveSet([(s['archive'], self.archive(s)) for s in sources])
        docs = {}
        def read(names):
            wanted = {grip.path_key(n) for n in names} - docs.keys()
            raw = archive_set.read(wanted)
            if set(raw) != wanted:
                raise ValueError('Exact configuration closure incomplete')
            for key, (data, source) in raw.items():
                docs[key] = parse(source['resource'], data)
        read([r['resource'] for r in weapon_receipts] + [r['mesh_document'] for r in mesh_receipts])
        trees = set()
        for receipt in weapon_receipts:
            doc = docs[grip.path_key(receipt['resource'])]
            if doc.sha256 != receipt['sha256']:
                raise ValueError('Weapon source content changed')
            instance = doc.instances[guid(receipt['instance_guid'])]
            for state in named(instance, 'WeaponStates').children:
                trees.add(reference_resource(named(state, 'AnimTree1p').attributes['ref']))
        read(trees)
        animations = []
        for key in trees:
            for instance in docs[key].instances.values():
                if instance.attributes['type'] == 'Animation.SpecificAnimTreeData':
                    animations.extend(reference_resource(r) if '/' in r else key
                                      for r in grip.fields(instance, 'ReplacmentAnimations'))
        read(animations)
        skeletons = []
        for key in animations:
            for instance in docs[key].instances.values():
                if instance.attributes['type'] == 'Animation.AnimationAsset':
                    skeletons.append(reference_resource(named(instance, 'Skeleton').attributes['ref']))
        read(skeletons)
        return list(docs.values())

    def validate(self, document, assets):
        rows = [r for r in document['profiles'] if r['native_asset_name'] in assets]
        sources = [{'archive': document['archive'], 'archive_index_sha256': document['archive_index_sha256']}]
        for row in rows:
            for source in row.get('reference_sources', []):
                if source not in sources:
                    sources.append(source)
        docs = self.closure(sources, [r['weapon'] for r in rows], [m for r in rows for m in r['meshes']])
        resolver = Resolver(docs)
        resources = {}
        for row in rows:
            if grip.digest({k:v for k,v in row.items() if k != 'binding_digest'}) != row['binding_digest']:
                raise ValueError('Cached source binding changed')
            doc = grip.required_document(resolver, row['weapon']['resource'])
            instance = doc.instances[guid(row['weapon']['instance_guid'])]
            if (grip.receipt(doc, instance) != row['weapon'] or
                    scalar(named(instance, 'Name'), 'string') != row['native_asset_name'] or
                    scalar(named(instance, 'WeaponClass'), 'string') != row['weapon_class']):
                raise ValueError('Weapon configuration identity changed')
            states = named(instance, 'WeaponStates').children
            if len(states) != 1 or row['state_count'] != 1 or row['state_index'] != 0:
                raise ValueError('Sole state required')
            if grip.authored_clip(resolver, doc, states[0]) != row['animation_chain']:
                raise ValueError('Authored animation role changed')
            if list(map(grip.ref_key, grip.fields(states[0], 'Meshes1p'))) != [grip.ref_key(m['reference']) for m in row['meshes']]:
                raise ValueError('Configured mesh references changed')
            for mesh in row['meshes']:
                md, mi = resolver.resolve(doc, mesh['reference'], 'Render.SkinnedMeshAsset')
                if (md.sha256 != mesh['mesh_document_sha256'] or
                        guid(mi.attributes['guid']) != mesh['mesh_instance_guid'] or
                        scalar(named(mi, 'Name'), 'string') != mesh['configured_mesh_path']):
                    raise ValueError('Exact configured mesh identity changed')
            key = (row['animation_sha256'], row['skeleton_sha256'])
            if key not in resources:
                chain = row['animation_chain']
                clip = Clip(self.one(row['animation_source'], chain['animation_resource'], key[0], 'GrannyAnimation'))
                skeleton = Skeleton(self.one(row['skeleton_source'], chain['skeleton_resource'], key[1], 'GrannyModel'))
                resources[key] = (clip, skeleton)
        return resources

    def m1_bindings(self, metadata, mesh_document, old_pose):
        config = {**metadata['configuration'], 'resolved_weapons': [w for w in metadata['configuration']['resolved_weapons'] if w['native_name'] == 'Garand']}
        if len(config['resolved_weapons']) != 2:
            raise ValueError('Expected exact M1 base and assault configurations')
        meshes = [w for w in mesh_document['weapons'] if w['native_name'] == 'Garand']
        receipts = [dict(resource=w['resource'], sha256=w['resource_sha256'], instance_guid=w['instance_guid']) for w in config['resolved_weapons']]
        source = dict(archive=COMMON, archive_index_sha256=COMMON_HASH)
        docs = self.closure([source], receipts, [m for w in meshes for s in w['states'] for m in s['meshes']])
        mesh = meshes[0]['states'][0]['meshes'][0]
        if any(w['states'][0]['meshes'][0] != mesh for w in meshes):
            raise ValueError('M1 base/assault mesh differs')
        archive = self.archive(source)
        mesh_name = mesh['mesh_resource']
        lod_name = mesh_name[:-4] + '_lod0_data.res'
        raw = archive.read_selected([mesh_name, lod_name])
        inventory = dict(schema='fvr.bc2.installed_weapon_inventory', schema_version=1, asset_profiles=[{
            'resource': mesh_name, 'sources': [source], 'geometry_variants': [{'sha256': sha(raw[mesh_name]), 'lods': [
                {'lod': 0, 'data_resource': lod_name, 'data_sha256': sha(raw[lod_name])}]}]}])
        bound_meshes = join(config, docs, inventory)
        clip_source = dict(archive=M1_ARCHIVE, archive_index_sha256=M1_INDEX, kind='GrannyAnimation', selection='exact_pinned_source_resource')
        clip = Clip(self.one(clip_source, M1_CLIP, M1_CLIP_HASH, 'GrannyAnimation'))
        skeleton_source = {**source, 'kind': 'GrannyModel', 'selection': 'exact_configuration_archive_resource'}
        sk_name = old_pose['skeleton']['resource']
        skeleton = Skeleton(self.one(skeleton_source, sk_name, old_pose['skeleton']['resource_sha256'], 'GrannyModel'))
        hands = clip.evaluate(skeleton, 0.0, ['RightHand', 'LeftHand'])
        poses = dict(schema='fvr.bc2.authored_hand_pose_batch.v1', skeleton={**skeleton.metadata(), 'resource': sk_name}, clips=[{
            'resource': M1_CLIP, 'sha256': clip.sha256, 'status': 'authored_hands_decoded', 'hands': hands,
            'sources': [{'archive': M1_ARCHIVE, 'index_sha256': M1_INDEX}]}])
        resource_map = {
            sk_name.casefold(): dict(kind='GrannyModel', sha256=skeleton.sha256, metadata=skeleton.metadata(), source=skeleton_source),
            M1_CLIP.casefold(): dict(kind='GrannyAnimation', sha256=clip.sha256, skeleton_sha256=skeleton.sha256, hands=hands, source=clip_source)}
        result = grip.build(config, docs, bound_meshes, poses, resource_map, reload_references_only=True)
        if result['gaps'] or len(result['profiles']) != 2:
            raise ValueError('M1 exact source join failed: ' + str(result['gaps']))
        result.update(archive=COMMON, archive_index_sha256=COMMON_HASH)
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--verify', action='store_true', help='Recompute and compare existing artifacts without writing')
    args = parser.parse_args()
    manifest = load(BASE / 'authoring-policy.json')
    documents = {}
    for name, spec in manifest['inputs'].items():
        path = RECOVERY / spec['path']
        raw = path.read_bytes()
        if sha(raw) != spec['sha256']:
            raise ValueError('Pinned source document changed: ' + name)
        documents[name] = json.loads(raw.decode('utf-8-sig'))
    exact = ExactSources(args.game)
    m1 = exact.m1_bindings(documents['common_metadata'], documents['mesh_bindings'], documents['hand_poses'])
    batches = [(documents['an94'], {'AN94', 'AN94_sp_k', 'AN94_sp_s'}),
               (documents['type5'], {'Garand'}), (m1, {'Garand'})]
    profiles, checks = [], []
    for document, assets in batches:
        resources = exact.validate(document, assets)
        selected = [r for r in document['profiles'] if r['native_asset_name'] in assets]
        policy = {**manifest['policy'], 'input_binding_document_digest': grip.digest(document),
                  'source_binding_digests': sorted(r['binding_digest'] for r in selected)}
        result = derive_explicit_contact_policy(document, assets, resources, policy, enable_contact=True)
        profiles.extend(result['profiles'])
        for r in result['profiles']:
            bound = r['source_reference_envelope']
            checks.append(dict(asset=r['native_asset_name'], mesh=r['configured_mesh_path'],
                               source_configurations=len(r['source_bindings']), source_status=r['source_left_hand_status'],
                               translation_bound_m=bound['translation_bound_m'], rotation_bound_deg=math.degrees(bound['rotation_bound_rad']),
                               previous_bounded_gate_passes=bound['translation_bound_m'] <= .00025 and bound['rotation_bound_rad'] <= math.radians(.05)))
    keys = [(r['native_asset_name'], r['configured_mesh_path'], r['rig_fingerprint']) for r in profiles]
    if len(keys) != 5 or len(set(keys)) != 5 or sum(len(r['source_bindings']) for r in profiles) != 8:
        raise ValueError('Expected five unique selectors from eight exact configurations')
    output = dict(schema='fvr.bc2.authored_support_bindings', schema_version=1, runtime_admission=False,
                  profiles=sorted(profiles, key=lambda r: (r['native_asset_name'], r['configured_mesh_path'])),
                  scope='Five explicit fixed mod contact selectors; source animation classifications and all runtime ownership checks unchanged.')
    report = dict(schema='fvr.bc2.support43.derivation.v1', authoring_policy_sha256=sha((BASE / 'authoring-policy.json').read_bytes()),
                  generator_sha256=sha(Path(__file__).read_bytes()), support_tool_sha256=sha((ROOT / 'tools/bc2_authored_support_bindings.py').read_bytes()),
                  rows=checks, exact_source_archives=exact.receipts, source_configurations=8, unique_support_keys=5,
                  native_checked=False, headset_accepted=False)
    generated = header(output).replace('// Private measured support data; no native capability admission.',
                                       '// Explicit fixed mod-authored HandsIkPose contacts; no static-source or native admission claim.')
    files = {BASE / 'm1-reference-bindings.json': json.dumps(m1, indent=2, allow_nan=False) + '\n',
             BASE / 'support-bindings.json': json.dumps(output, indent=2, allow_nan=False) + '\n',
             BASE / 'derivation.json': json.dumps(report, indent=2, allow_nan=False) + '\n',
             ROOT / 'profiles/weapons43/Support.additions.generated.h': generated}
    for path, text in files.items():
        if args.verify:
            if path.read_text(encoding='utf8') != text:
                raise ValueError('Generated artifact differs: ' + str(path))
        else:
            if path.exists():
                raise ValueError('Refusing to overwrite existing artifact: ' + str(path))
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding='utf8')
    print(json.dumps({'support_keys': len(keys), 'source_configurations': 8, 'verified': args.verify, 'native_claim': False}))


if __name__ == '__main__':
    main()
