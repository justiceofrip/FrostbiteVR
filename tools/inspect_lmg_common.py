"""Read one larger common archive; export LMG or all-weapon metadata, never assets.

The canonical reader retains its 512 MiB default. This standalone process permits
at most 640 MiB declared/inflated payload for exactly mp_common/level-00.fbrb;
stream chunks remain 1 MiB and selected resources remain bounded to 32 MiB.
"""
import argparse,hashlib,json,sys
from pathlib import Path

def run(game,tools,inventory,*,all_weapons=False):
    sys.path.insert(0,str(tools.resolve()))
    import inspect_bc2_mesh_asset as mesh
    from bc2_weapon_config_pipeline import parse,Resolver,effective_weapon
    from bc2_weapon_mesh_bindings import join
    relative=Path('Dist/win32/levels/mp_common/level-00.fbrb')
    path=(game/relative).resolve()
    if not path.is_relative_to(game.resolve()):raise ValueError('Archive outside supplied game root')
    previous=mesh.MAX_PAYLOAD
    try:
        mesh.MAX_PAYLOAD=640*1024*1024
        archive=mesh.Archive(path)
    finally:mesh.MAX_PAYLOAD=previous
    # Includes exact mesh DBX references, but no mesh resource/animation payload.
    entries=[e for e in archive.entries if e.flags==65536 and e.name.lower().startswith('objects/weapons/handheld/') and
             e.name.lower().endswith('.dbx') and not any(s in e.name.lower() for s in ('animtree','aiclones/'))]
    blobs=archive.read_selected([e.name for e in entries]);documents=[];errors=[]
    for name,data in blobs.items():
        try:documents.append(parse(name,data))
        except ValueError as exc:errors.append(dict(resource=name,sha256=mesh.sha(data),reason=str(exc)))
    resolver=Resolver(documents);weapons=[];unresolved=[]
    for doc in documents:
        for instance in doc.instances.values():
            if instance.attributes['type']!='GameSharedResources.SoldierWeaponData':continue
            try:
                weapon=effective_weapon(resolver,doc,instance)
                if all_weapons or weapon['weapon_class']=='wcLmg':weapons.append(weapon)
            except ValueError as exc:unresolved.append(dict(resource=doc.resource,sha256=doc.sha256,instance=instance.attributes,reason=str(exc)))
    config=dict(schema='fvr.bc2.authored_weapon_configuration',schema_version=1,archive=relative.as_posix(),index_sha256=archive.index_sha256,
        payload_sha256=archive.payload_sha256,declared_payload_bytes=archive.payload_size,stream_bound_bytes=640*1024*1024,
        selected_bytes=sum(e.size for e in entries),parsed_documents=len(documents),parse_errors=errors,resolved_weapons=weapons,
        unresolved_weapons=unresolved,filter=('all classes' if all_weapons else 'wcLmg only')+'; exact native Names retained',limits=['Authored definitions only; no native runtime binding or effective capacity inferred.'])
    bindings=join(config,documents,inventory)
    return dict(schema='fvr.bc2.common_weapon_metadata.v1' if all_weapons else 'fvr.bc2.lmg_common_metadata.v1',read_only=True,exported_assets=False,configuration=config,mesh_bindings=bindings,
        tools=[dict(name=p,sha256=mesh.sha((tools/p).read_bytes())) for p in ('inspect_bc2_mesh_asset.py','bc2_weapon_config_pipeline.py','bc2_weapon_mesh_bindings.py')])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',type=Path,required=True);p.add_argument('--tools',type=Path,required=True)
    p.add_argument('--inventory',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--all-weapons',action='store_true',help='Include every resolved weapon class for the shared reload catalog.');a=p.parse_args()
    data=a.inventory.read_bytes()
    if len(data)>64*1024*1024:raise ValueError('Inventory bound')
    result=run(a.game,a.tools,json.loads(data),all_weapons=a.all_weapons)
    result['mesh_bindings']['inventory_sha256']=hashlib.sha256(data).hexdigest()
    result['mesh_bindings']['archive']=result['configuration']['archive']
    result['mesh_bindings']['archive_index_sha256']=result['configuration']['index_sha256']
    a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    c=result['configuration'];print(json.dumps(dict(documents=c['parsed_documents'],errors=len(c['parse_errors']),unresolved=len(c['unresolved_weapons']),definitions=len(c['resolved_weapons']),filter=c['filter'],mesh_summary=result['mesh_bindings']['summary'])))
if __name__=='__main__':main()
