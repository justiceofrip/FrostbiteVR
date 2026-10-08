"""Join reviewed exact magazine, visibility and configured-body data.

This compiles only separately reviewed descriptor keys. Native owner, current
configuration, operation code binding, receipts and hand policy remain runtime
requirements. Installed vertices, native execution and headset acceptance are
not exported or inferred by this tool.
"""
from __future__ import annotations
import argparse,json,hashlib
from pathlib import Path
import bc2_magazine_registry_header as registry

def require(value, message):
    if not value:raise ValueError(message)

def unique(rows,key):
    result={}
    for row in rows:
        identity=key(row);require(identity and identity not in result,'Duplicate or missing exact identity')
        result[identity]=row
    return result

def join(jobs,reviews,geometry,visibility,catalog):
    header,summary=registry.build(jobs,reviews)
    require(summary['rows']==summary['enabled']==len(jobs['descriptors'])>0,'All requested keys require exact family review')
    require(visibility.get('native_admission_granted') is False and not visibility.get('excluded'),'Visibility extraction is not native admission')
    geo=unique(geometry['profiles'],lambda x:x['weapon']['resource'])
    vis=unique(visibility['rows'],lambda x:x['authored_configuration']['resource'])
    bodies=unique([x for x in catalog['profiles'] if x.get('configuration_path')],lambda x:x['authored_configuration']['resource'])
    requested={x['identity']['weapon']['resource'] for x in jobs['descriptors']}
    require(set(geo)==set(vis)==set(bodies)==requested,'Resource/visibility/body exact configuration set differs')
    joined=[]
    for job in jobs['descriptors']:
        identity=job['identity'];key=identity['weapon']['resource'];g,v,b=geo[key],vis[key],bodies[key]
        require(g['native_asset_name']==v['asset']==b['asset']==identity['assetName'],'Asset identity mismatch')
        cfg=v['authored_configuration'];require(cfg==b['authored_configuration'],'Body/visibility DBX references differ')
        require(cfg['sha256']==identity['weapon']['sha256']==g['weapon']['sha256'],'Configuration content hash differs')
        require(cfg['instance_guid'].lower()==identity['weapon']['guid'].lower()==g['weapon']['instance_guid'].lower(),'Configuration GUID differs')
        require(key==identity['assetPath']+'.dbx' and b['configuration_path']==identity['assetPath'],'Exact configuration path differs')
        require(g['configured_mesh_path']==b['mesh'] and b['mesh'] in v['meshes'],'Resource base mesh is not configured body mesh')
        require(v['meshes']==b['configured_meshes'] and len(set(v['meshes']))==len(v['meshes']),'Complete attachment set differs')
        require(int(v['rig_fingerprint'],16)==b['rig_fingerprint']==0xa7f219a1426216ab,'Reviewed rig differs')
        require(v['weighted_section_data_verified'] is True and v['native_admitted'] is False,'Visibility evidence flags invalid')
        require(b['display_only'] is True and b['display_anchor_not_native_grip_calibration'] is True,'Body geometry cannot grant grip authority')
        require(b['closed_pose']=='static_authored_pose' and b['display_anchor_time_seconds']==0,'Body pose not reviewed authored frame')
        for state in b['display_anchor_evaluation'].values():require(state in ('static_authored_pose','decoded_spline_candidate'),'Unresolved body anchor')
        sources=unique(b['sources'],lambda s:s['mesh']);resources=unique(v['resources'],lambda s:s['mesh'])
        require(set(sources)==set(resources)==set(v['meshes']),'Missing attachment resource')
        for mesh,s in sources.items():
            r=resources[mesh];lods=[x for x in r['lods'] if x['lod']==0]
            require(len(lods)==1 and lods[0]['sha256']==s['lod_sha256'],'LOD0 data differs')
            require(s['archive']==r['archive'] and s['archive_index_sha256']==r['index_sha256'] and s['mesh_sha256']==r['metadata_sha256'],'Whole mesh source differs')
        mags=[x for x in catalog['profiles'] if x['asset']==b['asset'] and x['mesh']==b['mesh'] and x['part']==g['role_evidence']['bone']]
        require(len(mags)==1 and mags[0]['rig_fingerprint']==b['rig_fingerprint'],'Missing exact detached magazine geometry')
        magazine=mags[0]
        if g.get('assembly'):
            from bc2_magazine_assembly import validate_receipt
            validate_receipt(g['assembly'],g)
            require(magazine.get('rigid_assembly') is True and magazine.get('display_only') is not True,'Assembly magazine is not a whole-weapon body prop')
            require(magazine.get('assembly_digest')==g['assembly']['assembly_digest'] and magazine.get('geometry_profile_digest')==g['profile_digest'],'Detached assembly proof differs')
            require(magazine.get('assembly_source',{}).get('profile')==g,'Detached assembly exact source profile differs')
            for field in ('mesh_sha256','lod_sha256','skeleton_sha256','static_clip_sha256'):
                require(magazine.get(field)==g[field],'Detached assembly source differs: '+field)
            require(magazine.get('reload_clip_sha256')==g['reload_clip']['sha256'],'Detached assembly reload source differs')
        else:
            require(not magazine.get('rigid_assembly') and not magazine.get('assembly_digest'),'Leaf geometry cannot borrow an assembly cache')
        joined.append(dict(key=job['key'],native_id=f"{registry.stable_id(job['descriptor_digest']):016x}",descriptor_digest=job['descriptor_digest'],
          identity=identity,configuration=job['configuration'],source_class=job['source_class'],family=registry.FAMILY,
          configured_meshes=v['meshes'],weighted_names=v['weighted_names'],rig_fingerprint=v['rig_fingerprint'],
          magazine_part=mags[0]['part'],body_part=b['part'],body_anchor_evaluation=b['display_anchor_evaluation'],
          required_archives=sorted({b['configuration_archive'],b['closed_archive'],*(s['archive'] for s in b['sources'])}),
          native_per_variant_tested=False,headset_tested=False))
    return dict(schema='fvr.bc2.exact-resource-visibility-body-enrollment.v1',family=registry.FAMILY,
        activation='explicit compiled reviewed registry + geometry; ordinary resource backend switch',
        runtime_requirements=['exact coherent native config and selected-carried identity','current native operation binding and source-bound review',
          'all required installed whole-weapon/attachment geometry','ordinary body custody and hand arbitration','independent native ammunition receipts'],
        native_constructor_permission=False,profiles=joined),header

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('jobs','reviews','geometry','visibility','catalog'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--header',type=Path,required=True);a=p.parse_args()
    paths={n:getattr(a,n) for n in ('jobs','reviews','geometry','visibility','catalog')}
    document,header=join(**{n:json.loads(path.read_text()) for n,path in paths.items()})
    document['inputs']={n:dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest()) for n,path in paths.items()}
    document['registry_header_sha256']=hashlib.sha256(header.encode()).hexdigest()
    for path,data in ((a.output,json.dumps(document,indent=2)+'\n'),(a.header,header)):
        path.parent.mkdir(parents=True,exist_ok=True);path.write_text(data,encoding='utf-8')
    print(json.dumps(dict(exact_configurations=len(document['profiles']),family=document['family'],native_calls=False)))
if __name__=='__main__':main()
