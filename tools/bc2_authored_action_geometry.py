"""Batch authored weapon actions using shared geometry/contact measurements.

Explicit tree roles, exact configuration joins and content-based numeric reuse.
No native process, gameplay admission, mount changes or copied game assets.
"""
import argparse
import json
import math
from pathlib import Path
import bc2_authored_magazine_geometry as mag
import bc2_authored_mechanism_geometry as mechanism
from bc2_authored_grip_bindings import digest,path_key,checked_hash
from bc2_authored_linear_mechanism import measure
from bc2_magazine_contact_batch import ArchivePool,read
from bc2_mesh_geometry import metadata,geometry
from bc2_weapon_animation_pipeline import Clip,Skeleton
from inspect_bc2_mesh_asset import bone_hash


def context(game,profile,meshes,pool):
    if digest({k:v for k,v in profile.items() if k!='binding_digest'})!=profile['binding_digest']:
        raise ValueError('Action profile digest differs')
    selected=[m for m in profile['meshes'] if m['configured_mesh_path']==profile['configured_mesh_path']]
    if len(selected)!=1:raise ValueError('Exact selected action mesh is ambiguous')
    mesh=selected[0];variant=mesh['geometry']
    matches=[m for w in meshes['weapons'] if mag.mesh_weapon_matches(w,profile)
             for s in w['states'] if s['index']==profile['state_index']
             for m in s['meshes'] if m.get('mesh_resource')==mesh['mesh_resource']]
    if len(matches)!=1:raise ValueError('Exact action mesh binding absent or ambiguous')
    sources=[s for s in matches[0]['geometry_sources'] if s['geometry_status']=='derived']
    if not sources:raise ValueError('No measured action mesh source')
    source=min(sources,key=lambda s:s['archive']);path_key(source['archive'])
    ma=pool(game/source['archive'])
    if ma.index_sha256!=checked_hash(source['index_sha256']):raise ValueError('Action mesh archive index differs')
    raw=mag.exact_resource(ma,mesh['mesh_resource'],'SkinnedMeshSet',checked_hash(variant['mesh_sha256']))
    lods=[l for l in variant['lods'] if l['lod']==0]
    if len(lods)!=1:raise ValueError('One exact action LOD0 required')
    lod=lods[0];data=mag.exact_resource(ma,lod['data_resource'],'MeshData',checked_hash(lod['data_sha256']))
    ss=profile['skeleton_source'];cs=profile['animation_source']
    for src in (ss,cs):path_key(src['archive']);checked_hash(src['archive_index_sha256'])
    sa=pool(game/ss['archive']);ca=pool(game/cs['archive'])
    if sa.index_sha256!=ss['archive_index_sha256'] or ca.index_sha256!=cs['archive_index_sha256']:
        raise ValueError('Action pose archive index differs')
    sk=Skeleton(mag.exact_resource(sa,profile['animation_chain']['skeleton_resource'],'GrannyModel',checked_hash(profile['skeleton_sha256'])))
    if sk.fingerprint!=profile['rig_fingerprint']:raise ValueError('Action skeleton fingerprint differs')
    reference=Clip(mag.exact_resource(ca,profile['animation_chain']['animation_resource'],'GrannyAnimation',checked_hash(profile['animation_sha256'])))
    parsed=metadata(raw)[0];parts=mag.rigid_parts(data,parsed,sk,strict_assembly_skin=True)
    receipt={'mesh_sha256':variant['mesh_sha256'],'lod_sha256':lod['data_sha256'],
             'mesh_source':{'archive':source['archive'],'archive_index_sha256':source['index_sha256']},
             'skin_sections':geometry(data,parsed,{bone_hash(n):n for n in sk.names})}
    return dict(skeleton=sk,reference=reference,parts=parts,receipt=receipt,sources=((cs,ca),(ss,sa)),graph_archive=sa)


def inspect_action(profile,ctx,action,raw,source,cache):
    # Configuration identity is rebound after numeric reuse. A shared clip or
    # skeleton never makes two selected weapons the same native item.
    sk=ctx['skeleton'];ref=ctx['reference'];receipt={**ctx['receipt'],**action,**source,
        'sha256':mag.sha(raw),'reference_clip_sha256':ref.sha256,
        'animation_tree':profile['animation_chain']['tree']}
    key=(receipt['mesh_sha256'],receipt['lod_sha256'],receipt['sha256'],ref.sha256,sk.sha256)
    if key not in cache:
        if len(cache)>=256:raise ValueError('Action content job bound')
        clip=Clip(raw);names=[n for n in sk.names if n in ref.tracks and n.startswith(('jntWpn','LeftHand'))]
        closed=ref.evaluate(sk,0.,names)
        value=mechanism.analyze(profile,ctx['parts'],closed,clip,sk,receipt)
        rails=[];rejected=[]
        for name,part in sorted(ctx['parts'].items()):
            if not name.startswith('jntWpn') or name=='jntWpn_1' or not part['triangles']:continue
            if part['mixed_triangles']:
                rejected.append({'part':name,'reason':'Cross-part weighted geometry needs a coupled assembly'});continue
            try:rail=measure(clip,ref,sk,name,lateral_tolerance_m=.001,rotation_tolerance_rad=math.radians(.05))
            except (ValueError,KeyError,TypeError) as error:rejected.append({'part':name,'reason':str(error)})
            else:
                rail.update(rigid_triangles=part['triangles'],part_bounds_local=mag.bounds(part['points']))
                rails.append(rail)
        value.update(linear_candidates=rails,linear_rejections=rejected)
        cache[key]=value
    result=mechanism.bind_cached(cache[key],profile,receipt)
    result.update(authored_role=action['role'],live_state_selection_verified=False,
                  part_analysis_status='partial' if result.get('gaps') else 'complete')
    result['digest']=digest({k:v for k,v in result.items() if k!='digest'})
    return result


def run(game,bindings,meshes,roles,assets=None,progress=None):
    roles=mag.checked_action_roles(roles)
    if bindings.get('schema') not in ('fvr.bc2.authored_grip_bindings','fvr.bc2.authored_reload_reference_bindings') or bindings.get('schema_version')!=1:
        raise ValueError('Action binding schema')
    if meshes.get('schema')!='fvr.bc2.authored_weapon_mesh_bindings' or meshes.get('schema_version')!=1:
        raise ValueError('Action mesh schema')
    profiles=bindings['profiles']
    if not isinstance(profiles,list) or not 0<len(profiles)<=2048:raise ValueError('Action profile count bound')
    available={p['native_asset_name'] for p in profiles};assets=available if assets is None else set(assets)
    if not assets or len(assets)>128 or not assets<=available:raise ValueError('Selected action assets absent or excessive')
    selected=[p for p in profiles if p['native_asset_name'] in assets]
    identities=[(path_key(p['weapon']['resource']),mag.guid(p['weapon']['instance_guid']),p['state_index'],path_key(p['configured_mesh_path'])) for p in selected]
    if len(set(identities))!=len(identities):raise ValueError('Duplicate exact action profile')
    common=selected[0]['skeleton_source'];pool=ArchivePool(game,common['archive'],common['archive_index_sha256'])
    rows=[];gaps=[];missing=[];cache={}
    for index,p in enumerate(selected):
        identity={'asset':p['native_asset_name'],'weapon':p['weapon'],'state_index':p['state_index'],'binding_digest':p['binding_digest']}
        try:
            ctx=context(game,p,meshes,pool)
            actions=mag.bound_action_assets(p,ctx['graph_archive'],roles)
        except (ValueError,KeyError,TypeError) as error:
            gaps.append({**identity,'stage':'context','reason':str(error)})
            continue
        for role in roles:
            if role not in actions:
                missing.append({**identity,'role':role,'reason':'No exact authored replacement role'});continue
            action=actions[role]
            try:
                raw,source=mag.selected_animation_resource(ctx['sources'],action['resource'])
                rows.append(inspect_action(p,ctx,action,raw,source,cache))
            except (ValueError,KeyError,TypeError) as error:
                gaps.append({**identity,'stage':'action','role':role,'reason':str(error)})
        if progress:progress({'profiles_done':index+1,'profiles_total':len(selected),'content_jobs':len(cache),'records':len(rows),'gaps':len(gaps)})
    return {'schema':'fvr.bc2.authored_action_geometry.v1','roles':list(roles),'assets':sorted(assets),
            'profile_count':len(selected),'records':rows,'gaps':gaps,'missing_roles':missing,
            'independent_content_jobs':len(cache),'runtime_admission':False,'native_actions_verified':False,
            'limits':['Authored replacement name is not proof of live animation state selection.',
                      'Contacts are sampled candidates; linear extrema bound the offline decoder.',
                      'Geometry and moving part names do not establish pump/bolt/chamber semantics.']}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('game','bindings','mesh-bindings','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--role',action='append',required=True)
    parser.add_argument('--asset',action='append')
    args=parser.parse_args()
    if args.output.exists():raise ValueError('Preserve completed action evidence; select a new output')
    bindings,bhash=read(args.bindings);meshes,mhash=read(args.mesh_bindings)
    result=run(args.game,bindings,meshes,args.role,args.asset)
    result['inputs']={'bindings_sha256':bhash,'mesh_bindings_sha256':mhash}
    with args.output.open('x',encoding='utf-8') as file:file.write(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({k:result[k] for k in ('profile_count','independent_content_jobs','runtime_admission')}))


if __name__=='__main__':main()
