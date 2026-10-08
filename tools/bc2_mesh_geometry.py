"""Shared bounded skin decoder; derived metadata only, no native memory access."""
import math,struct
from inspect_bc2_mesh_asset import cstring,sha

def need(ok,why):
    if not ok:raise ValueError(why)

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
        used=set();weighted=multi=0;maxweights=0
        for n in range(s['vertices']):
            at=start+ev+n*s['stride'];half=s['stride'] in (16,48)
            point=struct.unpack_from('<4e' if half else '<3f',data,at)
            need(all(math.isfinite(x) for x in point),'invalid position')
            if half:need(point[3] in (-1,1),'packed position fourth component')
            posbytes=8 if half else 12;indices=data[at+posbytes:at+posbytes+4];weights=data[at+posbytes+4:at+posbytes+8]
            need(sum(weights)==255,'skin weights do not sum255');active=sum(w>0 for w in weights);need(active<=s['max_authored_weights'],'more weights than authored');maxweights=max(maxweights,active);multi+=active>1
            for index,weight in zip(indices,weights):
                if weight:
                    need(index<len(s['palette']),'local weight outside palette')
                    used.add(lod['palette'][s['palette'][index]]);weighted+=1
        indices=struct.unpack_from('<'+'H'*(s['triangles']*3),data,start+vb+ei*2)
        need(max(indices)<s['vertices'],'triangle outside section')
        s.update(weighted_hashes=[f'{h:08x}' for h in sorted(used)],bone_names=[known.get(h) for h in sorted(used)],
                 multi_weight_vertices=multi,max_weights=maxweights,all_vertices_normalized=True,
                 vertex_skin_sha256=sha(b''.join(data[start+ev+n*s['stride']:start+ev+n*s['stride']+(16 if s['stride'] in (16,48) else 20)] for n in range(s['vertices']))),
                 index_sha256=sha(data[start+vb+ei*2:start+vb+(ei+s['triangles']*3)*2]))
        ev+=s['vertices']*s['stride'];ei+=s['triangles']*3
    return sections

