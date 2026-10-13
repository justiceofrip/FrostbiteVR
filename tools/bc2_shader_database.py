"""Bounded read-only BC2 DX11 shader material-table inspection.

Format research reference: Powback/Bad-Company-2-Map-Editor,
Assets/Scripts/Types/Graphics. This reader validates the installed serialized
tables independently; it does not execute shaders or export original bytecode.
It resolves candidate passes, not a claim about which pass the renderer uses.
"""
import struct
from inspect_bc2_mesh_asset import sha

def parse_shader_database(data):
    if not isinstance(data,bytes) or not 5<=len(data)<=32*1024*1024:
        raise ValueError('Shader database byte bound')
    d=data
    at=0
    def take(n):
     nonlocal at
     if not 0<=n<=len(d)-at:raise ValueError(f'Bounds at {at}: {n}')
     out=d[at:at+n];at+=n;return out
    def num(fmt='I'):
     return struct.unpack('<'+fmt,take(struct.calcsize('<'+fmt)))[0]
    def count(limit=100000):
     n=num()
     if n>limit:raise ValueError(f'Count {n} at {at-4}')
     return n
    def namez():
     end=d.find(b'\0',at,min(at+1024,len(d)))
     if end<0:raise ValueError('Name bound')
     p=take(end-at+1)[:-1]
     if not p or not all(32<=x<127 for x in p):raise ValueError('Invalid name')
     return p.decode('ascii')
    def dxbc():
     size=count(1024*1024);offset=at;b=take(size)
     if len(b)<32 or b[:4]!=b'DXBC' or struct.unpack_from('<2I',b,20)!=(1,size):raise ValueError('DXBC size/header')
     n=struct.unpack_from('<I',b,28)[0]
     if not 1<=n<=32 or 32+n*4>size:raise ValueError('DXBC chunks')
     chunks=[];intervals=[]
     for rel in struct.unpack_from('<'+'I'*n,b,32):
      if rel<32+n*4 or rel+8>size:raise ValueError('DXBC chunk offset')
      length=struct.unpack_from('<I',b,rel+4)[0]
      if rel+8+length>size or any(rel<end and start<rel+8+length for start,end in intervals):raise ValueError('DXBC chunk size/overlap')
      tag=b[rel:rel+4]
      if not all(32<=x<127 for x in tag):raise ValueError('DXBC chunk tag')
      intervals.append((rel,rel+8+length));chunks.append(tag.decode('ascii'))
     return dict(offset=offset,size=size,sha256=sha(b),chunks=chunks)
    if take(5)!=bytes.fromhex('7800000000'):raise ValueError('Unsupported DX11 database header')
    parameters=[]
    for _ in range(count()):
     offset=at;h=take(32);size=struct.unpack_from('<I',h)[0]
     if size<32 or size>1024*1024 or offset+size>len(d):raise ValueError('Parameter group size')
     textures=[]
     for _ in range(h[27]):
      index=num();s=take(136);end=s.find(b'\0')
      if end<0 or any(s[end:]) or not all(32<=x<127 for x in s[:end]):raise ValueError('Texture name layout')
      textures.append(dict(index=index,name=s[:end].decode('ascii')))
     properties=[]
     for n,span in ((h[28],56),(h[29],40)):
      for _ in range(n):
       b=take(span);key=b[:32].split(b'\0')[0].decode('ascii');properties.append(dict(name=key,raw_hex=b[32:].hex()))
     if at>offset+size:raise ValueError('Parameter group overrun')
     tail=take(offset+size-at)
     parameters.append(dict(offset=offset,size=size,textures=textures,properties=properties,tail_hex=tail.hex()))
    for _ in range(2):
     for _ in range(count()):n=num('H');take(2);take(n*4)
    vertex=[]
    for _ in range(count()):
     take(16);shader=dxbc();take(12);signature=dxbc()
     properties=[struct.unpack('<7I',take(28)) for _ in range(count(64))]
     names=[namez() for _ in range(count(64))]
     if len(properties)!=len(names):raise ValueError('Vertex declaration names')
     vertex.append(dict(shader=shader,signature=signature,elements=[dict(semantic=s,index=v[1],dxgi_format=v[2],input_slot=v[3],byte_offset=v[4],input_class=v[5],instance_step=v[6]) for s,v in zip(names,properties)]))
    pixel=[]
    for i in range(count()):take(16 if i==0 else 28);pixel.append(dxbc())
    take(12);bindings=[]
    for _ in range(count()):
     ref=take(16).hex();v=struct.unpack('<8I',take(32));bindings.append(dict(reference=ref,values=v,vertex=v[4],pixel=v[5],parameters=v[7]))
    for _ in range(count()):take(58);take(count()*4);take(2)
    take(count()*89)
    names=[]
    for group in range(3):
     for _ in range(count()):s=namez();ref=take(16).hex();names.append(dict(group=group,name=s,reference=ref))
    links={}
    for _ in range(count()):
     ref=take(16).hex();n=count();v=list(struct.unpack('<'+'H'*n,take(n*2)))
     if ref in links or any(x>=len(bindings) for x in v):raise ValueError('Binding link identity/range')
     links[ref]=v
    if at!=len(d):raise ValueError(f'Trailing data: {len(d)-at}')
    return dict(schema="fvr.bc2.material-tables.v1",sha256=sha(d),size=len(d),fully_consumed=True,parameters=parameters,vertex=vertex,pixel=pixel,bindings=bindings,names=names,links=links)

def resolve_material(database, mesh_name, material_name):
    """Resolve one exact mesh/material pair; ambiguity never selects a default."""
    key=(mesh_name+'/'+material_name).lower()
    matches=[n for n in database['names'] if n['name'].lower()==key]
    if len(matches)!=1:
        raise ValueError('Material name missing or ambiguous: '+key)
    row=matches[0]
    indices=database['links'].get(row['reference'])
    if not indices:
        raise ValueError('Material pass links missing: '+key)
    resolved=[]
    for index in indices:
        if not 0<=index<len(database['bindings']):raise ValueError('Material binding index')
        b=database['bindings'][index]
        if not (0<=b['vertex']<len(database['vertex']) and
                0<=b['pixel']<len(database['pixel']) and
                0<=b['parameters']<len(database['parameters'])):
            raise ValueError('Material program/parameter index')
        resolved.append(dict(binding_index=index,
            vertex=database['vertex'][b['vertex']],pixel=database['pixel'][b['pixel']],
            parameters=database['parameters'][b['parameters']]))
    return dict(name=row['name'],reference=row['reference'],passes=resolved,
                active_pass_verified=False,rendered_material_verified=False)
