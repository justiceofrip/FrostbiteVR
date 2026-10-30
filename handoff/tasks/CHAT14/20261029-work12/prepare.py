"""Exact M60_sp offline proof. No process is opened; no asset bytes exported."""
import argparse, hashlib, json, re, struct, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
TOOLS = ROOT / 'tools'
sys.path.insert(0, str(TOOLS))
from capture_reload_state import Image
from capture_reload_server import discover
from inspect_bc2_mesh_asset import Archive
from bc2_weapon_config_pipeline import parse, Resolver, effective_weapon, named, scalar
from bc2_weapon_animation_pipeline import Clip

RESOURCE = 'Objects/Weapons/Handheld/BU_lmg_M60/SP_lmg_M60.dbx'
ARCHIVE = 'Dist/win32/levels/sp_common/level-00.fbrb'
INDEX = '57835d6ace174f83eeb46ec8bbfe3acf8a3e7fde5cb8c8e49f084950e975a842'
sha = lambda data: hashlib.sha256(data).hexdigest()

def code_proof(image):
    # Consume the actual production observer's unique patterns, function sizes,
    # and complete FNV fingerprints. No second offset/signature allowlist.
    source = (ROOT/'src/games/bc2/Bc2ReloadState.cpp').read_text()
    sizes = [int(x) for x in re.search(r'codeSizes\{([^}]+)\}', source)[1].split(',')]
    hashes = [int(x, 16) for x in re.findall(r'0x([0-9a-f]+)ull', re.search(r'codeHashes\{([^}]+)\}', source)[1])]
    patterns = re.findall(r'"([0-9A-F? ]+)"', re.search(r'signatures\{(.*?)\};', source, re.S)[1])
    if len(sizes) != 6 or len(hashes) != 6 or len(patterns) != 6: raise ValueError('Observer source shape differs')
    result = []
    for pattern, size, expected in zip(patterns, sizes, hashes):
        rva = image.find(pattern); data = image.read(rva, size); actual = 14695981039346656037
        for byte in data: actual = ((actual ^ byte)*1099511628211) & 0xffffffffffffffff
        if actual != expected: raise ValueError('Complete ReloadState function proof differs')
        result.append(dict(rva=rva, size=size, fnv1a64=f'{actual:016x}', sha256=sha(data)))
    return result

def reflected_value_type(image, name):
    # Same metadata header and 24-byte FieldInfo records as Inspector.info.
    # ClassInfo is constructed at runtime: restrict this disk path to value
    # types/enums whose field table is file backed at metadata+24.
    def va(offset):
        for rva, _, raw, count, _ in image.sections:
            if raw <= offset < raw+count: return image.base+rva+offset-raw
        raise ValueError('File offset is outside sections')
    def text(address):
        out = bytearray()
        for n in range(192):
            c = image.read(address-image.base+n, 1)[0]
            if not c: return out.decode('ascii')
            if not 32 <= c < 127: raise ValueError('Non-ASCII reflected name')
            out.append(c)
        raise ValueError('Reflected name bound')
    candidates = []
    for match in re.finditer(re.escape(name.encode()+b'\0'), image.data):
        address = va(match.start())
        for ref in re.finditer(re.escape(struct.pack('<I', address)), image.data):
            at = va(ref.start()); meta = image.read(at-image.base, 28)
            flags, size = struct.unpack_from('<HH', meta, 4); count = meta[13]
            if flags not in (0x29, 0x179) or not 0 < count <= 64: continue
            table = struct.unpack_from('<I', meta, 24)[0]
            records = image.read(table-image.base, count*24); fields = []
            for n in range(count):
                label, fieldflags, ti, element, offset, _ = struct.unpack_from('<6I', records, n*24)
                if flags != 0x179 and offset >= size: raise ValueError('Field outside value type')
                fields.append(dict(name=text(label), offset_or_enum_value=offset,
                                   type_info_va=ti, element_info_va=element, flags=fieldflags))
            candidates.append(dict(name=name, metadata_rva=at-image.base, flags=flags, size=size,
                table_rva=table-image.base, metadata_sha256=sha(meta), table_sha256=sha(records), fields=fields))
    if len(candidates) != 1: raise ValueError(f'Expected unique reflected {name}: {len(candidates)}')
    return candidates[0]

def prepare(game):
    profile = json.loads((ROOT/'profiles/support312/support-bindings.json').read_text())
    def find_profiles(obj):
        if isinstance(obj, dict):
            if obj.get('native_asset_name') == 'M60_sp' and obj.get('source_weapons'): yield obj
            else:
                for item in obj.values(): yield from find_profiles(item)
        elif isinstance(obj, list):
            for item in obj: yield from find_profiles(item)
    selected, = list(find_profiles(profile))
    archive = Archive(game/ARCHIVE)
    if archive.index_sha256 != INDEX: raise ValueError('Cataloged archive index differs')
    paths = [RESOURCE, 'Objects/Weapons/Handheld/BU_lmg_M60/BU_lmg_M60_Firing.dbx',
        selected['animation_chain']['tree']['resource'],
        'Animations/Weapons/Handheld/BU_lmg_M60/1P/1P_Reload.dbx']
    blobs = archive.read_selected(paths)
    docs = [parse(n, b) for n, b in blobs.items() if n.endswith('.dbx')]
    resolver = Resolver(docs); doc = next(d for d in docs if d.resource == RESOURCE)
    weapon = doc.instances[selected['source_weapons'][0]['instance_guid'].lower()]
    if doc.sha256 != selected['source_weapons'][0]['sha256']: raise ValueError('Profile configuration hash differs')
    definition = effective_weapon(resolver, doc, weapon)
    authored_state = named(weapon, 'WeaponStates').children[0]
    presentation = dict(readiness_states={x.attributes['name']:scalar(x,'bool') for x in named(authored_state,'ReadinessStates').children},
        skip_reload_animation=scalar(named(authored_state,'SkipReloadAnimation'),'bool'),
        play_deploy_after_fire=scalar(named(authored_state,'PlayDeployAfterFire'),'bool'),
        hidden_bones_1p=[scalar(x,'string') for x in named(authored_state,'HiddenBones1p').children],
        readiness_meaning='Authored Relaxed/Alert presentation selection; not loaded/chamber/fire-ready authority')
    tree_doc, tree = resolver.resolve(doc, definition['weapon_states'][0]['animation_tree_references']['AnimTree1p'], 'Animation.SpecificAnimTreeData')
    if tree_doc.sha256 != selected['animation_chain']['tree']['sha256']: raise ValueError('Profile tree hash differs')
    replacement = [x.attributes['ref'] for x in named(tree, 'ReplacmentAnimations').children if '/1p_reload/' in x.attributes.get('ref', '').lower()]
    reference, = replacement
    clip_doc, clip_node = resolver.resolve(tree_doc, reference, 'Animation.AnimationAsset')
    clip_path = scalar(named(clip_node, 'Name'), 'string')+'.res'
    clip_archive_path = 'Dist/win32/async/weapon/sp_lmg_m60-00.fbrb'
    clip_archive = Archive(game/clip_archive_path)
    if clip_archive.index_sha256 != '4260b95b2de5e90945e0554bac1e55a5066a121810845b2b40d108d4de4b8b89':
        raise ValueError('Previously cataloged exact campaign clip archive differs')
    clip_bytes = clip_archive.read_selected([clip_path])[clip_path]
    if sha(clip_bytes) != 'a6b0e5c68340c3ec2211e25a7f682bbce93befc1535e13fc31bd46bf727614f3':
        raise ValueError('Previously cataloged reload clip differs')
    try:
        summary = Clip(clip_bytes).summary()
        summary['tracks'] = [x for x in summary['tracks'] if x['name'].startswith('jntWpn')]
        summary.pop('named_tracks'); summary.pop('wrapper_records')
    except (ValueError, KeyError, TypeError) as exc:
        summary = dict(status='unsupported', reason=str(exc), mechanism_roles=[])
    image = Image(game/'BFBC2Game.exe')
    reflection = [reflected_value_type(image, name) for name in
        ('FireLogicData','BoltActionData','AmmoConfigData','WeaponStateData','ReloadType','ReloadLogic','FireLogicType')]
    definition.update(selector=dict(asset_name='M60_sp', asset_path=RESOURCE[:-4]), runtime_enabled=False,
        source_archive=ARCHIVE, source_index_sha256=INDEX)
    jobs = dict(schema='fvr.bc2.lmg_binding_jobs.v1', runtime_admission='none', models=[dict(
        label='M60 campaign, exact unscoped configuration', mesh_resource=selected['configured_mesh_path']+'.res',
        runtime_enabled=False, definitions=[definition])])
    source_names = ['capture_lmg_native.py','capture_reload_state.py','capture_reload_server.py','read_bc2.py',
        'bc2_weapon_config_pipeline.py','inspect_bc2_mesh_asset.py','bc2_weapon_animation_pipeline.py',
        'bc2_granny_resource.py','bc2_granny_curves.py']
    proof = dict(schema='fvr.bc2.m60_stage_preparation.v1', evidence_level='offline_installed_asset_and_executable',
        game_process_opened=False, native_admission=False, executable_sha256=sha(image.data),
        executable=str(image.path.resolve()), archive=ARCHIVE, archive_index_sha256=INDEX,
        archive_payload_sha256=archive.payload_sha256,
        resources=[dict(resource=n, sha256=sha(b), size=len(b)) for n,b in blobs.items()],
        profile_source='profiles/support312/support-bindings.json', profile_binding_digest=selected['binding_digest'],
        reload_state_functions=code_proof(image), server_reader_binding=discover(image), reflection=reflection,
        authored_reload_animation=dict(tree_resource=tree_doc.resource, replacement_reference=reference,
            resource=clip_path, sha256=sha(clip_bytes), archive=clip_archive_path,
            archive_index_sha256=clip_archive.index_sha256, summary=summary),
        authored_presentation=presentation,
        reader_hashes={n:sha((TOOLS/n).read_bytes()) for n in source_names},
        reload_state_source_sha256=sha((ROOT/'src/games/bc2/Bc2ReloadState.cpp').read_bytes()),
        stage_contract=[
            dict(stage='cover/latch/container/feed/charge', authority='authored presentation candidates only',
                observer='exact replacement clip and original evaluated named weapon bones; roles remain Unknown until visual correlation',
                forbidden_inference='No cover/feed enum, native operation or acknowledgement follows from a bone, clip time or rtMagazine.'),
            dict(stage='reload begin/wait', authority='native sampled phase', observer='all three firing copies: current/previous/next 10/11, phaseTimer',
                limit='External polling may miss 10 or 12 within one native update; no event completeness or pose atomicity.'),
            dict(stage='native bulk transfer', authority='observed counter transition, not operation receipt',
                observer='all three same-owner copies; loaded increases, reserve decreases equally, totals conserved; retain before/after intervals',
                limit='Prediction/cross-copy skew and concurrent supply changes stay inconclusive; no independent belt/container resource is proved.'),
            dict(stage='return to input processing', authority='native phase candidate', observer='all three copies current=next=2, positive loaded, stable counts for 2 seconds',
                limit='Does not prove fire permission or chamber state; no fire command is issued. Zero bolt timing does not justify adding a charge step.')],
        capture_bounds=dict(seconds=20, interval_ms=20, max_records=1100, max_output_bytes=8*1024*1024,
            operator_precondition='Already partial positive clip and positive reserve; 2 seconds stable before one operator-owned ordinary reload, 2 seconds after; no preparation shot by observer'),
        pending='One exact mounted M60_sp ordinary reload and original-pose/visual correlation; optional native callback event observer needs separate source review.')
    return proof, jobs

def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--game', type=Path, required=True)
    args = parser.parse_args()
    if any((HERE/name).exists() for name in ('proof.json','jobs.json')): raise ValueError('Preserve existing preparation evidence')
    proof, jobs = prepare(args.game.resolve())
    for name, value in [('proof.json',proof),('jobs.json',jobs)]:
        (HERE/name).write_text(json.dumps(value, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps(dict(passed=True, resource=RESOURCE, functions=len(proof['reload_state_functions']),
        reflected_types=len(proof['reflection']), game_process_opened=False)))

if __name__ == '__main__': main()
