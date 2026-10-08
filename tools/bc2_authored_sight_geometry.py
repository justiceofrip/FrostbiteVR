"""Measure a configured two-mode sight hinge; never enables native interaction.

Explicit config GUIDs and archives are inputs. Reuses the bounded DBX, Granny,
skin and mechanism readers; no filename/class inheritance or native offsets.
"""
from __future__ import annotations
import argparse,json,math,re
from pathlib import Path
import bc2_authored_grip_bindings as g
import bc2_authored_magazine_geometry as m
from bc2_authored_mechanism_geometry import classify,paired_contacts,axis_angle
from bc2_weapon_config_pipeline import Resolver,parse,named,scalar
from bc2_weapon_mesh_bindings import reference_resource
from bc2_weapon_animation_pipeline import Clip,Skeleton
from bc2_mesh_geometry import metadata,geometry
from inspect_bc2_mesh_asset import bone_hash,sha

def role(resolver,origin,state,wanted):
    """Resolve one absolute authored role, preserving tree/asset/skeleton GUIDs."""
    if wanted not in ('handsikpose','1p_altdeploy'):raise ValueError('Unsupported sight role')
    tree_doc,tree=resolver.resolve(origin,named(state,'AnimTree1p').attributes.get('ref'),'Animation.SpecificAnimTreeData')
    found=[]
    for ref in g.fields(tree,'ReplacmentAnimations'):
        doc,asset=g.referenced_instance(resolver,tree_doc,ref)
        name=scalar(named(asset,'Name'),'string');g.path_key(name)
        if name.rsplit('/',1)[-1].casefold()!=wanted:continue
        if asset.attributes['type']!='Animation.AnimationAsset':raise ValueError('Sight role must be absolute AnimationAsset')
        sk_doc,sk=resolver.resolve(doc,named(asset,'Skeleton').attributes.get('ref'),'Animation.SkeletonAsset')
        sk_name=scalar(named(sk,'Name'),'string');g.path_key(sk_name)
        found.append({'tree':g.receipt(tree_doc,tree),'asset':g.receipt(doc,asset),
            'resource':name+'.res','skeleton':g.receipt(sk_doc,sk),'skeleton_resource':sk_name+'.res'})
    if len(found)!=1:raise ValueError('Missing/ambiguous exact sight role: '+wanted)
    return found[0]

def hinge_profile(closed,opened,opening,closing):
    """Parent-local closed/open frames, with reciprocal measured transitions."""
    for frame in (closed,opened,*opening,*closing):g.matrix(frame)
    forward=classify(opening);reverse=classify(closing)
    if forward['kind']!='hinge_candidate' or reverse['kind']!='hinge_candidate':raise ValueError('Both mode transitions must be rigid hinges')
    delta=m.multiply(opened,m.inverse_rigid(closed))
    axis,angle=axis_angle(delta)
    if axis is None or not .2<=angle<=math.pi/2 or m.norm(delta[12:15])>.002:raise ValueError('Invalid settled hinge travel')
    # Each transition must start/end at the opposite authored static poses.
    for actual,expected in ((opening[0],closed),(opening[-1],opened),(closing[0],opened),(closing[-1],closed)):
        distance,rotation=m.grasp_separation(actual,expected)
        if distance>.002 or rotation>.02:raise ValueError('Transition endpoints disagree with exact static modes')
    if m.dot(axis,forward['axis_in_baseline_part'])<.99 or m.dot(axis,reverse['axis_in_baseline_part'])>-.99:
        raise ValueError('Mode transition hinge directions disagree')
    # A moving parent is allowed, but a sight cannot acquire slide semantics.
    residual=max(m.norm(m.multiply(f,m.inverse_rigid(closed))[12:15]) for f in opening+closing)
    if residual>.002:raise ValueError('Hinge pivot moves in parent frame')
    # For row-vector part->parent frames, frame*inverse(closed) expresses the
    # rotation in the baseline part, already invariant under parent motion.
    return {'axis_part':axis,'settled_travel_rad':angle,'maximum_travel_rad':max(forward['max_rotation_rad'],reverse['max_rotation_rad']),
            'parent_from_closed':closed,'parent_from_open':opened,'pivot_residual_m':residual,
            'opening':forward,'closing':reverse}

def measure(skeleton,parts,sections,primary_static,secondary_static,opening,closing,bone,weapon):
    if bone not in parts or weapon not in skeleton.names:raise ValueError('Exact sight/weapon part missing')
    parents={n:skeleton.names[p] if p>=0 else None for n,p in zip(skeleton.names,skeleton.parents)}
    chain=[];at=bone
    while at!=weapon:
        if at is None or at in chain or len(chain)>=16:raise ValueError('Sight not a bounded weapon descendant')
        chain.append(at);at=parents[at]
    chain.append(weapon);parent=parents[bone];part=parts[bone]
    if parent is None or part['mixed_triangles'] or part['triangles']<12:
        raise ValueError('Sight must have independent rigid geometry')
    if any(bone in s['bone_names'] and s['multi_weight_vertices'] for s in sections):raise ValueError('Sight has weighted/coupled skin')
    descendants=[]
    for name in skeleton.names:
        at=parents[name];seen=set()
        while at is not None:
            if at in seen:raise ValueError('Skeleton parent cycle')
            seen.add(at)
            if at==bone:descendants.append(name);break
            at=parents[at]
    if any(n in parts or any(n in s['bone_names'] for s in sections) for n in descendants):
        raise ValueError('Sight descendants also own geometry; coupled profile required')
    names=[bone,parent,'LeftHand',*m.FINGERS]
    for digit in ('Thumb','Index','Middle','Ring','Pinky'):
        for index in (1,2,3):
            if parents.get(f'LeftHand{digit}{index}')!=('LeftHand' if index==1 else f'LeftHand{digit}{index-1}'):
                raise ValueError('Complete exact left finger topology required')
    static=[c.evaluate(skeleton,0,names,weapon=weapon) for c in (primary_static,secondary_static)]
    relative=lambda s:m.multiply(s['weapon_relative'][bone],m.inverse_rigid(s['weapon_relative'][parent]))
    settled_variation=[]
    for clip,start in zip((primary_static,secondary_static),static):
        if not 0<clip.duration<=.1:raise ValueError('Static role duration bound')
        # Some HandsIkPose ancestry has tiny nonconstant controls. Retain that
        # fact; bound the decoded parent-local motion rather than calling the
        # entire authored world chain constant or silently using bind defaults.
        errors=[m.grasp_separation(relative(start),relative(clip.evaluate(skeleton,i*clip.duration/8,names,weapon=weapon))) for i in range(9)]
        maximum=[max(e[k] for e in errors) for k in range(2)]
        if maximum[0]>.0001 or maximum[1]>.001:raise ValueError('Settled sight pose moves')
        settled_variation.append({'maximum_translation_m':maximum[0],'maximum_rotation_rad':maximum[1],
            'samples':9,'bone_evaluation_status':{n:start['bone_evaluation_status'][n] for n in (bone,parent)}})
    clips=[]
    for clip in (opening,closing):
        if not .2<=clip.duration<=15:raise ValueError('Sight clip duration bound')
        clips.append([clip.evaluate(skeleton,min(i/30,clip.duration),names,weapon=weapon) for i in range(math.ceil(clip.duration*30)+1)])
    mechanism=hinge_profile(*[relative(s) for s in static],*[list(map(relative,samples)) for samples in clips])
    contacts=[paired_contacts(samples,bone,part) for samples in clips]
    return {'bone_chain':chain,'unskinned_descendants':descendants,'bone':bone,'weapon':weapon,'rig_fingerprint':skeleton.fingerprint,
        'bounds_part_m':[part['minimum'],part['maximum']],'triangles':part['triangles'],'sections':part['sections'],
        'weapon_from_closed':static[0]['weapon_relative'][bone],'weapon_from_open':static[1]['weapon_relative'][bone],
        'mechanism':mechanism,'settled_pose_observation':settled_variation,'authored_contacts':contacts,
        'grasp_policy':'existing complete bind-derived MechanismGrip; raw controller wrist and distal-knuckle proxy',
        'authored_contact_available':any(c['pose'] is not None for c in contacts),
        'sample_hz':30,'runtime_admitted':False,'native_playback_verified':False,'headset_verified':False}

def run(game,plan):
    if plan.get('schema')!='fvr.bc2.sight_measurement_plan.v1':raise ValueError('Sight plan schema')
    archive=m.exact_archive(game,plan['configuration_archive'])
    entries={}
    for entry in archive.entries:
        if entry.flags!=65536 or not entry.name.lower().endswith('.dbx'):continue
        key=g.path_key(entry.name)
        if key in entries:raise ValueError('Ambiguous document path')
        entries[key]=entry
    documents={}
    def read_documents(paths):
        fresh=sorted({g.path_key(p) for p in paths}-documents.keys())
        if len(documents)+len(fresh)>128:raise ValueError('Sight document count bound')
        if any(p not in entries or entries[p].kind!='<non-resource>' for p in fresh):raise ValueError('Missing exact DBX')
        blobs=archive.read_selected([entries[p].name for p in fresh]) if fresh else {}
        for p in fresh:documents[p]=parse(entries[p].name,blobs[entries[p].name])
    refs=[plan['primary_reference'],plan['secondary_reference']]
    read_documents([reference_resource(r) for r in refs]);resolver=Resolver(documents.values())
    configs=[]
    for ref in refs:
        doc,node=resolver.resolve(documents[reference_resource(ref)],ref,'GameSharedResources.SoldierWeaponData')
        states=named(node,'WeaponStates')
        if states.tag!='array' or not 0<len(states.children)<=32:raise ValueError('Weapon state bound')
        index=plan['state_index']
        if isinstance(index,bool) or not isinstance(index,int) or not 0<=index<len(states.children):raise ValueError('Exact weapon state index')
        state=states.children[index];meshes=g.fields(state,'Meshes1p')
        if g.ref_key(plan['mesh_reference']) not in list(map(g.ref_key,meshes)):raise ValueError('Sight mesh not configured in both modes')
        configs.append((doc,node,state))
    tree_refs=[named(state,'AnimTree1p').attributes.get('ref') for _,_,state in configs]
    read_documents([reference_resource(ref) for ref in tree_refs]+[reference_resource(plan['mesh_reference'])]);resolver=Resolver(documents.values())
    animation_refs=[]
    for (doc,_,_),ref in zip(configs,tree_refs):
        tree_doc,tree=resolver.resolve(doc,ref,'Animation.SpecificAnimTreeData');animation_refs+=g.fields(tree,'ReplacmentAnimations')
    read_documents([reference_resource(r) for r in animation_refs]);resolver=Resolver(documents.values())
    skeleton_refs=[]
    for ref in animation_refs:
        doc,node=g.referenced_instance(resolver,configs[0][0],ref)
        if scalar(named(node,'Name'),'string').rsplit('/',1)[-1].lower() in ('handsikpose','1p_altdeploy'):
            skeleton_refs.append(named(node,'Skeleton').attributes.get('ref'))
    read_documents([reference_resource(ref) for ref in skeleton_refs]);resolver=Resolver(documents.values())
    roles=[[role(resolver,doc,state,r) for r in ('handsikpose','1p_altdeploy')] for doc,_,state in configs]
    if any(r['skeleton']!=roles[0][0]['skeleton'] or r['skeleton_resource']!=roles[0][0]['skeleton_resource'] for pair in roles for r in pair):
        raise ValueError('Both modes must reference the same exact skeleton')
    skeleton=Skeleton(m.exact_resource(archive,roles[0][0]['skeleton_resource'],'GrannyModel'))
    clips=[]
    for mode,pair in zip(('primary','secondary'),roles):
        source=m.exact_archive(game,plan[mode+'_animation_archive']);decoded=[]
        for receipt in pair:
            clip=Clip(m.exact_resource(source,receipt['resource'],'GrannyAnimation'))
            receipt.update(archive=plan[mode+'_animation_archive'],index_sha256=source.index_sha256,sha256=clip.sha256)
            decoded.append(clip)
        clips.append(decoded)
    mesh_doc,mesh_asset=resolver.resolve(configs[0][0],plan['mesh_reference'],'Render.SkinnedMeshAsset')
    mesh_name=scalar(named(mesh_asset,'Name'),'string');g.path_key(mesh_name)
    if g.path_key(mesh_name+'.dbx')!=g.path_key(mesh_doc.resource):raise ValueError('Configured mesh Name/path mismatch')
    source=m.exact_archive(game,plan['mesh_archive']);raw=m.exact_resource(source,mesh_name+'.res','SkinnedMeshSet')
    lods=metadata(raw)
    if not lods:raise ValueError('No mesh LOD0')
    lod_name=mesh_name+'_lod0_data.res';data=m.exact_resource(source,lod_name,'MeshData')
    parts=m.rigid_parts(data,lods[0],skeleton);sections=geometry(data,lods[0],{bone_hash(n):n for n in skeleton.names})
    result=measure(skeleton,parts,sections,clips[0][0],clips[1][0],clips[1][1],clips[0][1],plan['bone'],plan['weapon_bone'])
    result.update(schema='fvr.bc2.authored_sight_geometry.v1',configuration_archive=plan['configuration_archive'],configuration_index_sha256=archive.index_sha256,
        configurations=[{'config':g.receipt(doc,node),'asset_name':scalar(named(node,'Name'),'string'),'state_index':plan['state_index']} for doc,node,_ in configs],
        role_sources=roles,skeleton_sha256=skeleton.sha256,mesh_reference=plan['mesh_reference'],mesh_document=g.receipt(mesh_doc,mesh_asset),
        mesh_archive=plan['mesh_archive'],mesh_index_sha256=source.index_sha256,mesh_resource=mesh_name+'.res',mesh_sha256=sha(raw),lod_resource=lod_name,lod_sha256=sha(data))
    result['digest']=g.digest(result);return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',required=True,type=Path);p.add_argument('--plan',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--header',type=Path)
    a=p.parse_args();raw=a.plan.read_bytes()
    if len(raw)>65536:raise ValueError('Sight plan size bound')
    result=run(a.game,json.loads(raw));a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n');print(json.dumps({k:result[k] for k in ('digest','bone_chain','triangles','authored_contact_available','runtime_admitted')}))
    if a.header:
        a.header.parent.mkdir(parents=True,exist_ok=True);a.header.write_text(header(result))
    return 0

def header(result):
    """Private derived profile, not native admission and never copied mesh bytes."""
    if result.get('schema')!='fvr.bc2.authored_sight_geometry.v1' or result.get('runtime_admitted') is not False:raise ValueError('Derived sight schema/admission')
    if result.get('digest')!=g.digest({k:v for k,v in result.items() if k!='digest'}):raise ValueError('Sight profile digest mismatch')
    if not re.fullmatch(r'fnv1a64:[0-9a-f]{16}',result['rig_fingerprint']):raise ValueError('Invalid rig fingerprint')
    primary,secondary=result['configurations'];quote=json.dumps
    def vector(values):
        if len(values)!=3 or any(not math.isfinite(v) for v in values):raise ValueError('Invalid vector')
        def literal(v):
            text=format(v,'.9g');return text+('' if '.' in text or 'e' in text.lower() else '.0')+'f'
        return '{'+','.join(literal(v) for v in values)+'}'
    axis=result['mechanism']['axis_part'];length=m.norm(axis)
    if abs(length-1)>1e-6:raise ValueError('Nonunit hinge axis')
    chain=result['bone_chain']
    if not 2<=len(chain)<=16 or len(set(chain))!=len(chain):raise ValueError('Bone chain bound/duplicates')
    return '\n'.join(['// Generated private authored geometry; runtime admission remains disabled.',
        '#pragma once','#include "Bc2AuthoredSight.h"','namespace fvr::bc2::generated_sight {',
        'inline constexpr std::string_view Digest='+quote(result['digest'])+';',
        'inline constexpr std::array<std::string_view,'+str(len(chain))+'> Chain{'+','.join(map(quote,chain))+'};',
        'inline const AuthoredSightGeometry Profile{',
        ','.join(quote(v) for v in (result['mesh_resource'][:-4],primary['asset_name'],primary['config']['instance_guid'],secondary['asset_name'],secondary['config']['instance_guid']))+',',
        'Chain,0x'+result['rig_fingerprint'].removeprefix('fnv1a64:')+'ull,',
        ','.join(vector(v) for v in (*result['bounds_part_m'],axis))+',',
        format(result['mechanism']['settled_travel_rad'],'.9g')+'f};','}'])+'\n'
if __name__=='__main__':raise SystemExit(main())
