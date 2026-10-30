"""Catalog-driven exact configured body geometry preparation. No process access.

All configured attachment meshes are baked in authored closed pose. This creates
private display geometry only and grants no native or input authority.
"""
import hashlib,struct
import bc2_body_ammo_assets as ammo
import bc2_body_equipment_assets as equipment
from bc2_magazine_contact_batch import ArchivePool
from bc2_weapon_animation_pipeline import Skeleton,Clip,canonical,identity
from bc2_mesh_geometry import metadata,geometry
from inspect_bc2_mesh_asset import bone_hash,sha
from bc2_asset_resource_identity import catalog_resource_pins,VerifiedResources,profiles_match

def require(value,message):
 if not value:raise ValueError(message)

def composite(game,pool,spec,binding,base_geometry,skeleton,known,inverse,resource_content=False):
 cfg=spec['authored_configuration'];require(cfg['resource']==binding['weapon']['resource'] and cfg['sha256']==binding['weapon']['sha256'],'Configured asset invariant failed')
 path=binding['animation_source']['archive'];archive=pool(game/path)
 if not resource_content:require(archive.index_sha256==binding['animation_source']['archive_index_sha256'],'Configured asset invariant failed')
 name=binding['animation_chain']['animation_resource'];raw=archive.read_selected([name])[name];require(sha(raw)==binding['animation_sha256'],'Configured asset invariant failed');clip=Clip(raw)
 sections=[];required={'RightHand','jntWpn_Flash'};sources=[]
 for source in spec['meshes']:
  a=pool(game/source['archive']);mesh=source['mesh'];names=[mesh+'.res',mesh+'_lod0_data.res'];blobs=a.read_selected(names)
  if resource_content:require(source.get('expected_metadata_sha256') and source.get('expected_lod_sha256'),'Exact configured mesh content pins required')
  elif source.get('expected_index_sha256'):require(a.index_sha256==source['expected_index_sha256'],'Configured asset invariant failed')
  if source.get('expected_metadata_sha256'):require(sha(blobs[names[0]])==source['expected_metadata_sha256'],'Configured asset invariant failed')
  if source.get('expected_lod_sha256'):require(sha(blobs[names[1]])==source['expected_lod_sha256'],'Configured asset invariant failed')
  lod=metadata(blobs[names[0]])[0];data=blobs[names[1]];ss=geometry(data,lod,known)
  vb=sum(s['vertices']*s['stride'] for s in ss);ib=sum(s['triangles']*6 for s in ss);start=len(data)-vb-ib
  require(start>=0,'Configured asset invariant failed')
  for section in ss:
   if section['name'].endswith('_ZOnly'):continue
   required.update(section['bone_names']);sections.append((data,section,lod,start,vb))
  sources.append(dict(archive=source['archive'],archive_index_sha256=a.index_sha256,mesh=mesh,mesh_sha256=sha(blobs[names[0]]),lod_sha256=sha(data)))
 closed=clip.evaluate(skeleton,0.,sorted(required));bad={n:closed['bone_evaluation_status'][n] for n in required if n not in {'RightHand','jntWpn_Flash'} and closed['bone_evaluation_status'][n]!='static_authored_pose'}
 if bad:raise ValueError('Composite required closed controls not static: '+str(bad))
 anchor_status={n:closed['bone_evaluation_status'][n] for n in ('RightHand','jntWpn_Flash')}
 if any(v not in ('static_authored_pose','decoded_spline_candidate') for v in anchor_status.values()):raise ValueError('Unresolved grip/muzzle display anchor')
 vertices=[];indices=[]
 for data,section,lod,start,vb in sections:
  baked=equipment.bake_section(data,section,lod,known,inverse,closed['weapon_relative'],start)
  packed=data[start+vb+section['first_index']*2:start+vb+(section['first_index']+section['triangles']*3)*2]
  local=struct.unpack('<'+'H'*(len(packed)//2),packed)
  require(len(local)==section['triangles']*3 and local and max(local)<len(baked),'Configured asset invariant failed')
  offset=len(vertices);indices.extend(i+offset for i in local);vertices.extend(baked)
 require(0<len(indices)//3<=32768 and len(vertices)<=131072,'Configured asset invariant failed')
 vertex_bytes=b''.join(vertices);skin=b''.join(vertices[i] for i in indices);positions=b''.join(vertices[i][:12] for i in indices)
 index_size=2 if len(vertices)<=65536 else 4;index_bytes=struct.pack('<'+('H' if index_size==2 else 'I')*len(indices),*indices)
 part='body_holstered_'+hashlib.sha256(cfg['resource'].encode()).hexdigest()[:16];section_name='closed_configuration'
 metadata_row=dict(section=section_name,count=len(indices),stride=20,palette_size=1,part_palette_index=0,position='Float3',vertex_skin_hash=f'{ammo.fnv(skin):016x}',position_hash=f'{ammo.fnv(positions):016x}',part_triangles=len(indices)//3)
 rig=int(skeleton.fingerprint.split(':')[1],16);mesh=base_geometry['configured_mesh_path']
 row=dict(asset=spec['asset'],mesh=mesh,part=part,rig_fingerprint=rig,inverse_bind=identity(),sections=[metadata_row],display_only=True,
  configuration_path=cfg['resource'][:-4],configured_meshes=sorted(s['mesh'] for s in spec['meshes']),authored_configuration=cfg,
  closed_clip=name,closed_clip_sha256=sha(raw),closed_pose='static_authored_pose',sources=sources,display_anchor_evaluation=anchor_status,display_anchor_time_seconds=0,display_anchor_not_native_grip_calibration=True,
  holster_from_weapon=equipment.holster_transform(closed['weapon_relative']['RightHand'][12:15],closed['weapon_relative']['jntWpn_Flash'][12:15]),
  grip=closed['weapon_relative']['RightHand'][12:15],muzzle=closed['weapon_relative']['jntWpn_Flash'][12:15])
 record=ammo.text(row['asset'])+ammo.text(mesh)+ammo.text(part)+struct.pack('<QI',rig,1)+ammo.text(section_name)+struct.pack('<5I',len(vertex_bytes),len(index_bytes),index_size,0,0)+vertex_bytes+index_bytes
 return row,record

def compact_cache_sections(payload,rows):
 """Lossless format-preserving removal of unused per-section vertex ranges."""
 at=12;out=bytearray(payload[:12])
 def string():
  nonlocal at
  size=struct.unpack_from('<H',payload,at)[0];begin=at;at+=2+size
  return payload[begin:at]
 for row in rows:
  for _ in range(3):out+=string()
  out+=payload[at:at+12];rig,count=struct.unpack_from('<QI',payload,at);at+=12
  require(count==len(row['sections']),'Configured asset invariant failed')
  for section in row['sections']:
   out+=string();nv,ni,index_size,vertex_offset,index_offset=struct.unpack_from('<5I',payload,at);at+=20
   vertices=payload[at:at+nv];at+=nv;indices=payload[at:at+ni];at+=ni
   require(index_offset==0 and index_size in (2,4),'Configured asset invariant failed')
   ids=struct.unpack('<'+('H' if index_size==2 else 'I')*(ni//index_size),indices)
   needed=(max(ids)+1)*section['stride'];trimmed=vertices[vertex_offset:vertex_offset+needed]
   require(len(trimmed)==needed,'Configured asset invariant failed')
   # The exact indexed skin bytes used by the production parser are identical.
   size=8 if section['position']=='Half4' else 12
   skin=b''.join(trimmed[i*section['stride']:i*section['stride']+size+8] for i in ids)
   positions=b''.join(trimmed[i*section['stride']:i*section['stride']+size] for i in ids)
   require(f'{ammo.fnv(skin):016x}'==section['vertex_skin_hash'] and f'{ammo.fnv(positions):016x}'==section['position_hash'],'Configured asset invariant failed')
   out+=struct.pack('<5I',needed,ni,index_size,0,0)+trimmed+indices
 require(at==len(payload),'Configured asset invariant failed')
 return bytes(out)

def derive_catalog(game, reviewed, resource_content=False):
    if not reviewed or len(reviewed)>256:raise ValueError('Catalog profile bound')
    exact=[r for r in reviewed if r.get('configuration_path')]
    common=exact[0] if exact else None
    if common:
        common_path,common_index=common['configuration_archive'],common['configuration_archive_index_sha256']
    elif reviewed[0].get('rigid_assembly'):
        source=reviewed[0]['assembly_source']['skeleton'];common_path,common_index=source['archive'],source['archive_index_sha256']
    else:
        common_path,common_index=reviewed[0]['archive'],reviewed[0]['archive_index_sha256']
    # A mixed campaign/MP catalog can start with sp_common. The already bounded
    # MP reader still needs its exact named container and pin when a later row
    # owns mesh bytes there; row order must not silently select a smaller reader.
    common_refs=[]
    for row in reviewed:
        refs=[dict(archive=row.get('configuration_archive'),archive_index_sha256=row.get('configuration_archive_index_sha256')),
              dict(archive=row.get('archive'),archive_index_sha256=row.get('archive_index_sha256'))]
        refs+=row.get('sources',[])
        for field in ('assembly_source','mod_policy_source'):
            refs += [value for value in row.get(field,{}).values() if isinstance(value,dict)]
        for ref in refs:
            if str(ref.get('archive','')).replace('\\','/').lower()=='dist/win32/levels/mp_common/level-00.fbrb':
                common_refs.append((ref['archive'],ref['archive_index_sha256']))
    if common_refs:
        if len({ref[1] for ref in common_refs})!=1:raise ValueError('Conflicting MP common catalog pins')
        common_path,common_index=common_refs[0]
    pool=ArchivePool(game,common_path,common_index,verify_common_index=not resource_content)
    if resource_content:
        pool=VerifiedResources(game,pool,catalog_resource_pins(reviewed,ammo.SKELETON_ARCHIVE,ammo.SKELETON_RESOURCE,ammo.SKELETON_SHA))
    previous_ammo,previous_equipment=ammo.archive,equipment.archive
    ammo.archive=equipment.archive=lambda game,path:pool(game/path)
    try:
        sk=pool(game/ammo.SKELETON_ARCHIVE);raw=sk.read_selected([ammo.SKELETON_RESOURCE])[ammo.SKELETON_RESOURCE]
        if sha(raw)!=ammo.SKELETON_SHA:raise ValueError('Skeleton identity')
        skeleton=Skeleton(raw);known={bone_hash(n):n for n in skeleton.names}
        inverse={n:canonical(b['InverseWorldTransform']) for n,b in zip(skeleton.names,skeleton.bones)}
        rows=[];records=bytearray();keys=set()
        for r in reviewed:
            key=(r['asset'],r['mesh'],r['part'],r['rig_fingerprint'])
            if key in keys:raise ValueError('Duplicate catalog key')
            keys.add(key)
            if r.get('mod_authored_magazine'):
                from bc2_authored_magazine_policy import derive_reviewed_cache
                row,record=derive_reviewed_cache(game,pool,r,resource_content=resource_content)
            elif r.get('mod_authored_display'):
                from bc2_authored_magazine_policy import derive_reviewed_display
                row,record=derive_reviewed_display(game,pool,r,resource_content=resource_content)
            elif r.get('mod_authored_firearm_display'):
                from bc2_firearm_cache_completion import derive_reviewed_display
                row,record=derive_reviewed_display(game,pool,r,resource_content=resource_content)
            elif r.get('rigid_assembly'):
                from bc2_magazine_assembly_cache import derive_reviewed_cache
                row,record=derive_reviewed_cache(game,pool,r,resource_content=resource_content)
            elif r.get('configuration_path'):
                a=pool(game/r['configuration_archive'])
                if not resource_content and a.index_sha256!=r['configuration_archive_index_sha256']:raise ValueError('Configuration archive changed')
                cfg=r['authored_configuration']
                if cfg['resource']!=r['configuration_path']+'.dbx':raise ValueError('Exact configuration path')
                for ref in [cfg]+cfg['mesh_references']:
                    raw=a.read_selected([ref['resource']])[ref['resource']]
                    if sha(raw)!=ref['sha256']:raise ValueError('Configuration/mesh DBX identity changed')
                sources=[dict(archive=s['archive'],mesh=s['mesh'],expected_index_sha256=s['archive_index_sha256'],
                              expected_metadata_sha256=s['mesh_sha256'],expected_lod_sha256=s['lod_sha256']) for s in r['sources']]
                if sorted(s['mesh'] for s in sources)!=r['configured_meshes']:raise ValueError('Complete configured mesh set')
                spec=dict(asset=r['asset'],meshes=sources,authored_configuration=cfg)
                binding=dict(weapon=cfg,animation_source=dict(archive=r['closed_archive'],archive_index_sha256=r['closed_archive_index_sha256']),
                             animation_chain=dict(animation_resource=r['closed_clip']),animation_sha256=r['closed_clip_sha256'])
                row,record=composite(game,pool,spec,binding,dict(configured_mesh_path=r['mesh']),skeleton,known,inverse,resource_content=resource_content)
                for field in ('configuration_archive','configuration_archive_index_sha256','closed_archive','closed_archive_index_sha256'):
                    row[field]=r[field]
                row['configuration_archive_index_sha256']=a.index_sha256
                row['closed_archive_index_sha256']=pool(game/r['closed_archive']).index_sha256
            elif r.get('display_only'):
                derived,record=equipment.derive_equipment(game,[(r['asset'],r['archive'],r['mesh'],r['closed_clip'])])
                row=derived[0]
            else:
                derived,packed=ammo.derive(game,[(r['asset'],r['archive'],r['mesh'],r['part'])]);row=derived[0]
                record=packed[12:]
            expected={k:v for k,v in r.items() if k!='configured_aliases'}
            if not profiles_match([row],[expected],resource_content):raise ValueError('Installed geometry differs from exact catalog: '+str(key))
            # Every catalog entry uses the same lossless indexed-data contract.
            # Retaining unused legacy ranges can overflow the cache before new
            # family entries are appended, despite identical rendered geometry.
            record=compact_cache_sections(ammo.MAGIC+struct.pack('<I',1)+record,[row])[12:]
            if r.get('configured_aliases'):
                from bc2_firearm_body_aliases import verify_aliases
                row['configured_aliases']=verify_aliases(game,pool,r,record,resource_content)
            rows.append(row);records+=record
            if len(records)+12>ammo.MAX_CACHE:raise ValueError('Private cache size limit')
        return rows,ammo.MAGIC+struct.pack('<I',len(rows))+records
    finally:
        ammo.archive,equipment.archive=previous_ammo,previous_equipment
