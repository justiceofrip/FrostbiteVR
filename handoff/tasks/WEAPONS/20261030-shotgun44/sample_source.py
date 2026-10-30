"""Bounded eight-clip authored pose sampling; retain derived transforms only."""
import json, sys, math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools'))
import bc2_authored_action_geometry as action
import bc2_authored_magazine_geometry as mag
from bc2_magazine_contact_batch import ArchivePool
from bc2_weapon_animation_pipeline import Clip
from bc2_authored_grip_bindings import digest

def main():
    source=Path('G:/Unity/VRMMO/bc2vr-recovery/action379-batched/actions.json')
    if mag.sha(source.read_bytes())!='c1e775329c56c32bec9ed49392d5b080f4b9f50bbd66c0df15945f98d9dbd7b8':raise ValueError('Pinned action catalog differs')
    catalog=json.loads(source.read_text());game=Path('G:/New folder (2)/Battlefield Bad Company 2')
    inputs=[]
    for inp in catalog['inputs']:
        if mag.sha(Path(inp['bindings']['path']).read_bytes())!=inp['bindings']['sha256']:raise ValueError('Binding source hash differs')
        bindings=json.loads(Path(inp['bindings']['path']).read_text())
        inputs.append((inp,bindings))
    results=[];contexts={};pool=None
    for index in (189,190,200,201,205,206,386,387):
        record=catalog['records'][index]
        matches=[(inp,p) for inp,b in inputs for p in b['profiles'] if p['binding_digest']==record['binding_digest']]
        if len(matches)!=1:raise ValueError(('binding count',index,len(matches)))
        inp,profile=matches[0]
        if profile['binding_digest'] not in contexts:
            common=profile['skeleton_source']
            if pool is None:pool=ArchivePool(game,common['archive'],common['archive_index_sha256'])
            if mag.sha(Path(inp['meshes']['path']).read_bytes())!=inp['meshes']['sha256']:raise ValueError('Mesh source hash differs')
            meshes=json.loads(Path(inp['meshes']['path']).read_text())
            contexts[profile['binding_digest']]=action.context(game,profile,meshes,pool)
        ctx=contexts[profile['binding_digest']];src=record['source'];archive=pool(game/src['archive'])
        if archive.index_sha256!=src['archive_index_sha256']:raise ValueError('Archive identity')
        clip=Clip(mag.exact_resource(archive,src['resource'],'GrannyAnimation',src['sha256']))
        names=[n for n in ctx['skeleton'].names if n.startswith('jntWpn')]+['LeftHand',*mag.FINGERS]
        samples=[clip.evaluate(ctx['skeleton'],min(i/60,clip.duration),names) for i in range(math.ceil(clip.duration*60)+1)]
        result={'record_index':index,'record_digest':record['digest'],'binding_digest':record['binding_digest'],
                'mesh':record['configured_mesh'],'source':src,'rig_fingerprint':record['rig_fingerprint'],
                'duration_s':clip.duration,'parts':record['parts'],
                'samples':[{'time':s['time_seconds'],'poses':s['weapon_relative']}for s in samples]}
        if index in (189,200,205,386):
            pump='jntWpn_5' if index==386 else 'jntWpn_4'
            rail=next(r for r in record['linear_candidates'] if r['part']==pump)
            axis=rail['axis'];closed=rail['reference_part'];travel=rail['decoded_travel']['maximum_m']
            peak=rail['decoded_travel']['maximum_time_seconds']
            def evaluate(t):return clip.evaluate(ctx['skeleton'],t,names)['weapon_relative']
            def position(t):
                pose=clip.evaluate(ctx['skeleton'],t,[pump])['weapon_relative'][pump]
                return sum((pose[12+k]-closed[12+k])*axis[k] for k in range(3))
            companions=[p for p in record['parts'] if p['bone']!=pump and p['motion']['kind']!='static']
            phases=[]
            for leg in ('rear','forward'):
                poses=[]
                for step in range(17):
                    amount=travel*step/16
                    if step==0:
                        poses.append({'progress':0.,'source':'static_authored_closed','time':None,
                            'poses':{p['bone']:p['static_authored_closed_candidate'] for p in companions}});continue
                    if step==16:time=peak
                    else:
                        grid=sorted(set([0.,peak,clip.duration,*[s['time_seconds'] for s in samples]]))
                        intervals=[(a,b)for a,b in zip(grid,grid[1:]) if
                                   ((b<=peak and position(a)<=amount<=position(b)) if leg=='rear' else
                                    (a>=peak and position(a)>=amount>=position(b)))]
                        if not intervals:raise ValueError(('No monotonic pump crossing',index,leg,step))
                        low,high=intervals[0]
                        for _ in range(36):
                            mid=(low+high)/2
                            if (position(mid)<amount)==(leg=='rear'):low=mid
                            else:high=mid
                        time=(low+high)/2
                    evaluated=evaluate(time)
                    poses.append({'progress':step/16,'source':'exact_evaluated_physical_progress','time':time,
                                  'pump_error_m':abs(position(time)-amount),
                                  'poses':{p['bone']:evaluated[p['bone']]for p in companions}})
                phases.append(poses)
            result['pump_phase_samples']={'part':pump,'rail':rail,'rear':phases[0],'forward':phases[1]}
        result['digest']=digest(result);results.append(result)
        print(index,record['asset'],clip.duration,len(samples),flush=True)
    payload={'schema':'fvr.bc2.shotgun_exact_samples.v1','actions_sha256':mag.sha(source.read_bytes()),'records':results}
    (Path(__file__).parent/'samples.json').write_text(json.dumps(payload,separators=(',',':'),allow_nan=False)+'\n',encoding='utf-8')

if __name__=='__main__':main()
