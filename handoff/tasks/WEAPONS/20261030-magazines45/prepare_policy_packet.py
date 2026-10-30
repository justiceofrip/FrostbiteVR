"""Root-only incremental source packet for explicit mod magazine authoring.

Preserves prior strict failures. Exports measured hierarchy/pose metadata and
private previews; never promotes a contact, role or native registration.
"""
import argparse
import copy
import json
import math
from pathlib import Path
import struct
import sys
import zlib

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools'))
import bc2_firearm_magazine_batch as batch
import bc2_firearm_magazine_enrollment as enrollment
import bc2_authored_action_geometry as actions
import bc2_authored_grip_bindings as grip
import bc2_authored_magazine_geometry as g
import bc2_weapon_asset_pipeline as assets_pipeline
from bc2_magazine_contact_batch import ArchivePool,readers,filter_inputs,refresh_meshes
from bc2_weapon_animation_pipeline import Clip


def distance_box(point,lo,hi):
    return math.sqrt(sum(max(a-x,0,x-b)**2 for x,a,b in zip(point,lo,hi)))


def png(path,clouds):
    """Three measured orthographic point-cloud views; no generated anatomy."""
    width,height=1200,440
    pixels=bytearray([245,246,248]*(width*height))
    colors=[(35,42,52),(213,74,65),(48,127,194),(53,158,93),(175,83,181),(224,143,36),(35,171,164),(139,91,57)]
    allpoints=[p for _,pts in clouds for p in pts]
    if not allpoints:return
    for view,(a,b) in enumerate(((2,1),(0,1),(2,0))):
        lo=[min(p[k] for p in allpoints) for k in (a,b)];hi=[max(p[k] for p in allpoints) for k in (a,b)]
        scale=min(360/max(hi[0]-lo[0],.001),400/max(hi[1]-lo[1],.001))
        for index,points in clouds:
            color=colors[index%len(colors)]
            for point in points:
                x=round(view*400+20+(point[a]-lo[0])*scale);y=round(420-(point[b]-lo[1])*scale)
                if 0<=x<width and 0<=y<height:pixels[(y*width+x)*3:(y*width+x)*3+3]=bytes(color)
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    raw=b''.join(b'\0'+pixels[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))


def inspect(profile,ctx,clip,source,out):
    sk=ctx['skeleton'];parts=ctx['parts'];parents={n:sk.names[p] if p>=0 else None for n,p in zip(sk.names,sk.parents)}
    batch.require(0<clip.duration<=15 and len(parts)<=128,'Bounded authored clip/part packet required')
    names=sorted(set(parts)|{'LeftHand',*g.FINGERS})
    closed=ctx['reference'].evaluate(sk,0.,names)
    invalid=[]
    try:reload0=clip.evaluate(sk,0.,names)
    except ValueError as e:reload0=None;invalid.append(dict(time_seconds=0.,reason=str(e)))
    samples=[]
    for i in range(math.ceil(clip.duration*30)+1):
        t=min(clip.duration,i/30)
        try:samples.append((t,clip.evaluate(sk,t,names)['weapon_relative']))
        except ValueError as e:invalid.append(dict(time_seconds=t,reason=str(e)))
    batch.require(samples,'No valid exact authored frames; no normalization or substitute pose allowed')
    body_points=[g.point(p,closed['weapon_relative']['jntWpn_1']) for p in parts['jntWpn_1']['points']]
    candidates=[];clouds=[];all_parts=[]
    for bone,part in parts.items():
        all_parts.append(dict(bone=bone,parent=parents[bone],triangles=part['triangles'],mixed_triangles=part['mixed_triangles'],
                              local_bounds=g.bounds(part['points']),static_evaluation=closed['bone_evaluation_status'][bone],
                              weapon_from_reference=closed['weapon_relative'][bone],weapon_from_reload0=reload0['weapon_relative'][bone] if reload0 else None))
        motion=max(g.norm(g.sub(pose[bone][12:15],samples[0][1][bone][12:15])) for _,pose in samples)
        if bone=='jntWpn_1' or part['triangles']<24 or motion<.08:continue
        descendants=[]
        for child in parts:
            node=child
            while node and node!=bone:node=parents[node]
            if node==bone:descendants.append(child)
        attached=closed['weapon_relative'][bone];inverse=g.inverse_rigid(g.matrix(attached))
        merged=[];members=[]
        for child in descendants:
            transform=g.multiply(g.matrix(closed['weapon_relative'][child]),inverse)
            merged.extend(g.point(p,transform) for p in parts[child]['points'])
            if child!=bone:members.append(dict(bone=child,parent=parents[child],item_from_bone=transform))
        lo,hi=g.bounds(merged);extent=g.sub(hi,lo)
        if max(extent)>.7:continue
        poses=[]
        for t,pose in samples:
            item=g.matrix(pose[bone]);inv=g.inverse_rigid(item);wrist=g.matrix(pose['LeftHand'])
            relative={n:g.multiply(g.matrix(pose[n]),inv) for n in g.FINGERS}
            knuckles=[relative[n][12:15] for n in g.FINGERS if n.endswith('1')]
            near=max(distance_box(p,lo,hi) for p in knuckles)
            moved=g.norm(g.sub(item[12:15],attached[12:15]))
            if moved<.06:continue
            poses.append(dict(time_seconds=t,knuckle_max_distance_to_bounds_m=near,item_displacement_m=moved,
                              weapon_from_item=item,item_from_hand=g.multiply(wrist,inv),
                              wrist_from_fingers={n:g.multiply(g.matrix(pose[n]),g.inverse_rigid(wrist)) for n in g.FINGERS}))
        ranked=[]
        for pose in sorted(poses,key=lambda p:(p['knuckle_max_distance_to_bounds_m'],p['time_seconds'])):
            if all(abs(pose['time_seconds']-p['time_seconds'])>=.1 for p in ranked):ranked.append(pose)
            if len(ranked)==8:break
        proposals=[]
        mergedpart=dict(part,points=merged,extent=extent,triangles=sum(parts[n]['triangles'] for n in descendants),mixed_triangles=sum(parts[n]['mixed_triangles'] for n in descendants))
        for pose_index,pose in enumerate(ranked):
            hand=g.multiply(pose['item_from_hand'],g.matrix(attached))
            fingers={n:g.multiply(pose['wrist_from_fingers'][n],hand) for n in g.FINGERS}
            for axis in range(3):
                try:proposal=g.design(mergedpart,attached,body_points,hand,fingers,axis)
                except ValueError as e:proposals.append(dict(pose_index=pose_index,axis=axis,rejection=str(e)));continue
                proposals.append(dict(pose_index=pose_index,axis=axis,geometry=proposal))
        index=len(candidates)+1
        candidates.append(dict(bone=bone,parent=parents[bone],children=[n for n,p in parents.items() if p==bone],
                               color_index=index,rigid_geometry_parts=descendants,reference_members=members,
                               local_bounds=[lo,hi],extent_m=extent,triangles=mergedpart['triangles'],mixed_triangles=mergedpart['mixed_triangles'],
                               maximum_reload_translation_m=motion,static_evaluation=closed['bone_evaluation_status'][bone],
                               reference_attached_item=attached,reload0_attached_item=reload0['weapon_relative'][bone] if reload0 else None,
                               poses=ranked,design_proposals=proposals,mod_role_selected=False,measured_stable_contact_claim=False))
        clouds.append((index,[g.point(p,attached) for p in merged]))
        if ranked:
            contact_points=[g.point([0,0,0],g.multiply(pose_matrix,g.multiply(ranked[0]['item_from_hand'],attached))) for pose_matrix in ranked[0]['wrist_from_fingers'].values()]
            png(out/(bone+'-contact.png'),[(0,body_points),(index,[g.point(p,attached) for p in merged]),(7,contact_points)])
    png(out/'parts.png',[(0,body_points),*clouds])
    return dict(schema='fvr.bc2.magazines45.mod_policy_source_packet.v1',binding=profile,source=source,
                skeleton_sha256=sk.sha256,rig_fingerprint=sk.fingerprint,static_clip_sha256=ctx['reference'].sha256,
                reload_clip_sha256=clip.sha256,reference_time_seconds=0.,reference_is_native_closed=False,
                duration_seconds=clip.duration,sample_hz=30,invalid_pose_samples=invalid,parts=all_parts,candidates=candidates,
                runtime_admitted=False,native_role_verified=False,mod_authoring_review_required=True,
                preview= batch.pin(out/'parts.png'))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',type=Path,required=True);p.add_argument('--previous',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--asset',action='append',help='Exact targeted residual assets; other completed packet rows are untouched')
    p.add_argument('--targeted-common-meshes',action='store_true',help='Read only missing exact referenced mesh/LOD names from MP common')
    args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    batch.require(not (args.output/'packet.json').exists(),'Preserve earlier packet')
    plan=json.loads((args.previous/'batch.json').read_text());docs,bindings,meshes=enrollment.source_inputs(plan)
    if args.asset:plan['rows']=[r for r in plan['rows'] if r['asset'] in set(args.asset)]
    recovery=Path(plan['sources']['catalog']['path']).parent.parent
    inputs={n:batch.pin(path) for n,path in {'metadata':recovery/'manual-reload-catalog-20261003-candidate/mp-common-all-metadata.json',
               'poses':recovery/'authored-hand-pose-batch/result.json','inventory':recovery/'mesh312-research/inventory.json'}.items()}
    metadata,poses,inventory=(batch.read_pin(inputs[n]) for n in ('metadata','poses','inventory'))
    pool=ArchivePool(args.game,metadata['configuration']['archive'],metadata['configuration']['index_sha256'])
    result=dict(schema='fvr.bc2.magazines45.residual_packet.v1',previous=batch.pin(args.previous/'extraction-receipt.json'),inputs=inputs,groups=[],gaps=[])
    missing_assets={r['asset'] for r in plan['rows'] if r['binding_digest'] is None}
    with readers(pool):
        if missing_assets:
            config,old=filter_inputs(metadata['configuration'],metadata['mesh_bindings'],missing_assets)
            if args.targeted_common_meshes:
                archive=pool(args.game/config['archive']);entries={e.name:e for e in archive.entries if e.flags==65536}
                existing={p['resource'].casefold() for p in inventory['asset_profiles']}
                wanted={m['mesh_resource'] for w in old['weapons'] for s in w['states'] for m in s['meshes'] if m.get('mesh_resource') and m['mesh_resource'].casefold() not in existing}
                selected={e.name:e for e in entries.values() if e.name.casefold() in {n.casefold() for n in wanted}}
                targeted=dict(archive=config['archive'],index_sha256=archive.index_sha256,meshes=[],errors=[])
                for name,entry in selected.items():
                    names=[n for n in (name,*[name[:-4]+f'_lod{i}_data.res' for i in range(4)]) if n in entries]
                    try:targeted['meshes'].append(assets_pipeline.mesh_record(archive,entry,archive.read_selected(names),entries))
                    except (ValueError,KeyError,TypeError) as e:targeted['errors'].append(dict(resource=name,reason=str(e)))
                targeted['exact_absent_resources']=sorted(wanted-{e.name for e in selected.values()})
                inventory=copy.deepcopy(inventory);inventory['archives'].append(targeted)
                inventory['asset_profiles'].extend(assets_pipeline.combine([targeted]))
                batch.write(args.output/'targeted-common-inventory.json',inventory)
                result['targeted_common_meshes']=dict(names=sorted(selected),errors=targeted['errors'],absent=targeted['exact_absent_resources'])
            refreshed=refresh_meshes(config,inventory,pool,args.game)
            new=grip.load(args.game,Path(config['archive']),config,refreshed,poses,reload_references_only=True)
            batch.write(args.output/'new-bindings.json',new);batch.write(args.output/'new-meshes.json',refreshed)
            bindings.update({b['binding_digest']:b for b in new['profiles']})
            replaced={(w['resource'],w['instance_guid'].lower()) for w in refreshed['weapons']}
            meshes['weapons']=[w for w in meshes['weapons'] if (w['resource'],w['instance_guid'].lower()) not in replaced]+refreshed['weapons']
            for row in plan['rows']:
                if row['binding_digest'] is None:
                    hits=[b for b in new['profiles'] if batch.weapon_key(b['weapon'])==batch.weapon_key(row['identity']['weapon'])]
                    if len(hits)==1:row['binding_digest']=hits[0]['binding_digest']
            result['new_reference_gaps']=new['gaps']
        groups={};prior_content_groups={}
        for row in plan['rows']:
            if row['strict_geometry']:continue
            if not row['binding_digest']:
                result['gaps'].append(dict(key=row['key'],reason='Exact reference generation still unresolved'));continue
            b=bindings[row['binding_digest']]
            try:
                prior_group=row.get('geometry_content_group')
                if prior_group in prior_content_groups:
                    key=prior_content_groups[prior_group]
                    groups[key]['configurations'].append(dict(key=row['key'],binding_digest=b['binding_digest'],weapon=b['weapon'],asset=row['asset']))
                    continue
                ctx=actions.context(args.game,b,meshes,pool)
                roles=g.bound_action_assets(b,ctx['graph_archive'],('1p_reload',),ctx['graph_dependencies'])
                batch.require('1p_reload' in roles,'No exact authored reload role')
                action=roles['1p_reload'];raw,source=g.selected_animation_resource(ctx['sources'],action['resource']);clip=Clip(raw)
                source={**ctx['receipt'],**action,**source,'sha256':g.sha(raw),'reference_clip_sha256':ctx['reference'].sha256}
                key=batch.descriptors.digest((source['mesh_sha256'],source['lod_sha256'],ctx['skeleton'].sha256,ctx['reference'].sha256,clip.sha256))
                if key not in groups:
                    print(json.dumps(dict(stage='policy_packet',asset=row['asset'],group=key[:16])),flush=True)
                    folder=args.output/key[:16];folder.mkdir(exist_ok=True)
                    packet=inspect(b,ctx,clip,source,folder);packet['strict_failures_preserved_in']=str(args.previous)
                    batch.write(folder/'source.json',packet)
                    groups[key]=dict(key=key,source=batch.pin(folder/'source.json'),configurations=[])
                    if row['asset'] in ('M16','M16k','USAS12','USAS12_sp','USAS12_sp_s'):
                        selected=dict(schema='fvr.bc2.authored_reload_reference_bindings',schema_version=1,profiles=[b]);assets={b['native_asset_name']}
                        fresh=g.derive(args.game,selected,meshes,assets,paired_grasp_assets=assets,rigid_assembly_assets=assets)
                        batch.write(folder/'strict-linear.json',fresh);groups[key]['strict_linear']=batch.pin(folder/'strict-linear.json')
                groups[key]['configurations'].append(dict(key=row['key'],binding_digest=b['binding_digest'],weapon=b['weapon'],asset=row['asset']))
                if prior_group:prior_content_groups[prior_group]=key
            except (ValueError,KeyError,TypeError,OSError) as e:result['gaps'].append(dict(key=row['key'],asset=row['asset'],reason=str(e)))
    result['groups']=list(groups.values());result['selected_resource_bytes']=pool.bytes
    batch.write(args.output/'bindings.json',dict(schema='fvr.bc2.authored_reload_reference_bindings',schema_version=1,profiles=list(bindings.values())))
    batch.write(args.output/'meshes.json',meshes);batch.write(args.output/'batch.json',plan);batch.write(args.output/'packet.json',result)
    print(json.dumps(dict(groups=len(groups),gaps=len(result['gaps']),configurations=sum(len(g['configurations']) for g in groups.values()))))
    return 0 if not result['gaps'] else 2


if __name__=='__main__':raise SystemExit(main())
