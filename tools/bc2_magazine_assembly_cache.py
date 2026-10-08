"""Bake a proven rigid magazine subtree into the existing private BC2PROP1.

Uses the existing root-local Float3/single-palette renderer contract. Leaf
records keep using bc2_body_ammo_assets unchanged. No registry or game access.
The caller reads installed exact blobs; proprietary geometry stays local.
"""
import struct
from bc2_body_ammo_assets import MAGIC,MAX_CACHE,fnv,text
from bc2_mesh_geometry import metadata,geometry
from bc2_weapon_animation_pipeline import identity,canonical,multiply
from bc2_authored_magazine_geometry import rigid_parts,point
from bc2_authored_grip_bindings import digest
from bc2_magazine_assembly import derive_assembly,validate_receipt
from inspect_bc2_mesh_asset import bone_hash,sha

def derive_cache(mesh_raw,lod_data,skeleton,static_clip,reload_clip,profile):
    if (profile.get('profile_digest')!=digest({k:v for k,v in profile.items() if k!='profile_digest'}) or
        profile.get('runtime_admitted') is not False or profile.get('mesh_sha256')!=sha(mesh_raw) or
        profile.get('lod_sha256')!=sha(lod_data) or profile.get('skeleton_sha256')!=skeleton.sha256 or
        profile.get('static_clip_sha256')!=static_clip.sha256 or
        profile.get('reload_clip',{}).get('sha256')!=reload_clip.sha256):raise ValueError('Assembly cache exact source mismatch')
    receipt=profile['assembly'];validate_receipt(receipt,profile)
    root=receipt['root'];names=[root,*[m['bone'] for m in receipt['members']]]
    closed=static_clip.evaluate(skeleton,0.,names)
    lod=metadata(mesh_raw)[0];parts=rigid_parts(lod_data,lod,skeleton,True)
    _,measured=derive_assembly(parts,skeleton,closed,reload_clip,root)
    if measured!=receipt:raise ValueError('Assembly cache measured receipt changed')
    transforms={root:identity(),**{m['bone']:m['item_from_bone'] for m in receipt['members']}}
    known={bone_hash(n):n for n in skeleton.names}
    inverse={n:canonical(b['InverseWorldTransform']) for n,b in zip(skeleton.names,skeleton.bones)}
    ss=geometry(lod_data,lod,known);vb=sum(s['vertices']*s['stride'] for s in ss);ib=sum(s['triangles']*6 for s in ss)
    start=len(lod_data)-vb-ib;rows=[];records=[];total=0;excluded=[]
    for s in ss:
        if s['name'].endswith('_ZOnly'):excluded.append(s['name']);continue
        if not set(s['bone_names'])&set(names):continue
        half=s['stride'] in (16,48);posbytes=8 if half else 12
        raw_indices=struct.unpack_from('<'+'H'*(s['triangles']*3),lod_data,start+vb+s['first_index']*2)
        packed=[]
        for offset in range(0,len(raw_indices),3):
            tri=[];owners=[]
            for index in raw_indices[offset:offset+3]:
                at=start+s['vertex_offset']+index*s['stride'];ids=lod_data[at+posbytes:at+posbytes+4];weights=lod_data[at+posbytes+4:at+posbytes+8]
                if len(weights)!=4 or sum(w>0 for w in weights)!=1 or 255 not in weights:raise ValueError('Assembly cache nonrigid triangle')
                bone=known[lod['palette'][s['palette'][ids[weights.index(255)]]]];owners.append(bone)
                if bone in transforms:
                    p=struct.unpack_from('<3e' if half else '<3f',lod_data,at)
                    local=point((p[0],p[1],-p[2]),multiply(inverse[bone],transforms[bone]))
                    if max(map(abs,local))>1.5:raise ValueError('Assembly cache local point bound')
                    # Existing renderer reflects Z, with identity inverse bind.
                    tri.append(struct.pack('<3f8B',local[0],local[1],-local[2],0,0,0,0,255,0,0,0))
            if len(set(owners))!=1:raise ValueError('Assembly cache cross-part triangle')
            if owners[0] in transforms:packed.extend(tri)
        if not packed:continue
        if len(packed)%3 or len(packed)>32768*3:raise ValueError('Assembly cache triangle bound')
        vertices=b''.join(packed);size=2 if len(packed)<=65536 else 4
        indices=struct.pack('<'+('H' if size==2 else 'I')*len(packed),*range(len(packed)))
        rows.append(dict(section=s['name'],count=len(packed),stride=20,palette_size=1,part_palette_index=0,
                         position='Float3',vertex_skin_hash=f'{fnv(vertices):016x}',
                         position_hash=f'{fnv(b"".join(v[:12] for v in packed)):016x}',part_triangles=len(packed)//3))
        records.append(text(s['name'])+struct.pack('<5I',len(vertices),len(indices),size,0,0)+vertices+indices);total+=len(packed)//3
    if not 0<len(rows)<=8 or total>32768:raise ValueError('Assembly cache section/total bound')
    rig=int(skeleton.fingerprint.split(':')[1],16);asset=profile['native_asset_name'];mesh=profile['configured_mesh_path']
    row=dict(asset=asset,mesh=mesh,part=root,rig_fingerprint=rig,inverse_bind=identity(),sections=rows,
             geometry_profile_digest=profile['profile_digest'],assembly_digest=receipt['assembly_digest'],
             mesh_sha256=sha(mesh_raw),lod_sha256=sha(lod_data),skeleton_sha256=skeleton.sha256,
             static_clip_sha256=static_clip.sha256,reload_clip_sha256=reload_clip.sha256,
             excluded_depth_only_sections=excluded,rigid_assembly=True,render_only=True,native_admission=False)
    payload=MAGIC+struct.pack('<I',1)+text(asset)+text(mesh)+text(root)+struct.pack('<QI',rig,len(rows))+b''.join(records)
    if len(payload)>MAX_CACHE:raise ValueError('Assembly cache size bound')
    return row,payload

def derive_reviewed_cache(game,pool,reviewed):
    """Rebuild one assembly catalog row from its exact installed source pins."""
    from bc2_weapon_animation_pipeline import Skeleton,Clip
    source=reviewed.get('assembly_source',{})
    if set(source)!={'profile','mesh','lod','skeleton','static','reload'}:raise ValueError('Complete assembly catalog sources required')
    def load(label):
        ref=source[label]
        if set(ref)!={'archive','archive_index_sha256','resource','sha256'}:raise ValueError('Assembly source pin fields')
        archive=pool(game/ref['archive'])
        if archive.index_sha256!=ref['archive_index_sha256']:raise ValueError('Assembly source archive changed')
        raw=archive.read_selected([ref['resource']])[ref['resource']]
        if sha(raw)!=ref['sha256']:raise ValueError('Assembly source resource changed')
        return raw
    row,packed=derive_cache(load('mesh'),load('lod'),Skeleton(load('skeleton')),Clip(load('static')),Clip(load('reload')),source['profile'])
    row['assembly_source']=source
    if row!=reviewed:raise ValueError('Assembly catalog differs from installed sources')
    return row,packed[12:]
