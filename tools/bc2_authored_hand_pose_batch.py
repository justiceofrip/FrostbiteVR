"""Extract private authored hand-reference metadata using the shared BC2 reader.

The supplied inventory fixes archive identities. The explicit skeleton pairing
is a caller choice, not a native weapon/animation binding; the typed grip binding
generator independently resolves and verifies that relationship afterwards.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path, PurePosixPath
import re

from bc2_weapon_animation_pipeline import Clip, Skeleton
from inspect_bc2_mesh_asset import Archive

def resource(value):
    if not isinstance(value,str) or not value or '\\' in value or ':' in value or any(p in ('','.','..') for p in value.split('/')):
        raise ValueError('Archive-relative resource required')
    return value

def digest(value):
    return hashlib.sha256(value).hexdigest()

def checked_hash(value):
    if not isinstance(value,str) or not re.fullmatch('[0-9a-f]{64}',value):
        raise ValueError('SHA256 required')
    return value

def within(root,relative):
    path=(root/resource(relative)).resolve()
    if not path.is_relative_to(root):raise ValueError('Archive leaves game root')
    return path

def hand_resource(name):
    return PurePosixPath(resource(name)).name.casefold()=='handsikpose.res'

def run(game,inventory,skeleton_archive,skeleton_resource,*,archive_type=Archive,clip_type=Clip,skeleton_type=Skeleton):
    if inventory.get('schema')!='fvr.bc2.installed_weapon_inventory' or inventory.get('schema_version')!=1:
        raise ValueError('Installed inventory schema')
    sources=inventory.get('archives')
    if not isinstance(sources,list) or not 0<len(sources)<=1024:raise ValueError('Archive count bound')
    seen=set()
    for source in sources:
        name=resource(source['archive']);checked_hash(source['index_sha256'])
        if name.casefold() in seen:raise ValueError('Duplicate archive identity')
        seen.add(name.casefold())
    root=game.resolve();sk_archive=archive_type(within(root,skeleton_archive))
    skeleton_resource=resource(skeleton_resource)
    matches=[e for e in sk_archive.entries if e.flags==65536 and e.name.casefold()==skeleton_resource.casefold()]
    if len(matches)!=1 or matches[0].kind!='GrannyModel':raise ValueError('Exact single skeleton resource required')
    sk_entry=matches[0];skeleton=skeleton_type(sk_archive.read_selected([sk_entry.name])[sk_entry.name])
    clips={};gaps=[];occurrences=0
    for source in sorted(sources,key=lambda s:s['archive'].casefold()):
        relative=source['archive']
        try:
            archive=archive_type(within(root,relative))
            if archive.index_sha256!=source['index_sha256']:raise ValueError('Archive index changed since inventory')
            entries=[e for e in archive.entries if e.flags==65536 and e.kind=='GrannyAnimation' and hand_resource(e.name)]
            if len(entries)>256 or len({e.name.casefold() for e in entries})!=len(entries):raise ValueError('Ambiguous or excessive authored hand resources')
            expected={a['name'].casefold() for a in source.get('animations',[]) if a.get('kind')=='GrannyAnimation' and hand_resource(a['name'])}
            if {e.name.casefold() for e in entries}!=expected:raise ValueError('Hand resource set differs from inventory')
            blobs=archive.read_selected([e.name for e in entries]) if entries else {}
            for entry in entries:
                raw=blobs[entry.name];h=digest(raw);key=(entry.name.casefold(),h)
                provenance={'archive':relative,'index_sha256':archive.index_sha256};occurrences+=1
                if key in clips:clips[key]['sources'].append(provenance);continue
                if len(clips)>=1024:raise ValueError('Distinct hand pose count bound')
                row={'resource':entry.name,'sha256':h,'sources':[provenance],
                     'runtime_admission':False,'active_native_mesh_binding':False}
                try:
                    clip=clip_type(raw);row.update(clip.summary())
                    row['hands']=clip.evaluate(skeleton,0.,['RightHand','LeftHand'])
                    names=[name for name in skeleton.names if name in clip.tracks and name.startswith(('LeftHand','RightHand','jntWpn'))]
                    try:row['parts_and_fingers']=clip.evaluate(skeleton,0.,names)
                    except (ValueError,KeyError,TypeError,OverflowError) as exc:row['parts_error']=str(exc)
                    row['status']='authored_hands_decoded'
                except (ValueError,KeyError,TypeError,OverflowError) as exc:
                    row['status']='unsupported';row['reason']=str(exc)
                clips[key]=row
        except (ValueError,KeyError,TypeError,OSError,OverflowError) as exc:
            gaps.append({'archive':relative,'reason':str(exc)})
    rows=[clips[k] for k in sorted(clips)]
    return {'schema':'fvr.bc2.authored_hand_pose_batch.v1',
        'skeleton':{'archive':skeleton_archive,'archive_index_sha256':sk_archive.index_sha256,
                    'resource':sk_entry.name,**skeleton.metadata()},
        'archive_count':len(sources),'resource_occurrences':occurrences,'unique_resource_versions':len(rows),
        'statuses':dict(Counter(row['status'] for row in rows)),
        'static_hand_pose_resources':sum(row.get('hands',{}).get('evaluation_status')=='static_authored_pose' for row in rows),
        'gaps':gaps,'runtime_admission':False,'active_native_mesh_binding':False,'headset_tested':False,
        'scope':'Caller-paired authored reference; exact typed clip/skeleton/configuration binding remains required.',
        'clips':rows}

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('game','inventory','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--skeleton-archive',required=True)
    parser.add_argument('--skeleton-resource',required=True)
    args=parser.parse_args(argv)
    blob=args.inventory.read_bytes()
    if len(blob)>64*1024*1024:raise ValueError('Inventory exceeds 64 MiB bound')
    import bc2_granny_curves,bc2_granny_resource,bc2_weapon_animation_pipeline,inspect_bc2_mesh_asset
    paths=[Path(m.__file__) for m in (bc2_granny_curves,bc2_granny_resource,bc2_weapon_animation_pipeline,inspect_bc2_mesh_asset)]
    before={p.name:digest(p.read_bytes()) for p in paths}
    result=run(args.game,json.loads(blob),args.skeleton_archive,args.skeleton_resource)
    if before!={p.name:digest(p.read_bytes()) for p in paths}:raise ValueError('Decoder changed during batch')
    result.update(inventory_sha256=digest(blob),decoder_sources=before)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k not in ('clips','skeleton','decoder_sources')}))

if __name__=='__main__':main()
