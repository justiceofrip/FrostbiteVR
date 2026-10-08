"""Derive shell draw fingerprints from the named installed BC2 archive only.
No original mesh/vertex/index bytes are written to disk.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from inspect_bc2_mesh_asset import Archive, mesh_data

PREFIX = 'Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh'
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211

def fnv(data):
    value = FNV_OFFSET
    for byte in data: value = ((value ^ byte) * FNV_PRIME) & ((1 << 64)-1)
    return f'{value:016x}'

def derive(path):
    archive = Archive(path)
    name = PREFIX+'_lod0_data.res'
    data = archive.read_selected([name])[name]
    mesh = mesh_data(data)
    first = len(data)-mesh['vertex_bytes']-mesh['index_bytes']
    results=[]
    for section in mesh['sections']:
        if section['name'] not in ('jntWpn_7_Ammo_Brass','jntWpn_7_ammo_plastic'): continue
        count = section['triangles']*3
        indices = struct.unpack_from('<'+'H'*count, data, first+mesh['vertex_bytes']+section['first_index']*2)
        records = [data[first+section['vertex_offset']+i*section['stride']:first+section['vertex_offset']+i*section['stride']+16] for i in indices]
        expanded = b''.join(records)
        position = b''.join(v[:8] for v in records)
        results.append(dict(section=section['name'],index_count=count,stride=section['stride'],
            fingerprint_format='ordered indexed packed position8+skin8; FNV1a64',
            vertex_skin_fnv1a64=fnv(expanded),position_fnv1a64=fnv(position),
            expanded_sha256=hashlib.sha256(expanded).hexdigest(),
            asset_first_index=section['first_index'],asset_vertex_offset=section['vertex_offset'],
            local_skin_palette=section['skin_palette_ids'],all_vertices_single_weight=section['all_vertices_single_weight']))
    if len(results)!=2: raise ValueError('Expected exact SPAS shell sections')
    return dict(schema='fvr.bc2.shell_draw_fingerprints',version=1,read_only=True,
        archive=str(Path(path)),archive_index_sha256=archive.index_sha256,
        resource=name,resource_sha256=hashlib.sha256(data).hexdigest(),sections=results,
        limits=['Geometry match does not establish selected first-person owner or skin transform/remap.',
                'Topology/triangle order or vertex repacking changes beyond index relocation remain unverified.',
                'No source mesh bytes or triangles are exported.'])

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--archive',required=True)
    parser.add_argument('--output',required=True)
    args=parser.parse_args()
    result=derive(args.archive)
    Path(args.output).write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps(result,indent=2))
