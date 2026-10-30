"""Root-owned incremental installed cache join for reviewed magazine policies.

Preserves every prior catalog row/record. Explicit authored fixed poses are
replayed with their exact source pins and are never called constant controls.
"""
import argparse
import copy
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools'))
import authored_policy as policy
import bc2_firearm_magazine_batch as batch
import bc2_firearm_magazine_enrollment as enrollment
import bc2_body_ammo_assets as ammo
import bc2_body_equipment_assets as equipment
import bc2_configured_body_assets as body
import bc2_magazine_assembly_cache as assembly
import bc2_visibility_descriptors as visibility
import bc2_authored_magazine_geometry as g
from bc2_magazine_contact_batch import ArchivePool
from bc2_weapon_animation_pipeline import Skeleton,Clip,canonical,identity
from bc2_mesh_geometry import metadata,geometry,position_bytes
from inspect_bc2_mesh_asset import bone_hash,sha


def key(row):return row['asset'],row['mesh'],row['part'],row['rig_fingerprint']


def pack_row(row,records):
    return ammo.text(row['asset'])+ammo.text(row['mesh'])+ammo.text(row['part'])+struct.pack('<QI',row['rig_fingerprint'],len(row['sections']))+b''.join(records)


def section_record(name,vertices,indices,index_size=2):
    return ammo.text(name)+struct.pack('<5I',len(vertices),len(indices),index_size,0,0)+vertices+indices


def baked_section(name,vertices,indices):
    skin=b''.join(vertices[i] for i in indices);positions=b''.join(vertices[i][:12] for i in indices)
    return dict(section=name,count=len(indices),stride=20,palette_size=1,part_palette_index=0,position='Float3',
                vertex_skin_hash=f'{ammo.fnv(skin):016x}',position_hash=f'{ammo.fnv(positions):016x}',part_triangles=len(indices)//3)


def policy_magazine_cache(profile,source,blobs):
    """Bake the explicitly reviewed visible parts at the exact reference time."""
    policy.validate(profile)
    sk=Skeleton(blobs['skeleton']);clip=Clip(blobs['static']);root=profile['bones']['magazine']
    names=[root,*[m['bone'] for m in profile['mod_authored_members']]]
    closed=clip.evaluate(sk,0.,names);inverse_root=g.inverse_rigid(closed['weapon_relative'][root])
    batch.require(max(abs(a-b) for a,b in zip(closed['weapon_relative'][root],profile['geometry']['attached_item']))<1e-7,'Authored attachment reference changed')
    transforms={n:g.multiply(closed['weapon_relative'][n],inverse_root) for n in names}
    for member in profile['mod_authored_members']:
        batch.require(max(abs(a-b) for a,b in zip(transforms[member['bone']],member['item_from_bone']))<1e-7,'Authored visible member reference changed')
    known={bone_hash(n):n for n in sk.names};inverse={n:canonical(b['InverseWorldTransform']) for n,b in zip(sk.names,sk.bones)}
    lod=metadata(blobs['mesh'])[0];data=blobs['lod'];sections=geometry(data,lod,known)
    vb=sum(s['vertices']*s['stride'] for s in sections);ib=sum(s['triangles']*6 for s in sections);start=len(data)-vb-ib
    rows=[];records=[]
    for s in sections:
        if s['name'].endswith('_ZOnly') or not set(s['bone_names'])&set(names):continue
        size=position_bytes(s);half=size==8;rawids=struct.unpack_from('<'+'H'*(s['triangles']*3),data,start+vb+s['first_index']*2)
        vertices=[]
        for offset in range(0,len(rawids),3):
            tri=[];owners=[]
            for vertex in rawids[offset:offset+3]:
                at=start+s['vertex_offset']+vertex*s['stride'];ids=data[at+size:at+size+4];weights=data[at+size+4:at+size+8]
                batch.require(len(weights)==4 and sum(w>0 for w in weights)==1 and 255 in weights,'Policy magazine requires rigid triangle weights')
                name=known[lod['palette'][s['palette'][ids[weights.index(255)]]]];owners.append(name)
                if name in transforms:
                    p=struct.unpack_from('<3e' if half else '<3f',data,at);local=g.point((p[0],p[1],-p[2]),g.multiply(inverse[name],transforms[name]))
                    batch.require(max(map(abs,local))<=1.5,'Authored magazine point bound')
                    tri.append(struct.pack('<3f8B',local[0],local[1],-local[2],0,0,0,0,255,0,0,0))
            batch.require(len(set(owners))==1,'Policy magazine rejects cross-part triangles')
            if owners[0] in transforms:vertices+=tri
        if not vertices:continue
        indices=list(range(len(vertices)));index_size=2 if len(vertices)<=65536 else 4
        batch.require(len(vertices)<=32768*3,'Policy magazine section bound')
        rows.append(baked_section(s['name'],vertices,indices))
        records.append(section_record(s['name'],b''.join(vertices),struct.pack('<'+('H' if index_size==2 else 'I')*len(indices),*indices),index_size))
    batch.require(0<len(rows)<=8 and sum(r['part_triangles'] for r in rows)<=32768,'Policy magazine row bound')
    row=dict(asset=profile['native_asset_name'],mesh=profile['configured_mesh_path'],part=root,rig_fingerprint=int(sk.fingerprint.split(':')[1],16),
             inverse_bind=identity(),sections=rows,mesh_sha256=sha(blobs['mesh']),lod_sha256=sha(blobs['lod']),
             mod_authored_magazine=True,render_only=True,native_admission=False,geometry_profile_digest=profile['profile_digest'],
             mod_policy_review_digest=profile['geometry']['design']['mod_authored_policy']['review_digest'],mod_policy_source=source,
             closed_pose='sampled_authored_mod_reference',reference_time_seconds=0.)
    return row,pack_row(row,records)


def sampled_body(game,pool,spec,binding,profile,skeleton):
    """Independent authored display policy; does not alter strict body.composite."""
    policy.validate(profile);ss=binding['animation_source'];archive=pool(game/ss['archive']);name=binding['animation_chain']['animation_resource']
    raw=archive.read_selected([name])[name];batch.require(sha(raw)==binding['animation_sha256']==profile['static_clip_sha256'],'Sampled body source changed')
    clip=Clip(raw);known={bone_hash(n):n for n in skeleton.names};inverse={n:canonical(b['InverseWorldTransform']) for n,b in zip(skeleton.names,skeleton.bones)}
    decoded=[];sources=[];required={'RightHand','jntWpn_Flash'}
    for s in spec['meshes']:
        a=pool(game/s['archive']);mesh=s['mesh'];names=[mesh+'.res',mesh+'_lod0_data.res'];blobs=a.read_selected(names)
        if s.get('expected_metadata_sha256'):batch.require(sha(blobs[names[0]])==s['expected_metadata_sha256'],'Sampled body mesh differs')
        if s.get('expected_index_sha256'):batch.require(a.index_sha256==s['expected_index_sha256'],'Sampled body archive differs')
        lod=metadata(blobs[names[0]])[0];data=blobs[names[1]];sections=geometry(data,lod,known)
        vb=sum(r['vertices']*r['stride'] for r in sections);ib=sum(r['triangles']*6 for r in sections);start=len(data)-vb-ib
        for section in sections:
            if section['name'].endswith('_ZOnly'):continue
            required.update(section['bone_names']);decoded.append((data,section,lod,start,vb))
        sources.append(dict(archive=s['archive'],archive_index_sha256=a.index_sha256,mesh=mesh,mesh_sha256=sha(blobs[names[0]]),lod_sha256=sha(data)))
    closed=clip.evaluate(skeleton,0.,sorted(required))
    batch.require(all(v in ('static_authored_pose','decoded_spline_candidate') for v in closed['bone_evaluation_status'].values()),'Unsupported authored display frame')
    vertices=[];indices=[]
    for data,section,lod,start,vb in decoded:
        baked=equipment.bake_section(data,section,lod,known,inverse,closed['weapon_relative'],start)
        ids=struct.unpack_from('<'+'H'*(section['triangles']*3),data,start+vb+section['first_index']*2)
        batch.require(ids and max(ids)<len(baked),'Authored body indices invalid')
        offset=len(vertices);indices.extend(i+offset for i in ids);vertices.extend(baked)
    batch.require(0<len(indices)//3<=32768 and len(vertices)<=131072,'Authored body bounds')
    cfg=spec['authored_configuration'];part='body_holstered_'+batch.hashlib.sha256(cfg['resource'].encode()).hexdigest()[:16]
    section=baked_section('closed_configuration',vertices,indices);index_size=2 if len(vertices)<=65536 else 4
    row=dict(asset=spec['asset'],mesh=profile['configured_mesh_path'],part=part,rig_fingerprint=int(skeleton.fingerprint.split(':')[1],16),
             inverse_bind=identity(),sections=[section],display_only=True,configuration_path=cfg['resource'][:-4],configured_meshes=sorted(s['mesh'] for s in sources),
             authored_configuration=cfg,closed_clip=name,closed_clip_sha256=sha(raw),closed_pose='sampled_authored_mod_reference',sources=sources,
             display_anchor_evaluation={n:closed['bone_evaluation_status'][n] for n in ('RightHand','jntWpn_Flash')},display_anchor_time_seconds=0,
             display_anchor_not_native_grip_calibration=True,mod_authored_display=True,
             mod_policy_review_digest=profile['geometry']['design']['mod_authored_policy']['review_digest'],
             mod_policy_profile=profile,reference_bone_evaluation=closed['bone_evaluation_status'],
             holster_from_weapon=equipment.holster_transform(closed['weapon_relative']['RightHand'][12:15],closed['weapon_relative']['jntWpn_Flash'][12:15]),
             grip=closed['weapon_relative']['RightHand'][12:15],muzzle=closed['weapon_relative']['jntWpn_Flash'][12:15])
    return row,pack_row(row,[section_record('closed_configuration',b''.join(vertices),struct.pack('<'+('H' if index_size==2 else 'I')*len(indices),*indices),index_size)])


def run(args):
    previous=args.previous;prepared=args.prepared;output=args.output;output.mkdir(parents=True,exist_ok=True)
    batch.require(not (output/'receipt.json').exists(),'Preserve earlier incremental result')
    plan=json.loads((prepared/'batch.json').read_text());measured=json.loads((prepared/'geometry.json').read_text());proof=json.loads((prepared/'policy-preparation.json').read_text())
    bindings={b['binding_digest']:b for b in json.loads((prepared/'policy-bindings.json').read_text())['profiles']};meshes=json.loads((prepared/'policy-meshes.json').read_text())
    catalog=json.loads((previous/'body-ammo-assets.json').read_text());rows=list(catalog['profiles']);known={key(r):r for r in rows}
    baseline=(previous/'body-ammo.fvrprop').read_bytes();baseline=body.compact_cache_sections(baseline,rows)
    joined=json.loads((previous/'enrollment.json').read_text())['profiles'];joined_keys={r['key'] for r in joined}
    oldvis=json.loads((previous/'visibility.json').read_text());visrows={r['authored_configuration']['resource']:r for r in oldvis['rows']}
    docs={name:batch.read_pin(pin) for name,pin in plan['sources'].items()}
    configs={batch.weapon_key(dict(resource=c['resource'],sha256=c['resource_sha256'],instance_guid=c['instance_guid'])):c for c in docs['catalog']['configurations']}
    profiles={batch.weapon_key(p['weapon']):p for p in measured['profiles']};h=enrollment.helper(ROOT)
    inventory=batch.read_pin(proof['inventory'][-1]) if proof['inventory'] else json.loads((Path(plan['sources']['catalog']['path']).parent.parent/'mesh312-research/inventory.json').read_text())
    trace=oldvis['native_rig_proof'];h.read_pin(trace)
    first=next(iter(configs.values()));pool=ArchivePool(args.game,first['origin_archive'],first['origin_index_sha256'])
    receipt=dict(schema='fvr.bc2.magazines45.incremental_enrollment.v1',previous=batch.pin(previous/'extraction-receipt.json'),prepared=batch.pin(prepared/'policy-preparation.json'),
                 preserved_catalog_rows=len(rows),preserved_joined_configurations=len(joined),native_execution=False,headset_acceptance=False,installed=False,residuals=[])
    additions=[]
    with h.shared_readers(pool):
        for row in plan['rows']:
            if row['key'] in joined_keys:continue
            wkey=batch.weapon_key(row['identity']['weapon'])
            if wkey not in profiles:
                receipt['residuals'].append(dict(key=row['key'],stage='geometry',reason='No reviewed exact geometry'));continue
            p=profiles[wkey];b=bindings[p['grip_binding_digest']];c=configs.get(wkey)
            try:
                batch.require(c is not None,'Exact configured source absent')
                print(json.dumps(dict(stage='incremental_cache',asset=row['asset'],configuration=row['configuration'])),flush=True)
                configured=dict(archive=c['origin_archive'],index_sha256=c['origin_index_sha256'],resolved_weapons=[dict(c,weapon_states=c['authored_states'])])
                specs,excluded=visibility.specs_from_configuration(args.game,configured,classes=(c['weapon_class'],),inventory=inventory)
                batch.require(len(specs)==1 and not excluded,'Exact configured visibility source missing: '+str(excluded))
                spec=specs[0];spec['authored_configuration'].update(instance_guid=b['weapon']['instance_guid'],type=b['weapon']['type'])
                if wkey[0] not in visrows:
                    v=visibility.derive(args.game,[spec],Path(trace['path']));batch.require(len(v['rows'])==1 and not v['excluded'],'Weighted visibility rejected: '+str(v['excluded']))
                    visrows[wkey[0]]=v['rows'][0]
                magkey=(row['asset'],p['configured_mesh_path'],p['bones']['magazine'],int(p['rig_fingerprint'].split(':')[1],16))
                bodies=[r for r in rows if r.get('configuration_path')==row['configuration'] and r['asset']==row['asset']]
                source=None;blobs={}
                def load(name):
                    nonlocal source
                    if source is None:source=h.cache_sources(p,b,meshes)
                    if name not in blobs:blobs[name]=h.load_resource(args.game,pool,source[name])
                    return blobs[name]
                if magkey not in known:
                    if p.get('status')=='explicit_mod_authored_geometry':
                        for name in ('mesh','lod','skeleton','static','reload'):load(name)
                        mag,record=policy_magazine_cache(p,source,blobs)
                    elif 'assembly'in p:
                        for name in ('mesh','lod','skeleton','static','reload'):load(name)
                        mag,packed=assembly.derive_cache(blobs['mesh'],blobs['lod'],Skeleton(blobs['skeleton']),Clip(blobs['static']),Clip(blobs['reload']),p)
                        mag['assembly_source']=source;record=packed[12:]
                    else:
                        load('mesh');previous_archive=ammo.archive
                        try:
                            ammo.archive=lambda game,path:pool(game/path)
                            mags,packed=ammo.derive(args.game,[(row['asset'],source['mesh']['archive'],p['configured_mesh_path'],p['bones']['magazine'])]);mag=mags[0];record=packed[12:]
                        finally:ammo.archive=previous_archive
                    known[key(mag)]=mag;rows.append(mag);additions.append((mag,record))
                mag=known[magkey]
                batch.require(mag['mesh_sha256']==p['mesh_sha256'] and mag['lod_sha256']==p['lod_sha256'],'Existing magazine source differs')
                if not bodies:
                    sk=Skeleton(load('skeleton'));names={bone_hash(n):n for n in sk.names};inverse={n:canonical(v['InverseWorldTransform']) for n,v in zip(sk.names,sk.bones)}
                    try:display,record=body.composite(args.game,pool,spec,b,p,sk,names,inverse)
                    except ValueError as error:
                        if not str(error).startswith('Composite required closed controls not static:') or p.get('status')!='explicit_mod_authored_geometry':raise
                        display,record=sampled_body(args.game,pool,spec,b,p,sk)
                    display.update(configuration_archive=c['origin_archive'],configuration_archive_index_sha256=c['origin_index_sha256'],
                                   closed_archive=b['animation_source']['archive'],closed_archive_index_sha256=b['animation_source']['archive_index_sha256'])
                    batch.require(key(display) not in known,'Duplicate display catalog key')
                    known[key(display)]=display;rows.append(display);additions.append((display,record));bodies=[display]
                batch.require(len(bodies)==1,'Configured body ambiguity')
                display=bodies[0]
                batch.require(display['authored_configuration']['sha256']==wkey[1] and display['mesh']==p['configured_mesh_path'],'Existing body identity differs')
                joined.append(dict(key=row['key'],descriptor_digest=row['descriptor_digest'],native_id=row['native_id'],geometry_digest=p['profile_digest'],identity=row['identity'],
                                   configured_meshes=visrows[wkey[0]]['meshes'],magazine_catalog_key=list(magkey),body_catalog_key=list(key(display)),
                                   status='exact_geometry_descriptor_visibility_catalog_join',strict_geometry=row['strict_geometry'],mod_authored_geometry=row.get('mod_authored_geometry',False),native_tested=False,headset_accepted=False))
                joined_keys.add(row['key'])
            except (ValueError,KeyError,TypeError,OSError) as e:receipt['residuals'].append(dict(key=row['key'],asset=row['asset'],stage='cache_join',reason=str(e)))
    packed=ammo.MAGIC+struct.pack('<I',len(additions))+b''.join(record for _,record in additions)
    if additions:packed=body.compact_cache_sections(packed,[r for r,_ in additions])
    payload=ammo.MAGIC+struct.pack('<I',len(rows))+baseline[12:]+packed[12:]
    batch.require(len(rows)<=256 and len(payload)<=64*1024*1024,'Complete authored candidate exceeds reviewed root bound')
    (output/'body-ammo.fvrprop').write_bytes(payload)
    batch.write(output/'body-ammo-assets.json',dict(catalog,profiles=rows));batch.write(output/'BodyAmmoAssetData.generated.h',ammo.catalog_header(rows))
    batch.write(output/'visibility.json',dict(oldvis,rows=list(visrows.values())));batch.write(output/'Visibility.generated.h',visibility.header(dict(oldvis,rows=list(visrows.values()))))
    batch.write(output/'enrollment.json',dict(schema='fvr.bc2.magazines45.enrollment.v1',profiles=joined,source_complete=len(joined_keys)==len(plan['rows']),native_execution=False,headset_acceptance=False))
    for row in plan['rows']:row['source_complete']=row['key'] in joined_keys
    batch.write(output/'batch.json',plan)
    receipt.update(complete=len(joined_keys)==len(plan['rows']),configurations=len(plan['rows']),joined_configurations=len(joined_keys),
                   strict_geometry=plan['summary']['strict_geometry'],mod_authored_geometry=plan['summary']['mod_authored_geometry'],
                   catalog_rows=len(rows),cache_bytes=len(payload),cache_pin=batch.pin(output/'body-ammo.fvrprop'),appended_rows=len(additions),
                   selected_resource_bytes=pool.bytes,archives=len(pool.archives),runtime_consumer_integrated=False)
    batch.write(output/'receipt.json',receipt);print(json.dumps(receipt));return 0 if receipt['complete'] else 2


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',type=Path,required=True);p.add_argument('--previous',type=Path,required=True);p.add_argument('--prepared',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    return run(p.parse_args())


if __name__=='__main__':raise SystemExit(main())
