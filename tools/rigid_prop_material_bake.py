"""Bake an explicit material function onto a bounded private triangle atlas.

No game names or shader guesses. The adapter supplies exact triangle-order UV
channels, texture sampling policy, and its reviewed base-color function. This
does not reproduce dynamic lighting, normal mapping or specular reflection.
"""
import math
import numpy as np
from PIL import Image


def sample_rgba(image, uv, address='wrap'):
    """Bilinear normalized sampling with texel centers and explicit addressing."""
    a = np.asarray(image)
    q = np.asarray(uv, dtype=np.float64)
    if a.dtype != np.uint8 or a.ndim != 3 or a.shape[2] != 4 or not 1 <= min(a.shape[:2]) <= max(a.shape[:2]) <= 2048:
        raise ValueError('RGBA image shape')
    if q.ndim < 1 or q.shape[-1] != 2 or not np.isfinite(q).all() or np.max(np.abs(q), initial=0) > 65536:
        raise ValueError('Texture coordinates')
    if address not in ('wrap', 'clamp'):
        raise ValueError('Explicit address mode')
    h, w = a.shape[:2]
    p = q * (w, h) - .5
    low = np.floor(p).astype(np.int64)
    f = p - low
    def index(v, limit):
        return v % limit if address == 'wrap' else np.clip(v, 0, limit-1)
    x, y = low[..., 0], low[..., 1]
    p00 = a[index(y, h), index(x, w)].astype(np.float64) / 255
    p10 = a[index(y, h), index(x+1, w)].astype(np.float64) / 255
    p01 = a[index(y+1, h), index(x, w)].astype(np.float64) / 255
    p11 = a[index(y+1, h), index(x+1, w)].astype(np.float64) / 255
    sx, sy = f[..., :1], f[..., 1:2]
    return (p00*(1-sx)+p10*sx)*(1-sy)+(p01*(1-sx)+p11*sx)*sy


def bake_triangle_material(channels, evaluator, edge=32):
    if type(edge) is not int or edge not in (16, 32, 64) or not channels or len(channels) > 16:
        raise ValueError('Material atlas layout')
    converted = {}
    count = None
    for key, value in channels.items():
        a = np.asarray(value, dtype=np.float64)
        if a.ndim != 3 or a.shape[1:] != (3, 2) or not np.isfinite(a).all() or np.max(np.abs(a), initial=0) > 65536:
            raise ValueError('Triangle UV shape')
        if count is not None and count != a.shape[0]:
            raise ValueError('Triangle channel disagreement')
        count = a.shape[0]
        converted[key] = a
    if not count or count > (2048//edge)**2:
        raise ValueError('Atlas triangle bound')
    columns = math.ceil(math.sqrt(count))
    rows = math.ceil(count/columns)
    width, height = (1 << (x-1).bit_length() for x in (columns*edge, rows*edge))
    if width > 2048 or height > 2048:
        raise ValueError('Atlas dimension bound')
    canvas = np.zeros((height, width, 4), np.uint8)
    canvas[..., 3] = 255
    # Two texel gutters. Coordinates use pixel-center positions, while the
    # evaluator receives the exact corresponding source-triangle barycentrics.
    low, high = 2.5, edge-2.5
    axis = (np.arange(edge, dtype=np.float64)+.5-low)/(high-low)
    x, y = np.meshgrid(axis, axis)
    weights = np.stack((1-x-y, x, y), axis=-1)
    uv = []
    for triangle in range(count):
        inputs = {k: np.einsum('hwv,vc->hwc', weights, a[triangle]) for k, a in converted.items()}
        rgb = np.asarray(evaluator(inputs), dtype=np.float64)
        if rgb.shape != (edge, edge, 3) or not np.isfinite(rgb).all():
            raise ValueError('Material result shape/finite')
        at_x, at_y = triangle % columns * edge, triangle // columns * edge
        canvas[at_y:at_y+edge, at_x:at_x+edge, :3] = np.rint(np.clip(rgb, 0, 1)*255).astype(np.uint8)
        uv.extend([((at_x+low)/width, (at_y+low)/height), ((at_x+high)/width, (at_y+low)/height), ((at_x+low)/width, (at_y+high)/height)])
    # Stop before triangle tiles collapse into neighboring charts. All mip
    # reductions align with the power-of-two tile boundaries.
    levels = [canvas.tobytes()]
    image = Image.fromarray(canvas)
    for n in range(1, int(math.log2(edge))-2):
        levels.append(image.resize((width>>n, height>>n), Image.Resampling.BOX).tobytes())
    return dict(width=width, height=height, color_space='linear', address='clamp', mips=levels), uv
