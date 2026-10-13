"""Shared bounded skin decoder; derived metadata only, no native memory access."""
import math,struct
from inspect_bc2_mesh_asset import cstring,sha

def need(ok,why):
    if not ok:raise ValueError(why)

def position_bytes(section):
    """Use the validated layout, never infer the skin offset from stride."""
    layout=section.get('position_format')
    need(layout in ('Half4','Float3'),'validated position format required')
    return 8 if layout=='Half4' else 12

def _skin_layout(data,start,section,lod,known,layout):
    half=layout=='Half4';posbytes=8 if half else 12
    used=set();multi=maxweights=0
    for n in range(section['vertices']):
        at=start+section['vertex_offset']+n*section['stride']
        point=struct.unpack_from('<4e' if half else '<3f',data,at)
        need(all(math.isfinite(x) for x in point),'invalid position')
        if half:need(point[3] in (-1,1),'packed position fourth component')
        indices=data[at+posbytes:at+posbytes+4];weights=data[at+posbytes+4:at+posbytes+8]
        need(len(weights)==4 and sum(weights)==255,'skin weights do not sum255')
        active=sum(w>0 for w in weights)
        need(active<=section['max_authored_weights'],'more weights than authored')
        maxweights=max(maxweights,active);multi+=active>1
        for index,weight in zip(indices,weights):
            if weight:
                need(index<len(section['palette']),'local weight outside palette')
                used.add(lod['palette'][section['palette'][index]])
    return dict(position_format=layout,
                weighted_hashes=[f'{h:08x}' for h in sorted(used)],
                bone_names=[known.get(h) for h in sorted(used)],
                multi_weight_vertices=multi,max_weights=maxweights,
                all_vertices_normalized=True,
                vertex_skin_sha256=sha(b''.join(data[start+section['vertex_offset']+n*section['stride']:start+section['vertex_offset']+n*section['stride']+posbytes+8] for n in range(section['vertices']))))

def metadata(data):
    need(data[:5]==b'\x2a\0\0\0\0','MeshSet header')
    _,at=cstring(data,5);count=struct.unpack_from('<I',data,at)[0];at+=4
    need(1<=count<=4,'LOD bound');lods=[]
    for lod in range(count):
        flag,sections,variants=struct.unpack_from('<3B',data,at);at+=3
        need(flag==0 and 1<=sections<=32 and 1<=variants<=4,'LOD layout')
        materials=[]
        for _ in range(variants):
            _,at=cstring(data,at);variant=[]
            for _ in range(sections):name,at=cstring(data,at);variant.append(name)
            materials.append(variant)
        extent=struct.unpack_from('<6f3I',data,at);at+=36
        resource,reserved,bones=extent[-3:]
        need(reserved==0 and 0<bones<=256 and all(math.isfinite(x) for x in extent[:6]),'skin bounds')
        ids=struct.unpack_from('<'+'I'*bones,data,at);hashes=struct.unpack_from('<'+'I'*bones,data,at+4*bones);at+=8*bones
        need(len(set(ids))==bones,'duplicate palette id')
        lods.append(dict(lod=lod,section_count=sections,materials=materials,buffer_bytes=resource,
                         palette={i:h for i,h in zip(ids,hashes)}))
    need(at==len(data),'trailing MeshSet data');return lods

def geometry(data,lod,known):
    need(len(data)>=7 and data[:6]==b'\x01\0\0\0\0\0' and data[6]==lod['section_count'],'MeshData header')
    at=7;sections=[]
    for _ in range(data[6]):
        name,at=cstring(data,at);v=struct.unpack_from('<4I4BH',data,at);at+=22
        tri,vertices,index,offset,stride,topo,skin,n8,n16=v
        need(0<tri<=200000 and 0<vertices<=65536 and topo==3 and 1<=skin<=4 and 0<n8==n16<=64,'section layout '+name+' '+str(v))
        need(stride in (16,48,64,32,68),'unsupported position/skin stride: '+str(stride))
        palette=struct.unpack_from('<'+'H'*n16,data,at);at+=n16*2
        need(all(p in lod['palette'] for p in palette),'section palette '+name+' '+str(palette)+' keys '+str(list(lod['palette'])))
        sections.append(dict(name=name,triangles=tri,vertices=vertices,first_index=index,vertex_offset=offset,stride=stride,max_authored_weights=skin,palette=list(palette)))
    vb=sum(s['vertices']*s['stride'] for s in sections);ib=sum(s['triangles']*6 for s in sections);start=len(data)-vb-ib
    need(start>=at+12 and struct.unpack_from('<3I',data,start-12)==(vb,ib,0) and vb+ib+12==lod['buffer_bytes'],'buffer correspondence')
    ev=ei=0
    for s in sections:
        need(s['vertex_offset']==ev and s['first_index']==ei,'noncontiguous geometry')
        # The installed M1911 uses Float3+skin in a 48-byte stream; other
        # weapons use Half4+skin at that same stride. Require exactly one
        # complete section interpretation. Never silently prefer either one.
        layouts=('Half4','Float3') if s['stride']==48 else (('Half4',) if s['stride']==16 else ('Float3',))
        candidates=[];errors=[]
        for layout in layouts:
            try:candidates.append(_skin_layout(data,start,s,lod,known,layout))
            except ValueError as exc:errors.append(layout+': '+str(exc))
        need(len(candidates)==1,'ambiguous position/skin layout' if candidates else 'invalid position/skin layout: '+', '.join(errors))
        s.update(candidates[0])
        indices=struct.unpack_from('<'+'H'*(s['triangles']*3),data,start+vb+ei*2)
        need(max(indices)<s['vertices'],'triangle outside section')
        s['index_sha256']=sha(data[start+vb+ei*2:start+vb+(ei+s['triangles']*3)*2])
        ev+=s['vertices']*s['stride'];ei+=s['triangles']*3
    return sections

