"""Read named local weapon/arms assets and emit derived coverage only, never assets."""
from pathlib import Path
import hashlib,json,math,struct,sys
from inspect_bc2_mesh_asset import Archive,cstring,bone_hash,sha

from bc2_mesh_geometry import need,metadata,geometry

def derive(game, native_path):
    base=Path(game)/"Dist/win32"
    specs=[('spas','async/weapon/ul_shg_spas-12-00.fbrb','Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh'),
           ('xm8','async/weapon/sp_rgl_xm8_scoped-00.fbrb','Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh'),
           ('acog','levels/sp_common/level-00.fbrb','Objects/Weapons/Unlock/ACOG_4X/US_ACOG_4X_Mesh'),
           ('arms_standard','levels/sp_common/level-00.fbrb','Characters/US/US_FP_Arms/US_FP_ArmsStandard_Mesh'),
           ('arms_assault','levels/sp_common/level-00.fbrb','Characters/US/US_FP_Arms/US_FP_Assault_Mesh')]
    names=[f'jntWpn_{n}' for n in range(100)]+['jntWpn_Flash','jntWpnwpnJnt_16','jntWpnwpnJnt_14']
    names += [side+bone for side in ('Left','Right') for bone in ('Arm','ForeArm','Hand','Shoulder','ArmRoll','ForeArmRoll')]
    names += [side+'Hand'+finger+str(n) for side in ('Left','Right') for finger in ('Thumb','Index','Middle','Ring','Pinky') for n in range(1,4)]
    names += ['Root','Spine','Spine1','Spine2','Hips','Head','Neck']
    known={bone_hash(n):n for n in names};need(len(known)==len(names),'hash collision')
    resources=[]
    for role,archive,prefix in specs:
        a=Archive(base/archive)
        # Archive is case sensitive; the authored arms MeshData uses Us instead
        # of US. Resolve one exact case-insensitive identity, rejecting aliases.
        def exact(name):
            hits=[e for e in a.entries if e.flags==65536 and e.name.lower()==name.lower()]
            need(len(hits)==1,'missing/ambiguous resource: '+name);return hits[0].name
        setname=exact(prefix+'.res');setdata=a.read_selected([setname])[setname];lods=metadata(setdata)
        data_names=[exact(prefix+f'_lod{n}_data.res') for n in range(len(lods))]
        blobs=a.read_selected(data_names);outlods=[]
        for lod,name in zip(lods,data_names):
            sections=geometry(blobs[name],lod,known)
            outlods.append(dict(lod=lod['lod'],resource=name,sha256=sha(blobs[name]),bytes=len(blobs[name]),
                                palette=[dict(id=k,hash=f'{h:08x}',name=known.get(h)) for k,h in lod['palette'].items()],sections=sections))
        resources.append(dict(role=role,archive=str(a.path),index_sha256=a.index_sha256,resource=setname,sha256=sha(setdata),lods=outlods))
    weapon_hashes=set();arm_hashes=set()
    for resource in resources:
        into=arm_hashes if resource['role'].startswith('arms_') else weapon_hashes
        for lod in resource['lods']:
            for section in lod['sections']:into.update(section['weighted_hashes'])
    need(not weapon_hashes&arm_hashes,'weapon and arm vertex influences overlap')
    need(all(known.get(int(h,16),'').startswith('jntWpn') for h in weapon_hashes),'unrecognized weapon influence '+str([(h,known.get(int(h,16))) for h in weapon_hashes if not known.get(int(h,16),'').startswith('jntWpn')]))
    native_path=Path(native_path)
    native_bytes=native_path.read_bytes();native=json.loads(native_bytes)
    rows=[r for r in native['gameplay']['rig_publication']['weapon_profile_samples'] if r['asset_name']=='SPAS12_sp' and r['weapon_bones_complete']]
    need(bool(rows) and all(r['skeleton']=='fnv1a64:a7f219a1426216ab' for r in rows),'exact saved native rig unavailable')
    topologies={tuple(sorted((b['name'],b['parent_name']) for b in r['native_weapon_bones'])) for r in rows}
    need(len(topologies)==1,'saved native topology differs');parents=dict(next(iter(topologies)))
    for h in weapon_hashes:
        name=known[int(h,16)];visited=set()
        while name!='jntWpn_1':
            need(name in parents and name not in visited,'weighted bone not in exact native weapon branch')
            visited.add(name);name=parents[name]
    return dict(schema='fvr.bc2.empty_hands_asset_coverage.v1',read_only=True,assets_exported=False,resources=resources,
                native_rig_proof={'path':str(native_path),'sha256':sha(native_bytes),'fingerprint':'fnv1a64:a7f219a1426216ab',
                                  'all_weighted_names_resolve':True,'all_weighted_names_descend_from_weapon':True},
                weapon_bones=[known[int(h,16)] for h in sorted(weapon_hashes)],weapon_hashes=sorted(weapon_hashes),
                arm_weighted_hashes=sorted(arm_hashes),weighted_hashes_disjoint=True,
                native_current_arms_asset_verified=False,submitted_skin_visibility_verified=False,
                limits=['Authored skin coverage is not current submitted section visibility.',
                        'Only private final skin palette entries can collapse; never mutate hierarchy/native animation.',
                        'All-zero xyz for every weighted weapon bone maps all normalized weapon vertices to a single point under linear skinning; hand matrices remain byte-identical.',
                        'Native shader behavior/material effects and current arms asset association require bounded native validation before acknowledging holster visibility.'])

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--native-trace', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args=parser.parse_args()
    if args.output.exists(): parser.error('Preserve previous evidence: choose a new output')
    r=derive(args.game,args.native_trace);p=args.output;p.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({'output':str(p),'weapon_bones':r['weapon_bones'],'arm_hashes':len(r['arm_weighted_hashes']),
                      'resources':[(a['role'],[(l['lod'],len(l['sections']),sum(s['vertices'] for s in l['sections'])) for l in a['lods']]) for a in r['resources']]}))
