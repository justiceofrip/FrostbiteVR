"""One exact right-hand/bolt pose packet per distinct authored content set."""
import json,math,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools'))
import bc2_authored_action_geometry as action
import bc2_authored_magazine_geometry as mag
from bc2_magazine_contact_batch import ArchivePool
from bc2_weapon_animation_pipeline import Clip

def main():
    source=Path('G:/Unity/VRMMO/bc2vr-recovery/action379-batched/actions.json')
    if mag.sha(source.read_bytes())!='c1e775329c56c32bec9ed49392d5b080f4b9f50bbd66c0df15945f98d9dbd7b8':raise ValueError('Action catalog changed')
    actions=json.loads(source.read_text())
    jobs=json.loads((ROOT/'handoff/tasks/WEAPONS/20261030-sniper43/descriptor-jobs.json').read_text())
    wanted={r['configuration']['assetPath']:r for r in jobs['descriptors'] if r['configuration']['values'].get('fireLogicType')==1}
    inputs=[]
    for inp in actions['inputs']:
        for key in ('bindings','meshes'):
            if mag.sha(Path(inp[key]['path']).read_bytes())!=inp[key]['sha256']:raise ValueError('Pinned source changed')
        inputs.append((inp,json.loads(Path(inp['bindings']['path']).read_text()),json.loads(Path(inp['meshes']['path']).read_text())))
    game=Path('G:/New folder (2)/Battlefield Bad Company 2');pool=None;groups={};selected=[]
    fingers=[n.replace('LeftHand','RightHand') for n in mag.FINGERS]
    for inp,bindings,meshes in inputs:
        for profile in bindings['profiles']:
            path=profile['weapon']['resource'].removesuffix('.dbx')
            if path not in wanted:continue
            records=[r for r in actions['records'] if r['binding_digest']==profile['binding_digest'] and r['authored_role']=='1p_boltaction']
            if len(records)!=1:raise ValueError(('Exact bolt action missing',path,len(records)))
            record=records[0];src=record['source']
            key=(src['mesh_sha256'],src['lod_sha256'],src['sha256'],src['reference_clip_sha256'],profile['skeleton_sha256'])
            group=mag.sha('|'.join(key).encode())[:16]
            selected.append({'configuration':wanted[path]['configuration'],'binding_digest':profile['binding_digest'],
                'mesh':profile['configured_mesh_path'],'rig':profile['rig_fingerprint'],'content':group,
                'native_descriptor_digest':wanted[path]['descriptor_digest']})
            if group in groups:continue
            if '/bu_sni_m95/' in profile['configured_mesh_path'].lower():
                # Reuse the already derived and independently joined M95
                # stroke/grasp. Exact content keys still bind each variant.
                groups[group]={'asset':profile['native_asset_name'],'source':src,'binding':profile['binding_digest'],
                    'mesh':profile['configured_mesh_path'],'rig':profile['rig_fingerprint'],
                    'reuse':'Bc2M95BoltGeometry.h and Bc2BoltRuntimeGeometry.h M95ObservedBoltCalibration235'}
                continue
            if pool is None:
                s=profile['skeleton_source'];pool=ArchivePool(game,s['archive'],s['archive_index_sha256'])
            ctx=action.context(game,profile,meshes,pool);sk=ctx['skeleton']
            archive=pool(game/src['archive'])
            if archive.index_sha256!=src['archive_index_sha256']:raise ValueError('Clip archive changed')
            clip=Clip(mag.exact_resource(archive,src['resource'],'GrannyAnimation',src['sha256']))
            names=['jntWpn_3','RightHand',*fingers]
            reference=ctx['reference'].evaluate(sk,0.,names)['weapon_relative']
            part=ctx['parts']['jntWpn_3']
            if part['mixed_triangles'] or not part['triangles']:raise ValueError('Independent bolt geometry required')
            samples=[]
            for i in range(math.ceil(clip.duration*60)+1):
                t=min(i/60,clip.duration);poses=clip.evaluate(sk,t,names)['weapon_relative']
                inverse=mag.inverse_rigid(mag.matrix(poses['jntWpn_3']))
                points=[mag.multiply(mag.matrix(poses[n]),inverse)[12:15] for n in fingers if n.endswith('1')]
                distance=min(math.dist(point,vertex) for point in points for vertex in part['points'])
                samples.append({'time':t,'poses':poses,'knuckle_to_part_m':distance})
            groups[group]={'asset':profile['native_asset_name'],'source':src,'binding':profile['binding_digest'],
                'mesh':profile['configured_mesh_path'],'reference':reference,'samples':samples,
                'part_bounds':mag.bounds(part['points']),'triangles':part['triangles'],'rig':sk.fingerprint}
            print(json.dumps({'sampled':profile['native_asset_name'],'frames':len(samples),'content':group}),flush=True)
            save(groups,selected)
    if len({s['configuration']['assetPath'] for s in selected})!=len(wanted):raise ValueError('Missing exact bolt configuration')
    save(groups,selected)

def save(groups,selected):
    (Path(__file__).parent/'samples.json').write_text(json.dumps({'schema':'fvr.bc2.bolt47.exact_samples.v1',
        'groups':groups,'selected':selected},separators=(',',':'),allow_nan=False)+'\n',encoding='utf-8')

if __name__=='__main__':main()
