"""Validate locally installed PC ITexture mip payloads and wrap as standard DDS.

Supports the observed 2D BC1/BC2/BC3 layouts only. Does not infer normal-map
swizzles, color space, material role, streaming resources or cube layouts.
The returned asset bytes belong in a private local cache, not source packages.
"""
import struct
from inspect_bc2_mesh_asset import sha

def texture_metadata(data):
    if not isinstance(data,bytes) or not 92<=len(data)<=32*1024*1024:
        raise ValueError('ITexture byte bound')
    version,kind,fmt,flags,width,height,depth,mips=struct.unpack_from('<8I',data)
    if version!=3 or kind!=0 or depth!=0:raise ValueError('Unsupported ITexture dimension/header')
    if fmt not in (0,1,2):raise ValueError('Unsupported ITexture encoding')
    if not 1<=width<=8192 or not 1<=height<=8192:raise ValueError('ITexture dimensions')
    if not 1<=mips<=min(15,max(width,height).bit_length()):raise ValueError('ITexture mip count')
    sizes=struct.unpack_from('<15I',data,32)
    if any(sizes[mips:]):raise ValueError('Unexpected unused mip size')
    block=8 if fmt==0 else 16;offset=92;levels=[]
    for index in range(mips):
        w=max(1,width>>index);h=max(1,height>>index)
        size=((w+3)//4)*((h+3)//4)*block
        if size!=sizes[index] or size>len(data)-offset:raise ValueError('ITexture mip payload size')
        levels.append(dict(width=w,height=h,offset=offset,size=size));offset+=size
    if offset!=len(data):raise ValueError('Trailing ITexture bytes')
    return dict(width=width,height=height,mips=levels,encoding=('BC1','BC2','BC3')[fmt],
                format_id=fmt,source_flags=flags,sha256=sha(data),color_space_verified=False)

def private_dds(data):
    meta=texture_metadata(data);mips=meta['mips'];many=len(mips)>1
    flags=0x81007|(0x20000 if many else 0)
    caps=0x1000|(0x400008 if many else 0)
    fourcc=int.from_bytes((b'DXT1',b'DXT3',b'DXT5')[meta['format_id']],'little')
    header=struct.pack('<7I',124,flags,meta['height'],meta['width'],mips[0]['size'],0,len(mips))
    header+=bytes(44)+struct.pack('<8I',32,4,fourcc,0,0,0,0,0)+struct.pack('<5I',caps,0,0,0,0)
    return b'DDS '+header+data[92:]
