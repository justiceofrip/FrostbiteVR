"""Join the exact firearm roster to pinned magazine source content, offline.

The unit of reuse is mesh/LOD/skeleton/static/reload content, never a gun name.
Existing registrations are preserved by the combined headers. This preparation
does not prove installed content, native execution, or headset acceptance.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import bc2_magazine_descriptor_manifest as descriptors
import bc2_magazine_registry_header as registry
import bc2_authored_magazine_geometry as geometry
import bc2_singlefire_magazine_proof as single
import bc2_automatic_stock_bolt_proof as stock
import bc2_boltaction_magazine_proof as bolt

SCHEMA = 'fvr.bc2.firearm_magazine_batch.v1'


def require(value, message):
    if not value:
        raise ValueError(message)


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(value if isinstance(value, str) else json.dumps(value, indent=2, allow_nan=False)+'\n', encoding='utf-8')


def pin(path):
    raw = path.read_bytes()
    return dict(path=str(path.resolve()), sha256=hashlib.sha256(raw).hexdigest())


def read_pin(source):
    raw = Path(source['path']).read_bytes()
    require(len(raw) <= 128*1024*1024 and hashlib.sha256(raw).hexdigest() == source['sha256'], 'Pinned source changed: '+source['path'])
    return json.loads(raw)


def weapon_key(weapon):
    return (weapon['resource'], weapon['sha256'], weapon.get('instance_guid', weapon.get('guid')).lower())


def roster_key(row):
    return row['configuration']+'.dbx', row['identity'][1], row['identity'][2].lower()


def content_key(row):
    """All coordinate-producing source identities, with no name/class fallback."""
    if 'source' in row:
        s = row['source']
        return (s['mesh_sha256'], s['lod_sha256'], row['skeleton_sha256'],
                s['reference_clip_sha256'], s['sha256'], row['rig_fingerprint'])
    return tuple(row[k] for k in ('mesh_sha256', 'lod_sha256', 'skeleton_sha256', 'static_clip_sha256')) + (row['reload_clip']['sha256'], row['rig_fingerprint'])


def validate_geometry(profile):
    geometry.cpp_header(dict(schema='fvr.bc2.authored_magazine_geometry', schema_version=1, profiles=[profile]), {profile['native_asset_name']})


def rebind(profile, binding, action):
    """Materialize a distinct configuration only after a full content-hash join."""
    validate_geometry(profile)
    require(binding['binding_digest']==descriptors.digest({k:v for k,v in binding.items() if k!='binding_digest'}),'Binding digest differs')
    require(content_key(profile) == content_key(action), 'Geometry content differs')
    require(weapon_key(binding['weapon']) == weapon_key(action['weapon_configuration']), 'Action/binding identity differs')
    require(binding['binding_digest'] == action['binding_digest'], 'Action/binding digest differs')
    require(binding['animation_sha256'] == profile['static_clip_sha256'] and
            binding['skeleton_sha256'] == profile['skeleton_sha256'], 'Binding coordinate content differs')
    chosen = [m for m in binding['meshes'] if m['configured_mesh_path'] == binding['configured_mesh_path']]
    require(len(chosen) == 1 and chosen[0]['geometry']['mesh_sha256'] == profile['mesh_sha256'], 'Selected mesh content differs')
    lod = [x for x in chosen[0]['geometry']['lods'] if x['lod'] == 0]
    require(len(lod) == 1 and lod[0]['data_sha256'] == profile['lod_sha256'], 'Selected LOD content differs')
    # Review export schemas bind their configuration explicitly. Such rows are
    # preserved exactly; a different configuration needs its own reviewed join.
    has_review = ('semantic_role_review' in profile['role_evidence'] or
                  profile['geometry']['design'].get('paired_reload_grasp', {}).get('selection_mode') == 'reviewed_authored_frame')
    if has_review:
        require(profile.get('weapon') == binding['weapon'] and profile['grip_binding_digest'] == binding['binding_digest'],
                'Reviewed pose needs an explicit per-configuration review join')
        return copy.deepcopy(profile)
    out = copy.deepcopy(profile)
    out.update(native_asset_name=binding['native_asset_name'], weapon=binding['weapon'],
               configured_mesh_path=binding['configured_mesh_path'], grip_binding_digest=binding['binding_digest'])
    out.pop('profile_digest')
    out['profile_digest'] = descriptors.digest(out)
    validate_geometry(out)
    return out


def family(job):
    require(registry.eligible(job), 'Unproved descriptor dispatch')
    if job.get('boltaction_magazine_proof_digest'):
        return bolt.family_for(job), bolt.PROOF_DIGEST
    if job.get('singlefire_magazine_proof_digest'):
        return registry.SINGLE_FIRE_FAMILY, single.PROOF_DIGEST
    if job.get('automatic_stock_bolt_proof_digest'):
        return registry.AUTOMATIC_STOCK_BOLT_FAMILY, stock.PROOF_DIGEST
    return registry.FAMILY, None


def collect_sources(root, recovery):
    relative = {
        'roster': root/'handoff/tasks/WEAPONS/20261030-roster43/roster.json',
        'catalog': recovery/'manual-reload-catalog-20261003-candidate/manual-reload-catalog.json',
        'actions': recovery/'action379-batched/actions.json',
        'stock_proof': recovery/'automatic-magazine-stock-bolt-20261003-candidate/stock-bolt-proof.json',
        'single_proof': root/'profiles/singlefire238/static-proof.json',
        'bolt_proof': root/'handoff/tasks/WEAPONS/20261030-sniper43/static-proof.json',
        'baseline_catalog': root/'config/body-ammo-assets.json',
        'nambu_jobs': root/'profiles/opening426/native-jobs.json',
        'nambu_bindings': root/'profiles/opening426/reference-bindings.json',
        'pistol_bindings': root/'profiles/resource-enrollment295-pistol/reference-bindings.json',
    }
    geo_paths = [
        recovery/'arsmg-paired-contact-batch-candidate/all/paired-geometry.json',
        recovery/'weapon-pipeline206-variant-extraction/derived-variants.json',
        recovery/'pistol-sniper-shotgun-mechanisms-candidate/geometry-final.json',
        recovery/'arsmg-paired-contact-batch-candidate/private-candidates.json',
        recovery/'lmg-drum-followup-120602-candidate/private-candidates.json',
        root/'profiles/resource-batch227/zero-bolt-batch/exact-geometry.json',
        root/'profiles/resource-enrollment235-scar/exact-geometry.json',
        root/'profiles/resource-enrollment240-f2000/exact-geometry.json',
        root/'reports/coordinator-20261030-family-enrollment41/magazine-geometry.json',
        root/'profiles/opening426/magazine-geometry.json']
    for i, path in enumerate(geo_paths):
        relative['geometry_'+str(i)] = path
    sources = {name: pin(path) for name, path in relative.items()}
    actions = read_pin(sources['actions'])
    for i, pair in enumerate(actions['inputs']):
        for name in ('bindings', 'meshes'):
            sources[name+'_'+str(i)] = pair[name]
            read_pin(pair[name])
    return sources


def preserved_identities(root):
    paths = ['resource-enrollment233/ReviewedResourceRegistry.h', 'resource-enrollment235-scar/ScarRegistry.h',
             'resource-enrollment240-f2000/F2000Registry.h', 'resource-enrollment295-pistol/PistolRegistry.h',
             'opening426/NambuRegistry.generated.h', 'mechanism-family41/PistolRegistry.generated.h']
    result = {('XM8_sp_s', 'Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped'),
              ('AEK971_sp', 'Objects/Weapons/Handheld/RU_rgl_AEK971/SP_rgl_AEK971')}
    for path in paths:
        text = (root/'profiles'/path).read_text(encoding='utf-8')
        result.update(re.findall(r'RegistryProfile\d+\{\{"([^"\n]+)","([^"\n]+)"', text))
    return result


def build(root, sources):
    docs = {name: read_pin(p) for name, p in sources.items()}
    selected = [r for r in docs['roster']['rows'] if r['mechanism'] in ('detachable_magazine', 'bolt_magazine')]
    require(len({roster_key(r) for r in selected}) == len(selected), 'Duplicate roster identity')
    jobs = descriptors.build(docs['catalog'])
    jobs = stock.apply(jobs, docs['stock_proof'])
    jobs = single.apply(jobs, docs['single_proof'])
    jobs = bolt.apply(jobs, docs['bolt_proof'])
    by_job = {weapon_key(j['identity']['weapon']): j for j in jobs['descriptors']}
    for j in docs['nambu_jobs']['descriptors']:
        by_job.setdefault(weapon_key(j['identity']['weapon']), j)
    bindings = {}
    for name, doc in docs.items():
        if name.startswith('bindings_') or name in ('nambu_bindings', 'pistol_bindings'):
            for b in doc['profiles']:
                require(b['binding_digest'] == descriptors.digest({k:v for k,v in b.items() if k != 'binding_digest'}), 'Binding digest differs')
                bindings.setdefault(weapon_key(b['weapon']), []).append(b)
    actions = {}
    for i, a in enumerate(docs['actions']['records']):
        if a['authored_role'] == '1p_reload':
            actions.setdefault(weapon_key(a['weapon_configuration']), []).append((i, a))
    profiles = []
    for name, d in docs.items():
        if name.startswith('geometry_'):
            for i, p in enumerate(d['profiles']):
                validate_geometry(p)
                profiles.append((name, i, p))
    preserved = preserved_identities(root)
    rows, exact_geometry, selected_jobs = [], [], []
    groups = {}
    for r in sorted(selected, key=lambda r: (r['asset'], r['configuration'])):
        key = roster_key(r)
        require(key in by_job, 'Descriptor missing: '+r['configuration'])
        job = copy.deepcopy(by_job[key]); require(job['identity']['assetName'] == r['asset'], 'Descriptor name differs')
        selected_jobs.append(job)
        same_actions = actions.get(key, [])
        candidates = bindings.get(key, [])
        exact = [(s, i, p) for s,i,p in profiles if p.get('weapon') and weapon_key(p['weapon']) == key and p['native_asset_name'] == r['asset']]
        # Source precedence preserves reviewed generations over old candidates.
        matched = None; chosen_binding = None; chosen_action = None; rejection = []
        for source, index, p in reversed(exact):
            bs = [b for b in candidates if b['binding_digest'] == p['grip_binding_digest']]
            if bs:
                matched=(source,index,p); chosen_binding=bs[0]; break
        if matched is None:
            for ai, a in same_actions:
                bs = [b for b in candidates if b['binding_digest'] == a['binding_digest']]
                if not bs: continue
                for source,index,p in reversed(profiles):
                    if content_key(p) != content_key(a): continue
                    try: rebound = rebind(p, bs[0], a)
                    except ValueError as e: rejection.append(str(e)); continue
                    matched=(source,index,rebound); chosen_binding=bs[0]; chosen_action=ai; break
                if matched: break
        if chosen_binding is None and candidates:
            chosen_binding = candidates[0]
        action = same_actions[0][1] if same_actions else None
        if chosen_action is None and same_actions: chosen_action=same_actions[0][0]
        group_key = descriptors.digest(content_key(action)) if action else None
        if group_key:
            groups.setdefault(group_key, dict(content_identity=list(content_key(action)), configurations=[], action_record=chosen_action))['configurations'].append(job['key'])
        native_family, proof_digest = family(job)
        row = dict(asset=r['asset'], configuration=r['configuration'], identity=job['identity'], key=job['key'],
                   descriptor_digest=job['descriptor_digest'], native_family=native_family, instruction_proof_digest=proof_digest,
                   preserved_registration=(r['asset'], r['configuration']) in preserved,
                   native_id=('builtin-preserved' if (r['asset'],r['configuration']) in preserved and r['asset'] in ('XM8_sp_s','AEK971_sp') else f'{registry.stable_id(job["descriptor_digest"]):016x}'),
                   geometry_content_group=group_key, action_record=chosen_action,
                   binding_digest=chosen_binding['binding_digest'] if chosen_binding else None,
                   strict_geometry=bool(matched), source_complete=False, native_tested=False, headset_accepted=False)
        if matched:
            s,i,p = matched; exact_geometry.append(p)
            row.update(geometry_digest=p['profile_digest'], geometry_source=dict(source=s,pointer='/profiles/'+str(i)),
                       geometry_rebound=p['profile_digest'] != docs[s]['profiles'][i]['profile_digest'])
        else:
            row['remaining'] = ['exact_authored_geometry_extraction' if action else 'missing_exact_reload_action_source']
            row['prior_rejections'] = sorted(set(rejection))
        rows.append(row)
    source_jobs=[]
    raw_configs={roster_key(dict(configuration=c['resource'][:-4],identity=[c['native_name'],c['resource_sha256'],c['instance_guid']])):c for c in docs['catalog']['configurations']}
    for row in rows:
        if row['binding_digest'] is not None:continue
        c=raw_configs[weapon_key(row['identity']['weapon'])]
        source_jobs.append(dict(key=row['key'],identity=row['identity'],configuration_archive=c['origin_archive'],
                                configuration_archive_index_sha256=c['origin_index_sha256'],authored_states=c['authored_states'],
                                mesh_sources=c['meshes'],required=['exact_reference_binding','exact_1p_reload_role_resource','mesh_lod_skeleton_static_reload_content_pins']))
    return dict(schema=SCHEMA, sources=sources, rows=rows, content_groups=groups, source_jobs=source_jobs,
                summary=dict(configurations=len(rows), strict_geometry=sum(r['strict_geometry'] for r in rows),
                             native_dispatch_supported=len(rows), preserved_registrations=sum(r['preserved_registration'] for r in rows),
                             independent_content_groups=len(groups), missing_reload_actions=sum(r['action_record'] is None for r in rows)),
                native_execution=False, headset_acceptance=False), {**jobs,'descriptors':selected_jobs}, dict(
                    schema='fvr.bc2.authored_magazine_geometry',schema_version=1,profiles=exact_geometry,gaps=[],runtime_admission=False)


def emit(root, output, batch, jobs, measured):
    write(output/'batch.json',batch); write(output/'descriptor-jobs.json',jobs); write(output/'geometry.json',measured)
    evidence = dict(schema='fvr.bc2.magazines45.exact_native_review.v1', sources=batch['sources'],
                    basis='Existing exact scalar/dispatch proof; separate retained per-configuration key and digest.',
                    rows=[{k:r[k] for k in ('key','descriptor_digest','native_family','instruction_proof_digest')} for r in batch['rows']],
                    native_execution=False, physical_acceptance=False)
    write(output/'native-evidence.json',evidence)
    evidence_hash=pin(output/'native-evidence.json')['sha256']
    reviews=[]
    for r in batch['rows']:
        if r['preserved_registration'] or not r['strict_geometry']: continue
        review=dict(key=r['key'],descriptor_digest=r['descriptor_digest'],family_proof=r['native_family'],evidence_sha256=evidence_hash)
        if r['instruction_proof_digest']:review['instruction_proof_digest']=r['instruction_proof_digest']
        reviews.append(review)
    write(output/'exact-native-reviews.json',reviews)
    addition_keys={r['key'] for r in batch['rows'] if not r['preserved_registration']}
    additions={**jobs,'descriptors':[j for j in jobs['descriptors'] if j['key'] in addition_keys]}
    header,receipt=registry.build(additions,reviews)
    write(output/'Registry.additions.generated.h',header)
    existing=preserved_identities(root)
    geometry_additions={**measured,'profiles':[p for p in measured['profiles'] if (p['native_asset_name'],p['weapon']['resource'][:-4]) not in existing]}
    write(output/'Geometry.additions.generated.h',geometry.cpp_header(geometry_additions,{p['native_asset_name'] for p in geometry_additions['profiles']}))
    write(output/'registry-receipt.json',receipt)
    # Separate namespaces preserve every existing registration identity, lifetime
    # and ordering. Candidate additions remain disabled until strict data exists.
    for kind, symbol, type_ in [('Registry','MagazineNativeRegistrations','MagazineNativeRegistration'),('Geometry','ExperimentalMagazineGeometry','MagazineGeometryProfile')]:
        # The old combined headers themselves use namespace macros. Importing
        # them under an outer macro would be undone by their internal #undef.
        # Preserve their declarations verbatim, renaming only the final array.
        baseline=(root/'profiles/mechanism-family41'/('Combined'+kind+'.h')).read_text(encoding='utf-8')
        baseline=baseline.replace('inline const auto '+symbol+'=', 'inline const auto Magazines45Preserved'+symbol+'=')
        baseline=re.sub(r'#include "([^"]+)"',lambda m:'#include "'+os.path.relpath(
            (root/'profiles/mechanism-family41'/m.group(1)).resolve(),output.resolve()).replace('\\','/')+'"',baseline)
        preserved_symbol='Magazines45Preserved'+symbol
        write(output/('Combined'+kind+'.h'), baseline+'\n#define generated magazines45_added\n#include "'+kind+'.additions.generated.h"\n#undef generated\nnamespace fvr::bc2::generated {\ninline const auto '+symbol+'=[] {\n std::array<'+type_+','+preserved_symbol+'.size()+magazines45_added::'+symbol+'.size()> out{};\n std::size_t n=0;for(const auto& p:'+preserved_symbol+')out[n++]=p;\n for(const auto& p:magazines45_added::'+symbol+')out[n++]=p;\n return out;\n}();\n}\n')
    write(output/'Enrollment.cmake', 'set(BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER "${CMAKE_CURRENT_LIST_DIR}/CombinedRegistry.h" CACHE FILEPATH "Exact firearm magazines" FORCE)\nset(BC2_EXPERIMENTAL_MAGAZINE_HEADER "${CMAKE_CURRENT_LIST_DIR}/CombinedGeometry.h" CACHE FILEPATH "Exact firearm magazine geometry" FORCE)\n')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--recovery',type=Path)
    parser.add_argument('--extraction',type=Path,help='Re-emit completed installed extraction into the integration profile directory')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.extraction:
        batch=json.loads((args.extraction/'batch.json').read_text(encoding='utf-8'))
        jobs=json.loads((args.extraction/'descriptor-jobs.json').read_text(encoding='utf-8'))
        measured=json.loads((args.extraction/'geometry.json').read_text(encoding='utf-8'))
        for name in ('body-ammo-assets.json','BodyAmmoAssetData.generated.h','visibility.json','Visibility.generated.h','visibility-specs.json','enrollment.json','extraction-receipt.json'):
            path=args.extraction/name
            if path.exists():write(args.output/name,path.read_text(encoding='utf-8'))
    else:
        require(args.recovery is not None,'--recovery or --extraction required')
        sources=collect_sources(args.root,args.recovery)
        batch,jobs,measured=build(args.root,sources)
    emit(args.root,args.output,batch,jobs,measured)
    print(json.dumps(batch['summary']))


if __name__=='__main__':main()
