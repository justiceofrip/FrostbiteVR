"""Serialize explicit converted body materials for the shared host renderer.

This writer does not choose textures, infer UVs or convert native shaders. Its
inputs are adapter-reviewed RGBA mip images and triangle-order UV coordinates.
The result contains private asset pixels and must not be distributed as source.
"""
import math
import struct

MAGIC = b'FVRTEX01'
MAX_BYTES = 96 * 1024 * 1024


def _integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError('Material integer bound')
    return value


def encode_materials(textures, assignments):
    _integer(len(textures), 1, 128)
    _integer(len(assignments), 1, 512)
    out = bytearray(MAGIC + struct.pack('<2I', len(textures), len(assignments)))
    total = 0
    for texture in textures:
        width = _integer(texture['width'], 1, 2048)
        height = _integer(texture['height'], 1, 2048)
        if texture['color_space'] not in ('linear', 'srgb') or texture['address'] not in ('wrap', 'clamp'):
            raise ValueError('Explicit texture color space and addressing required')
        mips = texture['mips']
        _integer(len(mips), 1, 12)
        out += struct.pack('<5I', width, height, texture['color_space'] == 'srgb', texture['address'] == 'clamp', len(mips))
        for index, pixels in enumerate(mips):
            if not isinstance(pixels, bytes) or len(pixels) != width * height * 4:
                raise ValueError('Texture mip payload')
            total += len(pixels)
            if total > 64 * 1024 * 1024 or (width == height == 1 and index + 1 < len(mips)):
                raise ValueError('Texture mip/total bound')
            out += struct.pack('<I', len(pixels)) + pixels
            width, height = max(1, width // 2), max(1, height // 2)
    seen = set()
    used = set()
    for assignment in assignments:
        key = tuple(_integer(x, 1, 2**64 - 1) for x in assignment['key'])
        if len(key) != 3:
            raise ValueError('Geometry identity')
        section = _integer(assignment['section'], 0, 7)
        texture = _integer(assignment['texture'], 0, len(textures) - 1)
        if (key, section) in seen:
            raise ValueError('Duplicate section assignment')
        seen.add((key, section))
        used.add(texture)
        skin = _integer(assignment['skin'], 1, 2**64 - 1)
        positions = _integer(assignment['positions'], 1, 2**64 - 1)
        uv = assignment['uv']
        _integer(len(uv), 1, 32768 * 3)
        if len(uv) % 3:
            raise ValueError('Triangle list required')
        color = assignment['color']
        if len(color) != 4 or color[3] != 1 or any(not math.isfinite(x) or not 0 <= x <= 1 for x in color):
            raise ValueError('Opaque tint')
        out += struct.pack('<3Q2I2QI4f', *key, section, texture, skin, positions, len(uv), *color)
        for pair in uv:
            if len(pair) != 2 or any(not math.isfinite(x) or abs(x) > 65536 for x in pair):
                raise ValueError('Texture coordinates')
            out += struct.pack('<2f', *pair)
        if len(out) > MAX_BYTES:
            raise ValueError('Material cache byte bound')
    if used != set(range(len(textures))):
        raise ValueError('Unreferenced texture')
    return bytes(out)


def rigid_triangle_coordinates(indices, owners, part, coordinates, *, reflect):
    """Match the shared extractor's selected rigid triangles and reflected order."""
    if not indices or len(indices) % 3 or len(indices) != len(owners):
        raise ValueError('Triangle ownership layout')
    out = []
    for n in range(0, len(indices), 3):
        if len(set(owners[n:n + 3])) != 1:
            raise ValueError('Mixed-owner triangle')
        if owners[n] != part:
            continue
        triangle = indices[n:n + 3]
        if reflect:
            triangle = [triangle[0], triangle[2], triangle[1]]
        for i in triangle:
            _integer(i, 0, len(coordinates) - 1)
            out.append(coordinates[i])
    if not out:
        raise ValueError('Empty selected part')
    return out
