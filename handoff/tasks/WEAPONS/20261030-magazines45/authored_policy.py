"""Explicit content-keyed mod magazine policies, distinct from strict inference.

An author selects an exact measured part, complete own-clip finger pose and feed
face. No failed generic measurement is changed into a pass. The fixed carry and
rail are authored VR policy; native and headset acceptance remain separate.
"""
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools'))
import bc2_firearm_magazine_batch as batch
import bc2_firearm_magazine_enrollment as enrollment
import bc2_authored_magazine_geometry as g
import bc2_magazine_registry_header as registry


def require(value,message):batch.require(value,message)


def choose(packet,review):
    require(review.get('schema')=='fvr.bc2.mod_authored_magazine_review.v1','Explicit mod policy review required')
    require(review.get('review_digest')==batch.descriptors.digest({k:v for k,v in review.items() if k!='review_digest'}),'Policy review digest changed')
    require(review['native_verified'] is False and review['headset_tested'] is False and review['measured_stable_contact_claim'] is False,'Authored policy cannot promote measurements')
    require(packet['preview']['sha256']==review['preview_sha256'],'Different measured geometry preview')
    candidate=[c for c in packet['candidates'] if c['bone']==review['magazine_bone']]
    require(len(candidate)==1,'Exact reviewed magazine root absent')
    c=candidate[0]
    require(c['parent']=='jntWpn_1' and c['mixed_triangles']==0 and c['triangles']>=24,'Magazine policy requires independent rigid geometry directly owned by weapon')
    require(len(c['reference_members'])<=8,'Weighted assembly member bound')
    require(type(review['pose_index']) is int and 0<=review['pose_index']<len(c['poses']),'Exact source pose index')
    pose=c['poses'][review['pose_index']]
    require(pose['time_seconds']==review['frame_seconds'] and batch.descriptors.digest(pose)==review['pose_digest'],'Selected complete authored frame changed')
    require(set(pose['wrist_from_fingers'])==set(g.FINGERS),'Complete own-asset finger hierarchy required')
    for transform in [c['reference_attached_item'],pose['item_from_hand'],*pose['wrist_from_fingers'].values()]:g.inverse_rigid(g.matrix(transform))
    axis=review['entry_axis'];sign=review['inward_sign']
    require(type(axis) is int and axis in (0,1,2) and type(sign) is int and sign in (-1,1),'Explicit feed-face policy')
    require(.015<=c['extent_m'][axis]<=.4 and min(c['extent_m'])>=.005,'Finite measured policy envelope')
    return c,pose


def geometry_for(packet,review):
    c,pose=choose(packet,review)
    axis,sign=review['entry_axis'],review['inward_sign']
    if review['carry_policy']=='measured_lower_third_design':
        proposals=[p for p in c['design_proposals'] if p['pose_index']==review['pose_index'] and p['axis']==axis and 'geometry'in p]
        require(len(proposals)==1,'Selected geometry-based carry/rail proposal absent')
        result=copy.deepcopy(proposals[0]['geometry'])
        require(result['design']['inward_direction'][axis]==sign,'Selected feed face differs from measured design')
    elif review['carry_policy']=='exact_same_frame_short_box':
        # The source magazine is shorter than the generic 60mm long-axis
        # envelope. This separate authored policy selects its measured upper
        # bounding face; it never changes that generic envelope or claims a
        # sampled native insertion trajectory.
        lo,hi=c['local_bounds'];tip=[(a+b)/2 for a,b in zip(lo,hi)];tip[axis]=hi[axis] if sign>0 else lo[axis]
        inward=[0.,0.,0.];inward[axis]=float(sign)
        short=min((n for n in range(3) if n!=axis),key=lambda n:c['extent_m'][n]);across=[0.,0.,0.];across[short]=1.
        insertion=g.frame(across,g.cross(inward,across),inward,tip)
        entry=g.multiply(insertion,c['reference_attached_item']);entry[12:15]=g.sub(entry[12:15],g.scale(entry[8:11],g.UX['travel_m']))
        result=dict(attached_item=c['reference_attached_item'],item_from_hand=pose['item_from_hand'],
                    wrist_from_fingers=pose['wrist_from_fingers'],item_from_insertion=insertion,weapon_from_entry=entry,
                    measured=dict(bone_local_extent_m=c['extent_m'],bounds_m=c['local_bounds'],part_triangle_count=c['triangles']),
                    design=dict(status='experimental_geometry_based_estimate',carry='complete own-clip wrist and fingers at the reviewed exact frame',
                                rail='explicit mod rail through the center of the measured receiver-facing bounding face',
                                long_axis=axis,short_axis=short,inward_direction=inward,authored_reload_grasp=False,
                                native_insertion_trajectory=False,ux_defaults=g.UX))
    else:raise ValueError('Unknown reviewed carry policy')
    result['measured']['closed_part_frame']='exact evaluated authored reference at t=0; mod attachment policy, not native closed-pose proof'
    result['design']['mod_authored_policy']=review
    result['design']['authored_reload_grasp']=False
    return result


def materialize(packet,review,binding):
    c,_=choose(packet,review);source=packet['source']
    selected=[m for m in binding['meshes'] if m['configured_mesh_path']==binding['configured_mesh_path']]
    require(len(selected)==1,'Policy selected base mesh ambiguous')
    m=selected[0]['geometry'];lod=[l for l in m['lods'] if l['lod']==0]
    require(len(lod)==1 and m['mesh_sha256']==source['mesh_sha256'] and lod[0]['data_sha256']==source['lod_sha256'],'Policy mesh content differs')
    require(binding['animation_sha256']==packet['static_clip_sha256'] and binding['skeleton_sha256']==packet['skeleton_sha256'] and binding['rig_fingerprint']==packet['rig_fingerprint'],'Policy reference coordinates differ')
    require(binding['binding_digest']==batch.descriptors.digest({k:v for k,v in binding.items() if k!='binding_digest'}),'Policy target binding digest differs')
    reload={k:source[k] for k in ('resource','sha256','archive','archive_index_sha256','asset_document','asset_document_sha256','asset_guid','skeleton_reference')}
    p=dict(native_asset_name=binding['native_asset_name'],configured_mesh_path=binding['configured_mesh_path'],weapon=binding['weapon'],
           grip_binding_digest=binding['binding_digest'],mesh_sha256=source['mesh_sha256'],lod_sha256=source['lod_sha256'],
           skeleton_sha256=packet['skeleton_sha256'],rig_fingerprint=packet['rig_fingerprint'],static_clip_sha256=packet['static_clip_sha256'],
           reload_clip=reload,role_evidence=dict(bone=c['bone'],parent=c['parent'],child_count=len(c['children']),
                triangles=c['triangles'],extent_m=c['extent_m'],role='explicit_mod_authored_detachable_magazine',native_role_verified=False,
                mod_policy_review_digest=review['review_digest']),
           bones=dict(weapon='jntWpn_1',magazine=c['bone'],wrist='LeftHand',fingers=g.FINGERS),
           geometry=geometry_for(packet,review),mod_authored_members=c['reference_members'],
           mod_policy_source=review['source_packet'],runtime_admitted=False,status='explicit_mod_authored_geometry',
           strict_inference_passed=False,source_evidence_level='exact authored content plus explicit mod policy')
    p['profile_digest']=batch.descriptors.digest(p);validate(p)
    return p


def validate(profile):
    batch.validate_geometry(profile)
    if profile.get('status')!='explicit_mod_authored_geometry':return
    require(profile.get('strict_inference_passed') is False,'Mod policy must retain strict inference failure')
    packet=batch.read_pin(profile['mod_policy_source']);review=profile['geometry']['design']['mod_authored_policy'];c,_=choose(packet,review)
    require(profile['geometry']==geometry_for(packet,review),'Authored policy emitted geometry differs from its measured source and review')
    require(profile['mod_authored_members']==c['reference_members'],'Authored visible assembly differs')
    require(profile['bones']['magazine']==c['bone'] and profile['role_evidence']['mod_policy_review_digest']==review['review_digest'],'Authored root/review mismatch')
    parents={c['bone']}
    for member in profile['mod_authored_members']:
        require(member['parent'] in parents and member['bone'] not in parents,'Authored assembly parent order or duplicate')
        g.inverse_rigid(g.matrix(member['item_from_bone']));parents.add(member['bone'])


def header(profiles):
    for p in profiles:validate(p)
    result=g.cpp_header(dict(schema='fvr.bc2.authored_magazine_geometry',schema_version=1,profiles=profiles),{p['native_asset_name'] for p in profiles})
    def number(x):
        s=format(x,'.9g');return s+('f' if '.'in s or 'e'in s else '.f')
    def matrix(values):return 'math::Matrix4{{{'+','.join('{{'+','.join(number(v) for v in values[n:n+4])+'}}' for n in range(0,16,4))+'}}}'
    for index,p in enumerate(sorted(profiles,key=lambda p:(p['native_asset_name'],p['weapon']['resource'][:-4]))):
        members=p.get('mod_authored_members',[])
        if not members:continue
        needle=f'auto& g=out[{index}];g.asset='+json.dumps(p['native_asset_name'])+';g.mesh='+json.dumps(p['configured_mesh_path'])+';'
        lines=[f'g.assemblyCount={len(members)};']
        for i,m in enumerate(members):lines.append(f'g.assembly[{i}]={{'+json.dumps(m['bone'])+','+json.dumps(m['parent'])+','+matrix(m['item_from_bone'])+'};')
        require(result.count(needle)==1,'Exact geometry output insertion point')
        result=result.replace(needle,needle+'\n  '+'\n  '.join(lines))
    return result


def load_packets(paths):
    groups={};bindings={};meshes={};inventories=[]
    for path in paths:
        document=json.loads((path/'packet.json').read_text(encoding='utf-8'))
        for group in document['groups']:groups[group['key']]=group
        for b in json.loads((path/'bindings.json').read_text(encoding='utf-8'))['profiles']:bindings[b['binding_digest']]=b
        # Newest exact reference closure takes precedence over earlier gaps.
        doc=json.loads((path/'meshes.json').read_text(encoding='utf-8'))
        if (path/'new-meshes.json').exists():doc['weapons']+=json.loads((path/'new-meshes.json').read_text())['weapons']
        for m in doc['weapons']:
            key=(m['resource'],m['instance_guid'].lower())
            score=lambda w:sum(s.get('geometry_status')=='derived' for state in w['states'] for part in state['meshes'] for s in part.get('geometry_sources',[]))
            if key not in meshes or score(m)>score(meshes[key]):meshes[key]=m
        if (path/'targeted-common-inventory.json').exists():inventories.append(path/'targeted-common-inventory.json')
    return groups,bindings,dict(schema='fvr.bc2.authored_weapon_mesh_bindings',schema_version=1,weapons=list(meshes.values())),inventories


def prepare(previous,packets,reviews_path,output):
    plan=json.loads((previous/'batch.json').read_text());jobs=json.loads((previous/'descriptor-jobs.json').read_text());measured=json.loads((previous/'geometry.json').read_text())
    groups,bindings,meshes,inventories=load_packets(packets)
    reviews=json.loads(reviews_path.read_text())['reviews'];by_group={r['content_group']:r for r in reviews}
    require(len(by_group)==len(reviews),'Duplicate policy content group review')
    current={batch.weapon_key(p['weapon']):p for p in measured['profiles']};strict={batch.weapon_key(p['weapon']) for p in measured['profiles']}
    docs,oldbindings,oldmeshes=enrollment.source_inputs(plan);bindings={**oldbindings,**bindings}
    for group in groups.values():
        packet=batch.read_pin(group['source'])
        if group.get('strict_linear'):
            result=batch.read_pin(group['strict_linear'])
            if result['profiles']:
                require(len(result['profiles'])==1,'Strict content representative count')
                source=result['profiles'][0]
                for target in group['configurations']:
                    b=bindings[target['binding_digest']]
                    action=dict(source=packet['source'],skeleton_sha256=packet['skeleton_sha256'],rig_fingerprint=packet['rig_fingerprint'],weapon_configuration=b['weapon'],binding_digest=b['binding_digest'])
                    p=batch.rebind(source,b,action);key=batch.weapon_key(p['weapon']);current[key]=p;strict.add(key)
                continue
        if group['key'] not in by_group:continue
        review=by_group[group['key']];require(review['source_packet']==group['source'],'Review source packet pin differs')
        for target in group['configurations']:
            p=materialize(packet,review,bindings[target['binding_digest']]);current[batch.weapon_key(p['weapon'])]=p
    measured['profiles']=list(current.values())
    for row in plan['rows']:
        key=batch.weapon_key(row['identity']['weapon'])
        if key in current:
            p=current[key];row.update(strict_geometry=key in strict,mod_authored_geometry=key not in strict,geometry_available=True,
                                     geometry_digest=p['profile_digest'],binding_digest=p['grip_binding_digest'])
    plan['summary'].update(strict_geometry=len(strict),mod_authored_geometry=len(current)-len(strict),geometry_available=len(current))
    # Base writer preserves every existing native registration and baseline
    # geometry declaration. Exact authored policies are reviewed separately.
    batch.emit(ROOT,output,plan,jobs,measured)
    evidence=dict(schema='fvr.bc2.magazines45.geometry_native_review.v1',jobs=batch.pin(output/'descriptor-jobs.json'),
                  policy_reviews=batch.pin(reviews_path),native_execution=False,headset_acceptance=False,
                  rows=[dict(key=r['key'],descriptor_digest=r['descriptor_digest'],geometry_digest=r.get('geometry_digest'),
                             strict_geometry=r['strict_geometry'],mod_authored_geometry=r.get('mod_authored_geometry',False)) for r in plan['rows']])
    batch.write(output/'native-evidence.json',evidence);evidence_hash=batch.pin(output/'native-evidence.json')['sha256']
    newrows=[r for r in plan['rows'] if not r['preserved_registration']];newkeys={r['key'] for r in newrows};native_reviews=[]
    for row in newrows:
        if not row.get('geometry_available'):continue
        review=dict(key=row['key'],descriptor_digest=row['descriptor_digest'],family_proof=row['native_family'],evidence_sha256=evidence_hash)
        if row['instruction_proof_digest']:review['instruction_proof_digest']=row['instruction_proof_digest']
        native_reviews.append(review)
    native_header,summary=registry.build(dict(jobs,descriptors=[j for j in jobs['descriptors'] if j['key'] in newkeys]),native_reviews)
    additions=[p for p in measured['profiles'] if (p['native_asset_name'],p['weapon']['resource'][:-4]) not in batch.preserved_identities(ROOT)]
    batch.write(output/'Registry.additions.generated.h',native_header);batch.write(output/'exact-native-reviews.json',native_reviews)
    batch.write(output/'Geometry.additions.generated.h',header(additions));batch.write(output/'registry-receipt.json',summary)
    batch.write(output/'policy-bindings.json',dict(schema='fvr.bc2.authored_reload_reference_bindings',schema_version=1,profiles=list(bindings.values())))
    batch.write(output/'policy-meshes.json',meshes)
    batch.write(output/'policy-preparation.json',dict(schema='fvr.bc2.magazines45.policy_preparation.v1',previous=batch.pin(previous/'extraction-receipt.json'),
                reviews=batch.pin(reviews_path),packets=[batch.pin(p/'packet.json') for p in packets],inventory=[batch.pin(p) for p in inventories],
                summary=plan['summary'],native_execution=False,headset_acceptance=False))
    print(json.dumps(plan['summary']))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--previous',type=Path,required=True);p.add_argument('--packet',type=Path,action='append',required=True)
    p.add_argument('--reviews',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();prepare(a.previous,a.packet,a.reviews,a.output)


if __name__=='__main__':main()
