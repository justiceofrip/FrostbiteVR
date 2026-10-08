"""Prepare private rigid-part cache from installed BC2 resources; no native access.

The public profile catalog contains only resource names, hashes, rig and inverse
bind metadata. The generated .fvrprop contains game geometry and must stay local.
"""
from __future__ import annotations
import argparse,hashlib,json,struct
from pathlib import Path
from inspect_bc2_mesh_asset import Archive,bone_hash,sha
from bc2_mesh_geometry import metadata,geometry
from bc2_weapon_animation_pipeline import Skeleton,canonical

MAGIC=b'BC2PROP1'
MAX_CACHE=16*1024*1024
SKELETON_ARCHIVE='Dist/win32/levels/sp_common/level-00.fbrb'
SKELETON_RESOURCE='Characters/Skeletons/ske01.res'
SKELETON_SHA='d1d713cc7301bc5ae2b011a0991ca03c2fcbb26c1187900f6fd129533d173c09'
# Data-only seeds. New exact meshes/parts use the same extractor/cache/runtime.
SPECS=(
 ('XM8_sp_s','Dist/win32/async/weapon/sp_rgl_xm8_scoped-00.fbrb','Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh','jntWpn_6'),
 ('SPAS12_sp','Dist/win32/async/weapon/ul_shg_spas-12-00.fbrb','Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh','jntWpn_7'),
 ('AEK971_sp','Dist/win32/async/weapon/ai_rif_aek971-00.fbrb','Objects/Weapons/Handheld/RU_rgl_AEK971/RU_rgl_AEK971_Mesh','jntWpn_6'),
)
def fnv(data):
    value=14695981039346656037
    for byte in data:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return value
def text(value):
    raw=value.encode('ascii')
    if not 0<len(raw)<=512 or any(c<32 or c>126 for c in raw):raise ValueError('Invalid cache name')
    return struct.pack('<H',len(raw))+raw
def archive(game,path):
    resolved=(game/path).resolve()
    if not resolved.is_relative_to(game.resolve()):raise ValueError('Archive outside installation')
    return Archive(resolved)
def derive(game,specs=SPECS):
    sk=archive(game,SKELETON_ARCHIVE);raw=sk.read_selected([SKELETON_RESOURCE])[SKELETON_RESOURCE]
    if sha(raw)!=SKELETON_SHA:raise ValueError('Installed skeleton content changed')
    skeleton=Skeleton(raw);known={bone_hash(n):n for n in skeleton.names}
    if len(known)!=len(skeleton.names):raise ValueError('Bone hash collision')
    rows=[];payload=bytearray(MAGIC+struct.pack('<I',len(specs)))
    for asset,path,mesh,part in specs:
        a=archive(game,path);names=[mesh+'.res',mesh+'_lod0_data.res'];blobs=a.read_selected(names)
        lod=metadata(blobs[names[0]])[0];data=blobs[names[1]];ss=geometry(data,lod,known)
        vb_size=sum(s['vertices']*s['stride'] for s in ss);ib_size=sum(s['triangles']*6 for s in ss);start=len(data)-vb_size-ib_size
        if start<0 or not 0<vb_size<=4*1024*1024:raise ValueError('Vertex range bound')
        vertices=data[start:start+vb_size];part_rows=[];part_bytes=[]
        for s in ss:
            if s['name'].endswith('_ZOnly') or part not in s['bone_names']:continue
            palette=[known[lod['palette'][n]] for n in s['palette']];part_index=palette.index(part)
            half=s['stride'] in (16,48);posbytes=8 if half else 12
            indices=data[start+vb_size+s['first_index']*2:start+vb_size+(s['first_index']+s['triangles']*3)*2]
            ids=struct.unpack('<'+'H'*(len(indices)//2),indices);skin=bytearray();positions=bytearray();owners=[]
            for n in ids:
                at=s['vertex_offset']+n*s['stride'];v=vertices[at:at+posbytes+8]
                if len(v)!=posbytes+8:raise ValueError('Vertex range invalid')
                skin+=v;positions+=v[:posbytes];weights=v[posbytes+4:posbytes+8]
                if sum(w>0 for w in weights)!=1 or 255 not in weights:raise ValueError('Nonrigid weighted section')
                owners.append(v[posbytes+weights.index(255)])
            count=0
            for n in range(0,len(owners),3):
                if len(set(owners[n:n+3]))!=1:raise ValueError('Cross-part triangle')
                count+=owners[n]==part_index
            if not count:continue
            part_rows.append(dict(section=s['name'],count=len(ids),stride=s['stride'],palette_size=len(palette),part_palette_index=part_index,
                position='Half4' if half else 'Float3',vertex_skin_hash=f'{fnv(skin):016x}',position_hash=f'{fnv(positions):016x}',part_triangles=count))
            part_bytes.append(text(s['name'])+struct.pack('<5I',len(vertices),len(indices),2,s['vertex_offset'],0)+vertices+indices)
        if not 0<len(part_rows)<=8:raise ValueError('Part section count')
        inverse=canonical(skeleton.bones[skeleton.names.index(part)]['InverseWorldTransform']);rig=int(skeleton.fingerprint.split(':')[1],16)
        rows.append(dict(asset=asset,archive=path,archive_index_sha256=a.index_sha256,mesh=mesh,mesh_sha256=sha(blobs[names[0]]),
            lod_sha256=sha(data),part=part,rig_fingerprint=rig,inverse_bind=inverse,sections=part_rows))
        payload+=text(asset)+text(mesh)+text(part)+struct.pack('<QI',rig,len(part_rows))+b''.join(part_bytes)
        if len(payload)>MAX_CACHE:raise ValueError('Private cache size limit')
    return rows,bytes(payload)
def derive_all(game,include_equipment=False):
    rows,payload=derive(game)
    if include_equipment:
        from bc2_body_equipment_assets import derive_equipment
        equipment,records=derive_equipment(game);rows+=equipment
        payload=MAGIC+struct.pack('<I',len(rows))+payload[12:]+records
    if len(payload)>MAX_CACHE:raise ValueError('Private cache size limit')
    return rows,payload

def catalog_header(rows):
    lines=['#pragma once','#include "Bc2BodyAmmoAssetProfile.h"','#include <array>','namespace fvr::bc2 {']
    for n,row in enumerate(rows):
        sections=row['sections'];lines.append(f'inline constexpr std::array<BeltPropSectionProfile,{len(sections)}> BodyAmmoAssetSections{n}'+'{{')
        for s in sections:
            values=','.join(str(s[k]) for k in ('count','stride','palette_size','part_palette_index'))
            lines.append('{'+','.join(json.dumps(row[k]) for k in ('asset','mesh','part'))+','+json.dumps(s['section'])+',{'+values+',graphics::RigidPropPosition::'+s['position']+',0x'+s['vertex_skin_hash']+'ull,0x'+s['position_hash']+'ull,'+str(s['part_triangles'])+'}},')
        lines.append('}};')
    lines.append(f'inline constexpr std::array<BodyAmmoAssetProfile,{len(rows)}> BodyAmmoAssetProfiles'+'{{')
    def number(x):
        value=format(x,'.9g');return value+('f' if any(c in value for c in '.eE') else '.f')
    for n,row in enumerate(rows):
        matrix='{{{'+','.join('{'+','.join(number(x) for x in row['inverse_bind'][r*4:r*4+4])+'}' for r in range(4))+'}}}'
        lines.append('{'+','.join(json.dumps(row[k]) for k in ('asset','mesh','part'))+',0x'+f"{row['rig_fingerprint']:016x}"+'ull,'+matrix+',BodyAmmoAssetSections'+str(n)+'},')
    return '\n'.join(lines+['}};','}'])+'\n'
def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--game',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--catalog',type=Path,help='Reviewed public metadata catalog; ordinary preparation verifies it')
    parser.add_argument('--derive-catalog',type=Path,help='Developer-only metadata derivation output')
    parser.add_argument('--header',type=Path,help='Developer-only generated metadata header')
    parser.add_argument('--body-equipment',action='store_true',help='Include reviewed closed weapons for back display')
    args=parser.parse_args(argv);rows,payload=derive_all(args.game,args.body_equipment)
    if args.catalog and json.loads(args.catalog.read_text())['profiles']!=rows:raise ValueError('Installed profile data differs from reviewed catalog')
    if not args.catalog and not args.derive_catalog:raise ValueError('Require reviewed catalog or explicit developer derivation')
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(payload)
    if args.derive_catalog:args.derive_catalog.parent.mkdir(parents=True,exist_ok=True);args.derive_catalog.write_text(json.dumps({'schema':'fvr.bc2.body_ammo_assets','version':1,'profiles':rows},indent=2)+'\n')
    if args.header:args.header.parent.mkdir(parents=True,exist_ok=True);args.header.write_text(catalog_header(rows))
    print(json.dumps({'private_cache':str(args.output),'bytes':len(payload),'sha256':sha(payload),'parts':len(rows),'sections':sum(len(r['sections']) for r in rows),
        'triangles':sum(s['part_triangles'] for r in rows for s in r['sections']),'native_or_gpu_calls':False,'public_asset_export':False}))
if __name__=='__main__':main()
