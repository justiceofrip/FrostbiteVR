"""Exact firearm metadata join. No game reads, builds, installation or native claim."""
from __future__ import annotations
import collections
import hashlib
import json
import pathlib
import re
import sys
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[4]
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools'))
from bc2_firearm_body_aliases import expand_equipment_rows
from bc2_body_equipment_assets import equipment_header

pins = {}


def text(path):
    p = ROOT / path
    raw = p.read_bytes()
    pins[p.relative_to(ROOT).as_posix()] = hashlib.sha256(raw).hexdigest()
    return raw.decode('utf-8-sig')


def data(path):
    return json.loads(text(path))


def need(condition, why):
    if not condition:
        raise ValueError(why)


def unique(rows, key, label):
    out = {}
    for row in rows:
        k = key(row)
        need(k not in out, 'Duplicate ' + label + ': ' + str(k))
        out[k] = row
    return out


def cachekey(row):
    return row['asset'], row['mesh'], row['part'], row['rig_fingerprint']


def identity(asset, sha, guid):
    return [asset,sha,str(uuid.UUID(guid))]


def include_tree(path, seen=None):
    seen = set() if seen is None else seen
    p = (ROOT / path).resolve()
    if p in seen:
        return ''
    seen.add(p)
    s = text(p.relative_to(ROOT))
    for name in re.findall(r'^#include "([^"]+)"', s, re.M):
        candidate = (p.parent / name).resolve()
        if candidate.is_file() and candidate.is_relative_to(ROOT):
            s += '\n' + include_tree(candidate.relative_to(ROOT), seen)
    return s


def generated_mechanisms(path, prefix, kind):
    s = text(path)
    rows = {}
    for num, block in re.findall(r'inline constexpr Bc2' + kind + r'Geometry Mechanism' + prefix + r'(\d+)Geometry=\[\]\{(.*?)\n\}\(\);', s, re.S):
        obj = 'out.selection.' if kind == 'PistolSlide' else 'out.'
        def value(k):
            return re.search(re.escape(obj + k) + r'="([^"]+)";', block).group(1)
        a, c, m = value('asset'), value('configurationPath'), value('meshPath')
        rig = int(re.search(re.escape(obj+'rigFingerprint')+r'=(\d+)ull;',block).group(1))
        reg = re.search(r'Mechanism'+prefix+num+r'Row=\[\]\{(.*?)\n\}\(\);',s,re.S).group(1)
        need('out.enabled=true;' in reg and 'out.reviewed=true;' in reg, 'Unreviewed '+c)
        need('Mechanism'+prefix+num+'Row' in s[s.index('Generated'+('PistolSlide' if kind=='PistolSlide' else 'Feed')+'Profiles'):], 'Unregistered '+c)
        rows[(a,c)] = dict(asset=a,configuration=c,mesh=m,rig=rig,source=path,block=block)
    return rows


def run():
    roster = data('handoff/tasks/WEAPONS/20261030-roster43/roster.json')['rows']
    unique(roster, lambda r:(r['asset'],r['configuration']), 'roster')
    catalog = data('config/body-ammo-assets.json')['profiles']
    cache = unique(catalog, cachekey, 'cache key')
    exact = [r for r in expand_equipment_rows(catalog) if r.get('configuration_path')]
    bodies = unique(exact, lambda r:(r['asset'],r['configuration_path']), 'body selector')
    need(len(roster)==181 and len(cache)==252 and len(bodies)==181, 'Unexpected scope')
    body_header = text('src/games/bc2/Bc2BodyEquipmentProfiles.h').replace('\r\n','\n')
    need(body_header==equipment_header([r for r in catalog if r.get('display_only')]), 'Stale body selector header')

    supports = collections.defaultdict(list)
    parent = pathlib.Path('profiles/weapons43/CombinedSupport.h')
    for include in re.findall(r'^#include "([^"]+)"',text(parent),re.M):
        path = (ROOT / parent.parent / include).resolve().relative_to(ROOT)
        for a,m,rig in re.findall(r'AuthoredSupportProfile\{"([^"]+)","([^"]+)","[^"]+","[^"]+","[^"]+",0x([0-9a-f]+)ull',text(path)):
            supports[(a,m,int(rig,16))].append(path.as_posix())

    mag = data('profiles/weapons45-magazines/batch.json')['rows']
    magazines = unique(mag,lambda r:(r['asset'],r['configuration']),'magazine selector')
    enrollment = unique(data('profiles/weapons45-magazines/enrollment.json')['profiles'],lambda r:r['key'],'magazine enrollment')
    geometry = unique(data('profiles/weapons45-magazines/geometry.json')['profiles'],lambda r:r['profile_digest'],'magazine geometry')
    native_mag = include_tree('profiles/weapons45-magazines/CombinedRegistry.h')
    physical_mag = include_tree('profiles/weapons45-magazines/CombinedGeometry.h')
    geometry_blocks=re.split(r'auto& g=out\[\d+\];',physical_mag)[1:]
    builtin_mag = include_tree('src/games/bc2/Bc2MagazineNativeProfile.h')
    builtin_geometry = text('src/games/bc2/Bc2MagazineGeometryProfile.cpp')

    shotgun = data('handoff/tasks/WEAPONS/20261030-shotgun44/equipment.json')
    shotguns = unique(shotgun['profiles'],lambda r:(r['asset'],r['asset_path']),'shotgun selector')
    shell_groups = {g['geometry_digest']:g for g in shotgun['groups']}
    shotgun_header=text('profiles/weapons43/ShotgunEquipment.generated.h')
    shotgun_native=text('src/games/bc2/Bc2ShotgunReloadRegistry.h')
    cycles = unique(data('profiles/weapons45-cycles/exact-cycle-reviews.json')['reviews'],lambda r:(r['configuration']['assetName'],r['configuration']['assetPath']),'cycle selector')
    cycle_header = text('src/games/bc2/Bc2BoltActionNativeProfile.h')
    bolt_header = text('profiles/weapons43/BoltEquipment.generated.h')
    bolts = {(a,c):m for a,m,c in re.findall(r'c.asset="([^"]+)";c.mesh="([^"]+)";c.configurationPath="([^"]+)";',bolt_header)}
    need(len(bolts)==17,'Bolt header row count')

    feeds = generated_mechanisms('profiles/mechanism-family41/Feed.generated.h','Feed','Feed')
    builtin_feed = text('src/games/bc2/Bc2FeedEquipmentProfile.h')
    a,c,m,rig=re.search(r'M60Geometry\{\s*"([^"]+)","([^"]+)",\s*"([^"]+)",0x([0-9a-f]+)ull',builtin_feed).groups()
    need('out.enabled=out.reviewed=true' in builtin_feed,'Disabled builtin feed')
    feeds[(a,c)]=dict(asset=a,configuration=c,mesh=m,rig=int(rig,16),source='src/games/bc2/Bc2FeedEquipmentProfile.h',block=builtin_feed)
    slides=generated_mechanisms('profiles/mechanism-family41/Slide.generated.h','Slide','PistolSlide')
    builtin_slide=text('src/games/bc2/Bc2PistolSlideProfile.h')
    a,c,guid,sha,m,rig=re.search(r'Mp443Geometry\{\s*\{"([^"]+)","([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",0x([0-9a-f]+)ull',builtin_slide).groups()
    slides[(a,c)]=dict(asset=a,configuration=c,mesh=m,rig=int(rig,16),source='src/games/bc2/Bc2PistolSlideProfile.h')
    fixed={}
    for model in data('profiles/weapons46-special/geometry.json')['models']:
        for sel in model['selectors']:
            fixed[(sel['asset'],sel['configurationPath'])]=(sel,model)
    fixed_header=text('profiles/weapons46-special/FixedContainer.generated.h')
    fixed_native=text('src/games/bc2/Bc2FixedContainerNative.h')

    results=[]
    for row in roster:
        key=row['asset'],row['configuration'];a,c=key;family=row['mechanism']
        out=dict(asset=a,configuration=c,family=family,metadata_complete=False,errors=[])
        try:
            body=bodies[key];cfg=body['authored_configuration']
            need(row['identity']==identity(a,cfg['sha256'],cfg['instance_guid']),'Roster/configuration identity differs')
            need(cfg['resource']==c+'.dbx','Body configuration resource differs')
            need(cachekey(body) in cache,'Body cache key absent')
            need(body['mesh'] in body['configured_meshes'],'Body base mesh is not configured')
            support=supports.get((a,body['mesh'],body['rig_fingerprint']))
            need(bool(support),'Support exact asset/mesh/rig selector absent')
            out.update(two_hand=support,body_cache_key=list(cachekey(body)))
            if family in ('bolt_magazine','detachable_magazine'):
                r=magazines[key];e=enrollment[r['key']];g=geometry[r['geometry_digest']]
                if e.get('status')=='preserved_existing_enrollment':
                    # Nambu predates this enrollment. Join its actual reviewed
                    # geometry/native records and final cache, not an empty stub.
                    need(e['native_id']==r['native_id'],'Preserved native identity differs')
                    rig=int(g['rig_fingerprint'].split(':')[1],16)
                    e=dict(e,identity=r['identity'],descriptor_digest=r['descriptor_digest'],
                        geometry_digest=g['profile_digest'],configured_meshes=[g['configured_mesh_path']],
                        magazine_catalog_key=[a,g['configured_mesh_path'],g['bones']['magazine'],rig])
                need(r['source_complete'] and r['geometry_available'],'Incomplete magazine source')
                need(e['identity']==r['identity'] and e['descriptor_digest']==r['descriptor_digest'],'Magazine identity/descriptor mismatch')
                need(identity(a,r['identity']['weapon']['sha256'],r['identity']['weapon']['guid'])==row['identity'],'Magazine weapon mismatch')
                need(e['geometry_digest']==r['geometry_digest'] and g['grip_binding_digest']==r['binding_digest'],'Magazine geometry mismatch')
                need(tuple(e['magazine_catalog_key']) in cache,'Magazine cache key absent')
                need(set(e['configured_meshes'])==set(body['configured_meshes']),'Magazine configured mesh set differs')
                if r['native_id']=='builtin-preserved':
                    need(c in builtin_mag,'Builtin magazine native registration absent')
                else:
                    need(c in native_mag and r['native_id'] in native_mag,'Magazine native registration absent')
                blocks=[b for b in geometry_blocks if b.startswith('g.asset="'+a+'";g.mesh="'+g['configured_mesh_path']+'";')]
                def matches_block(block):
                    configured=re.search(r'g.configurationPath="([^"]+)";',block)
                    return g['profile_digest'][:16] in block and (not configured or configured.group(1)==c)
                direct=any(matches_block(b) for b in blocks)
                legacy_xm8=(a=='XM8_sp_s' and 'Xm8MagazineGeometry()' in builtin_geometry)
                legacy_aek=(a=='AEK971_sp' and 'g.asset="AEK971_sp";g.mesh="'+g['configured_mesh_path']+'";' in physical_mag and 'p.id=0x148b30c5b8939168ULL' in physical_mag)
                need(direct or legacy_xm8 or legacy_aek,'Magazine runtime geometry absent')
                out.update(manual_reload=r['native_family'],supply_keys=[e['magazine_catalog_key']],geometry_digest=g['profile_digest'])
            if family in ('bolt_magazine','tube_pump'):
                cy=cycles[key]
                need(cy['native_cycle_admission'] and cy['family']==('Bolt' if family=='bolt_magazine' else 'Pump'),'Cycle admission absent')
                need(c in cycle_header and cy['descriptor_digest'] in cycle_header,'Exact runtime cycle absent')
                out['cycle_descriptor']=cy['descriptor_digest']
                if family=='bolt_magazine':
                    need(bolts[key]==body['mesh'],'Bolt mesh differs')
                    out['mechanism']='bolt'
                else:
                    r=shotguns[key];group=shell_groups[r['geometry_digest']]
                    need(r['physicalReviewed'] and r['mesh']==body['mesh'],'Shotgun geometry differs')
                    need('ShotgunReloadRegistrations['+str(r['native_index'])+']' in shotgun_header and r['descriptor_digest'] in shotgun_native,'Shotgun runtime registration absent')
                    supply=(a,r['mesh'],group['shell']['bone'],body['rig_fingerprint'])
                    need(supply in cache,'Shotgun shell cache absent')
                    out.update(manual_reload='single_shell',mechanism='pump',supply_keys=[list(supply)],geometry_digest=r['geometry_digest'])
            elif family=='belt_feed':
                r=feeds[key]
                need(r['mesh']==body['mesh'] and r['rig']==body['rig_fingerprint'],'Feed mesh/rig differs')
                need('pouchBone' in r['block'] or 'M60Geometry{' in r['block'],'Feed pouch geometry absent')
                need('chain' in r['block'] or 'M60MeasuredPoses' in r['block'],'Feed chain geometry absent')
                out.update(manual_reload='belt_feed',mechanism='feed_cover_container_chain',supply_geometry=r['source'],supply_render='owned_native_palette')
            elif family in ('revolver_cylinder','clip_or_exact_configuration_review'):
                sel,model=fixed[key]
                need(sel['mesh']==body['mesh'] and sel['rigFingerprint']==body['rig_fingerprint'],'Fixed geometry mesh/rig differs')
                need(identity(a,sel['configurationSha256'],sel['configurationGuid'])==row['identity'],'Fixed identity differs')
                need(c in fixed_header and c in fixed_native,'Fixed runtime/native selector absent')
                keys=[cachekey(p) for p in model['body_cache_entries']]
                need(all(k in cache for k in keys),'Fixed ammunition cache absent')
                out.update(manual_reload=model['interaction']['kind'],mechanism=model['label'],supply_keys=[list(k) for k in keys])
            if key in slides:
                sl=slides[key]
                need(sl['mesh']==body['mesh'] and sl['rig']==body['rig_fingerprint'],'Slide geometry mesh/rig differs')
                out['mechanism']='pistol_slide'
            if re.search(r'/[^/]+_hg_|/M1911',c) and family!='revolver_cylinder':
                need(key in slides,'Pistol slide absent')
            need('manual_reload' in out,'Unhandled firearm family')
            out['metadata_complete']=True
        except (ValueError,KeyError) as ex:
            out['errors'].append(str(ex))
        results.append(out)
    summary=dict(configurations=len(results),asset_names=len({r['asset'] for r in results}),
        metadata_complete=sum(r['metadata_complete'] for r in results),
        families=dict(collections.Counter(r['family'] for r in results)),
        support_profiles=sum(map(len,supports.values())),body_selectors=len(bodies),cache_records=len(cache),
        magazine_profiles=len(magazines),pump_profiles=len(shotguns),bolt_profiles=len(bolts),
        feed_profiles=len(feeds),pistol_slide_profiles=len(slides),fixed_container_profiles=len(fixed))
    report=dict(schema='fvr.bc2.exact_firearm_coverage.v1',summary=summary,
        complete=all(r['metadata_complete'] for r in results),
        evidence_level='exact metadata/header reference join; compilation and runtime checks are separate',
        installed=False,headset_accepted=False,inputs=pins,rows=results)
    (HERE/'coverage.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(summary))
    for r in results:
        if r['errors']:print(r['configuration'],r['errors'])
    return 0 if report['complete'] else 1


if __name__=='__main__':
    raise SystemExit(run())
