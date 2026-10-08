"""Strict bounded saved-trace body holster audit. No live access or pixel inference."""
from __future__ import annotations
import argparse,json,math
from pathlib import Path
from audit_weapon_visibility import need,integer,positive,sha,load,parse,fresh,MissingProof
from preflight_body_holster_probe import validate as validate_counts
PHASE={0:'warmup',1:'baseline',2:'reach_holster',3:'press_holster',4:'wait_empty',5:'free_a',6:'free_b',7:'reach_draw',8:'press_draw',9:'wait_draw',10:'restored',11:'done',12:'failed'}
SCHEMA='fvr.bc2.body_holster_audit.v1'
def audit(trace,manifest,completion,receiver,pairs,folder,before,after):
    g=trace.get('gameplay',{});probe=g.get('body_holster_probe',{});rig=g.get('rig_publication',{})
    sections={};samples=[];unusable=[]
    def section(name,fn):
        try:sections[name]=dict(passed=True,evidence=fn())
        except (MissingProof,KeyError,TypeError,IndexError,ValueError,OSError) as e:sections[name]=dict(passed=False,reason=str(e))
    def diagnostic_profile():
        # Missing selectors are accepted only for historical SPAS-only reports.
        profile=integer(manifest.get('body_holster_diagnostic_profile',1))
        need(profile in (1,2,3) and integer(probe.get('diagnostic_profile',1))==profile,'diagnostic profile differs')
        asset='SPAS12_sp' if profile==1 else 'XM8_sp_s'
        need(probe.get('asset','SPAS12_sp')==asset,'diagnostic asset differs')
        need(manifest.get('body_holster_profile_mask',0)==0,'diagnostic enabled production profile')
        return profile,asset
    def identity():
        profile,asset=diagnostic_profile()
        need(positive(trace['pid'])==manifest['pid'],'PID differs')
        need(manifest['body_holster_probe'] is True and probe['requested'] is True and receiver['body_holster_fixture'] is True,'wrong fixture')
        need(manifest['duration_ms']==15000 and manifest['until_host_exit'] is False,'unbounded diagnostic')
        need(manifest['body_inventory'] is True and manifest['physical_reload'] is True,'actual body/physical consumers missing')
        for k in ('weapon_visibility_probe','physical_reload_probe','physical_reload_repeat_probe','reload_request_probe','reload_hold_probe','reload_round_probe','optic_filter_observe','death_probe','equip_probe','rig_pulse','sight_flip','pass_evidence'):
            need(manifest.get(k) is False,'conflicting/missing fixture marker '+k)
        need(manifest['body_holster_input_accepted'] is False and probe['production_input_accepted'] is False and probe['gpu_visibility_verified'] is False and probe['headset_verified'] is False,'test permission promoted to acceptance')
        for k in ('game_sha256','probe_sha256','bootstrap_sha256'):
            h=manifest[k];need(isinstance(h,str) and len(h)==64 and all(c in '0123456789abcdefABCDEF' for c in h),'missing binary hash')
        need(receiver['headset_tested'] is False,'synthetic trial is not headset evidence')
        return dict(pid=trace['pid'],duration_ms=15000,diagnostic_profile=profile,asset=asset,binary_hashes={k:manifest[k] for k in ('game_sha256','probe_sha256','bootstrap_sha256')})
    def cleanup():
        need(trace['hooks_disabled'] is True and trace['native_stream']['camera_restore_failures']==0,'hook/camera restoration failed')
        need(completion['bootstrap_exit']==0 and completion['game_exited'] is False and completion['new_crash_report'] is False and completion['game_responding'] is True,'game/bootstrap failure')
        need(integer(completion['stability_observation_ms'])>=15000,'post-run stability missing');return dict(hooks_drained=True)
    def delivery():
        need(receiver['consumed_pairs']==240 and receiver['async_timeouts']==0,'240 pairs/zero timeouts required')
        actual=[r for r in pairs if 'pair' in r];need(len(actual)==240 and [integer(r['pair']) for r in actual]==list(range(240)),'missing/reordered pairs')
        need(not any('copy_failed' in r for r in pairs),'copy failure')
        need(all(positive(a['native_frame'])<positive(b['native_frame']) for a,b in zip(actual,actual[1:])),'frame order regressed')
        n=trace['native_stream'];need(n['published']==240 and n['consumed']==240 and n['captured_eyes']==480 and n['gpu_failure_code']==0,'producer counts/GPU failure')
        return dict(pairs=240,timeouts=0)
    def lifecycle():
        need(probe['phase']==11 and probe['failure']==0,'diagnostic did not complete')
        for k in ('player','soldier','weak','weapon','actor_generation','native_equip_generation','physical_equip_generation','space'):positive(probe[k])
        original=positive(probe['baseline_claim']);hide=positive(probe['hidden_request']);show=positive(probe['show_request']);need(show!=hide,'hide/show request reused')
        rows=probe['rows'];need(isinstance(rows,list) and 0<len(rows)<=192,'row bound')
        seen={};last_now=0;last_phase=0;last_input=0;free={5:[],6:[]};restored=[];baseline=[]
        for r in rows:
            p=integer(r['phase']);now=integer(r['now_ns']);rank=(15 if p==11 else p) if diagnostic_profile()[0]==3 else p;need(now>=last_now and rank>=last_phase,'clock/phase regression');last_now=now;last_phase=rank
            if p==0:continue
            seq=positive(r['input']);need(seq>=last_input,'input sequence regressed');last_input=seq;fresh(r['observed_ns'],r['deadline_ns'],now)
            source=(r['observed_ns'],r['deadline_ns']);need(seq not in seen or seen[seq]==source,'input restamped');seen[seq]=source
            positive(r['native_tick']);need(r['left_claim']==0,'unexpected left owner')
            if p==1:need(r['right_claim']==original and r['body_phase']==1,'baseline shared GunHold missing');baseline.append(r)
            if p in free:
                need(r['right_claim']==0 and r['body_phase']==3 and r['free_right'] is True and r['hidden'] is True and r['suppressed'] is True and r['request']==hide,'not actual empty hands with hide/suppression')
                positive(r['draw_serial']);free[p].append(r)
            if p==10:
                need(r['body_phase']==1 and positive(r['right_claim'])!=original and r['free_right'] is False,'draw did not reacquire a distinct shared GunHold')
                if diagnostic_profile()[0]==2 or 'blocks_actions' in r:
                    need(r.get('blocks_actions') is False and r.get('allows_gun_hold') is True,'restored weapon action gate remains blocked')
                restored.append(r)
        need(baseline and all(free.values()) and restored,'baseline/free/restore rows missing')
        need(any(r['draw_serial'] and not r['hidden'] for r in restored),'genuine shown receipt at draw commit missing')
        need(max(r['paired_free_copies'] for r in free[6])>min(r['paired_free_copies'] for r in free[5])+4,'actual free-right Pack progression missing')
        # Recorded consumed controller pose, not the fixture command. Two
        # separated poses plus actual free-right copies provide audit anchors;
        # pixels still require a human to judge the arm/hand's appearance.
        a=free[5][-1]['consumed_grip'];b=free[6][-1]['consumed_grip'];need(len(a)==len(b)==3 and all(math.isfinite(x) for x in a+b),'invalid consumed poses')
        need(math.dist(a,b)>.2,'independent free-right motion not exercised')
        need(rig['native_animation_written'] is False and rig['source_changes']==0 and rig['packing_failures']==0,'native source/private pack preservation failed')
        need(positive(rig['free_right_poses'])>0 and positive(rig['free_right_paired_copies'])>0,'actual free-right render pipeline absent')
        v=rig['weapon_visibility'];need(positive(v['verified_hidden_copies'])>0 and positive(v['verified_shown_copies'])>0,'actual hide/show Pack absent')
        return dict(original_gun_claim=original,restored_gun_claim=restored[0]['right_claim'],hidden_request=hide,show_request=show,consumed_hand_separation_m=math.dist(a,b),native_source_changes=0)
    def challenge():
        c=probe['challenge'];need(c and c['staged'] is True and c['committed'] is True and c['unrelated_preserved'] is True,'actual challenge/production suppression commit missing')
        positive(c['input']);positive(c['native_tick']);positive(c['cache']);need(c['request']==probe['hidden_request'],'wrong hidden transaction challenge')
        masks=[0xffffffff,0xffffffff,(1<<12)|(1<<14)|(1<<29),0x72]
        for k in ('before','challenged','written','observed'):need(isinstance(c[k],list) and len(c[k])==4 and all(type(v) is int and 0<=v<=0xffffffff for v in c[k]),'malformed cache words')
        for n,m in enumerate(masks):
            need(c['challenged'][n]==(0x3f800000 if n<2 else c['before'][n]|m),'verified action challenge not staged')
            need(c['written'][n]==(c['before'][n]&~m) and c['observed'][n]==c['written'][n],'production suppression readback differs')
        need(g['body_holster']['suppression_failures']==0 and positive(g['body_holster']['suppression_commits'])>0,'suppression failure reported')
        return c
    def counts():
        _,asset=diagnostic_profile()
        a,b=validate_counts(before,asset),validate_counts(after,asset)
        need(before['pid']==after['pid']==trace['pid'] and before['executable_sha256']==after['executable_sha256'],'native process changed')
        need(before['samples'][-1]['client_owner']==after['samples'][0]['client_owner'],'selected native owner changed')
        if diagnostic_profile()[0]==3:
            from audit_body_holster_fire import firing_evidence
            need(manifest.get('body_holster_fire_probe') is True,'explicit fire selector missing')
            return firing_evidence(g,probe,before,after,a,b)
        need(a==b,'ammo changed across challenge; do not infer no firing')
        return dict(asset=a['asset'],loaded=a['loaded'],reserve=a['reserve'],unchanged=True,scope='authoritative selected native count observations; not all possible firing routes')
    def native_firing():
        _,asset=diagnostic_profile()
        need(diagnostic_profile()[0]==3 and manifest.get('body_holster_fire_probe') is True,'explicit fire selector missing')
        a,b=validate_counts(before,asset),validate_counts(after,asset)
        need(before['pid']==after['pid']==trace['pid'] and before['executable_sha256']==after['executable_sha256'],'native process changed')
        from audit_body_holster_fire import native_firing_evidence
        return native_firing_evidence(g,probe,before,after,a,b)
    def pixels():
        eyes=trace['body_holster_eyes'];need(isinstance(eyes,list) and len(eyes)<=512,'eye row bound');byframe={}
        for row in eyes:byframe.setdefault(integer(row['native_frame']),[]).append(row)
        targets=trace['world_color_captures'][1:3];need(len(targets)==2,'native eye target missing');dims=[]
        for c in targets:
            need(c['captured'] is True and c['hresult']==0,'target capture failed');t=c['target'];w,h=positive(t['width']),positive(t['height'])
            need(w<=4096 and h<=4096 and t['samples']==1 and t['format'] in (28,29,87,91),'unsupported measured target');dims.append((w,h))
        need(dims[0]==dims[1],'eye sizes differ');w,h=dims[0];directory=folder.resolve()
        for pair in (p for p in pairs if 'pair' in p):
            num=integer(pair['pair']);need(0<=num<240,'pair outside bound');paths=[directory/f'pair-{num}-eye-{eye}.rgba' for eye in (0,1)]
            if not any(p.exists() for p in paths):continue
            try:
                rows=byframe.get(pair['native_frame'],[]);need(len(rows)==2 and {r['eye'] for r in rows}=={0,1},'both-eye evidence missing')
                rows=sorted(rows,key=lambda r:r['eye']);phase=rows[0]['phase'];need(phase in (1,5,6,10) and all(r['phase']==phase and r['request']==rows[0]['request'] for r in rows),'not stable baseline/free/restore phase')
                for r in rows:
                    if phase in (5,6):need(r['free_right'] is True and r['right_claim']==0 and r['suppressed'] is True and r['hidden_receipt'] is True and r['paired_pack_receipt'] is True and positive(r['draw_serial']) and r['receipt_deadline_ns']>r['now_ns'],'empty eye state lacks current receipt')
                    else:need(positive(r['right_claim'])>0 and r['body_phase']==1,'held eye state missing')
                saved=[]
                for eye,p in enumerate(paths):
                    need(p.is_file() and p.resolve().is_relative_to(directory) and p.stat().st_size==w*h*4,'missing/wrong-sized pixel file')
                    saved.append(dict(eye=eye,path=str(p.resolve()),width=w,height=h,bytes=w*h*4,sha256=sha(p),layout='RGBA8'))
                samples.append(dict(pair=num,native_frame=pair['native_frame'],phase=PHASE[phase],phase_id=phase,pixels=saved,gpu_visibility_verified=False))
            except (MissingProof,KeyError,TypeError,ValueError,OSError) as e:unusable.append(dict(pair=num,reason=str(e)))
        covered={s['phase_id'] for s in samples};need({1,5,6,10}<=covered,'missing saved baseline/two-free-hand/restored eye phases');return dict(covered=sorted(covered),dimensions=[w,h])
    for name,fn in [('run_identity',identity),('cleanup',cleanup),('stereo_delivery',delivery),('actual_consumer_lifecycle',lifecycle),('native_cache_challenge',challenge),('native_ammo_counts',counts),('saved_eye_samples',pixels)]:section(name,fn)
    mechanical=all(sections[k]['passed'] for k in sections if k!='saved_eye_samples')
    post_draw_firing=False
    if probe.get('diagnostic_profile')==3:
        section('post_draw_native_firing',native_firing)
        post_draw_firing=sections['post_draw_native_firing']['passed'] and all(sections[k]['passed'] for k in
            ('run_identity','cleanup','stereo_delivery','actual_consumer_lifecycle','native_cache_challenge'))
    return dict(schema=SCHEMA,status='mechanical_pass_pending_visual' if mechanical else 'mechanical_evidence_incomplete',mechanical_verified=mechanical,post_draw_native_firing_verified=post_draw_firing,visual_review_ready=mechanical and sections['saved_eye_samples']['passed'],gpu_visibility_verified=False,headset_verified=False,production_input_accepted=False,sections=sections,sampleable_eye_pairs=samples,unusable_saved_pairs=unusable,diagnosis=dict(phase=PHASE.get(probe.get('phase'),'missing'),failure=probe.get('failure')))
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--trace',type=Path,required=True);ap.add_argument('--receiver',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    paths=[a.trace/'native-trace.json',a.trace/'manifest.json',a.trace/'completion.json',a.receiver/'result.json',a.receiver/'pairs.jsonl',a.receiver/'body-holster-preflight.json',a.receiver/'body-holster-postflight.json']
    try:
        values=[load(p) for p in paths[:4]];need(paths[4].stat().st_size<=4*1024*1024,'pair log too large');pairs=[parse(s) for s in paths[4].read_text(encoding='utf-8-sig').splitlines() if s.strip()];need(len(pairs)<=1024,'pair rows exceed bound')
        result=audit(*values,pairs,a.receiver,load(paths[5]),load(paths[6]));result['sources']=[dict(path=str(p.resolve()),sha256=sha(p),bytes=p.stat().st_size) for p in paths]
        result['audit_sources']=[dict(path=str(p.resolve()),sha256=sha(p)) for p in (Path(__file__),Path(__file__).with_name('audit_weapon_visibility.py'),Path(__file__).with_name('preflight_body_holster_probe.py'),Path(__file__).with_name('audit_body_holster_fire.py'))]
    except (OSError,MissingProof,ValueError,TypeError,KeyError,IndexError) as e:result=dict(schema=SCHEMA,status='input_error',mechanical_verified=False,visual_review_ready=False,gpu_visibility_verified=False,headset_verified=False,production_input_accepted=False,error=str(e))
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8');print(json.dumps({k:result.get(k) for k in ('status','mechanical_verified','visual_review_ready','error')}));return 0 if result['mechanical_verified'] else 1
if __name__=='__main__':raise SystemExit(main())
