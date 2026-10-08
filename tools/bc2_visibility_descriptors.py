"""Derive complete weighted-section descriptors from exact local mesh sets.

Input: [{asset, meshes:[{archive,mesh}]}]. Configured mesh sets are candidate
identities until the native reader matches them exactly; extraction grants no
runtime admission. No vertices, indexes or copyrighted animation are exported.
"""
import argparse,json
from pathlib import Path
from inspect_bc2_mesh_asset import Archive,bone_hash,sha
from bc2_mesh_geometry import metadata,geometry
from bc2_weapon_animation_pipeline import Skeleton
from bc2_weapon_config_pipeline import parse,guid,named,scalar

RIG=0xa7f219a1426216ab
SKELETON_SHA='d1d713cc7301bc5ae2b011a0991ca03c2fcbb26c1187900f6fd129533d173c09'
def require(ok,why):
    if not ok:raise ValueError(why)
def weighted_names(hashes,arms,known,parents):
    require(hashes and len(hashes)<=64,'weighted influence bound')
    require(not hashes&arms,'weapon/arm vertex influence overlap')
    names=[]
    for h in sorted(hashes):
        name=known.get(h);require(name is not None,'unknown weighted bone')
        node=name;visited=set()
        while node!='jntWpn_1':
            require(node in parents and node not in visited,'weighted bone outside verified weapon subtree')
            visited.add(node);node=parents[node]
        names.append(name)
    return names
def local_archive(game,relative):
    path=(game/relative).resolve();require(path.is_relative_to(game.resolve()),'archive outside installation')
    return Archive(path)
def specs_from_configuration(game,configured,classes=('wcAssault','wcSmg'),inventory=None):
    """Resolve complete authored sets by exact resource AND mesh-instance GUID."""
    relative=configured['archive'];a=local_archive(game,relative)
    require(a.index_sha256==configured['index_sha256'],'configuration archive index changed')
    specs=[];excluded=[];documents={};identities=set()
    selected=[w for w in configured['resolved_weapons'] if w['weapon_class'] in classes]
    requested={w['resource'] for w in selected}
    for weapon in selected:
        for state in weapon['weapon_states']:
            for reference in state['mesh_asset_references']:
                path=reference.rsplit('/',1)[0]
                requested.update(e.name for e in a.entries if e.flags==65536 and e.name.lower()==path.lower()+'.dbx')
    raw_documents=a.read_selected(sorted(requested))
    for weapon in configured['resolved_weapons']:
        if weapon['weapon_class'] not in classes:continue
        try:
            require(len(weapon['weapon_states'])==1,'multiple authored weapon states')
            references=weapon['weapon_states'][0]['mesh_asset_references'];require(0<len(references)<=8,'configured mesh count')
            raw_weapon=raw_documents[weapon['resource']]
            require(sha(raw_weapon)==weapon['resource_sha256'],'authored weapon resource changed')
            weapon_document=parse(weapon['resource'],raw_weapon)
            native=weapon_document.instances[guid(weapon['instance_guid'])]
            require(native.attributes['type']=='GameSharedResources.SoldierWeaponData' and
                    scalar(named(native,'Name'),'string')==weapon['native_name'],'authored weapon identity changed')
            native_states=named(native,'WeaponStates').children
            require(len(native_states)==1 and [i.attributes['ref'] for i in named(native_states[0],'Meshes1p').children if i.tag=='item']==references,
                    'authored configured mesh set changed')
            meshes=[];proof=[]
            for reference in references:
                path,identifier=reference.rsplit('/',1);identifier=guid(identifier)
                matches=[e.name for e in a.entries if e.flags==65536 and e.name.lower()==path.lower()+'.dbx']
                require(len(matches)==1,'missing/ambiguous referenced mesh DBX '+path)
                resource=matches[0]
                if resource not in documents:
                    raw=raw_documents[resource];documents[resource]=(parse(resource,raw),sha(raw))
                document,digest=documents[resource]
                instance=document.instances.get(identifier)
                require(instance is not None and instance.attributes.get('type','').endswith('.SkinnedMeshAsset'),
                        'referenced instance is not exact SkinnedMeshAsset')
                # Resource stems are resolved against exact authored DBX identity,
                # never inferred from a weapon display name or enum classification.
                geometry_matches=[e.name for e in a.entries if e.flags==65536 and e.name.lower()==path.lower()+'.res']
                if len(geometry_matches)==1:meshes.append({'archive':relative,'mesh':geometry_matches[0][:-4]})
                else:
                    sources=[(source,mesh) for source in (inventory or {}).get('archives',[]) for mesh in source.get('meshes',[])
                             if mesh['resource'].lower()==path.lower()+'.res' and mesh.get('lods')]
                    signatures={(mesh['sha256'],tuple(lod['data_sha256'] for lod in mesh['lods'])) for _,mesh in sources}
                    require(sources and len(signatures)==1,'missing/ambiguous indexed mesh resource '+path)
                    source,mesh=min(sources,key=lambda pair:pair[0]['archive'])
                    meshes.append({'archive':source['archive'],'mesh':mesh['resource'][:-4],
                        'expected_index_sha256':source['index_sha256'],'expected_metadata_sha256':mesh['sha256']})
                proof.append({'reference':reference,'resource':resource,'sha256':digest,'instance_guid':identifier})
            identity=(weapon['native_name'],tuple(sorted(x['mesh'] for x in meshes)))
            if identity in identities:continue
            identities.add(identity);specs.append({'asset':weapon['native_name'],'meshes':meshes,
                'authored_configuration':{'resource':weapon['resource'],'sha256':weapon['resource_sha256'],
                  'instance_guid':weapon['instance_guid'],'mesh_references':proof}})
        except (ValueError,KeyError,OSError) as exc:excluded.append({'asset':weapon.get('native_name'),'reason':str(exc)})
    return specs,excluded
def coverage(game,spec,known):
    a=local_archive(game,spec['archive']);mesh=spec['mesh']
    if 'expected_index_sha256' in spec:require(a.index_sha256==spec['expected_index_sha256'],'indexed mesh archive changed')
    require(isinstance(mesh,str) and 0<len(mesh)<512 and '\0' not in mesh and mesh.isascii(),'mesh path bounds')
    def exact(name):
        hits=[e.name for e in a.entries if e.flags==65536 and e.name.lower()==name.lower()]
        require(len(hits)==1,'missing/ambiguous exact resource '+name);return hits[0]
    name=exact(mesh+'.res');raw=a.read_selected([name])[name];lods=metadata(raw)
    if 'expected_metadata_sha256' in spec:require(sha(raw)==spec['expected_metadata_sha256'],'indexed mesh metadata changed')
    all_hashes=set();evidence=[]
    for lod in lods:
        resource=exact(mesh+f"_lod{lod['lod']}_data.res");data=a.read_selected([resource])[resource]
        sections=geometry(data,lod,known)
        for section in sections:
            require(section['all_vertices_normalized'],'unnormalized section')
            all_hashes.update(int(h,16) for h in section['weighted_hashes'])
        evidence.append({'lod':lod['lod'],'resource':resource,'sha256':sha(data),
          'sections':[{'name':s['name'],'weighted_hashes':s['weighted_hashes'],'vertices':s['vertices'],
                       'triangles':s['triangles'],'normalized':s['all_vertices_normalized']} for s in sections]})
    return all_hashes,{'mesh':name[:-4],'archive':spec['archive'],'index_sha256':a.index_sha256,
                     'metadata_sha256':sha(raw),'lods':evidence}
def derive(game,specs,trace):
    require(0<len(specs)<=64,'descriptor catalog bound')
    a=local_archive(game,'Dist/win32/levels/sp_common/level-00.fbrb')
    raw=a.read_selected(['Characters/Skeletons/ske01.res'])['Characters/Skeletons/ske01.res']
    require(sha(raw)==SKELETON_SHA,'installed skeleton changed')
    skeleton=Skeleton(raw);known={bone_hash(n):n for n in skeleton.names}
    require(len(known)==len(skeleton.names),'native-name hash collision')
    proof=trace.read_bytes();samples=json.loads(proof)['gameplay']['rig_publication']['weapon_profile_samples']
    samples=[s for s in samples if s['weapon_bones_complete'] and s['skeleton']==f'fnv1a64:{RIG:016x}']
    require(samples,'exact saved native rig proof missing')
    topologies={tuple(sorted((b['name'],b['parent_name']) for b in s['native_weapon_bones'])) for s in samples}
    require(len(topologies)==1,'native topology changes');parents=dict(next(iter(topologies)))
    arms=set();arm_evidence=[]
    for mesh in ('Characters/US/US_FP_Arms/US_FP_ArmsStandard_Mesh','Characters/US/US_FP_Arms/US_FP_Assault_Mesh'):
        hashes,e=coverage(game,{'archive':'Dist/win32/levels/sp_common/level-00.fbrb','mesh':mesh},known)
        arms.update(hashes);arm_evidence.append(e)
    rows=[];excluded=[];identities=set();mesh_cache={}
    for spec in specs:
        try:
            require(isinstance(spec['asset'],str) and 0<len(spec['asset'])<128 and '\0' not in spec['asset'] and
                    spec['asset'].isascii() and 0<len(spec['meshes'])<=8,'asset/mesh bounds')
            hashes=set();meshes=[];evidence=[]
            for mesh in spec['meshes']:
                identity_key=(mesh['archive'],mesh['mesh'],mesh.get('expected_index_sha256'),mesh.get('expected_metadata_sha256'))
                if identity_key not in mesh_cache:mesh_cache[identity_key]=coverage(game,mesh,known)
                h,e=mesh_cache[identity_key];hashes.update(h);meshes.append(e['mesh']);evidence.append(e)
            require(len(set(meshes))==len(meshes),'duplicate configured mesh')
            identity=(spec['asset'],tuple(sorted(meshes)));require(identity not in identities,'duplicate descriptor identity')
            names=weighted_names(hashes,arms,known,parents)
            identities.add(identity);rows.append({'asset':spec['asset'],'meshes':sorted(meshes),'weighted_names':names,
                'rig_fingerprint':f'{RIG:016x}','native_admitted':False,'native_configured_set_proven':False,
                'weighted_section_data_verified':True,
                'authored_configuration':spec.get('authored_configuration'),'resources':evidence})
        except (ValueError,KeyError,OSError) as exc:excluded.append({'asset':spec.get('asset'),'reason':str(exc)})
    return {'schema':'fvr.bc2.visibility-descriptors.v1','assets_exported':False,'native_actions':False,
      'native_admission_granted':False,'native_rig_proof':{'path':str(trace),'sha256':sha(proof)},
      'skeleton_sha256':sha(raw),'arm_weighted_hashes':sorted(arms),'arms':arm_evidence,'rows':rows,'excluded':excluded}
def header(report):
    out=['#pragma once','#include "Bc2VisibilityProfiles.h"','#include <array>','namespace fvr::bc2 {']
    for i,row in enumerate(report['rows']):
        for key,name in [('meshes','Meshes'),('weighted_names','Bones')]:
            values=','.join(json.dumps(v) for v in row[key])
            out.append(f'inline constexpr std::array<std::string_view,{len(row[key])}> Visibility{name}{i}{{{values}}};')
    out.append(f'inline constexpr std::array<VisibilityDescriptor,{len(report["rows"])}> VisibilityDescriptors{{{{')
    for i,r in enumerate(report['rows']):
        config=r.get('authored_configuration')
        path=config['resource'][:-4] if config else ''
        if config and (not config['resource'].endswith('.dbx') or '\0' in path or '\\' in path or ':' in path or path.startswith('/') or any(p in ('','.', '..') for p in path.split('/'))):
            raise ValueError('Invalid authored configuration path')
        data_verified='true' if r.get('weighted_section_data_verified') is True else 'false'
        out.append('{'+json.dumps(r['asset'])+f',VisibilityMeshes{i},VisibilityBones{i},0x{r["rig_fingerprint"]}ull,false,'+json.dumps(path)+f',false,false,{data_verified}'+'},')
    out.extend(['}};','}']);return '\n'.join(out)+'\n'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',type=Path,required=True)
    inputs=p.add_mutually_exclusive_group(required=True)
    inputs.add_argument('--specs',type=Path);inputs.add_argument('--configured-catalog',type=Path)
    p.add_argument('--weapon-class',action='append',help='Authored class filter; defaults to wcAssault and wcSmg')
    p.add_argument('--installed-inventory',type=Path);p.add_argument('--trace',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True);p.add_argument('--header',type=Path,required=True);a=p.parse_args()
    if a.configured_catalog:
        if not a.installed_inventory:p.error('--configured-catalog requires --installed-inventory')
        specs,gaps=specs_from_configuration(a.game,json.loads(a.configured_catalog.read_text()),
                                           classes=tuple(a.weapon_class or ('wcAssault','wcSmg')),
                                           inventory=json.loads(a.installed_inventory.read_text()))
    else:specs=json.loads(a.specs.read_text());gaps=[]
    report=derive(a.game,specs,a.trace);report['configuration_excluded']=gaps
    a.report.write_text(json.dumps(report,indent=2)+'\n');a.header.write_text(header(report))
    print(json.dumps({'descriptors':len(report['rows']),'excluded':report['excluded'],'native_admitted':False}))
if __name__=='__main__':main()
