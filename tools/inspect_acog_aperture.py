"""Read only the explicitly named ACOG resources; save derived evidence, no assets."""
import argparse,json,struct
from pathlib import Path
from inspect_bc2_mesh_asset import Archive,cstring,bounds,sha,bone_hash

def inspect(path):
 a=Archive(path);prefix='Objects/Weapons/Unlock/ACOG_4X/US_ACOG_4X_Mesh'
 names=[prefix+'.res',prefix+'_lod0_data.res'];b=a.read_selected(names)
 d=b[names[0]];assert d[:5]==b'\x2a\0\0\0\0'
 lod_name,at=cstring(d,5);n=struct.unpack_from('<I',d,at)[0];at+=4;assert n==3
 lods=[]
 for i in range(n):
  flag,count,variants=struct.unpack_from('<3B',d,at);at+=3;assert flag==0 and count in (2,4) and variants==2
  materials=[]
  for _ in range(variants):
   obj,at=cstring(d,at);ms=[]
   for _ in range(count):m,at=cstring(d,at);ms.append(m)
   materials.append(ms)
  assert materials[0]==materials[1]
  ext=struct.unpack_from('<6f3I',d,at);at+=36;assert ext[-2:]==(0,1)
  identity,name_hash=struct.unpack_from('<2I',d,at);at+=8;assert (identity,name_hash)==(35,bone_hash('jntWpn_10'))
  lods.append(dict(lod=i,section_materials=materials[0],asset_bind_bounds=dict(minimum=list(ext[:3]),maximum=list(ext[3:6])),
    buffer_bytes=ext[6],skin_map=[dict(palette_id=identity,name_hash=f'{name_hash:08x}',name='jntWpn_10')]))
 assert at==len(d)
 d=b[names[1]];assert d[:7]==b'\x01\0\0\0\0\0\x04';at=7;sections=[]
 for i in range(4):
  name,at=cstring(d,at);v=struct.unpack_from('<4I4BH',d,at);at+=22
  assert v[4] in (68,32) and v[5:]==(3,1,1,1)
  pal=struct.unpack_from('<H',d,at)[0];at+=2;assert pal==35
  sections.append(dict(name=name,triangles=v[0],vertices=v[1],first_index=v[2],vertex_offset=v[3],stride=v[4],asset_palette_id=pal))
 vs=sum(s['vertices']*s['stride'] for s in sections);inds=sum(s['triangles']*6 for s in sections);start=len(d)-vs-inds
 assert struct.unpack_from('<3I',d,start-12)==(vs,inds,0) and vs+inds+12==lods[0]['buffer_bytes']
 ev=ei=0;glass=[]
 for s,material in zip(sections,lods[0]['section_materials']):
  assert s['vertex_offset']==ev and s['first_index']==ei;s['material']=material;positions=[]
  for j in range(s['vertices']):
   p=start+s['vertex_offset']+j*s['stride'];positions.append(struct.unpack_from('<3f',d,p))
   assert d[p+12:p+20]==b'\0\0\0\0\xff\0\0\0'
  indices=struct.unpack_from('<'+'H'*(s['triangles']*3),d,start+vs+s['first_index']*2)
  assert max(indices)<s['vertices'];s['bind_bounds']=bounds(positions);s['rigid_weight']=255
  s['vertex_slice_sha256']=sha(d[start+s['vertex_offset']:start+s['vertex_offset']+s['vertices']*s['stride']])
  s['index_slice_sha256']=sha(d[start+vs+s['first_index']*2:start+vs+(s['first_index']+s['triangles']*3)*2])
  if 'Glass' in s['name']:glass=positions
  ev+=s['vertices']*s['stride'];ei+=s['triangles']*3
 # Glass perimeter groups are exactly eight coplanar vertices each, plus a domed centre.
 groups={}
 for p in glass:groups.setdefault(p[2],[]).append(p)
 rims=[dict(z=z,outline_xy=[[p[0],p[1]] for p in ps]) for z,ps in sorted(groups.items()) if len(ps)==8]
 assert len(rims)==2
 return dict(schema='fvr.bc2.acog_aperture_evidence',schema_version=1,read_only=True,archive=str(a.path.resolve()),
  archive_index_sha256=a.index_sha256,archive_compressed_payload_sha256=a.payload_sha256,
  resources=[dict(name=n,sha256=sha(b[n]),bytes=len(b[n])) for n in names],lods=lods,lod0_sections=sections,
  measured_glass_perimeters=rims,reticle_ahead_of_housing_m=sections[2]['bind_bounds']['minimum'][2]-sections[0]['bind_bounds']['maximum'][2],
  interpretation=['The authored red-dot geometry lies at least 80mm ahead of the housing, matching the direction of the user video artifact.',
   'Every LOD0 section shares asset palette35/jntWpn_10. A bone-collapse workaround would also remove the housing and glass.',
   'LOD1/2 have no RedDot/Glass sections. Do not force an LOD or invent a dot at those LODs.',
   'A per-eye aperture cone can reject off-axis reticle geometry without affecting zoom, ADS, the housing or glass.'],
  unknown=['No synchronized native draw/geometry/skin-transform capture establishes that this section generated the recorded artifact.',
   'Asset palette35 does not establish a GPU constant slot. Exact draw and current immutable skin request ownership remain required.',
   'Authored glass perimeters are a geometric visibility model; native material/shader effects and headset edge feel remain unverified.'])

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--archive',required=True);p.add_argument('--output',required=True);args=p.parse_args()
 report=inspect(args.archive);Path(args.output).write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(output=args.output,reticle_data_sha256=report['resources'][1]['sha256'],apertures=len(report['measured_glass_perimeters']))))
