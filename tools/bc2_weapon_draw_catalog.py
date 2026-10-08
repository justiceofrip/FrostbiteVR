"""Derive bounded indexed-draw fingerprints, never game vertex/index bytes.

An exact geometry match does not bind a selected item, rig pose, animation clip,
or GPU skin remap. Different resources/variants with equal hashes stay ambiguous.
"""
import argparse
import json
from pathlib import Path
import struct
from inspect_bc2_mesh_asset import Archive, sha
from bc2_mesh_geometry import metadata, geometry

MAX_SECTIONS = 2048

def fnv(data):
    value = 14695981039346656037
    for byte in data: value = ((value ^ byte) * 1099511628211) & ((1 << 64)-1)
    return f'{value:016x}'

def text(value, maximum):
    if not isinstance(value, str) or not 0 < len(value) <= maximum or any(not 32 <= ord(c) <= 126 for c in value):
        raise ValueError('Invalid catalog text')
    return value

def derive_sections(resource, mesh_bytes, data_resource, data, lod):
    # The existing decoder proves contiguous buffers, indexed bounds and palette
    # correspondence before any signature is emitted. It rejects unknown layouts.
    sections = geometry(data, lod, {})
    vb = sum(s['vertices']*s['stride'] for s in sections)
    ib = sum(s['triangles']*6 for s in sections)
    start = len(data)-vb-ib
    result = []
    for number, s in enumerate(sections):
        count = s['triangles']*3
        if count > 100000: raise ValueError('Draw index bound')
        posbytes = 8 if s['stride'] in (16,48) else 12
        indices = struct.unpack_from('<'+'H'*count, data, start+vb+s['first_index']*2)
        records = [data[start+s['vertex_offset']+i*s['stride']:start+s['vertex_offset']+i*s['stride']+posbytes+8] for i in indices]
        expanded = b''.join(records)
        positions = b''.join(record[:posbytes] for record in records)
        result.append(dict(resource=text(resource,512), variant=sha(mesh_bytes+data),
            section=text(s['name'],256), lod=lod['lod'], section_index=number,
            mesh_sha256=sha(mesh_bytes), data_resource=text(data_resource,512), data_sha256=sha(data),
            index_count=count, stride=s['stride'], position_format='Half4' if posbytes==8 else 'Float3',
            palette_size=len(s['palette']), palette_bone_hashes=[f"{lod['palette'][p]:08x}" for p in s['palette']],
            vertex_skin_fnv1a64=fnv(expanded), position_fnv1a64=fnv(positions), expanded_sha256=sha(expanded),
            native_association_verified=False, shader_skin_remap_verified=False))
    return result

def archive_sections(path, label):
    archive = Archive(path)
    entries = {e.name:e for e in archive.entries if e.flags==65536}
    meshes = [e.name for e in entries.values() if e.kind=='SkinnedMeshSet' and e.name.endswith('.res')]
    names = set(meshes)
    for mesh in meshes:
        for lod in range(4):
            name = mesh[:-4]+f'_lod{lod}_data.res'
            if name in entries and entries[name].kind=='MeshData': names.add(name)
    raw = archive.read_selected(names)
    sections, gaps = [], []
    for mesh in sorted(meshes):
        try:
            for lod in metadata(raw[mesh]):
                name = mesh[:-4]+f"_lod{lod['lod']}_data.res"
                if name not in raw: raise ValueError('Missing exact LOD MeshData')
                sections.extend(derive_sections(mesh,raw[mesh],name,raw[name],lod))
        except (ValueError, KeyError, struct.error, UnicodeError) as exc:
            gaps.append(dict(archive=label,resource=mesh,reason=str(exc)))
    provenance = dict(archive=label,index_sha256=archive.index_sha256)
    for section in sections: section['sources']=[provenance.copy()]
    return sections,gaps

def combine(groups, gaps=()):
    unique = {}
    for group in groups:
        for section in group:
            key = tuple(section[k] for k in ('resource','variant','lod','section_index'))
            if key in unique:
                existing = unique[key]
                if {k:v for k,v in existing.items() if k!='sources'} != {k:v for k,v in section.items() if k!='sources'}:
                    raise ValueError('Conflicting exact section identity')
                for source in section['sources']:
                    if source not in existing['sources']: existing['sources'].append(source)
            else: unique[key] = dict(section,sources=list(section['sources']))
    if not 0 < len(unique) <= MAX_SECTIONS: raise ValueError('Catalog section bound')
    rows = [unique[k] for k in sorted(unique)]
    for row in rows: row['sources'].sort(key=lambda s:(s['archive'],s['index_sha256']))
    return dict(schema='fvr.bc2.weapon_draw_catalog',version=1,read_only=True,
        native_association_verified=False,sections=rows,gaps=list(gaps),limits=[
        'Matches ordered indexed position and local skin bytes, not UV/material/texture or runtime remapping.',
        'Equal signatures across resources or variants are ambiguous; no first-match asset selection.',
        'Selected-item labels, frame proximity and shared skeletons are not pose-to-mesh association.',
        'No game vertex/index bytes are exported; no profile or native feature is enabled.'])

def header(catalog):
    rows = catalog['sections']
    if not 0 < len(rows) <= MAX_SECTIONS: raise ValueError('Catalog section bound')
    lines=['#pragma once','#include "Bc2WeaponDrawCatalog.h"','namespace fvr::bc2 {',
           'inline std::span<const WeaponDrawSection> GeneratedWeaponDrawCatalog(){',
           ' static const WeaponDrawSection data[]{']
    for s in rows:
        fields=','.join(json.dumps(text(s[k],limit),ensure_ascii=True) for k,limit in [('resource',512),('variant',128),('section',256)])
        if s['position_format'] not in ('Half4','Float3'): raise ValueError('Position format')
        for key,low,high in [('lod',0,3),('section_index',0,31),('index_count',3,100000),('stride',16,256),('palette_size',1,256)]:
            if type(s[key]) is not int or not low<=s[key]<=high: raise ValueError('Catalog numeric bound')
        for key in ('vertex_skin_fnv1a64','position_fnv1a64'):
            if len(s[key])!=16 or any(c not in '0123456789abcdef' for c in s[key]) or int(s[key],16)==0: raise ValueError('Fingerprint')
        lines.append('  {'+fields+f",{s['lod']},{s['section_index']},"+'{'+
            f"{s['index_count']},{s['stride']},{s['palette_size']},0,graphics::RigidPropPosition::{s['position_format']},0x{s['vertex_skin_fnv1a64']}ull,0x{s['position_fnv1a64']}ull,0"+'},0},')
    return '\n'.join(lines+[' };return data;','}','} // namespace fvr::bc2',''])

def run(root, archives):
    root=Path(root).resolve();groups=[];gaps=[]
    if not archives or len(archives)>512: raise ValueError('Explicit archive list bound')
    for relative in sorted(set(archives)):
        path=(root/relative).resolve()
        if not path.is_relative_to(root) or not path.is_file(): raise ValueError('Archive outside install or missing')
        label=path.relative_to(root).as_posix()
        try:
            group,missing=archive_sections(path,label);groups.append(group);gaps.extend(missing)
        except (ValueError,KeyError,struct.error,UnicodeError) as exc:
            gaps.append(dict(archive=label,reason=str(exc)))
    return combine(groups,gaps)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root',required=True)
    parser.add_argument('--archive',action='append',required=True,help='Install-relative FBRB path; repeat for a batch')
    parser.add_argument('--output',required=True)
    parser.add_argument('--header',help='Optional compiled diagnostic metadata catalog, never runtime admission')
    args=parser.parse_args();result=run(args.game_root,args.archive)
    Path(args.output).write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    if args.header: Path(args.header).write_text(header(result),encoding='utf8')
    print(json.dumps(dict(sections=len(result['sections']),gaps=len(result['gaps']),native_association_verified=False)))
