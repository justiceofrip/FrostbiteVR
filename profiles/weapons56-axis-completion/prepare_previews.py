"""Bounded offline exact LOD0 previews; no native process or saved game bytes."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
import bc2_authored_magazine_geometry as geo
from bc2_magazine_contact_batch import ArchivePool
from bc2_weapon_animation_pipeline import Clip, Skeleton

OUT = ROOT/'reports/coordinator-20261030-feedback56/axis-completion'
RECOVERY = Path('G:/Unity/VRMMO/bc2vr-recovery')
GAME = Path('G:/New folder (2)/Battlefield Bad Company 2')
COMMON = dict(archive='Dist/win32/levels/mp_common/level-00.fbrb',
              archive_index_sha256='6ec10fba796c14d2a05f86d3079e819c38a4aaab3ec9205d7345bd2869554965')


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def pin(path):
    return dict(path=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def png(path, clouds):
    """Upper panel Z/Y, lower panel Z/X; positive horizontal right, vertical up."""
    width, height = 1400, 800
    pixels = bytearray([245, 246, 248] * (width*height))
    colors = [(24,60,83),(157,85,64),(50,122,74),(102,88,139),(141,117,28)]
    points = [p for _, group in clouds for p in group]
    lo, hi = geo.bounds(points)
    for panel, (a,b) in enumerate(((2,1),(2,0))):
        scale = min(1300/max(hi[a]-lo[a],.001), 320/max(hi[b]-lo[b],.001))
        for index, (_,group) in enumerate(clouds):
            color = bytes(colors[index % len(colors)])
            for point in group:
                x = round(50+(point[a]-lo[a])*scale)
                y = round(370+panel*400-(point[b]-lo[b])*scale)
                for dx,dy in ((0,0),(1,0),(-1,0),(0,1),(0,-1)):
                    if 0 <= x+dx < width and 0 <= y+dy < height:
                        at=((y+dy)*width+x+dx)*3
                        pixels[at:at+3]=color
    def chunk(kind,data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    raw=b''.join(b'\0'+pixels[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
    return [lo,hi]


def main():
    admission=read(OUT/'storage.json')
    if not admission.get('generated') or admission['planned_bytes'] < 32*1024*1024:
        raise ValueError('Complete StorageBudget admission required')
    if (OUT/'preview-receipt.json').exists():
        raise ValueError('Frozen preview receipt already exists')
    new=ROOT/'reports/coordinator-20261030-magazines45-policy04/new-bindings.json'
    m1=ROOT/'handoff/tasks/WEAPONS/20261030-support43/m1-reference-bindings.json'
    opening=RECOVERY/'overnight426/smg/opening-level-reference-bindings.json'
    type5_proof=RECOVERY/'overnight426/smg/opening-level-mesh-bindings.json'
    type5=next(w for w in read(type5_proof)['weapons'] if w['native_name']=='Garand')
    ts=type5['states'][0]['meshes'][0]['geometry_sources'][0]
    jobs=[('G3',new,0,COMMON),('Thompson',new,4,COMMON),('M1Garand',m1,1,COMMON),
          ('Type5',opening,6,dict(archive=ts['archive'],archive_index_sha256=ts['index_sha256']))]
    pool=ArchivePool(GAME,COMMON['archive'],COMMON['archive_index_sha256'])
    def resource(source,name,kind,expected):
        archive=pool(GAME/source['archive'])
        if archive.index_sha256!=source['archive_index_sha256']:
            raise ValueError('Exact archive index changed')
        return geo.exact_resource(archive,name,kind,expected)
    poses=[]
    for family,path,index,mesh_source in jobs:
        b=read(path)['profiles'][index]
        if geo.digest({k:v for k,v in b.items() if k!='binding_digest'})!=b['binding_digest']:
            raise ValueError('Source binding content changed')
        meshes=[m for m in b['meshes'] if m['configured_mesh_path']==b['configured_mesh_path']]
        if len(meshes)!=1 or b['state_count']!=1 or b['weapon_bone']!='jntWpn_1':
            raise ValueError('Exact mesh/state/root required')
        mesh=meshes[0];lod=next(v for v in mesh['geometry']['lods'] if v['lod']==0)
        raw_mesh=resource(mesh_source,mesh['mesh_resource'],'SkinnedMeshSet',mesh['geometry']['mesh_sha256'])
        raw_lod=resource(mesh_source,lod['data_resource'],'MeshData',lod['data_sha256'])
        chain=b['animation_chain']
        sk=Skeleton(resource(b['skeleton_source'],chain['skeleton_resource'],'GrannyModel',b['skeleton_sha256']))
        clip=Clip(resource(b['animation_source'],chain['animation_resource'],'GrannyAnimation',b['animation_sha256']))
        if sk.fingerprint!=b['rig_fingerprint']:
            raise ValueError('Exact rig required')
        parts=geo.rigid_parts(raw_lod,geo.metadata(raw_mesh)[0],sk,True)
        clouds=[];gaps=[];part_rows=[]
        for bone,part in parts.items():
            try:
                pose=clip.evaluate(sk,0.,[bone]);transform=pose['weapon_relative'][bone]
                if part['points']:
                    clouds.append((bone,[geo.point(p,transform) for p in part['points']]))
                part_rows.append(dict(bone=bone,triangles=part['triangles'],mixed_triangles=part['mixed_triangles'],
                                      weapon_from_part=transform,evaluation_status=pose['bone_evaluation_status'][bone]))
            except (ValueError,KeyError) as error:
                gaps.append(dict(bone=bone,reason=str(error),omitted_triangles=part['triangles']))
        image=OUT/(family+'.png');bounds=png(image,clouds)
        poses.append(dict(family=family,source_binding=pin(path),profiles_index=index,binding_digest=b['binding_digest'],
            weapon=b['weapon'],configured_mesh_path=b['configured_mesh_path'],mesh_sha256=mesh['geometry']['mesh_sha256'],
            lod_sha256=lod['data_sha256'],mesh_source=mesh_source,skeleton_sha256=sk.sha256,rig_fingerprint=sk.fingerprint,
            animation_sha256=clip.sha256,weapon_bone=b['weapon_bone'],coordinate_convention=b['coordinate_convention'],
            geometry_image=pin(image),reference_time_seconds=0.,parts=part_rows,gaps=gaps,bounds_m=bounds,
            mixed_triangle_note='Mixed-weight triangles omitted; per-part mixed counts may overlap.'))
    report=dict(schema='fvr.bc2.axis_completion_previews.v1',renderer=pin(Path(__file__)),
        tools={name:pin(ROOT/'tools'/name) for name in ['bc2_authored_magazine_geometry.py','bc2_mesh_geometry.py','bc2_weapon_animation_pipeline.py','bc2_magazine_contact_batch.py']},
        type5_source_mesh_proof=pin(type5_proof),selected_resource_bytes=pool.bytes,
        archives=[dict(path=key,index_sha256=a.index_sha256) for key,a in pool.archives.items()],
        projection='Upper: horizontal canonical +Z right and +Y up. Lower: canonical +Z right and +X up.',
        native_verified=False,headset_accepted=False,poses=poses)
    (OUT/'preview-receipt.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(previews=len(poses),selected_resource_bytes=pool.bytes,gaps=sum(len(p['gaps']) for p in poses))))


if __name__=='__main__':
    main()
