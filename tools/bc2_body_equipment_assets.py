"""Bake installed closed weapon geometry for local body equipment display.

This is display data, not reload/grip/weapon capability admission. Game vertices
remain in the private cache. Public metadata contains only identities and hashes.
"""
from __future__ import annotations
import json, math, struct
from bc2_body_ammo_assets import archive, text, fnv, SKELETON_ARCHIVE, SKELETON_RESOURCE, SKELETON_SHA
from bc2_weapon_animation_pipeline import Skeleton, Clip, canonical, multiply, identity
from bc2_authored_magazine_geometry import point, unit, sub, cross, dot
from bc2_mesh_geometry import metadata, geometry
from inspect_bc2_mesh_asset import bone_hash, sha

PART='body_holstered_weapon'
SPECS=(
 ('XM8_sp_s','Dist/win32/async/weapon/sp_rgl_xm8_scoped-00.fbrb','Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh','Animations/Weapons/Handheld/US_rgl_XM8/1P/HandsIkPose.res'),
 ('SPAS12_sp','Dist/win32/async/weapon/ul_shg_spas-12-00.fbrb','Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh','Animations/Weapons/Handheld/UL_shg_SPAS12/1P/HandsIkPose.res'),
 ('AEK971_sp', 'Dist/win32/async/weapon/ai_rif_aek971-00.fbrb', 'Objects/Weapons/Handheld/RU_rgl_AEK971/RU_rgl_AEK971_Mesh', 'Animations/Weapons/Handheld/RU_rgl_AEK971/1P/HandsIkPose.res'),
 ('AEK971', 'Dist/win32/async/weapon/ai_rif_aek971-00.fbrb', 'Objects/Weapons/Handheld/RU_rgl_AEK971/RU_rgl_AEK971_Mesh', 'Animations/Weapons/Handheld/RU_rgl_AEK971/1P/HandsIkPose.res'),
 ('XM8C', 'Dist/win32/async/weapon/us_rif_xm8c-00.fbrb', 'Objects/Weapons/Handheld/US_rif_XM8c/US_rif_XM8c_Mesh', 'Animations/Weapons/Handheld/US_rif_XM8_C/1P/HandsIKPose.res'),
 ('F2000', 'Dist/win32/async/weapon/ai_rif_f2000-00.fbrb', 'Objects/Weapons/Handheld/BU_rif_F2000/BU_rif_F2000_Mesh', 'Animations/Weapons/Handheld/BU_rif_F2000/1P/HandsIkPose.res'),
 ('M416', 'Dist/win32/async/weapon/ul_rif_hk416-00.fbrb', 'Objects/Weapons/Handheld/UL_rif_HK416/UL_rif_HK416_Mesh', 'Animations/Weapons/Handheld/UL_rif_HK416/1P/HandsIKPose.res'),
 ('SCAR', 'Dist/win32/async/weapon/ai_smg_fnscarl-00.fbrb', 'Objects/Weapons/Handheld/UL_rif_FNSCARL/UL_rif_FNSCARL_Mesh', 'Animations/Weapons/Handheld/UL_rif_FNSCARL/1P/HandsIKPose.res'),
 ('Mk14EBR', 'Dist/win32/async/weapon/bu_rif_mk14ebr-00.fbrb', 'objects/weapons/handheld/bu_rif_mk14ebr/bu_rif_mk14ebr_mesh', 'Animations/Weapons/Handheld/BU_rif_Mk14EBR/1P/HandsIkPose.res'),
 ('M16', 'Dist/win32/async/weapon/bu_rif_m16a2-00.fbrb', 'Objects/Weapons/Handheld/BU_rif_M16A2/BU_rif_M16A2_Mesh', 'Animations/Weapons/Handheld/BU_rif_M16A2/1P/HandsIkPose.res'),
 ('XM8', 'Dist/win32/async/weapon/ai_rif_xm8-00.fbrb', 'Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh', 'Animations/Weapons/Handheld/US_rgl_XM8/1P/HandsIkPose.res'),
 ('M16k', 'Dist/win32/async/weapon/bu_rif_m16a2-00.fbrb', 'Objects/Weapons/Handheld/BU_rif_M16A2/BU_rif_M16A2_Mesh', 'Animations/Weapons/Handheld/BU_rif_M16A2/1P/HandsIkPose.res'),
 ('PP2000', 'Dist/win32/async/weapon/bu_smg_pp2000-00.fbrb', 'Objects/Weapons/Handheld/BU_smg_PP2000/BU_smg_PP2000_Mesh', 'Animations/Weapons/Handheld/BU_smg_PP2000/1P/HandsIkPose.res'),
 ('UMP', 'Dist/win32/async/weapon/bu_smg_ump-00.fbrb', 'Objects/Weapons/Handheld/BU_smg_UMP/BU_smg_UMP_Mesh', 'Animations/Weapons/Handheld/BU_smg_UMP/1P/HandsIKPose.res'),
 ('9A91', 'Dist/win32/async/weapon/mec_rif_9a91-00.fbrb', 'Objects/Weapons/Handheld/MEC_rif_9A91/MEC_rif_9A91_Mesh', 'Animations/Weapons/Handheld/MEC_rif_9A91/1P/HandsIKPose.res'),
 ('UMPk', 'Dist/win32/async/weapon/bu_smg_ump-00.fbrb', 'Objects/Weapons/Handheld/BU_smg_UMP/BU_smg_UMP_Mesh', 'Animations/Weapons/Handheld/BU_smg_UMP/1P/HandsIKPose.res'),
 ('F2000_sp', 'Dist/win32/async/weapon/ai_rif_f2000-00.fbrb', 'Objects/Weapons/Handheld/BU_rif_F2000/BU_rif_F2000_Mesh', 'Animations/Weapons/Handheld/BU_rif_F2000/1P/HandsIkPose.res'),
 ('XM8_sp', 'Dist/win32/async/weapon/ai_rif_xm8-00.fbrb', 'Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh', 'Animations/Weapons/Handheld/US_rgl_XM8/1P/HandsIkPose.res'),
 ('SCAR_sp', 'Dist/win32/async/weapon/ai_smg_fnscarl-00.fbrb', 'Objects/Weapons/Handheld/UL_rif_FNSCARL/UL_rif_FNSCARL_Mesh', 'Animations/Weapons/Handheld/UL_rif_FNSCARL/1P/HandsIKPose.res'),
)

def holster_transform(grip, muzzle):
    """Shared presentation design: muzzle up, grip at the actual shoulder handle."""
    if len(grip)!=3 or len(muzzle)!=3 or not all(math.isfinite(x) for x in (*grip,*muzzle)) or sum(x*x for x in sub(muzzle,grip))<1e-8:
        raise ValueError('Invalid holster grip/muzzle')
    forward=unit(sub(muzzle,grip))
    basis=min(((0.,1.,0.),(0.,0.,1.),(1.,0.,0.)),key=lambda x:abs(dot(x,forward)))
    up=unit(sub(basis,[dot(basis,forward)*v for v in forward]));right=unit(cross(up,forward))
    # Source rows right/up/forward map to body right/back/up respectively.
    target=((1.,0.,0.),(0.,0.,-1.),(0.,1.,0.))
    result=identity()
    for i in range(3):
        for j in range(3):result[i*4+j]=sum(src[i]*dst[j] for src,dst in zip((right,up,forward),target))
    result[12:15]=[-sum(grip[i]*result[i*4+j] for i in range(3)) for j in range(3)]
    return result

def bake_section(data, section, lod, known, inverse, pose, vertex_start):
    """Bake complete weighted triangles; no hand/unknown bone can enter a gun."""
    s=section;positions=[]
    half=s['stride'] in (16,48);posbytes=8 if half else 12
    for n in range(s['vertices']):
        at=vertex_start+s['vertex_offset']+n*s['stride']
        p=struct.unpack_from('<3e' if half else '<3f',data,at);p=(p[0],p[1],-p[2])
        ids=data[at+posbytes:at+posbytes+4];weights=data[at+posbytes+4:at+posbytes+8]
        if len(weights)!=4 or sum(weights)!=255:raise ValueError('Skin weights must normalize')
        result=[0.,0.,0.]
        for i,w in zip(ids,weights):
            if not w:continue
            if i>=len(s['palette']) or s['palette'][i] not in lod['palette']:raise ValueError('Skin palette bounds')
            bone=known.get(lod['palette'][s['palette'][i]],'')
            if not bone.startswith('jntWpn') or bone not in pose:raise ValueError('Unknown/nonweapon closed bone: '+bone)
            v=point(p,multiply(inverse[bone],pose[bone]))
            result=[x+y*w/255 for x,y in zip(result,v)]
        if not all(math.isfinite(x) for x in result) or max(map(abs,result))>3:raise ValueError('Closed weapon bound exceeded')
        # Existing cache parser reflects native Z. Use its normal Float3+skin
        # encoding with a single rigid owner after baking, never native vertices.
        positions.append(struct.pack('<3f8B',result[0],result[1],-result[2],0,0,0,0,255,0,0,0))
    return positions

def derive_equipment(game,specs=SPECS,strict_anchors=False):
    """Strict enrollment additionally proves constant grip/muzzle anchors.
    Legacy preparation retains its reviewed authored frame-zero contract.
    This flag validates data; it grants no native hiding or gameplay authority.
    """
    sk=archive(game,SKELETON_ARCHIVE);raw=sk.read_selected([SKELETON_RESOURCE])[SKELETON_RESOURCE]
    if sha(raw)!=SKELETON_SHA:raise ValueError('Installed skeleton changed')
    skeleton=Skeleton(raw);known={bone_hash(n):n for n in skeleton.names}
    inverse={n:canonical(b['InverseWorldTransform']) for n,b in zip(skeleton.names,skeleton.bones)}
    rows=[];payload=bytearray()
    for asset,path,mesh,clip_name in specs:
        a=archive(game,path);names=[mesh+'.res',mesh+'_lod0_data.res',clip_name];blobs=a.read_selected(names)
        clip=Clip(blobs[clip_name]);lod=metadata(blobs[names[0]])[0];data=blobs[names[1]]
        sections=geometry(data,lod,known);vb=sum(s['vertices']*s['stride'] for s in sections);ib=sum(s['triangles']*6 for s in sections)
        start=len(data)-vb-ib
        required={n for s in sections if not s['name'].endswith('_ZOnly') for n in s['bone_names']}
        closed=clip.evaluate(skeleton,0.,sorted(required|{'RightHand','jntWpn_Flash'}))
        if any(closed['bone_evaluation_status'][n]!='static_authored_pose' for n in required):raise ValueError('Closed geometry requires constant authored transforms')
        if strict_anchors and any(closed['bone_evaluation_status'][n]!='static_authored_pose' for n in {'RightHand','jntWpn_Flash'}):raise ValueError('Grip/muzzle anchors require constant authored transforms')
        part_rows=[];part_bytes=[];total=0
        for s in sections:
            if s['name'].endswith('_ZOnly'):continue
            vertices=bake_section(data,s,lod,known,inverse,closed['weapon_relative'],start)
            indices=data[start+vb+s['first_index']*2:start+vb+(s['first_index']+s['triangles']*3)*2]
            ids=struct.unpack('<'+'H'*(len(indices)//2),indices)
            if len(ids)!=s['triangles']*3 or not ids or max(ids)>=len(vertices):raise ValueError('Index bounds')
            packed=b''.join(vertices);skin=b''.join(vertices[n] for n in ids);positions=b''.join(vertices[n][:12] for n in ids)
            part_rows.append(dict(section=s['name'],count=len(ids),stride=20,palette_size=1,part_palette_index=0,
                position='Float3',vertex_skin_hash=f'{fnv(skin):016x}',position_hash=f'{fnv(positions):016x}',part_triangles=s['triangles']))
            part_bytes.append(text(s['name'])+struct.pack('<5I',len(packed),len(indices),2,0,0)+packed+indices)
            total+=s['triangles']
        if not 0<len(part_rows)<=8 or total>32768:raise ValueError('Whole weapon geometry bound')
        rig=int(skeleton.fingerprint.split(':')[1],16)
        rows.append(dict(asset=asset,archive=path,archive_index_sha256=a.index_sha256,mesh=mesh,mesh_sha256=sha(blobs[names[0]]),
            lod_sha256=sha(data),part=PART,rig_fingerprint=rig,inverse_bind=identity(),sections=part_rows,
            display_only=True,closed_clip=clip_name,closed_clip_sha256=clip.sha256,closed_pose='static_authored_pose',
            holster_from_weapon=holster_transform(closed['weapon_relative']['RightHand'][12:15],closed['weapon_relative']['jntWpn_Flash'][12:15]),
            grip=closed['weapon_relative']['RightHand'][12:15],muzzle=closed['weapon_relative']['jntWpn_Flash'][12:15]))
        payload+=text(asset)+text(mesh)+text(PART)+struct.pack('<QI',rig,len(part_rows))+b''.join(part_bytes)
    return rows,bytes(payload)

def equipment_header(rows):
    # Exact complete configurations supersede legacy asset/base-mesh display
    # rows. Legacy cache records remain available for older compatible clients.
    exact={(r['asset'],r['mesh']) for r in rows if r.get('configuration_path')}
    rows=[r for r in rows if r.get('configuration_path') or (r['asset'],r['mesh']) not in exact]
    lines=['#pragma once','#include "fvr/math/StereoMath.h"','#include <array>','#include <cstdint>','#include <span>','#include <string_view>',
        'namespace fvr::bc2 {',
        '// Render-only installed closed-pose identities, never gameplay capability.',
        'struct BodyEquipmentProfile {const char* asset;const char* mesh;const char* part;std::uint64_t rig;math::Matrix4 modelToAnchor;const char* configurationPath=nullptr;std::span<const std::string_view> configuredMeshes{};};',
        ]
    for n,row in enumerate(rows):
        if row.get('configuration_path'):
            values=','.join(json.dumps(x) for x in row['configured_meshes'])
            lines.append(f'inline constexpr std::array<std::string_view,{len(row["configured_meshes"])}> BodyEquipmentMeshes{n}{{{values}}};')
    lines.append(f'inline constexpr std::array<BodyEquipmentProfile,{len(rows)}> BodyEquipmentProfiles'+'{{')
    def number(x):
        value=format(x,'.9g');return value+('f' if any(c in value for c in '.eE') else '.f')
    for n,row in enumerate(rows):
        matrix='{{{'+','.join('{'+','.join(number(x) for x in row['holster_from_weapon'][r*4:r*4+4])+'}' for r in range(4))+'}}}'
        lines.append('{'+','.join(json.dumps(row[k]) for k in ('asset','mesh','part'))+',0x'+f"{row['rig_fingerprint']:016x}"+'ull,'+matrix+(','+json.dumps(row['configuration_path'])+',BodyEquipmentMeshes'+str(n) if row.get('configuration_path') else '')+'},')
    return '\n'.join(lines+['}};','}'])+'\n'

