"""Join authored weapon mesh GUIDs to extracted geometry; no runtime admission."""
from __future__ import annotations
import argparse
import json
from pathlib import Path

# In a source export this resolves locally. The development caller can supply
# PYTHONPATH to the canonical tools without embedding an installation path.
from bc2_weapon_config_pipeline import Resolver, guid, inspect, named, parse, scalar
from inspect_bc2_mesh_asset import Archive, sha

def reference_resource(reference):
    if not isinstance(reference,str) or '/' not in reference:
        raise ValueError('Mesh reference requires an exact resource path and GUID')
    path,identifier=reference.rsplit('/',1)
    if '\\' in path or ':' in path or any(x in ('','.','..') for x in path.split('/')):
        raise ValueError('Invalid mesh resource path')
    guid(identifier)
    return path.lower()+'.dbx'

def join(configurations,documents,inventory):
    if configurations.get('schema')!='fvr.bc2.authored_weapon_configuration' or configurations.get('schema_version')!=1:
        raise ValueError('Unsupported authored configuration schema')
    if inventory.get('schema')!='fvr.bc2.installed_weapon_inventory' or inventory.get('schema_version')!=1:
        raise ValueError('Unsupported geometry inventory schema')
    resolver=Resolver(documents);profiles={}
    for profile in inventory['asset_profiles']:
        key=profile['resource'].lower()
        if key in profiles:raise ValueError('Ambiguous geometry resource')
        profiles[key]=profile
    weapons=[]
    for weapon in configurations['resolved_weapons']:
        row={k:weapon[k] for k in ('native_name','resource','resource_sha256','instance_guid')}
        row.update(runtime_admitted=False,states=[])
        for state in weapon['weapon_states']:
            state_row=dict(index=state['index'],meshes=[],missing=list(state['missing']))
            for reference in state['mesh_asset_references']:
                entry=dict(reference=reference,status='unresolved',runtime_admitted=False)
                try:
                    resource=reference_resource(reference)
                    document=resolver.documents.get(resource[:-4])
                    if document is None:raise ValueError('Referenced mesh document unavailable')
                    document,instance=resolver.resolve(document,reference,'Render.SkinnedMeshAsset')
                    # Require the reference's GUID, not whichever instance happens
                    # to be primary. MeshShaderSetAsset often shares the Name.
                    name=scalar(named(instance,'Name'),'string')
                    if reference_resource(name+'/'+instance.attributes['guid'])!=resource:
                        raise ValueError('Authored mesh Name differs from referenced resource')
                    entry.update(status='authored_mesh_resolved',mesh_document=document.resource,
                        mesh_document_sha256=document.sha256,mesh_instance_guid=guid(instance.attributes['guid']),
                        mesh_resource=name+'.res')
                    profile=profiles.get((name+'.res').lower())
                    if profile is None:
                        entry['geometry_status']='not_in_inventory'
                    else:
                        variants=[]
                        for variant in profile['geometry_variants']:
                            variants.append(dict(mesh_sha256=variant['sha256'],lods=[
                                dict(lod=lod['lod'],data_resource=lod['data_resource'],data_sha256=lod['data_sha256'])
                                for lod in variant.get('lods',[])]))
                        entry.update(geometry_status='candidate_inventory_match',geometry_variants=variants,
                            variant_count=len(variants),geometry_sources=profile['sources'])
                except (ValueError,KeyError) as exc:entry['reason']=str(exc)
                state_row['meshes'].append(entry)
            row['states'].append(state_row)
        weapons.append(row)
    entries=[m for w in weapons for s in w['states'] for m in s['meshes']]
    return dict(schema='fvr.bc2.authored_weapon_mesh_bindings',schema_version=1,admission='none',weapons=weapons,
        summary=dict(weapon_definitions=len(weapons),mesh_references=len(entries),
            resolved=sum(e['status']=='authored_mesh_resolved' for e in entries),
            geometry_matches=sum(e.get('geometry_status')=='candidate_inventory_match' for e in entries),
            unresolved=sum(e['status']=='unresolved' for e in entries)),
        limits=['Authored mesh GUIDs do not establish current native selection, rig, hand contact, muzzle or attachment eligibility.',
                'Geometry matches refer to the supplied inventory snapshot; native bytes and current LOD must still match.',
                'All content variants are retained. Shared mesh references never transfer a sibling weapon runtime capability.'])

def run(game,relative,inventory):
    configurations=inspect(game,relative)
    path=(game/relative).resolve()
    if not path.is_relative_to(game.resolve()):raise ValueError('Archive leaves game root')
    archive=Archive(path);entries={};documents=[];errors=[]
    for entry in archive.entries:
        if entry.flags!=65536 or not entry.name.lower().endswith('.dbx'):continue
        key=entry.name.lower()
        if key in entries:raise ValueError('Ambiguous archive DBX path')
        entries[key]=entry.name
    wanted=set()
    for weapon in configurations['resolved_weapons']:
        for state in weapon['weapon_states']:
            for reference in state['mesh_asset_references']:
                try:wanted.add(reference_resource(reference))
                except ValueError:pass # join records the exact reference failure.
    for resource,data in archive.read_selected([entries[key] for key in sorted(wanted) if key in entries]).items():
        try:documents.append(parse(resource,data))
        except ValueError as exc:errors.append(dict(resource=resource,sha256=sha(data),reason=str(exc)))
    result=join(configurations,documents,inventory)
    result.update(archive=relative.as_posix(),archive_index_sha256=archive.index_sha256,
        mesh_documents=len(documents),mesh_parse_errors=errors,
        config_parse_errors=configurations['parse_errors'],unresolved_weapons=configurations['unresolved_weapons'])
    return result

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',required=True,type=Path)
    parser.add_argument('--archive',type=Path,default=Path('Dist/win32/levels/sp_common/level-00.fbrb'))
    parser.add_argument('--inventory',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args(argv);data=args.inventory.read_bytes()
    if len(data)>64*1024*1024:parser.error('Inventory exceeds64MiB bound')
    result=run(args.game,args.archive,json.loads(data))
    result['inventory_sha256']=sha(data)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(result['summary']));return 0

if __name__=='__main__':raise SystemExit(main())
