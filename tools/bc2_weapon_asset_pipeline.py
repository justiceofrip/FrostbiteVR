"""Batch installed BC2 weapon evidence; never exports assets or enables features.

Archive names and animation names are discovery hints, not native capabilities.
Geometry comes from the existing bounded FBRB/MeshSet/MeshData reader. Failures
remain per-resource coverage gaps so one unusual gun cannot hide the others.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
from inspect_bc2_mesh_asset import Archive, bone_hash, mesh_data, mesh_set, sha
from bc2_mesh_geometry import metadata, geometry as decode_geometry

LIMITS = [
    'Derived metadata only; no game asset bytes or vertex/index arrays exported.',
    'Names/palettes/bounds do not identify a native reload port, magazine axis, pump, or firing mechanism.',
    'Archive variants do not establish current carried-item identity or attachment eligibility.',
    'No aim, support, visibility, reload, underbarrel or native-write capability is enabled by this output.',
]

def known_bones():
    names = [f'jntWpn_{n}' for n in range(256)] + ['jntWpn_Flash','jntWpnwpnJnt_14','jntWpnwpnJnt_16']
    if len({bone_hash(n) for n in names}) != len(names):
        raise ValueError('Candidate bone-name hash collision')
    return names

def dbx_dictionary(data):
    """Read the bounded name dictionary, NOT the unverified typed value tree."""
    if len(data)<24 or data[:8]!=b'{binary}':raise ValueError('Unsupported DBX header')
    end,reserved,length,count=struct.unpack_from('>4I',data,8)
    if reserved or end!=length+24 or not 0<count<=65536 or not 24+4*count<end<=len(data):
        raise ValueError('DBX dictionary bounds')
    start=24+4*count;offsets=struct.unpack_from('>'+str(count)+'I',data,24)
    if offsets[0]!=0 or any(a>=b for a,b in zip(offsets,offsets[1:])):raise ValueError('DBX dictionary ordering')
    names=[]
    for index,offset in enumerate(offsets):
        at=start+offset;limit=start+offsets[index+1] if index+1<count else end
        if at>=limit or limit>end or data[limit-1]!=0 or b'\0' in data[at:limit-1]:raise ValueError('DBX dictionary string bounds')
        names.append(data[at:limit-1].decode('utf-8'))
    return names

def configuration_field(name):
    return re.fullmatch(r'(?:Reload|Ammo|Magazine|Chamber|Rechamber|PrimaryFire|WeaponClass|Firing|Shot|Bullets|Projectiles|NumberOf)[A-Za-z0-9_]*',name) is not None

def configuration_inventory(path, root):
    archive=Archive(path)
    selected=[e for e in archive.entries if e.flags==65536 and e.name.lower().startswith('objects/weapons/handheld/')
              and e.name.endswith('.dbx') and not any(s in e.name.lower() for s in ('animtree','mesh','aiclones/'))]
    blobs=archive.read_selected([e.name for e in selected]);records=[]
    for entry in selected:
        data=blobs[entry.name]
        try:
            names=dbx_dictionary(data)
            records.append(dict(resource=entry.name,sha256=sha(data),bytes=len(data),dictionary_status='derived',
                class_names=[n for n in names if n.startswith(('GameSharedResources.','GameClientResources.'))],
                field_names=[n for n in names if configuration_field(n)],
                referenced_weapon_resources=[n for n in names if n.lower().startswith(('objects/weapons/','weapons/'))],
                typed_values_status='not_decoded',native_abi_status='not_proven'))
        except (ValueError,UnicodeError,struct.error) as exc:
            records.append(dict(resource=entry.name,sha256=sha(data),dictionary_status='unsupported',reason=str(exc)))
    return dict(archive=path.relative_to(root).as_posix(),index_sha256=archive.index_sha256,payload_sha256=archive.payload_sha256,records=records,
                limitation='Dictionary field names are not field values. The binary XML value tree and inheritance/reference resolution need a validated decoder before data-driven native configuration admission.')

def inventory_entries(entries):
    """Keep semantic claims separate from actual resource/type observations."""
    live = [e for e in entries if e.flags == 65536]
    meshes = [e for e in live if e.kind == 'SkinnedMeshSet']
    animations = [dict(name=e.name,kind=e.kind,bytes=e.size) for e in live
                  if e.kind in ('GrannyAnimation','DeltaAnimation')]
    return meshes, animations

def part_bounds(data, lod, sections, known):
    """Bound already-validated weighted vertices; emit no original geometry."""
    vb=sum(s['vertices']*s['stride'] for s in sections)
    ib=sum(s['triangles']*6 for s in sections)
    start=len(data)-vb-ib;parts={}
    for section in sections:
        for n in range(section['vertices']):
            at=start+section['vertex_offset']+n*section['stride']
            half=section['stride'] in (16,48);pos=struct.unpack_from('<3e' if half else '<3f',data,at)
            offset=8 if half else 12
            ids=data[at+offset:at+offset+4];weights=data[at+offset+4:at+offset+8]
            count=sum(w>0 for w in weights)
            for index,weight in zip(ids,weights):
                if not weight:continue
                h=lod['palette'][section['palette'][index]]
                p=parts.setdefault(h,dict(bone_hash=f'{h:08x}',candidate_name=known.get(h),vertex_influences=0,
                    all_vertices_single_weight=True,minimum=list(pos),maximum=list(pos),sections=[]))
                p['vertex_influences']+=1;p['all_vertices_single_weight'] &= count==1
                for axis in range(3):p['minimum'][axis]=min(p['minimum'][axis],pos[axis]);p['maximum'][axis]=max(p['maximum'][axis],pos[axis])
                if section['name'] not in p['sections']:p['sections'].append(section['name'])
    return [dict(p,role='unassigned_weighted_part',coordinate_space='asset_bind; native bone transform not applied') for _,p in sorted(parts.items())]

def mesh_record(archive, entry, blobs, entries):
    lods=metadata(blobs[entry.name]);known={bone_hash(n):n for n in known_bones()};decoded=[]
    for lod in lods:
        data_name = entry.name[:-4] + f'_lod{lod["lod"]}_data.res'
        if data_name not in entries or entries[data_name].kind != 'MeshData':
            raise ValueError('Exact LOD MeshData pair missing')
        sections=decode_geometry(blobs[data_name],lod,known)
        bound_status='not_decoded';bounds=[]
        try:
            bound=mesh_data(blobs[data_name]);bounds=[s['bind_position_bounds'] for s in bound['sections']];bound_status='derived'
        except ValueError:
            pass # General skin decoder above remains authoritative; bounds optional.
        decoded.append(dict(lod=lod['lod'],data_resource=data_name,data_sha256=sha(blobs[data_name]),
                            metadata=lod,sections=sections,bounds_status=bound_status,section_bounds=bounds,
                            candidate_parts=part_bounds(blobs[data_name],lod,sections,known)))
    return dict(resource=entry.name,sha256=sha(blobs[entry.name]),
                data_sha256=hashlib.sha256(''.join(d['data_sha256'] for d in decoded).encode()).hexdigest(),
                lods=decoded,geometry_status='derived',
                native_binding_status='not_verified',mechanism_binding_status='not_verified')

def inspect_archive(path, root):
    archive = Archive(path)
    meshes, animations = inventory_entries(archive.entries)
    row = dict(archive=path.relative_to(root).as_posix(),index_sha256=archive.index_sha256,
               bytes=path.stat().st_size,animations=animations,meshes=[],errors=[])
    entries = {e.name:e for e in archive.entries if e.flags == 65536}
    # One bounded inflation per archive, keeping only exact metadata/data pairs.
    selected = [name for e in meshes for name in (e.name,*(e.name[:-4]+f'_lod{n}_data.res' for n in range(4))) if name in entries]
    if not selected:
        row['geometry_status']='no_mesh_in_this_archive'
        return row
    blobs = archive.read_selected(selected)
    row['compressed_payload_sha256']=archive.payload_sha256
    for entry in meshes:
        try:
            row['meshes'].append(mesh_record(archive,entry,blobs,entries))
        except (ValueError,KeyError,IndexError,UnicodeError,struct.error) as exc:
            row['meshes'].append(dict(resource=entry.name,geometry_status='unsupported_or_incomplete',reason=str(exc),native_binding_status='not_verified',mechanism_binding_status='not_verified'))
    row['geometry_status']='derived' if all(m['geometry_status']=='derived' for m in row['meshes']) else 'partial'
    return row

def combine(rows):
    grouped={}
    for row in rows:
        for mesh in row.get('meshes',[]):
            key=mesh['resource']
            profile=grouped.setdefault(key,dict(resource=key,sources=[],geometry_variants={},
                runtime_features={name:'unverified' for name in ('aim_alignment','support_grip','translated_muzzle','holster','physical_reload','underbarrel')},
                reload_family='unassigned',underbarrel_eligibility='unverified',
                next_evidence=['Exact native asset/mesh and skeleton binding','Stable authored wrist capture','Mechanism-specific native ownership/receipts before physical reload']))
            profile['sources'].append(dict(archive=row['archive'],index_sha256=row['index_sha256'],geometry_status=mesh['geometry_status'],reason=mesh.get('reason')))
            if mesh['geometry_status']=='derived':
                pair=mesh['sha256']+':'+mesh['data_sha256']
                profile['geometry_variants'].setdefault(pair,mesh)
    return [dict(p,geometry_variants=list(p['geometry_variants'].values())) for _,p in sorted(grouped.items())]

def build_blueprints(profiles, plan):
    if plan.get('schema')!='fvr.weapon_family_work_plan' or plan.get('schema_version')!=1:
        raise ValueError('Unsupported work-plan schema')
    allowed={'detachable_magazine','tube_pump','tube_semiautomatic','en_bloc_clip','revolver_cylinder','belt_feed','bolt_magazine','single_round_launcher','unassigned'}
    names={};result=[]
    for rule in plan['items']:
        if rule['family_proposal'] not in allowed:raise ValueError('Unknown family proposal')
        for name in rule['exact_mesh_names']:
            key=name.lower()
            if key in names:raise ValueError('Ambiguous exact mesh work-plan entry')
            names[key]=rule
    for profile in profiles:
        key=profile['resource'].rsplit('/',1)[-1].lower();rule=names.get(key)
        parts=[dict(mesh_sha256=v['sha256'],lod=lod['lod'],**p) for v in profile['geometry_variants'] for lod in v['lods'] for p in lod['candidate_parts']]
        result.append(dict(stable_id='bc2:resource:'+profile['resource'],resource=profile['resource'],
            display_intent=rule['label'] if rule else None,
            family_proposal=rule['family_proposal'] if rule else 'unassigned',
            family_status='work_plan_only_not_native_verified',
            action_variant=rule.get('action_variant','unassigned') if rule else 'unassigned',
            underbarrel=dict(eligible=None,status='not_admitted',basis='Per-weapon native attachment proof required; family never grants eligibility',note=rule.get('underbarrel_note') if rule else None),
            runtime_enabled=False,features=dict(profile['runtime_features']),
            candidate_parts=parts,
            bindings=dict(native_asset='required',skeleton='required',grip='stable authored capture required',
                supply_object='required',insertion_frame='required',insertion_axis='required',retirement='required',
                native_ammunition_authority='required',underbarrel_parent_relationship='separate capability'),
            sources=profile['sources']))
    observed={p['resource'].rsplit('/',1)[-1].lower() for p in profiles}
    gaps=[dict(label=r['label'],family_proposal=r['family_proposal'],status='no_unique_matching_mesh_binding',expected_exact_mesh_names=r['exact_mesh_names'],note=r.get('note'),underbarrel_note=r.get('underbarrel_note')) for r in plan['items'] if not any(n.lower() in observed for n in r['exact_mesh_names'])]
    families={
        'detachable_magazine':['idle loaded/total capacity/reserve','eject old magazine without transfer','old-magazine return without refill','new-magazine seat with exact native receipt','partial/full/empty and zero-reserve cancellation'],
        'tube_pump':['idle tube/chamber counts','fore-end closed/rear/closed motion','native shot/rechamber state relation','one shell seat and one receipt','single pump cannot create rounds'],
        'tube_semiautomatic':['one shell seat and one receipt','native chamber/feed state'],
        'en_bloc_clip':['clip versus individual round owner','clip insert and native refill receipt','last-shot clip ejection'],
        'revolver_cylinder':['cylinder open/closed and chamber identity','spent case eject','per-chamber supply and native round receipt'],
        'belt_feed':['latch and cover open/closed','container versus belt identity','belt seat and cover/charge ordering','native refill receipt and cancellation'],
        'bolt_magazine':['magazine or internal supply owner','bolt locked/rear/forward/locked','native shot/rechamber state relation','reload receipt independent of bolt presentation'],
        'single_round_launcher':['ammunition supply and loading-part identity','one seat/one receipt and native ready state'],
        'unassigned':['classify actual supply and action before choosing a controller family']}
    jobs=[]
    for p in result:
        family=p['family_proposal']
        jobs.append(dict(resource=p['resource'],family=family,execute_automatically=False,
            selector=dict(exact_native_asset_name=None,expected_mesh=p['resource'],native_name_binding='required'),
            geometry_inputs=[dict(bone_hash=x['bone_hash'],candidate_name=x['candidate_name'],mesh_sha256=x['mesh_sha256']) for x in p['candidate_parts'] if x['lod']==0],
            observations=['native asset/data/persistence and current selected mesh','stable authored wrist-to-weapon relation after equip','source muzzle and actual firing origin/axis separately','current owner/equipment/reference epoch and both-eye visibility receipt']+families[family],
            native_config_fields=['reload logic','capacity and chamber policy','reload timing','native ready/reload/rechamber states','abort and retirement evidence'],
            missing=['Stable authored capture does not admit aim axes/support contact/muzzle translation','Exact XM8 capacity/timing/signatures are not inherited by a sibling magazine profile']))
    return dict(schema='fvr.weapon_interaction_blueprints',schema_version=1,admission='none',profiles=result,roster_gaps=gaps,capture_jobs=jobs,
        limits=['Family values are explicit work-plan proposals, not facts inferred from mesh shape or runtime native admission.',
                'Every candidate part is unassigned; bind bounds cannot become an insertion socket or moving mechanism axis without native evidence.'])

def join_runtime(blueprints, snapshot, root):
    if snapshot.get('schema')!='fvr.bc2.runtime_weapon_scope_snapshot' or snapshot.get('schema_version')!=1:
        raise ValueError('Unsupported runtime scope snapshot')
    for source in snapshot['sources']:
        rel=Path(source['path']);file=(root/rel).resolve()
        if rel.is_absolute() or not file.is_relative_to(root.resolve()) or sha(file.read_bytes())!=source['sha256']:
            raise ValueError('Runtime source snapshot mismatch: '+source['path'])
    for profile in blueprints['profiles']:
        profile['existing_runtime_scopes']=[scope for scope in snapshot['scopes'] if scope['mesh_resource']==profile['resource']]
    blueprints['runtime_scope_snapshot']=snapshot
    blueprints['limits'].append('Existing exact native asset scopes are source-bound annotations; their capabilities never transfer to another variant sharing the mesh.')

def run(root):
    directory=root/'Dist/win32/async/weapon'
    if not directory.is_dir():raise ValueError('Expected installed Dist/win32/async/weapon directory')
    paths=sorted(directory.glob('*.fbrb'))
    if not paths or len(paths)>2048:raise ValueError('Weapon archive count outside bound')
    rows=[];errors=[]
    for path in paths:
        try:rows.append(inspect_archive(path,root))
        except (OSError,ValueError,KeyError,IndexError,UnicodeError,struct.error) as exc:
            errors.append(dict(archive=path.relative_to(root).as_posix(),reason=str(exc)))
    profiles=combine(rows)
    return dict(schema='fvr.bc2.installed_weapon_inventory',schema_version=1,read_only=True,
                exported_assets=False,archive_count=len(paths),archives=rows,asset_profiles=profiles,errors=errors,
                summary=dict(archives_parsed=len(rows),mesh_profiles=len(profiles),
                  geometry_status=dict(Counter(m['geometry_status'] for r in rows for m in r['meshes']))),limits=LIMITS)

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--family-plan',type=Path,help='Explicit offline family work plan; never runtime configuration')
    parser.add_argument('--blueprints',type=Path,help='Output generated per-resource interaction work items')
    parser.add_argument('--config-archive',type=Path,action='append',default=[],help='Game-relative FBRB with actual weapon DBX metadata; repeatable')
    parser.add_argument('--runtime-snapshot',type=Path,help='Explicit source-hash-bound existing capability annotations')
    parser.add_argument('--runtime-root',type=Path,help='Source tree used to verify --runtime-snapshot')
    args=parser.parse_args(argv)
    result=run(args.game)
    result['configuration_archives']=[]
    for relative in args.config_archive:
        path=(args.game/relative).resolve()
        if not path.is_relative_to(args.game.resolve()):parser.error('Configuration archive must stay under --game')
        result['configuration_archives'].append(configuration_inventory(path,args.game.resolve()))
    if bool(args.family_plan)!=bool(args.blueprints):parser.error('--family-plan and --blueprints are required together')
    if args.family_plan:
        plan=json.loads(args.family_plan.read_text(encoding='utf-8-sig'))
        blueprints=build_blueprints(result['asset_profiles'],plan)
        blueprints['family_plan_sha256']=sha(args.family_plan.read_bytes())
        if bool(args.runtime_snapshot)!=bool(args.runtime_root):parser.error('--runtime-snapshot and --runtime-root are required together')
        if args.runtime_snapshot:join_runtime(blueprints,json.loads(args.runtime_snapshot.read_text(encoding='utf-8-sig')),args.runtime_root)
        args.blueprints.parent.mkdir(parents=True,exist_ok=True)
        args.blueprints.write_text(json.dumps(blueprints,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(result['summary']))
    return 0
if __name__=='__main__':raise SystemExit(main())
