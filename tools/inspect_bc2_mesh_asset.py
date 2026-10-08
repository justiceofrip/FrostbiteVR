"""Bounded, read-only BC2 FBRB and first-person mesh evidence extraction.

Only reads explicitly named archives. Emits hashes, names, skin palettes and bounds;
never exports original assets, edits the installation or assigns a reload socket.
"""
from __future__ import annotations
import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import struct
import zlib

MAX_INDEX = 16 * 1024 * 1024
MAX_PAYLOAD = 512 * 1024 * 1024
MAX_SELECTED = 32 * 1024 * 1024


def sha(data): return hashlib.sha256(data).hexdigest()


def cstring(data, offset):
    if not 0 <= offset < len(data): raise ValueError('String offset outside table')
    end = data.find(b'\0', offset, min(len(data), offset + 2048))
    if end < 0: raise ValueError('Unterminated or excessive string')
    result = data[offset:end].decode('ascii')
    if any(ord(c) < 32 or ord(c) > 126 for c in result): raise ValueError('Non-ASCII name')
    return result, end + 1


def inflate(data, limit):
    decoder = zlib.decompressobj(31)
    plain = decoder.decompress(data, limit + 1)
    if len(plain) > limit or decoder.unconsumed_tail: raise ValueError('Inflation limit')
    if not decoder.eof or decoder.unused_data: raise ValueError('Incomplete or trailing gzip data')
    return plain


@dataclass(frozen=True)
class Entry:
    name: str
    kind: str
    flags: int
    offset: int
    size: int
    stored_size: int


class Archive:
    def __init__(self, path):
        self.path = Path(path)
        with self.path.open('rb') as stream:
            header = stream.read(8)
            if len(header) != 8 or header[:4] != b'FbRB': raise ValueError('Not an FBRB archive')
            length = int.from_bytes(header[4:], 'big')
            if not 0 < length <= MAX_INDEX: raise ValueError('Compressed index bound')
            compressed = stream.read(length)
            if len(compressed) != length: raise ValueError('Truncated index')
        self.index = inflate(compressed, MAX_INDEX)
        data = self.index
        if len(data) < 17: raise ValueError('Truncated metadata')
        version, name_size = struct.unpack_from('>2I', data)
        if version != 2 or name_size > len(data)-17: raise ValueError('Unsupported index format')
        names = data[8:8+name_size]
        at = 8+name_size
        count = struct.unpack_from('>I', data, at)[0]
        if not count or count > 100000 or at+4+count*24+5 != len(data): raise ValueError('Entry count/record bounds')
        self.entries = []
        seen = set()
        for i in range(count):
            n, flags, offset, size, packed, k = struct.unpack_from('>6I', data, at+4+i*24)
            name, _ = cstring(names, n); kind, _ = cstring(names, k)
            if not name or name in seen: raise ValueError('Empty or duplicate asset name')
            seen.add(name)
            self.entries.append(Entry(name, kind, flags, offset, size, packed))
        self.compressed = data[-5]
        self.payload_size = int.from_bytes(data[-4:], 'big')
        if self.compressed not in (0, 1) or self.payload_size > MAX_PAYLOAD: raise ValueError('Payload format/bound')
        for entry in self.entries:
            if entry.offset+entry.size > self.payload_size: raise ValueError('Asset outside payload')
            if entry.flags not in (0, 65536) or entry.size != entry.stored_size: raise ValueError('Unsupported per-entry encoding')
        self.payload_offset = 8 + length
        self.index_sha256 = sha(data)

    def read_selected(self, names):
        wanted = set(names)
        entries = [e for e in self.entries if e.name in wanted and e.flags == 65536]
        if len(entries) != len(wanted): raise ValueError('Missing or deleted selected asset')
        if sum(e.size for e in entries) > MAX_SELECTED: raise ValueError('Selection bound')
        output = {e.name: bytearray() for e in entries}
        cursor = 0
        compressed_hash = hashlib.sha256()
        decoder = zlib.decompressobj(31) if self.compressed else None
        with self.path.open('rb') as stream:
            stream.seek(self.payload_offset)
            while chunk := stream.read(65536):
                compressed_hash.update(chunk)
                pending = chunk
                while pending:
                    plain = decoder.decompress(pending, 1024*1024) if decoder else pending
                    pending = decoder.unconsumed_tail if decoder else b''
                    if cursor+len(plain) > self.payload_size: raise ValueError('Payload exceeds declared size')
                    for entry in entries:
                        low, high = max(cursor, entry.offset), min(cursor+len(plain), entry.offset+entry.size)
                        if low < high: output[entry.name].extend(plain[low-cursor:high-cursor])
                    cursor += len(plain)
                    if decoder and decoder.unused_data: raise ValueError('Trailing payload stream')
        if cursor != self.payload_size or (decoder and not decoder.eof): raise ValueError('Truncated payload')
        if any(len(output[e.name]) != e.size for e in entries): raise ValueError('Incomplete resource')
        self.payload_sha256 = compressed_hash.hexdigest()
        return {key: bytes(value) for key, value in output.items()}


def bone_hash(name):
    value = 5381
    for byte in name.lower().encode('ascii'): value = ((value*33)^byte)&0xffffffff
    return value


def bounds(points):
    if not points or not all(math.isfinite(v) for p in points for v in p): raise ValueError('Invalid positions')
    return {'minimum':[min(p[a] for p in points) for a in range(3)],
            'maximum':[max(p[a] for p in points) for a in range(3)]}


def mesh_data(data):
    # Only the observed PC skinned, uncompressed-position layout is accepted.
    if data[:6] != b'\x01\0\0\0\0\0' or len(data)<7 or not 1 <= data[6] <= 32:
        raise ValueError('Unsupported MeshData header')
    at = 7; sections = []
    for _ in range(data[6]):
        name, at = cstring(data, at)
        if at+22 > len(data): raise ValueError('Truncated section')
        triangles, vertices, first_index, vertex_offset, stride, topology, skin, count8, count16 = struct.unpack_from('<4I4BH',data,at)
        at += 22
        if not triangles or not vertices or topology != 3 or skin != 1 or stride not in (16,48) or not 0 < count8 == count16 <= 64:
            raise ValueError('Unsupported section layout')
        if at+count16*2 > len(data): raise ValueError('Truncated skin palette')
        palette = list(struct.unpack_from('<'+'H'*count16,data,at)); at += count16*2
        if len(set(palette)) != len(palette): raise ValueError('Duplicate palette entry')
        sections.append(dict(name=name, triangles=triangles, vertices=vertices,
                             first_index=first_index, vertex_offset=vertex_offset,
                             stride=stride, skin_palette_ids=palette))
    vertex_size = sum(s['vertices']*s['stride'] for s in sections)
    index_size = sum(s['triangles']*6 for s in sections)
    vertex_start = len(data)-vertex_size-index_size
    if vertex_start < at+12 or struct.unpack_from('<3I', data, vertex_start-12) != (vertex_size,index_size,0):
        raise ValueError('Buffer lengths do not corroborate section table')
    expected_vertex = expected_index = 0
    for section in sections:
        if section['vertex_offset'] != expected_vertex or section['first_index'] != expected_index:
            raise ValueError('Noncontiguous section ranges')
        first = vertex_start+expected_vertex
        positions=[]; used=set(); rigid=True
        for i in range(section['vertices']):
            v = first+i*section['stride']
            position=struct.unpack_from('<4e',data,v)
            if position[3] not in (-1,1): raise ValueError('Unsupported packed position component')
            positions.append(position[:3]); indices=data[v+8:v+12]; weights=data[v+12:v+16]
            if sum(weights)!=255: raise ValueError('Non-normalized skin weights')
            for slot, weight in zip(indices,weights):
                if weight:
                    if slot>=len(section['skin_palette_ids']): raise ValueError('Skin index outside section palette')
                    used.add(section['skin_palette_ids'][slot])
            if sum(w>0 for w in weights)!=1: rigid=False
        index_at=vertex_start+vertex_size+expected_index*2
        indices=struct.unpack_from('<'+'H'*(section['triangles']*3),data,index_at)
        if max(indices)>=section['vertices']: raise ValueError('Triangle outside section vertices')
        section.update(bind_position_bounds=bounds(positions), used_palette_ids=sorted(used),
                       all_vertices_single_weight=rigid)
        expected_vertex += section['vertices']*section['stride'];expected_index += section['triangles']*3
    return {'sections':sections,'vertex_bytes':vertex_size,'index_bytes':index_size,
            'vertex_layout':'half3 position + signed packed component, uint8x4 palette indices, uint8x4 normalized weights',
            'coordinate_space':'asset bind positions; native handedness and units not changed',
            'uninterpreted_draw_metadata_sha256':sha(data[at:vertex_start-12])}


def mesh_set(data, known_bones):
    if data[:5] != b'\x2a\0\0\0\0': raise ValueError('Unsupported MeshSet header')
    lod_settings, at = cstring(data,5)
    if at+8 > len(data): raise ValueError('Truncated MeshSet')
    count = struct.unpack_from('<I',data,at)[0];at+=4
    if count != 1: raise ValueError('Only first-person single LOD supported')
    flag, sections, skinned = struct.unpack_from('<3B',data,at);at+=3
    if flag != 0 or skinned != 1 or not 1<=sections<=32: raise ValueError('Unsupported LOD header')
    object_name,at=cstring(data,at)
    materials=[]
    for _ in range(sections):
        value,at=cstring(data,at);materials.append(value)
    if at+36>len(data): raise ValueError('Truncated LOD extent')
    extent=struct.unpack_from('<6f',data,at);resource_size,reserved,bone_count=struct.unpack_from('<3I',data,at+24);at+=36
    if reserved!=0 or not 0<bone_count<=256 or at+bone_count*8!=len(data): raise ValueError('Skin map bounds')
    if not all(math.isfinite(v) for v in extent) or any(extent[a]>extent[a+3] for a in range(3)):raise ValueError('Invalid LOD bounds')
    identifiers=struct.unpack_from('<'+'I'*bone_count,data,at);hashes=struct.unpack_from('<'+'I'*bone_count,data,at+4*bone_count)
    if len(set(identifiers))!=bone_count:raise ValueError('Duplicate skin identity')
    mapped=[]
    for identifier,value in zip(identifiers,hashes):
        matches=[name for name in known_bones if bone_hash(name)==value]
        mapped.append({'palette_id':identifier,'name_hash':f'{value:08x}',
                       'matched_native_name':matches[0] if len(matches)==1 else None,
                       'hash_collision':len(matches)>1})
    return {'lod_settings':lod_settings,'section_materials':materials,'skin_map':mapped,
            'buffer_block_declared_size':resource_size,'asset_bind_bounds':{'minimum':list(extent[:3]),'maximum':list(extent[3:])}}


def evidence(archive, resource_prefix, native_trace=None, asset_name=None):
    resource_names=[resource_prefix+'.res',resource_prefix+'_lod0_data.res']
    entries={e.name:e for e in archive.entries}
    if any(name not in entries for name in resource_names):raise ValueError('Exact mesh resource pair missing')
    if [entries[n].kind for n in resource_names]!=['SkinnedMeshSet','MeshData']:raise ValueError('Wrong resource types')
    blobs=archive.read_selected(resource_names)
    native_names=set(); native_identity=None
    if native_trace:
        trace=json.loads(Path(native_trace).read_text(encoding='utf8'))
        rows=[r for r in trace.get('gameplay',{}).get('rig_publication',{}).get('weapon_profile_samples',[]) if r.get('asset_name')==asset_name]
        if not rows:raise ValueError('No matching saved native weapon')
        topologies={tuple(sorted(b['name'] for b in r['native_weapon_bones'])) for r in rows if r.get('weapon_bones_complete')}
        if len(topologies)!=1:raise ValueError('Saved native topology not unique')
        native_names=set(next(iter(topologies)))
        native_identity={'path':str(Path(native_trace).resolve()),'sha256':sha(Path(native_trace).read_bytes()),
                         'asset_name':asset_name,'skeleton_fingerprints':sorted(set(r['skeleton'] for r in rows))}
    geometry=mesh_data(blobs[resource_names[1]]);metadata=mesh_set(blobs[resource_names[0]],native_names)
    if metadata['buffer_block_declared_size']!=geometry['vertex_bytes']+geometry['index_bytes']+12 or len(metadata['section_materials'])!=len(geometry['sections']):raise ValueError('MeshSet/MeshData mismatch')
    skin={row['palette_id']:row for row in metadata['skin_map']}
    for section,material in zip(geometry['sections'],metadata['section_materials']):
        if any(index not in skin for index in section['used_palette_ids']):raise ValueError('Palette ID absent from mesh skin map')
        section['material']=material
        section['bone_names']=[skin[index]['matched_native_name'] for index in section['used_palette_ids']]
    return {'schema':'fvr.bc2.mesh_asset_evidence','schema_version':1,'read_only':True,
            'archive':str(archive.path.resolve()),'archive_index_sha256':archive.index_sha256,
            'archive_compressed_payload_sha256':archive.payload_sha256,'native_capture':native_identity,
            'resources':[{'name':name,'type':entries[name].kind,'sha256':sha(blobs[name]),'bytes':len(blobs[name])} for name in resource_names],
            'mesh_set':metadata,'geometry':geometry,'native_insertion_profile':None,
            'limitations':['Asset binding to the current live WeaponStateData remains to be captured.',
              'Bind positions are not an authored hand grip or loading-port frame.',
              'Material and bone identity do not identify live visibility or native ammo authority.']}


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',type=Path,required=True)
    parser.add_argument('--mesh',required=True,help='Exact asset path without .res')
    parser.add_argument('--native',type=Path)
    parser.add_argument('--asset')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(argv)
    if bool(args.native)!=bool(args.asset):parser.error('--native and --asset must be provided together')
    result=evidence(Archive(args.archive),args.mesh,args.native,args.asset)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf8')
    print(json.dumps({'output':str(args.output),'sections':len(result['geometry']['sections']),
                      'native_insertion_profile':None}))

if __name__=='__main__':main()


