"""Bounded saved-trace audit of actual BC2 hide/show Pack and eye samples.

No live process access, image editing, native calls or GPU visibility inference.
Mechanical acceptance and availability of pixels for visual review are separate.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

SCHEMA='fvr.bc2.weapon_visibility_audit.v1'
PHASE={0:'warmup',1:'baseline',2:'hidden',3:'restored',4:'done',5:'failed'}
FAILURE={0:'none',1:'clock',2:'sequence timeout',3:'stale/untracked input',4:'input rollback/restamp',
         5:'unsupported weapon',6:'configured snapshot missing/stale',7:'changed full owner',
         8:'owner/state cancellation',9:'cleanup before completion',10:'initial evidence warmup timeout'}
class MissingProof(ValueError):pass
def need(value,message):
    if not value:raise MissingProof(message)
def integer(value):
    need(type(value) is int,'expected recorded integer');return value
def positive(value):
    need(integer(value)>0,'expected positive recorded integer');return value
def sha(path:Path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def unique_object(items):
    obj={}
    for key,value in items:
        if key in obj:raise ValueError('duplicate JSON field: '+key)
        obj[key]=value
    return obj
def parse(text):
    return json.loads(text,object_pairs_hook=unique_object,
        parse_constant=lambda x:(_ for _ in ()).throw(ValueError('non-finite JSON number')))
def load(path):
    need(path.stat().st_size<=64*1024*1024,'report exceeds 64 MiB limit')
    return parse(path.read_text(encoding='utf-8-sig'))
def fresh(observed,deadline,now,limit=150_000_000):
    need(0<integer(observed)<=integer(now)<integer(deadline) and deadline-observed<=limit,
         'source/receipt was expired, future dated, or its original lease was extended')

def audit(trace,manifest,completion,receiver,pairs,receiver_folder:Path):
    g=trace.get('gameplay',{});probe=g.get('weapon_visibility_probe',{});rig=g.get('rig_publication',{})
    visibility=rig.get('weapon_visibility',{});sections={};samples=[];unusable=[]
    def section(name,fn):
        try:sections[name]={'passed':True,'evidence':fn()}
        except (MissingProof,KeyError,TypeError,IndexError,ValueError,OSError) as error:
            sections[name]={'passed':False,'reason':str(error)}

    def identity():
        need(positive(trace['pid'])==manifest['pid'],'trace/manifest PID differs')
        need(manifest['weapon_visibility_probe'] is True and probe['requested'] is True,'visibility diagnostic not enabled')
        need(manifest['duration_ms']==15000 and manifest['until_host_exit'] is False,'wrong bounded diagnostic duration')
        for name in ('physical_reload','physical_reload_probe','reload_request_probe','reload_hold_probe','reload_round_probe',
                     'optic_filter_observe','body_inventory','death_probe','equip_probe','rig_pulse','sight_flip','pass_evidence'):
            need(manifest.get(name) is False,'conflicting or missing fixture marker: '+name)
        need(manifest.get('physical_reload_repeat_probe',False) is False,'repeat reload fixture conflicts')
        for name in ('game_sha256','probe_sha256','bootstrap_sha256'):
            value=manifest[name];need(isinstance(value,str) and len(value)==64 and all(c in '0123456789abcdefABCDEF' for c in value),'missing binary hash: '+name)
        need(receiver['weapon_visibility_fixture'] is True and receiver['headset_tested'] is False,'receiver mode/headset declaration differs')
        need(probe['empty_hands_acknowledged'] is False and probe['gpu_visibility_verified'] is False and probe['headset_verified'] is False,'evidence scope incorrectly elevated')
        return {'pid':trace['pid'],'duration_ms':15000,'recorded_binary_hashes':{k:manifest[k] for k in ('game_sha256','probe_sha256','bootstrap_sha256')}}

    def cleanup():
        need(trace['hooks_disabled'] is True,'hooks not drained/disabled')
        need(completion['bootstrap_exit']==0 and completion['game_exited'] is False and completion['new_crash_report'] is False and completion['game_responding'] is True,'game/bootstrap completion failed')
        need(integer(completion['stability_observation_ms'])>=15000,'post-session game stability missing')
        need(trace['native_stream']['camera_restore_failures']==0,'camera restoration failure')
        return {'hooks_disabled':True,'stability_observation_ms':completion['stability_observation_ms']}

    def delivery():
        need(receiver['consumed_pairs']==240 and receiver['native_tracking_transport_verified'] in (True,1),'receiver did not verify240 pairs')
        need(receiver['async_timeouts']==0,'receiver frame request timed out')
        actual=[r for r in pairs if 'pair' in r]
        need(len(actual)==240 and [integer(r['pair']) for r in actual]==list(range(240)),'pair records missing/duplicate/reordered')
        need(not any('copy_failed' in r for r in pairs),'receiver copy failure')
        need(all(positive(a['native_frame'])<positive(b['native_frame']) for a,b in zip(actual,actual[1:])),'native frame order does not advance')
        need(all(positive(a['tracking'])<=positive(b['tracking']) for a,b in zip(actual,actual[1:])),'tracking sequence regressed')
        stream=trace['native_stream'];need(stream['published']==240 and stream['consumed']==240 and stream['captured_eyes']==480,'producer counts differ')
        need(stream['gpu_failure_code']==0,'producer GPU failure')
        return {'pairs':240,'eyes':480,'timeouts':0}

    def mechanical():
        need(probe['phase']==4 and probe['failure']==0 and probe['paired_private_palette_sequence_verified'] is True,'probe did not complete restored phase')
        counts=probe['receipts'];need(len(counts)==3 and all(integer(n)>=2 for n in counts),'baseline/hidden/restored paired receipt counts missing')
        for k in ('player','soldier','weak','weapon','actor_generation','native_equip_generation','physical_equip_generation','space'):positive(probe[k])
        need(visibility['private_palette_consumer_integrated'] is True,'private palette consumer absent')
        need(visibility['native_animation_written'] is False and visibility['submitted_visibility_verified'] is False and visibility['headset_verified'] is False,'private palette scope incorrectly elevated')
        need(rig['native_animation_written'] is False and rig['source_changes']==0 and rig['packing_failures']==0,'original source or packed destination comparison failed')
        need(integer(visibility['verified_hidden_copies'])>=4 and integer(visibility['verified_shown_copies'])>=8 and integer(visibility['paired_pack_receipts'])>=6,'actual native Pack sequence not observed')
        rows=probe['rows'];need(isinstance(rows,list) and len(rows)<=64,'phase row bound exceeded')
        seen={};accepted={1:[],2:[],3:[]};last_now=0;last_phase=0;last_input=0;phase_started={}
        for row in rows:
            phase=integer(row['phase']);now=integer(row['now_ns']);need(now>=last_now and phase>=last_phase,'phase/clock regressed');last_now=now;last_phase=phase
            if phase not in accepted:continue
            sequence=positive(row['input']);need(sequence>=last_input,'original input sequence regressed');last_input=sequence
            phase_started.setdefault(phase,now)
            need(row['request']==phase and row['hidden'] is (phase==2),'phase request/hide identity differs')
            fresh(row['source_observed_ns'],row['source_deadline_ns'],now)
            source=(row['source_observed_ns'],row['source_deadline_ns'])
            need(sequence not in seen or seen[sequence]==source,'same input was restamped');seen[sequence]=source
            if row['draw_serial']:
                positive(row['draw_serial']);need(0<integer(row['receipt_input'])<=sequence,'receipt belongs to future/missing input')
                fresh(row['receipt_observed_ns'],row['receipt_deadline_ns'],now)
                need(integer(row['ordinary_entries_preserved'])>0 and integer(row['weighted_entries']) in (9,16),'exact known weighted palette/ordinary preservation missing')
                accepted[phase].append(row)
        need(all(accepted.values()),'accepted saved receipt row missing for baseline/hidden/restored')
        need(phase_started[2]-phase_started[1]>=2_000_000_000 and phase_started[3]-phase_started[2]>=2_000_000_000,
             'recorded phase boundaries did not preserve the two-second sampling windows')
        need(len({r['weighted_entries'] for rs in accepted.values() for r in rs})==1,'weapon mesh family changed during sequence')
        # IDs need not be consecutive, but saved evidence cannot reuse one copy
        # pair as both visible and hidden receipts.
        draws=[{r['draw_serial'] for r in accepted[p]} for p in (1,2,3)]
        need(not(draws[0]&draws[1] or draws[0]&draws[2] or draws[1]&draws[2]),'draw receipt reused across phases')
        return {'receipts':counts,'source_changes':0,'packing_failures':0,
                'weighted_entries':accepted[1][0]['weighted_entries'],'ordinary_entries_preserved_min':min(r['ordinary_entries_preserved'] for rs in accepted.values() for r in rs),
                'native_owner':{k:probe[k] for k in ('player','soldier','weak','weapon','actor_generation','native_equip_generation','physical_equip_generation','space')}}

    def suppression():
        values=g['weapon_visibility_action_suppression'];need(positive(values['verified_commits'])>0 and values['failures']==0,'native fire/ADS/reload/grenade suppression not verified')
        return values

    def captured_samples():
        eye_rows=trace['weapon_visibility_eyes'];need(isinstance(eye_rows,list) and len(eye_rows)<=512,'eye evidence bound exceeded')
        byframe={}
        for row in eye_rows:byframe.setdefault(integer(row['native_frame']),[]).append(row)
        captures=trace['world_color_captures'];need(len(captures)>=3,'measured native eye target dimensions missing')
        targets=[]
        for capture in captures[1:3]:
            need(capture['captured'] is True and capture['hresult']==0,'native eye target measurement failed')
            target=capture['target'];w,h=positive(target['width']),positive(target['height'])
            need(w<=4096 and h<=4096 and target['samples']==1 and target['format'] in (28,29,87,91),'unsupported measured native eye target')
            targets.append((w,h))
        need(targets[0]==targets[1],'left/right measured eye sizes differ')
        width,height=targets[0];expected=width*height*4;folder=receiver_folder.resolve()
        actual=[r for r in pairs if 'pair' in r]
        need(len(actual)<=240,'too many receiver pairs')
        for pair in actual:
            number=integer(pair['pair']);need(0<=number<240,'pair number outside fixture bound')
            paths=[folder/f'pair-{number}-eye-{eye}.rgba' for eye in (0,1)]
            if not any(p.exists() for p in paths):continue
            rows=byframe.get(pair['native_frame'],[])
            try:
                need(len(rows)==2 and {r['eye'] for r in rows}=={0,1},'missing/duplicate native eye rows')
                rows=sorted(rows,key=lambda r:r['eye']);phase=integer(rows[0]['phase'])
                need(phase in (1,2,3) and all(r['phase']==phase and r['request']==phase and r['hidden_intent'] is (phase==2) for r in rows),'both eyes did not record one stable phase/request')
                for r in rows:
                    need(r['paired_pack_receipt'] is True and positive(r['draw_serial'])>0 and 0<integer(r['receipt_input'])<=positive(r['input']),'eye phase lacks current paired receipt')
                    fresh(r['receipt_observed_ns'],r['receipt_deadline_ns'],r['now_ns'])
                pixels=[]
                for eye,path in enumerate(paths):
                    need(path.is_file() and path.resolve().is_relative_to(folder),'missing/outside receiver pixel file')
                    need(path.stat().st_size==expected,'raw pixel byte count differs from measured native eye dimensions')
                    pixels.append({'eye':eye,'path':str(path.resolve()),'width':width,'height':height,'channels':4,'layout':'RGBA8',
                        'bytes':expected,'sha256':sha(path),'draw_serial':rows[eye]['draw_serial'],'receipt_input':rows[eye]['receipt_input']})
                samples.append({'pair':number,'native_frame':pair['native_frame'],'phase':PHASE[phase],'phase_id':phase,'request':phase,'pixels':pixels,
                    'dimension_evidence':'measured native eye targets; raw byte count verified; receiver descriptor not separately logged',
                    'gpu_visibility_verified':False})
            except (MissingProof,KeyError,TypeError,ValueError,OSError) as error:
                unusable.append({'pair':number,'native_frame':pair.get('native_frame'),'reason':str(error)})
        covered={s['phase_id'] for s in samples}
        need(covered=={1,2,3},'saved both-eye samples unavailable for: '+','.join(PHASE[p] for p in (1,2,3) if p not in covered))
        return {'sample_count':len(samples),'phases':[PHASE[p] for p in sorted(covered)],'width':width,'height':height,'visibility_requires_pixel_review':True}

    for name,fn in [('run_identity',identity),('cleanup',cleanup),('stereo_delivery',delivery),('private_palette_sequence',mechanical),
                    ('input_suppression',suppression),('saved_eye_samples',captured_samples)]:section(name,fn)
    mechanical_names=('run_identity','cleanup','stereo_delivery','private_palette_sequence','input_suppression')
    mechanical_ok=all(sections[k]['passed'] for k in mechanical_names)
    counts=probe.get('receipts',[])
    status='mechanical_pass_pending_visual' if mechanical_ok else 'mechanical_evidence_incomplete'
    if isinstance(counts,list) and len(counts)==3:
        for p,label in ((0,'no_baseline_receipts'),(1,'no_hide_receipts'),(2,'no_restore_receipts')):
            if counts[p]==0:status=label;break
    return {'schema':SCHEMA,'status':status,'mechanical_verified':mechanical_ok,
        'visual_review_ready':mechanical_ok and sections['saved_eye_samples']['passed'],'gpu_visibility_verified':False,'headset_verified':False,
        'empty_hands_acknowledged':False,'sections':sections,'sampleable_eye_pairs':samples,'unusable_saved_pairs':unusable,
        'diagnosis':{'phase':PHASE.get(probe.get('phase'),'unknown/missing'),'failure':FAILURE.get(probe.get('failure'),'unknown/missing'),
            'phase_receipt_counts':probe.get('receipts'),'reported_source_changes':rig.get('source_changes'),
            'reported_packing_failures':rig.get('packing_failures'),'verified_hidden_copies':visibility.get('verified_hidden_copies'),
            'verified_shown_copies':visibility.get('verified_shown_copies')}}

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--trace',type=Path,required=True)
    parser.add_argument('--receiver',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();paths=[args.trace/'native-trace.json',args.trace/'manifest.json',args.trace/'completion.json',args.receiver/'result.json',args.receiver/'pairs.jsonl']
    try:
        values=[load(path) for path in paths[:4]]
        need(paths[4].stat().st_size<=4*1024*1024,'pair log exceeds4 MiB bound')
        pairs=[parse(line) for line in paths[4].read_text(encoding='utf-8-sig').splitlines() if line.strip()]
        need(len(pairs)<=1024,'pair row count exceeds1024')
        report=audit(*values,pairs,args.receiver)
        report['sources']=[{'path':str(path.resolve()),'bytes':path.stat().st_size,'sha256':sha(path)} for path in paths]
        report['audit_source']={'path':str(Path(__file__).resolve()),'sha256':sha(Path(__file__))}
    except (OSError,MissingProof,ValueError,TypeError,KeyError,IndexError,AttributeError) as error:
        report={'schema':SCHEMA,'status':'input_error','mechanical_verified':False,'visual_review_ready':False,'gpu_visibility_verified':False,
                'headset_verified':False,'empty_hands_acknowledged':False,'input_error':str(error)}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps({k:report.get(k) for k in ('status','mechanical_verified','visual_review_ready','gpu_visibility_verified','input_error')}))
    return 0 if report['mechanical_verified'] else 1
if __name__=='__main__':raise SystemExit(main())
