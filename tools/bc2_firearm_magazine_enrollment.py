"""Root-owned grouped installed extraction for the exact magazine roster.

Run only inside the root's StorageBudget wrapper. Cached strict results are
reused; one representative of each missing source-content group is derived.
Private BC2PROP1 shards obey existing byte/count limits; the complete candidate
has explicit developer bounds, pending matching root consumer integration.
Public outputs contain metadata only. No native operations or installation.
"""
from __future__ import annotations
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import struct
import bc2_firearm_magazine_batch as batch
import bc2_authored_magazine_geometry as geometry
import bc2_body_ammo_assets as ammo
import bc2_configured_body_assets as body
import bc2_magazine_assembly_cache as assembly
import bc2_visibility_descriptors as visibility
from bc2_magazine_contact_batch import ArchivePool
from bc2_weapon_animation_pipeline import Skeleton, Clip, canonical
from inspect_bc2_mesh_asset import bone_hash


def helper(root):
    path=root/'handoff/tasks/CHAT16/20261030-magazinebatch39/prepare_batch.py'
    spec=importlib.util.spec_from_file_location('magazine39_helpers',path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


def cache_key(row):
    return row['asset'],row['mesh'],row['part'],row['rig_fingerprint']


def source_inputs(plan):
    docs={k:batch.read_pin(v) for k,v in plan['sources'].items()}
    bindings={};meshes={}
    for name,doc in docs.items():
        if name.startswith('bindings_') or name in ('nambu_bindings','pistol_bindings'):
            for b in doc['profiles']:bindings[b['binding_digest']]=b
        if name.startswith('meshes_'):
            for m in doc['weapons']:
                key=(m['resource'],m['instance_guid'].lower())
                meshes.setdefault(key,m)
    return docs,bindings,dict(schema='fvr.bc2.authored_weapon_mesh_bindings',schema_version=1,weapons=list(meshes.values()))


def extract(args):
    root=Path(__file__).resolve().parents[1]
    h=helper(root);plan=json.loads(args.batch.read_text(encoding='utf-8'))
    batch.require(plan['schema']==batch.SCHEMA,'Exact batch schema required')
    docs,bindings,meshes=source_inputs(plan)
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    batch.require(not output.is_relative_to(args.game.resolve()),'Output cannot be inside game installation')
    batch.require(not (output/'extraction-receipt.json').exists(),'Preserve prior extraction; use a new output directory')
    jobs=json.loads(args.batch.with_name('descriptor-jobs.json').read_text(encoding='utf-8'))
    measured=json.loads(args.batch.with_name('geometry.json').read_text(encoding='utf-8'))
    current={batch.weapon_key(p['weapon']):p for p in measured['profiles']}
    catalog=docs['baseline_catalog'];baseline=catalog['profiles']
    baseline_path=root/'reports/coordinator-20261030-family-enrollment41/body-ammo.fvrprop'
    baseline_payload=baseline_path.read_bytes()
    batch.require(baseline_payload[:8]==ammo.MAGIC and struct.unpack_from('<I',baseline_payload,8)[0]==len(baseline),'Baseline cache/catalog row count differs')
    # This validates every indexed skin and position hash while compacting.
    baseline_compacted=body.compact_cache_sections(baseline_payload,baseline)
    prior_plan=json.loads((root/'handoff/tasks/CHAT16/20261030-magazinebatch39/review-input.json').read_text(encoding='utf-8'))
    inventory=batch.read_pin(prior_plan['sources']['inventory'])
    trace=prior_plan['sources']['trace'];h.read_pin(trace)
    receipt=dict(schema='fvr.bc2.magazines45.installed_extraction.v1',batch_pin=batch.pin(args.batch),
                 native_execution=False,headset_acceptance=False,installed=False,residuals=[],geometry_groups=[],cache_shards=[])
    first=docs['catalog']['configurations'][0]
    pool=ArchivePool(args.game,first['origin_archive'],first['origin_index_sha256'])
    action_records=docs['actions']['records']
    pending={}
    for row in plan['rows']:
        if row['strict_geometry']:continue
        if row['binding_digest'] is None:
            receipt['residuals'].append(dict(stage='source',key=row['key'],reason='No cached exact reference binding/action; source job retained'))
            continue
        key=row['geometry_content_group'] or row['binding_digest']
        pending.setdefault(key,[]).append(row)
    with h.shared_readers(pool):
        for group,rows in pending.items():
            representative=rows[0];b=bindings[representative['binding_digest']];assets={b['native_asset_name']}
            selected=dict(schema='fvr.bc2.authored_reload_reference_bindings',schema_version=1,profiles=[b])
            print(json.dumps(dict(stage='geometry',asset=b['native_asset_name'],configurations=len(rows))),flush=True)
            fresh=geometry.derive(args.game,selected,meshes,assets,paired_grasp_assets=assets,
                                  rigid_assembly_assets=assets,sampled_path_assets=assets)
            batch.write(output/('group-'+group[:16]+'.json'),fresh)
            receipt['geometry_groups'].append(dict(group=group,configurations=len(rows),profiles=len(fresh['profiles']),gaps=fresh['gaps']))
            if not fresh['profiles']:
                for row in rows:receipt['residuals'].append(dict(stage='geometry',key=row['key'],reasons=[g['reason'] for g in fresh['gaps']]))
                continue
            batch.require(len(fresh['profiles'])==1,'Representative geometry ambiguous')
            profile=fresh['profiles'][0];batch.validate_geometry(profile)
            for row in rows:
                target=bindings[row['binding_digest']]
                try:
                    if row is representative:p=profile
                    else:p=batch.rebind(profile,target,action_records[row['action_record']])
                    current[batch.weapon_key(p['weapon'])]=p
                except (ValueError,TypeError,KeyError) as e:
                    receipt['residuals'].append(dict(stage='content_join',key=row['key'],reason=str(e)))
        measured['profiles']=list(current.values())
        batch.write(output/'geometry.json',measured)
        for row in plan['rows']:
            key=batch.weapon_key(row['identity']['weapon'])
            if key in current:
                row['strict_geometry']=True;row['geometry_digest']=current[key]['profile_digest']
        plan['summary']['strict_geometry']=len(current)
        batch.emit(root,output,plan,jobs,measured)
        # Every configured mesh set is resolved against its exact DBX identity.
        configs={batch.weapon_key(dict(resource=c['resource'],sha256=c['resource_sha256'],instance_guid=c['instance_guid'])):c for c in docs['catalog']['configurations']}
        rows=list(baseline);known={cache_key(r):r for r in rows};records=[];vis_rows=[];specs=[];joined=[]
        vis_template=None
        cached_vis={}
        for row in plan['rows']:
            key=batch.weapon_key(row['identity']['weapon'])
            if key not in current:continue
            p=current[key];b=bindings.get(p['grip_binding_digest'])
            if not b or key not in configs:
                # Campaign-only preserved rows already have exact enrollment.
                if row['preserved_registration']:
                    joined.append(dict(key=row['key'],status='preserved_existing_enrollment',native_id=row['native_id']))
                    continue
                receipt['residuals'].append(dict(stage='cache_source',key=row['key'],reason='Exact configuration/binding missing'));continue
            c=configs[key]
            try:
                configured=dict(archive=c['origin_archive'],index_sha256=c['origin_index_sha256'],
                                resolved_weapons=[dict(c,weapon_states=c['authored_states'])])
                exact_specs,excluded=visibility.specs_from_configuration(args.game,configured,classes=(c['weapon_class'],),inventory=inventory)
                batch.require(len(exact_specs)==1 and not excluded,'Configured visibility spec: '+str(excluded))
                spec=exact_specs[0];spec['authored_configuration'].update(instance_guid=b['weapon']['instance_guid'],type=b['weapon']['type'])
                viskey=batch.descriptors.digest(spec['meshes'])
                if viskey not in cached_vis:
                    v=visibility.derive(args.game,[spec],Path(trace['path']))
                    batch.require(len(v['rows'])==1 and not v['excluded'],'Weighted visibility: '+str(v['excluded']))
                    cached_vis[viskey]=v
                v=cached_vis[viskey];vis_template=v
                vrow=copy.deepcopy(v['rows'][0]);vrow.update(asset=row['asset'],authored_configuration=spec['authored_configuration'])
                vis_rows.append(vrow);specs.append(spec)
                source=h.cache_sources(p,b,meshes)
                # Reuse exact existing catalog keys. New records are extracted
                # once per asset/mesh/part, even for multiple ammo variants.
                magkey=(row['asset'],p['configured_mesh_path'],p['bones']['magazine'],int(p['rig_fingerprint'].split(':')[1],16))
                blobs={}
                def load(name):
                    if name not in blobs:blobs[name]=h.load_resource(args.game,pool,source[name])
                    return blobs[name]
                if magkey not in known:
                    if 'assembly' in p:
                        mag,packed=assembly.derive_cache(load('mesh'),load('lod'),Skeleton(load('skeleton')),Clip(load('static')),Clip(load('reload')),p)
                        mag['assembly_source']=source
                    else:
                        previous=ammo.archive
                        try:
                            ammo.archive=lambda game,path:pool(game/path)
                            magrows,packed=ammo.derive(args.game,[(row['asset'],source['mesh']['archive'],p['configured_mesh_path'],p['bones']['magazine'])])
                            mag=magrows[0]
                        finally:ammo.archive=previous
                    batch.require(mag['mesh_sha256']==p['mesh_sha256'] and mag['lod_sha256']==p['lod_sha256'],'Detached cache geometry differs')
                    known[cache_key(mag)]=mag;rows.append(mag);records.append((mag,packed[12:]))
                body_matches=[r for r in rows if r.get('configuration_path')==row['configuration'] and r['asset']==row['asset']]
                if not body_matches:
                    sk=Skeleton(load('skeleton'));names={bone_hash(n):n for n in sk.names}
                    inverse={n:canonical(bone['InverseWorldTransform']) for n,bone in zip(sk.names,sk.bones)}
                    display,record=body.composite(args.game,pool,spec,b,p,sk,names,inverse)
                    display.update(configuration_archive=c['origin_archive'],configuration_archive_index_sha256=c['origin_index_sha256'],
                                   closed_archive=b['animation_source']['archive'],closed_archive_index_sha256=b['animation_source']['archive_index_sha256'])
                    batch.require(cache_key(display) not in known,'Duplicate configured body key')
                    known[cache_key(display)]=display;rows.append(display);records.append((display,record));body_matches=[display]
                batch.require(len(body_matches)==1,'Ambiguous configured body')
                display=body_matches[0]
                batch.require(display['mesh']==p['configured_mesh_path'] and display['authored_configuration']['sha256']==key[1], 'Configured body join differs')
                joined.append(dict(key=row['key'],descriptor_digest=row['descriptor_digest'],native_id=row['native_id'],
                                   geometry_digest=p['profile_digest'],identity=row['identity'],configured_meshes=vrow['meshes'],
                                   magazine_catalog_key=list(magkey),body_catalog_key=list(cache_key(display)),
                                   status='exact_geometry_descriptor_visibility_catalog_join',native_tested=False,headset_accepted=False))
            except (ValueError,KeyError,TypeError,OSError) as e:
                receipt['residuals'].append(dict(stage='cache_visibility',key=row['key'],reason=str(e)))
        batch.write(output/'body-ammo-assets.json',dict(catalog,profiles=rows))
        batch.write(output/'BodyAmmoAssetData.generated.h',ammo.catalog_header(rows))
        if vis_template:
            report=dict(vis_template,rows=vis_rows,excluded=[])
            batch.write(output/'visibility.json',report);batch.write(output/'Visibility.generated.h',visibility.header(report))
        batch.write(output/'visibility-specs.json',specs)
        batch.write(output/'enrollment.json',dict(schema='fvr.bc2.magazines45.enrollment.v1',profiles=joined,
                    source_complete=len(joined)==len(plan['rows']),native_execution=False,headset_acceptance=False))
        # Shards respect the existing parser bounds; root decides consumer and
        # staged-cache integration after reviewing the exact total requirements.
        shard_rows=[];shard_records=[];shard_bytes=12
        def flush():
            nonlocal shard_rows,shard_records,shard_bytes
            if not shard_rows:return
            name='cache-additions-%02d.fvrprop'%len(receipt['cache_shards'])
            payload=ammo.MAGIC+struct.pack('<I',len(shard_rows))+b''.join(shard_records)
            payload=body.compact_cache_sections(payload,shard_rows)
            (output/name).write_bytes(payload)
            receipt['cache_shards'].append(dict(pin=batch.pin(output/name),rows=len(shard_rows),bytes=len(payload),catalog_keys=[list(cache_key(r)) for r in shard_rows]))
            shard_rows=[];shard_records=[];shard_bytes=12
        for r,record in records:
            if len(shard_rows)>=64 or shard_bytes+len(record)>ammo.MAX_CACHE:flush()
            batch.require(len(record)+12<=ammo.MAX_CACHE,'Single cache record exceeds shared byte bound')
            shard_rows.append(r);shard_records.append(record);shard_bytes+=len(record)
        flush()
        raw_additions=ammo.MAGIC+struct.pack('<I',len(records))+b''.join(record for _,record in records)
        compact_additions=body.compact_cache_sections(raw_additions,[r for r,_ in records]) if records else raw_additions
        full_payload=ammo.MAGIC+struct.pack('<I',len(rows))+baseline_compacted[12:]+compact_additions[12:]
        receipt['full_cache']=dict(rows=len(rows),bytes=len(full_payload),maximum_rows=args.max_cache_rows,
                                   maximum_bytes=args.max_cache_bytes,baseline_pin=batch.pin(baseline_path),
                                   baseline_compact_bytes=len(baseline_compacted),addition_compact_bytes=len(compact_additions)-12)
        batch.require(len(rows)<=args.max_cache_rows and len(full_payload)<=args.max_cache_bytes,'Complete cache exceeds explicit candidate bound')
        (output/'body-ammo.fvrprop').write_bytes(full_payload)
        receipt['full_cache']['pin']=batch.pin(output/'body-ammo.fvrprop')
        receipt.update(configurations=len(plan['rows']),strict_geometry=len(current),joined_configurations=len(joined),
                       baseline_rows=len(baseline),catalog_rows=len(rows),appended_rows=len(rows)-len(baseline),
                       selected_resource_bytes=pool.bytes,archives=len(pool.archives),
                       complete=len(joined)==len(plan['rows']),runtime_consumer_integrated=False)
    batch.write(output/'extraction-receipt.json',receipt)
    print(json.dumps({k:v for k,v in receipt.items() if k not in ('geometry_groups','cache_shards','residuals')}))
    return 0 if receipt['complete'] else 2


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--batch',type=Path,required=True);parser.add_argument('--game',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--max-cache-rows',type=int,default=256)
    parser.add_argument('--max-cache-bytes',type=int,default=64*1024*1024)
    return extract(parser.parse_args())


if __name__=='__main__':raise SystemExit(main())
