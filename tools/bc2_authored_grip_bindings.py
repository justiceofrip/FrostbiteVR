"""Bind static authored hand poses through exact weapon/animation/mesh references."""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import re

from bc2_weapon_config_pipeline import Resolver,guid,named,parse,scalar
from bc2_weapon_mesh_bindings import reference_resource
from bc2_weapon_animation_pipeline import Clip,Skeleton,inverse_rigid
from inspect_bc2_mesh_asset import Archive,sha

SCHEMA='fvr.bc2.authored_grip_bindings'
HASH=re.compile(r'[0-9a-f]{64}')
def digest(value):return sha(json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False).encode())
def checked_hash(value):
    if not isinstance(value,str) or not HASH.fullmatch(value):raise ValueError('Invalid content hash')
    return value
def path_key(value):
    if not isinstance(value,str) or not value or '\\' in value or ':' in value or any(p in ('','.','..') for p in value.split('/')):
        raise ValueError('Invalid resource path')
    return value.lower()
def ref_key(value):
    if '/' not in value:return guid(value)
    path,identifier=value.rsplit('/',1)
    return path_key(path)+'/'+guid(identifier)
def fields(instance,field):
    node=named(instance,field)
    if node.tag!='array' or len(node.children)>256:raise ValueError('Invalid bounded reference array: '+field)
    refs=[]
    for item in node.children:
        if item.tag!='item' or item.children or 'ref' not in item.attributes:raise ValueError('Invalid reference item: '+field)
        refs.append(item.attributes['ref'])
    if len(set(map(ref_key,refs)))!=len(refs):raise ValueError('Duplicate array reference: '+field)
    return refs
def required_document(resolver,resource):
    key=path_key(resource)
    if not key.endswith('.dbx'):raise ValueError('DBX resource required')
    try:return resolver.documents[key[:-4]]
    except KeyError as exc:raise ValueError('Exact authored document unavailable: '+resource) from exc
def receipt(document,instance):
    return {'resource':document.resource,'sha256':document.sha256,'instance_guid':guid(instance.attributes['guid']),
            'type':instance.attributes['type']}
def referenced_instance(resolver,origin,reference):
    key=ref_key(reference)
    if '/' in key:
        path,identifier=key.rsplit('/',1);doc=resolver.documents.get(path)
        if doc is None:raise ValueError('Missing referenced animation document')
    else:doc=origin;identifier=key
    instance=doc.instances.get(identifier)
    if instance is None:raise ValueError('Missing referenced animation GUID')
    return doc,instance
def matrix(value):
    if not isinstance(value,list) or len(value)!=16 or any(isinstance(x,bool) or not isinstance(x,(int,float)) or not math.isfinite(x) for x in value):
        raise ValueError('Invalid finite pose matrix')
    if any(abs(value[n])>1e-9 for n in (3,7,11)) or abs(value[15]-1)>1e-9:raise ValueError('Nonaffine pose matrix')
    inverse_rigid(value)
    return list(value)
def unique_pose_source(resource,pose_batch):
    rows=[row for row in pose_batch['clips'] if path_key(row['resource'])==path_key(resource)]
    if len(rows)!=1:raise ValueError('Missing or ambiguous exact animation content variant')
    row=rows[0];checked_hash(row['sha256']);sources=row.get('sources',[])
    if not sources:raise ValueError('Exact animation source archive absent')
    for source in sources:path_key(source['archive']);checked_hash(source['index_sha256'])
    return row,min(sources,key=lambda s:s['archive'])

def authored_clip(resolver,document,state):
    reference=named(state,'AnimTree1p').attributes.get('ref')
    tree_document,tree=resolver.resolve(document,reference,'Animation.SpecificAnimTreeData')
    candidates=[]
    for animation_ref in fields(tree,'ReplacmentAnimations'):
        clip_document,asset=referenced_instance(resolver,tree_document,animation_ref)
        name=scalar(named(asset,'Name'),'string');path_key(name)
        # Select the explicitly authored role name, independent of array order.
        if name.rsplit('/',1)[-1].casefold()=='handsikpose':
            if asset.attributes['type']!='Animation.AnimationAsset':raise ValueError('HandsIkPose is not an absolute AnimationAsset')
            candidates.append((clip_document,asset,name))
    if len(candidates)!=1:raise ValueError('Missing or ambiguous authored HandsIkPose role')
    clip_document,asset,name=candidates[0]
    skeleton_ref=named(asset,'Skeleton').attributes.get('ref')
    skeleton_document,skeleton=resolver.resolve(clip_document,skeleton_ref,'Animation.SkeletonAsset')
    skeleton_name=scalar(named(skeleton,'Name'),'string');path_key(skeleton_name)
    return {'tree':receipt(tree_document,tree),'animation_asset':receipt(clip_document,asset),
            'animation_resource':name+'.res','skeleton_asset':receipt(skeleton_document,skeleton),
            'skeleton_resource':skeleton_name+'.res'}

def mesh_state(weapon,state_index,state,mesh_bindings,resolver,origin):
    matches=[w for w in mesh_bindings['weapons'] if w['native_name']==weapon['native_name'] and
             path_key(w['resource'])==path_key(weapon['resource']) and guid(w['instance_guid'])==guid(weapon['instance_guid'])]
    if len(matches)!=1 or matches[0]['resource_sha256']!=weapon['resource_sha256']:raise ValueError('Mesh binding weapon/config identity mismatch')
    states=[s for s in matches[0]['states'] if s['index']==state_index]
    if len(states)!=1:raise ValueError('Mesh binding state missing/ambiguous')
    expected=fields(state,'Meshes1p');meshes=states[0]['meshes']
    if [ref_key(e['reference']) for e in meshes]!=list(map(ref_key,expected)):raise ValueError('Mesh binding state reference mismatch')
    if not meshes:raise ValueError('No authored first-person meshes')
    output=[]
    for mesh in meshes:
        if mesh['status']!='authored_mesh_resolved':raise ValueError('Unresolved first-person mesh')
        entry={k:mesh[k] for k in ('reference','mesh_document','mesh_document_sha256','mesh_instance_guid','mesh_resource')}
        checked_hash(entry['mesh_document_sha256']);path_key(entry['mesh_resource'])
        doc,instance=resolver.resolve(origin,entry['reference'],'Render.SkinnedMeshAsset')
        name=scalar(named(instance,'Name'),'string');path_key(name)
        if (path_key(doc.resource)!=path_key(entry['mesh_document']) or doc.sha256!=entry['mesh_document_sha256'] or
            guid(instance.attributes['guid'])!=guid(entry['mesh_instance_guid']) or name+'.res'!=entry['mesh_resource']):
            raise ValueError('Mesh document/GUID/Name content mismatch')
        entry['configured_mesh_path']=name
        variants=mesh.get('geometry_variants',[])
        # Do not choose a mesh variant using a weapon directory or clip co-occurrence.
        if len(variants)>1:raise ValueError('Ambiguous mesh content variants require an exact source')
        entry['geometry']=copy.deepcopy(variants[0]) if variants else None
        if variants:checked_hash(variants[0]['mesh_sha256'])
        output.append(entry)
    if not any(e['geometry'] for e in output):raise ValueError('No exact mesh geometry binding')
    return output

def rifle_support_reference(profile):
    expected=profile.get('weapon_class') in ('wcAssault','wcSmg')
    value=profile.get('authored_rifle_support',False)
    if not isinstance(value,bool) or value!=expected:raise ValueError('Authored support classification mismatch')
    return value

def build(configurations,documents,mesh_bindings,pose_batch,resources,*,reload_references_only=False):
    """Pure join. resources contains exact decoded current-archive evidence from load()."""
    if configurations.get('schema')!='fvr.bc2.authored_weapon_configuration' or configurations.get('schema_version')!=1:raise ValueError('Configuration schema')
    if mesh_bindings.get('schema')!='fvr.bc2.authored_weapon_mesh_bindings' or mesh_bindings.get('schema_version')!=1:raise ValueError('Mesh binding schema')
    if pose_batch.get('schema')!='fvr.bc2.authored_hand_pose_batch.v1':raise ValueError('Pose batch schema')
    if len(configurations['resolved_weapons'])>1024 or len(pose_batch['clips'])>1024:raise ValueError('Join count bound')
    resolver=Resolver(documents);by_resource={}
    for row in pose_batch['clips']:
        key=(path_key(row['resource']),checked_hash(row['sha256']))
        if key in by_resource:raise ValueError('Duplicate exact pose resource/content')
        by_resource[key]=row
    output=[];gaps=[];seen=set()
    for weapon in configurations['resolved_weapons']:
        identity=(path_key(weapon['resource']),guid(weapon['instance_guid']))
        if identity in seen:raise ValueError('Duplicate weapon definition')
        seen.add(identity)
        try:
            doc=required_document(resolver,weapon['resource'])
            if doc.sha256!=weapon['resource_sha256']:raise ValueError('Weapon DBX content mismatch')
            instance=doc.instances.get(identity[1])
            if instance is None or instance.attributes['type']!='GameSharedResources.SoldierWeaponData':raise ValueError('Weapon GUID/type mismatch')
            if scalar(named(instance,'Name'),'string')!=weapon['native_name']:raise ValueError('Native weapon name mismatch')
            weapon_class=scalar(named(instance,'WeaponClass'),'string')
            if weapon_class!=weapon.get('weapon_class'):raise ValueError('Authored weapon class mismatch')
            states=named(instance,'WeaponStates')
            if states.tag!='array' or not 0<len(states.children)<=32:raise ValueError('Weapon state count/type')
        except (ValueError,KeyError,TypeError) as exc:
            gaps.append({'native_name':weapon.get('native_name'),'resource':weapon.get('resource'),'reason':str(exc)});continue
        for index,state in enumerate(states.children):
            try:
                chain=authored_clip(resolver,doc,state)
                meshes=mesh_state(weapon,index,state,mesh_bindings,resolver,doc)
                clip=resources[path_key(chain['animation_resource'])]
                skeleton=resources[path_key(chain['skeleton_resource'])]
                if clip['kind']!='GrannyAnimation' or skeleton['kind']!='GrannyModel':raise ValueError('Exact resource type mismatch')
                if clip.get('skeleton_sha256')!=skeleton['sha256']:raise ValueError('Clip evaluated with different skeleton')
                row=by_resource.get((path_key(chain['animation_resource']),checked_hash(clip['sha256'])))
                if row is None:raise ValueError('Exact authored clip bytes absent from pose batch')
                sk=pose_batch['skeleton'];meta=skeleton['metadata']
                if path_key(sk['resource'])!=path_key(chain['skeleton_resource']) or sk['resource_sha256']!=skeleton['sha256'] or sk['rig_fingerprint']!=meta['rig_fingerprint'] or sk['bones']!=meta['bones']:
                    raise ValueError('Pose batch skeleton identity/topology mismatch')
                if not re.fullmatch(r'fnv1a64:[0-9a-f]{16}',meta['rig_fingerprint']):raise ValueError('Rig fingerprint format')
                actual=clip['hands'];saved=row.get('hands',{})
                if row.get('status')!='authored_hands_decoded':raise ValueError('Required hands are not decoded')
                if not reload_references_only and (actual.get('evaluation_status')!='static_authored_pose' or saved.get('evaluation_status')!='static_authored_pose'):raise ValueError('Required hands are not static authored controls')
                if actual!=saved:raise ValueError('Saved pose differs from exact current authored evaluation')
                hands={name:matrix(actual['weapon_relative'][name]) for name in ('RightHand','LeftHand')}
                parts={}
                actual_parts=clip.get('parts_and_fingers',{})
                for name,transform in actual_parts.get('weapon_relative',{}).items():
                    if actual_parts['bone_evaluation_status'].get(name)=='static_authored_pose':parts[name]=matrix(transform)
                entry={'native_asset_name':weapon['native_name'],'weapon_class':weapon_class,'authored_rifle_support':weapon_class in ('wcAssault','wcSmg'),'weapon':receipt(doc,instance),'state_index':index,'state_count':len(states.children),'meshes':meshes,
                    'animation_chain':chain,'animation_sha256':clip['sha256'],'skeleton_sha256':skeleton['sha256'],
                    'animation_source':clip['source'],'skeleton_source':skeleton['source'],
                    'rig_fingerprint':meta['rig_fingerprint'],'rig_topology':meta['bones'],
                    'weapon_bone':'jntWpn_1','coordinate_convention':actual['coordinate_convention'],
                    'right_hand_in_weapon':hands['RightHand'],'left_hand_in_weapon':hands['LeftHand'],
                    'right_hand_status':actual['bone_evaluation_status']['RightHand'],
                    'left_hand_status':actual['bone_evaluation_status']['LeftHand'],
                    'all_required_controls_constant':True,'evaluation_status':'static_authored_pose',
                    'static_parts_and_fingers':parts,'status':'authored_static_grip_bound',
                    'runtime_accepted':False,'active_native_mesh_binding':False}
                if reload_references_only:
                    entry.update(status='authored_reload_references_only',right_hand_status=actual['bone_evaluation_status']['RightHand'],
                        left_hand_status=actual['bone_evaluation_status']['LeftHand'],all_required_controls_constant=actual['all_required_controls_constant'],
                        evaluation_status=actual['evaluation_status'],authored_rifle_support=False)
                for mesh in meshes:
                    if mesh['geometry'] is None:continue
                    profile=copy.deepcopy(entry);profile['configured_mesh_path']=mesh['configured_mesh_path']
                    profile['binding_digest']=digest(profile);output.append(profile)
            except (ValueError,KeyError,TypeError) as exc:
                gaps.append({'native_name':weapon['native_name'],'resource':doc.resource,'state_index':index,'reason':str(exc)})
    return {'schema':('fvr.bc2.authored_reload_reference_bindings' if reload_references_only else SCHEMA),'schema_version':1,'profiles':output,'gaps':gaps,'runtime_admission':False,
            'scope':'Exact authored reference and content binding; current native equipment/mesh/rig guards remain required',
            'summary':{'weapon_definitions':len(configurations['resolved_weapons']),'bound_profiles':len(output),'gaps':len(gaps)}}

def load(game,archive_relative,configurations,mesh_bindings,pose_batch,*,reload_references_only=False):
    """Resolve a bounded authored reference closure in five bulk archive reads."""
    if len(configurations.get('resolved_weapons',[]))>1024 or len(mesh_bindings.get('weapons',[]))>1024 or len(pose_batch.get('clips',[]))>1024:
        raise ValueError('Join count bound before archive reads')
    for weapon in mesh_bindings.get('weapons',[]):
        if len(weapon.get('states',[]))>32 or any(len(state.get('meshes',[]))>32 for state in weapon.get('states',[])):
            raise ValueError('Mesh state count bound')
    game=game.resolve();path=(game/archive_relative).resolve()
    if not path.is_relative_to(game):raise ValueError('Archive escapes game root')
    archive=Archive(path);entries={}
    for entry in archive.entries:
        if entry.flags!=65536:continue
        key=path_key(entry.name)
        if key in entries:raise ValueError('Ambiguous case-insensitive archive resource')
        entries[key]=entry
    docs={};errors=[]
    def read_documents(names):
        selected=[]
        for name in sorted(set(names)):
            key=path_key(name)
            if key not in docs and key in entries:selected.append(entries[key].name)
        if len(selected)>4096:raise ValueError('Document closure bound')
        if not selected:return
        for name,data in archive.read_selected(selected).items():
            try:docs[path_key(name)]=parse(name,data)
            except ValueError as exc:errors.append({'resource':name,'sha256':sha(data),'reason':str(exc)})
    def referenced(origin,reference):
        return reference_resource(reference) if '/' in reference else origin
    read_documents([w['resource'] for w in configurations['resolved_weapons']]+
                   [m['mesh_document'] for w in mesh_bindings['weapons'] for s in w['states'] for m in s['meshes'] if m.get('status')=='authored_mesh_resolved'])
    trees=[]
    for w in configurations['resolved_weapons']:
        try:
            doc=docs[path_key(w['resource'])];instance=doc.instances[guid(w['instance_guid'])]
            for state in named(instance,'WeaponStates').children:trees.append(referenced(doc.resource,named(state,'AnimTree1p').attributes['ref']))
        except (ValueError,KeyError):pass # Exact join records missing ownership.
    read_documents(trees)
    animations=[]
    for name in set(trees):
        doc=docs.get(path_key(name))
        if doc:
            for instance in doc.instances.values():
                if instance.attributes['type']=='Animation.SpecificAnimTreeData':
                    try:animations.extend(referenced(doc.resource,ref) for ref in fields(instance,'ReplacmentAnimations'))
                    except ValueError:pass
    read_documents(animations)
    skeletons=[]
    for name in set(animations):
        doc=docs.get(path_key(name))
        if doc:
            for instance in doc.instances.values():
                if instance.attributes['type']=='Animation.AnimationAsset':
                    try:skeletons.append(referenced(doc.resource,named(instance,'Skeleton').attributes['ref']))
                    except (ValueError,KeyError):pass
    read_documents(skeletons)
    resolver=Resolver(list(docs.values()));chains=[]
    for w in configurations['resolved_weapons']:
        try:
            doc=docs[path_key(w['resource'])];instance=doc.instances[guid(w['instance_guid'])]
            for state in named(instance,'WeaponStates').children:
                try:chains.append(authored_clip(resolver,doc,state))
                except (ValueError,KeyError):pass
        except (ValueError,KeyError):pass
    wanted={path_key(c[k]) for c in chains for k in ('animation_resource','skeleton_resource')}
    raw=archive.read_selected([entries[k].name for k in sorted(wanted) if k in entries]) if wanted else {}
    blobs={path_key(k):v for k,v in raw.items()};resources={};skeleton_cache={}
    source_info={k:{'selection':'exact_configuration_archive_resource','archive':archive_relative.as_posix(),
                   'archive_index_sha256':archive.index_sha256,'kind':entries[k].kind} for k in blobs}
    # If the configuration archive omits the payload, a single content variant
    # across the explicit batch is usable. Multiple variants require an exact
    # source and remain unresolved; no weapon-directory heuristic is involved.
    fallback={}
    for key in sorted(wanted-set(blobs)):
        try:row,source=unique_pose_source(key,pose_batch)
        except ValueError as exc:errors.append({'resource':key,'reason':str(exc)});continue
        relative=Path(source['archive'])
        target=(game/relative).resolve()
        if relative.is_absolute() or not target.is_relative_to(game):raise ValueError('Pose source archive escapes game root')
        fallback.setdefault(source['archive'],[]).append((key,row,source))
    for relative,jobs in sorted(fallback.items()):
        external=Archive(game/relative);index={}
        for entry in external.entries:
            if entry.flags!=65536:continue
            key=path_key(entry.name)
            if key in index:raise ValueError('Ambiguous external resource case')
            index[key]=entry
        chosen=[]
        for key,row,source in jobs:
            if external.index_sha256!=source['index_sha256']:raise ValueError('Animation source archive index changed')
            if key not in index or index[key].kind!='GrannyAnimation':raise ValueError('Animation source resource absent/wrong type')
            chosen.append(index[key].name)
        external_blobs=external.read_selected(chosen)
        for key,row,source in jobs:
            data=external_blobs[index[key].name]
            if sha(data)!=checked_hash(row['sha256']):raise ValueError('Animation source content changed')
            blobs[key]=data
            source_info[key]={'selection':'unique_exact_resource_content_in_batch','archive':relative,
                              'archive_index_sha256':external.index_sha256,'kind':index[key].kind}
    for chain in chains:
        sk=path_key(chain['skeleton_resource']);ck=path_key(chain['animation_resource'])
        if sk not in blobs or ck not in blobs:continue
        try:
            if source_info[sk]['kind']!='GrannyModel' or source_info[ck]['kind']!='GrannyAnimation':raise ValueError('Referenced resource kind mismatch')
            if sk not in skeleton_cache:skeleton_cache[sk]=Skeleton(blobs[sk])
            skeleton=skeleton_cache[sk]
            resources[sk]={'kind':source_info[sk]['kind'],'sha256':sha(blobs[sk]),'metadata':skeleton.metadata(),'source':source_info[sk]}
            if ck in resources:
                if resources[ck]['skeleton_sha256']!=skeleton.sha256:raise ValueError('Clip is associated with multiple skeleton contents')
                continue
            clip=Clip(blobs[ck]);hands=clip.evaluate(skeleton,0.,['RightHand','LeftHand'])
            item={'kind':source_info[ck]['kind'],'sha256':sha(blobs[ck]),'hands':hands,
                  'skeleton_sha256':skeleton.sha256,'source':source_info[ck]}
            names=[n for n in skeleton.names if n in clip.tracks and n.startswith(('LeftHand','RightHand','jntWpn'))]
            try:item['parts_and_fingers']=clip.evaluate(skeleton,0.,names)
            except ValueError:pass
            resources[ck]=item
        except (ValueError,KeyError) as exc:errors.append({'resource':chain['animation_resource'],'reason':str(exc)})
    result=build(configurations,list(docs.values()),mesh_bindings,pose_batch,resources,reload_references_only=reload_references_only)
    result.update(archive=archive_relative.as_posix(),archive_index_sha256=archive.index_sha256,parse_or_evaluation_errors=errors)
    return result

def header_profiles(result,assets):
    if result.get('schema')!=SCHEMA or result.get('schema_version')!=1:raise ValueError('Binding schema')
    if not assets or any(not a or any(ord(c)<32 for c in a) for a in assets):raise ValueError('Header requires explicit asset allowlist')
    groups={};seen_assets=set()
    for profile in result['profiles']:
        asset=profile['native_asset_name']
        if asset not in assets:continue
        seen_assets.add(asset)
        clean={k:v for k,v in profile.items() if k!='binding_digest'}
        if digest(clean)!=profile['binding_digest']:raise ValueError('Binding digest mismatch')
        if profile.get('runtime_accepted') is not False or profile.get('active_native_mesh_binding') is not False:raise ValueError('Input promotes native admission')
        if profile['state_count']!=1:raise ValueError('Runtime header requires sole configured weapon state')
        if profile['evaluation_status']!='static_authored_pose' or not profile['all_required_controls_constant'] or any(profile[k]!='static_authored_pose' for k in ('right_hand_status','left_hand_status')):
            raise ValueError('Header requires static authored hands')
        matrix(profile['right_hand_in_weapon']);matrix(profile['left_hand_in_weapon']);rifle_support_reference(profile)
        path_key(profile['configured_mesh_path']);checked_hash(profile['animation_sha256']);checked_hash(profile['skeleton_sha256'])
        if not re.fullmatch(r'fnv1a64:[0-9a-f]{16}',profile['rig_fingerprint']):raise ValueError('Header fingerprint')
        key=(asset,profile['configured_mesh_path'],profile['rig_fingerprint'])
        groups.setdefault(key,[]).append(profile)
    if seen_assets!=set(assets):raise ValueError('Requested asset lacks a bound static profile: '+','.join(sorted(set(assets)-seen_assets)))
    result_rows=[]
    for key,rows in sorted(groups.items()):
        signature=lambda p:(p['animation_sha256'],p['skeleton_sha256'],p['right_hand_in_weapon'],p['left_hand_in_weapon'],p.get('weapon_class'),rifle_support_reference(p))
        if any(signature(row)!=signature(rows[0]) for row in rows[1:]):raise ValueError('Conflicting authored profiles for exact runtime key')
        row=dict(rows[0]);row['binding_digest']=digest(sorted(r['binding_digest'] for r in rows)) if len(rows)>1 else row['binding_digest']
        result_rows.append(row)
    return result_rows

def axis_descriptors(rows,source):
    """Explicit reviewed metadata; never infer a frame from weapon class/name."""
    if source is None:return {}
    if not isinstance(source,dict) or source.get('schema')!='fvr.bc2.authored_model_axes' or source.get('schema_version')!=1:
        raise ValueError('Model-axis schema')
    entries=source.get('profiles')
    if not isinstance(entries,list) or len(entries)>128:raise ValueError('Model-axis count bound')
    available={(r['native_asset_name'],r['configured_mesh_path'],r['rig_fingerprint']):r for r in rows};out={}
    keys=('native_asset_name','configured_mesh_path','rig_fingerprint','binding_digest','animation_sha256','skeleton_sha256')
    for entry in entries:
        if not isinstance(entry,dict):raise ValueError('Model-axis entry')
        key=tuple(entry.get(k) for k in keys[:3])
        if any(not isinstance(k,str) for k in key) or key not in available:raise ValueError('Model-axis unknown exact key')
        if key in out:raise ValueError('Duplicate model-axis key')
        row=available[key]
        if any(entry.get(k)!=row[k] for k in keys):raise ValueError('Model-axis binding identity mismatch')
        meshes=[m for m in row['meshes'] if m.get('configured_mesh_path')==row['configured_mesh_path']]
        if len(meshes)!=1 or checked_hash(entry.get('mesh_sha256'))!=meshes[0]['geometry']['mesh_sha256']:
            raise ValueError('Model-axis mesh content mismatch')
        vectors=[]
        for name in ('model_forward','model_up'):
            v=entry.get(name)
            if not isinstance(v,list) or len(v)!=3 or any(type(x) not in (int,float) or not math.isfinite(x) for x in v):
                raise ValueError('Model-axis finite vec3 required')
            if abs(sum(x*x for x in v)-1)>1e-4:raise ValueError('Model-axis unit vector required')
            vectors.append(v)
        if abs(sum(a*b for a,b in zip(*vectors)))>1e-4:raise ValueError('Model-axis perpendicular vectors required')
        review=entry.get('review')
        if not isinstance(review,dict) or review.get('status')!='authored_geometry_reviewed' or review.get('native_verified') is not False:
            raise ValueError('Model-axis review must remain authored experimental')
        for field in ('geometry_image_sha256','firing_fields_sha256','static_flash_sha256'):checked_hash(review.get(field))
        if not isinstance(review.get('description'),str) or not 1<=len(review['description'])<=2048:
            raise ValueError('Model-axis review description')
        # Digest covers vectors, exact binding and all review provenance. The
        # explicit review is an author input, not a parser-generated native proof.
        digest(entry);out[key]=entry
    return out

def cpp_header(result,assets,model_axes=None):
    rows=header_profiles(result,assets);axes=axis_descriptors(rows,model_axes)
    def number(value):
        text=format(value,'.9g')
        if '.' not in text and 'e' not in text:text+='.'
        return text+'f'
    def transform(values):
        return 'math::Matrix4{{{'+','.join('{{'+','.join(number(v) for v in values[i:i+4])+'}}' for i in range(0,16,4))+'}}}'
    lines=['// Private generated authored reference data. No native capability admission.','#pragma once','#include "Bc2AuthoredGrip.h"','#include <array>',
           'namespace fvr::bc2::generated {',f'inline const std::array<AuthoredGripProfile,{len(rows)}> AuthoredGrips = {{{{']
    for row in rows:
        axis=axes.get((row['native_asset_name'],row['configured_mesh_path'],row['rig_fingerprint']))
        extra=''
        if axis:
            vector=lambda v:'math::Vec3{'+','.join(number(x) for x in v)+'}'
            extra=','+vector(axis['model_forward'])+','+vector(axis['model_up'])+','+json.dumps(digest(axis))
        texts=[row[k] for k in ('native_asset_name','configured_mesh_path','binding_digest','animation_sha256','skeleton_sha256')]
        lines.append('    AuthoredGripProfile{'+','.join(json.dumps(t,ensure_ascii=True) for t in texts)+',0x'+row['rig_fingerprint'].split(':')[1]+'ULL,'+
                     transform(row['right_hand_in_weapon'])+','+transform(row['left_hand_in_weapon'])+','+str(rifle_support_reference(row)).lower()+extra+'},')
    lines+=['}};','}'];return '\n'.join(lines)+'\n'

def read_json(path):
    data=path.read_bytes()
    if len(data)>64*1024*1024:raise ValueError('Input JSON bound')
    return json.loads(data),sha(data)
def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game',required=True,type=Path);p.add_argument('--archive',required=True,type=Path)
    p.add_argument('--configurations',required=True,type=Path);p.add_argument('--mesh-bindings',required=True,type=Path)
    p.add_argument('--hand-poses',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    p.add_argument('--header',type=Path,help='Optional private generated C++ registry; requires --asset')
    p.add_argument('--model-axes',type=Path,help='Optional exact-key reviewed model-axis descriptor; requires --header')
    p.add_argument('--asset',action='append',default=[],help='Exact asset permitted in generated header; repeat as needed')
    args=p.parse_args(argv);config,ch=read_json(args.configurations);meshes,mh=read_json(args.mesh_bindings);poses,ph=read_json(args.hand_poses)
    if args.model_axes and not args.header:p.error('--model-axes requires --header')
    axes=read_json(args.model_axes)[0] if args.model_axes else None
    result=load(args.game,args.archive,config,meshes,poses)
    result['input_sha256']={'configurations':ch,'mesh_bindings':mh,'hand_poses':ph}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    if args.header:
        header=cpp_header(result,set(args.asset),axes);args.header.parent.mkdir(parents=True,exist_ok=True);args.header.write_text(header,encoding='utf-8')
    print(json.dumps(result['summary']));return 0
if __name__=='__main__':raise SystemExit(main())
