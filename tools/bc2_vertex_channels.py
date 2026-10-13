"""Decode explicit BC2 mesh channels using a resolved shader declaration.

The caller must resolve the mesh/material pair first. A TEXCOORD semantic alone
does not establish a texture's UV source: BC2 also uses these semantics for
positions, bones and normals. This reader preserves all components and vertex
order and makes no choice of texture, UV pair, color space or active shader pass.
Instance streams are metadata only; their bytes are not in the mesh resource.
"""
import hashlib
import math
import struct


# DXGI formats observed in the installed declarations, plus explicit float2.
FORMATS = {2: '4f', 6: '3f', 10: '4e', 16: '2f', 30: '4B'}


def _integer(value, minimum, maximum, label):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError('Invalid ' + label)
    return value


def mesh_declaration(section, declaration):
    """Validate a declaration against the independently decoded mesh layout."""
    stride = _integer(section['stride'], 1, 256, 'vertex stride')
    layout = section.get('position_format')
    if layout not in ('Half4', 'Float3'):
        raise ValueError('Validated mesh position layout required')
    if not isinstance(declaration, (list, tuple)) or not 1 <= len(declaration) <= 64:
        raise ValueError('Vertex declaration count')
    seen = set()
    intervals = []
    per_vertex = []
    instances = []
    for element in declaration:
        semantic = element['semantic']
        if not isinstance(semantic, str) or not semantic or len(semantic) > 64 or not semantic.isascii() or not semantic.isalnum():
            raise ValueError('Vertex semantic')
        key = (semantic.upper(), _integer(element['index'], 0, 31, 'semantic index'))
        if key in seen:
            raise ValueError('Duplicate vertex semantic')
        seen.add(key)
        fmt = _integer(element['dxgi_format'], 1, 255, 'vertex format')
        if fmt not in FORMATS:
            raise ValueError('Unsupported vertex format')
        slot = _integer(element['input_slot'], 0, 15, 'input slot')
        offset = _integer(element['byte_offset'], 0, 4095, 'channel offset')
        kind = _integer(element['input_class'], 0, 1, 'input class')
        step = _integer(element['instance_step'], 0, 65535, 'instance step')
        row = dict(element, semantic=key[0], byte_size=struct.calcsize('<' + FORMATS[fmt]))
        if kind == 1:
            if slot == 0 or step == 0:
                raise ValueError('Instance stream declaration')
            instances.append(row)
            continue
        if slot != 0 or step != 0:
            raise ValueError('Mesh requires one explicit per-vertex stream')
        end = offset + row['byte_size']
        if end > stride or any(offset < b and a < end for a, b in intervals):
            raise ValueError('Vertex channel range or overlap')
        intervals.append((offset, end))
        per_vertex.append(row)
    by_key = {(e['semantic'], e['index']): e for e in per_vertex}
    position = by_key.get(('TEXCOORD', 0))
    skin = by_key.get(('TEXCOORD', 1))
    expected_format, position_bytes = (10, 8) if layout == 'Half4' else (6, 12)
    if not position or (position['byte_offset'], position['dxgi_format']) != (0, expected_format):
        raise ValueError('Shader position differs from validated mesh')
    if not skin or (skin['byte_offset'], skin['dxgi_format']) != (position_bytes, 30):
        raise ValueError('Shader bone indices differ from validated mesh')
    return dict(per_vertex=per_vertex, instances=instances)


def decode_channel(vertices, section, declaration, semantic, index):
    """Return one explicit channel without changing axes, coordinates or order."""
    if not isinstance(vertices, bytes) or not 1 <= len(vertices) <= 4 * 1024 * 1024:
        raise ValueError('Vertex buffer byte bound')
    parsed = mesh_declaration(section, declaration)
    if not isinstance(semantic, str):
        raise ValueError('Requested semantic')
    key = (semantic.upper(), _integer(index, 0, 31, 'requested semantic index'))
    matches = [e for e in parsed['per_vertex'] if (e['semantic'], e['index']) == key]
    if len(matches) != 1:
        raise ValueError('Requested channel is missing or belongs to an instance stream')
    element = matches[0]
    count = _integer(section['vertices'], 1, 65536, 'vertex count')
    offset = _integer(section['vertex_offset'], 0, len(vertices), 'section offset')
    stride = section['stride']
    if offset + count * stride > len(vertices):
        raise ValueError('Section outside vertex buffer')
    fmt = '<' + FORMATS[element['dxgi_format']]
    size = element['byte_size']
    raw = b''.join(vertices[offset + n * stride + element['byte_offset']:
                            offset + n * stride + element['byte_offset'] + size] for n in range(count))
    values = list(struct.iter_unpack(fmt, raw))
    if len(values) != count or any(not math.isfinite(x) for value in values for x in value):
        raise ValueError('Nonfinite or incomplete vertex channel')
    return dict(semantic=key[0], index=index, dxgi_format=element['dxgi_format'],
                byte_offset=element['byte_offset'], count=count, values=values,
                sha256=hashlib.sha256(raw).hexdigest(), uv_assignment_verified=False)
