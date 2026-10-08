"""Fail-closed saved-trace audit for the bounded actual-consumer SPAS fixture.

No live process access. Native completion, private palette Pack, fallback and
stereo delivery are independent findings; no result grants headset acceptance.
"""
from __future__ import annotations
import argparse, hashlib, json, math
from pathlib import Path

class MissingProof(ValueError):pass

def need(ok, message):
    if not ok:raise MissingProof(message)

def integer(value):
    need(type(value) is int,'expected a recorded integer')
    return value

def number(value):
    need(type(value) in (int,float) and math.isfinite(value),'expected a finite number')
    return value

def fresh(obj,now,maximum=200_000_000):
    a,b=integer(obj['observed_ns']),integer(obj['deadline_ns'])
    need(0<a<=now<b and b-a<=maximum,'original observation/deadline is not fresh')

def counts(obj):return tuple(integer(obj[k]) for k in ('loaded','reserve','capacity'))

def owner_from_boundary(b,identity):
    return isinstance(b,dict) and all(b.get(k)==identity[k] for k in ('player','soldier','weak','weapon','actor_generation','equip_generation','space'))

CANCEL={0:'unspecified runtime cancellation',1:'entry admission rejected',2:'request lock contention',
        3:'native boundary read rejected',4:'owner publication unavailable',5:'owner/source publication rejected',6:'explicit consumer/stop cancellation'}
FAILURE={0:'none',1:'control',2:'owner',3:'timing',4:'state',5:'overlap',6:'patch',7:'completion',8:'expired',9:'stopped'}
PROBE={0:'none',1:'total timeout/clock rollback',2:'identity/input unavailable',3:'input sequence rollback',4:'duplicate completion',
       5:'idle reserve/raw contact unavailable',6:'pouch acquisition failed',7:'native hold unavailable',8:'controller inverse failed',
       9:'approach/alignment timeout',10:'rail stroke timeout',11:'native acknowledgement timeout',12:'final count conservation missing',
       13:'unsupported fixture round count',14:'fresh pouch neutral/reacquisition unavailable',
       15:'insufficient native empty slots after preparation',16:'insufficient native reserve rounds'}

def audit(trace,manifest,completion,receiver,pairs):
    g=trace.get('gameplay',{});consumer=g.get('physical_reload',{});probe=g.get('physical_reload_probe',{})
    flow=g.get('reload_flow',{});cycle=flow.get('request_cycle',{});rig=g.get('rig_publication',{})
    presentation=rig.get('reload_presentation',{});sections={};expected_rounds=1
    def section(name,fn):
        try:
            detail=fn();sections[name]={'passed':True,'evidence':detail}
        except (MissingProof,KeyError,TypeError,IndexError,ValueError) as exc:
            sections[name]={'passed':False,'reason':str(exc)}

    def run_identity():
        nonlocal expected_rounds
        declarations=('physical_reload_repeat_probe' in manifest,'physical_reload_requested_rounds' in manifest,'requested_rounds' in probe)
        need(not any(declarations) or all(declarations),'partial round declaration is not a legacy one-shell fixture')
        repeat=manifest.get('physical_reload_repeat_probe',False)
        need(type(repeat) is bool,'repeat fixture marker must be boolean')
        declared=integer(manifest.get('physical_reload_requested_rounds',1))
        reported=integer(probe.get('requested_rounds',1))
        need(declared==reported==(2 if repeat else 1),'manifest/probe round count or repeat marker is absent/contradictory')
        expected_rounds=declared
        need(integer(trace['pid'])==integer(manifest['pid']) and trace['pid']>0,'trace/manifest PID mismatch')
        need(manifest['physical_reload'] is True and manifest['physical_reload_probe'] is True,'actual physical fixture was not enabled')
        need(manifest.get('reload_request_probe') is False,'scripted native request fixture is not the physical consumer')
        need(manifest['duration_ms']==30000 and manifest['until_host_exit'] is False,'wrong bounded fixture duration')
        for k in ('game_sha256','probe_sha256','bootstrap_sha256'):
            v=manifest[k];need(isinstance(v,str) and len(v)==64 and all(c in '0123456789abcdefABCDEF' for c in v),'missing exact binary hash: '+k)
        need(probe['synthetic_input'] is True and probe['headset_verified'] is False and receiver['headset_tested'] is False,'synthetic fixture/headset labels missing')
        need(receiver['physical_reload_fixture'] is True and consumer['consumer_code_integrated'] is True,'consumer/receiver mode mismatch')
        return {'pid':trace['pid'],'duration_ms':30000,'input':'synthetic controller stream through actual Gameplay consumer','headset_tested':False,'requested_rounds':expected_rounds}

    def cleanup():
        need(trace['hooks_disabled'] is True,'native hooks did not drain/disable')
        need(flow['drained'] is True and flow.get('records_unavailable_until_drain',False) is False,'native callback records were not drained')
        need(completion['bootstrap_exit']==0 and completion['game_exited'] is False and completion['new_crash_report'] is False and
             completion['game_responding'] is True,'game/exception watcher completion failed')
        need(integer(completion['stability_observation_ms'])>=15000,'post-session game stability not observed')
        hold=flow['diagnostic_hold']
        need(all(integer(hold[k])==0 for k in ('patch_failures','restore_failures')),'native hold patch/restore failed')
        need(len(hold['applied'])==3 and hold['applied']==hold['restored'],'three-copy exact hold restoration mismatch')
        need(trace['native_stream']['camera_restore_failures']==0,'native camera restore failed')
        return {'hooks_disabled':True,'native_callbacks_drained':True,'stability_observation_ms':completion['stability_observation_ms'],
                'hold_applied_restored':hold['applied']}

    def delivery():
        need(receiver['consumed_pairs']==240 and receiver['native_tracking_transport_verified'] in (True,1),'receiver did not verify 240 pairs')
        need(receiver['async_timeouts']==0,'receiver reported timed-out frame requests')
        actual=[p for p in pairs if 'pair' in p]
        need(len(actual)==240 and [integer(p['pair']) for p in actual]==list(range(240)),'pair rows missing, duplicated or out of order')
        for a,b in zip(actual,actual[1:]):
            need(integer(a['native_frame'])<integer(b['native_frame']),'native frame sequence did not advance')
            need(integer(a['tracking'])<=integer(b['tracking']),'tracking sequence regressed')
        need(not any('copy_failed' in p for p in pairs),'pair copy failure recorded')
        stream=trace['native_stream'];need(stream['published']==240 and stream['consumed']==240 and stream['captured_eyes']==480,'producer/receiver pair counts disagree')
        need(stream['gpu_failure_code']==0,'producer GPU failure recorded')
        return {'pairs':240,'eyes':480,'timeouts':0,'mean_latency_us':receiver.get('async_mean_latency_us'),
                'max_latency_us':receiver.get('async_max_latency_us'),'latency_over_50ms':receiver.get('async_completed_after_50ms')}

    def transaction():
        need(sections['run_identity']['passed'],'fixture identity/round declaration failed')
        need(consumer['enabled'] is True and probe['enabled'] is True,'consumer or fixture disabled')
        need(all(integer(obj[k])==expected_rounds for obj in (consumer,probe) for k in ('submitted','completed')),
             'actual Submit/completion count differs from explicitly requested rounds')
        need(probe['actual_consumer_completed'] is True and probe['phase']==7 and probe['failure']==0,'physical fixture did not finish successfully')
        need(consumer['dropped']==0 and consumer['transaction_dropped']==0,'bounded consumer evidence was dropped')
        ts=consumer['transactions'];need(len(ts)==expected_rounds,'retained transaction count differs from requested rounds')
        if expected_rounds==2:
            need(integer(consumer['acquired'])==2 and integer(consumer['cycles'])==1,'two shells must use two acquisitions in one native cycle')
            need(all(sum(e['event']==kind for e in consumer['events'])==amount for kind,amount in ((1,2),(2,1),(3,2),(4,2))),
                 'extra/missing acquisition, native cycle start, submission or completion event')
            for key in ('item_generation','claim','seat','request'):
                need(len({integer(t[key]) for t in ts})==2,'repeated shell reused original '+key)
            need(ts[0]['cycle']==ts[1]['cycle'] and ts[0]['before']['identity']==ts[1]['before']['identity'],'repeated shell changed cycle/owner')
            need(ts[0]['resolved_ns']<ts[1]['submitted_ns'],'second shell preceded first receipt')
            need(ts[0]['current_input']['sequence']<ts[1]['original_input']['sequence'],'second shell reused original source input')
        results=[transaction_one(t,n) for n,t in enumerate(ts)]
        return results[0] if expected_rounds==1 else {'rounds':results,'same_native_cycle':True}

    def transaction_one(t,index):
        need(t['resolved'] is True,'native receipt did not reconcile original item')
        for k in ('item_id','item_generation','claim','seat','request','cycle','source_sequence','started_ns','submitted_ns','resolved_ns'):
            need(integer(t[k])>0,'missing original reservation field: '+k)
        need(t['units']==1 and t['operation']==3,'not a one-shell InsertRound operation')
        need(t['started_ns']==t['submitted_ns']<t['resolved_ns'],'reservation/submit/receipt ordering invalid')
        events=consumer['events'];need(all(integer(a['now_ns'])<=integer(b['now_ns']) for a,b in zip(events,events[1:])),'consumer event clock regressed')
        matches=lambda e: all(e.get(k)==t[k] for k in ('item_id','item_generation','claim'))
        acquisitions=[e for e in events if e['event']==1 and matches(e) and e['now_ns']<=t['submitted_ns']]
        first=consumer['transactions'][0]
        starts=[e for e in events if e['event']==2 and e['cycle']==t['cycle'] and
                all(e.get(k)==first[k] for k in ('item_id','item_generation','claim'))]
        submits=[e for e in events if e['event']==3 and matches(e) and e['cycle']==t['cycle'] and e['request']==t['request']]
        need(len(acquisitions)==len(starts)==len(submits)==1,'acquired original item → cycle → Submit is not an exact join')
        if index==0:need(acquisitions[0]['now_ns']<=starts[0]['now_ns'],'native cycle started before original acquisition')
        else:need(starts[0]['now_ns']<=first['resolved_ns']<acquisitions[0]['now_ns'],'second acquisition did not follow first native receipt')
        need(max(acquisitions[0]['now_ns'],starts[0]['now_ns'])<=submits[0]['now_ns']==t['submitted_ns'],'wrong submit/start/acquisition time')
        need(any(e['event']==4 and e['now_ns']==t['resolved_ns'] and e['cycle']==t['cycle'] for e in events),'successful Resolve event missing')
        # Event4 can refer to an already-held replacement; only the retained
        # original reservation is authoritative for this diagnostic join.
        original,current=t['original_input'],t['current_input'];b=t['before'];identity=b['identity']
        need(integer(original['sequence'])<=integer(current['sequence']),'renderer input came from the future')
        fresh(original,t['submitted_ns'],150_000_000);fresh(current,t['submitted_ns'],150_000_000)
        if original['sequence']<current['sequence']:
            need(original['observed_ns']<current['observed_ns'],'N-1 contact was restamped with current input time')
        else:need(original==current,'same input sequence has different immutable evidence')
        for i in (original,current):
            need(i['actor']==(integer(identity['weak'])<<32)|integer(identity['soldier']),'physical actor does not map to exact native weak/soldier')
            need(i['actor_generation']==identity['actor_generation'] and i['space']==identity['space'],'physical/native actor generation or space differs')
            need(i['equip_generation']==t['physical_equip_generation'],'physical equip generation lost')
        need(t['weapon']==identity['weapon'] and t['pool']==identity['server_item'] and t['pool_generation']==identity['equip_generation'],
             'weapon/reserve pool/native equip identity mismatch')
        return {'item':[t['item_id'],t['item_generation']],'claim':t['claim'],'cycle':t['cycle'],'request':t['request'],
                'original_input':original['sequence'],'submit_input':current['sequence'],
                'physical_equip_generation':t['physical_equip_generation'],'native_equip_generation':identity['equip_generation']}

    def receipt():
        need(sections['consumer_transaction']['passed'],'original transaction proof failed')
        ts=consumer['transactions']
        results=[receipt_one(t,n) for n,t in enumerate(ts)]
        if expected_rounds==2:
            a,b=ts
            need(a['ack']['server_invocation']!=b['ack']['server_invocation'],'same native server transfer was reused')
            need(counts(a['after'])==counts(b['before']),'second round baseline differs from first native receipt')
            need(b['before']['observed_ns']>=a['after']['observed_ns'] and
                 b['before']['sequence']>=a['after']['sequence'],'second native lease is older than first receipt')
            need(b['source_sequence']>=a['reserve_after']['sequence'],'second reserve observation predates first reserve receipt')
            need(b['after']['loaded']==a['before']['loaded']+2 and b['after']['reserve']==a['before']['reserve']-2,'two-shell total is not conserved')
        return results[0] if expected_rounds==1 else {'rounds':results,'two_distinct_server_transfers':True}

    def receipt_one(t,index):
        before,after,ack,reserve=t['before'],t['after'],t['ack'],t['reserve_after']
        ident=before['identity'];now=t['resolved_ns'];submitted=t['submitted_ns']
        for obj in (before,after,ack,reserve):
            need(obj['identity']==ident and obj['verified'] is True,'native receipt/lease/reserve owner or verification mismatch')
        need(before['all_three_held'] is True,'submission was not under a genuine three-copy native hold')
        need(before['cycle']==after['cycle']==ack['cycle']==t['cycle'] and ack['request']==t['request'],'native cycle/request mismatch')
        need(ack['operation']==3 and ack['status']==1 and integer(ack['server_invocation'])>0,'ack is not a genuine Applied shell insertion')
        fresh(before,submitted);fresh(after,now);fresh(ack,now);fresh(reserve,now)
        need(submitted<=ack['observed_ns']<=after['observed_ns']<=now and ack['observed_ns']<=reserve['observed_ns'],
             'receipt reused a pre-ack native/reserve observation')
        need(integer(after['sequence'])>=integer(ack['sample_sequence'])>integer(before['sequence']),'native sample acknowledgement ordering invalid')
        need(integer(reserve['sequence'])>integer(t['source_sequence']),'reserve source did not advance in its own sequence domain')
        # Deliberately do not compare reserve sequence with native sampleSequence.
        lb,rb,cap=counts(before);la,ra,capa=counts(after)
        need(0<=lb<cap and rb>0 and capa==cap and counts(reserve)==counts(after),'invalid or disagreeing native/reserve count samples')
        need(la==lb+1 and ra==rb-1 and la+ra==lb+rb and t['reserve_before']==rb,'native count conservation failed')
        need(probe['loaded_before']+index==lb and probe['reserve_before']-index==rb,'fixture baseline differs from actual native reservation')
        records=flow['records'];need(flow['dropped']==0,'native callback evidence dropped')
        transfers=[r for r in records if r.get('kind')==2 and r.get('native_invocation')==ack['server_invocation']]
        need(len(transfers)==1,'actual native server Transfer invocation missing/duplicated')
        tr=transfers[0]
        need(tr['transfer_path']==0 and tr['finished'] is True and tr['identity_retained'] is True,'server Transfer is not owned ordinary State12 completion')
        need(submitted<=integer(tr['begin_ns'])<=integer(tr['end_ns'])<=ack['observed_ns'],'native transfer happened outside submitted transaction')
        for key in ('before','after'):
            b=tr[key];need(owner_from_boundary(b,ident),'server Transfer native owner differs')
            need(b['branch']==2 and b['firing']==ident['firing'][2] and all(b[k]==ident[k] for k in ('server_player','server_soldier','server_item')),
                 'server Transfer branch/association differs')
        need((tr['before']['loaded'],tr['before']['reserve'])==(lb,rb) and
             (tr['after']['loaded'],tr['after']['reserve'])==(la,ra),'actual server transfer count delta differs from receipt')
        same=[r for r in records if r.get('kind')==2 and (r.get('before') or {}).get('branch')==2 and
              owner_from_boundary(r.get('before',{}),ident) and submitted<=r.get('begin_ns',-1)<=ack['observed_ns']]
        need(len(same)==1,'multiple server transfers occurred for the single physical request')
        need(not cycle.get('clock_failure'),'native request clock failure recorded')
        early=[c for c in cycle.get('cancellations',[]) if consumer['events'][0]['now_ns']<=c['now_ns']<now]
        need(not early,'native request cancelled before physical receipt')
        held=set()
        for r in records:
            b=r.get('before') or {}
            if (r.get('hold_applied') and owner_from_boundary(b,ident) and
                (0 if index==0 else consumer['transactions'][index-1]['resolved_ns'])<=r.get('begin_ns',0)<=submitted):
                need(r.get('hold_restored') is True and r.get('hold_unexpected_native_write') is False,'native held invocation was not exactly restored')
                if 0<=b.get('branch',-1)<3 and b.get('firing')==ident['firing'][b['branch']]:held.add(b['branch'])
        need(held=={0,1,2},'actual hold/restore evidence missing for a firing copy')
        return {'server_invocation':ack['server_invocation'],'before':[lb,rb],'after':[la,ra],
                'native_sample':ack['sample_sequence'],'reserve_sample':reserve['sequence'],'independent_sequence_domains':True}

    def geometry():
        need(sections['consumer_transaction']['passed'],'original contact transaction missing')
        rows=probe['rows'];moving=[r for r in rows if r.get('input',0)>0 and r.get('raw',0)>0]
        need(len(moving)>=3,'authored rail controller trajectory not retained')
        for r in moving:
            need(0<integer(r['raw'])<=integer(r['input']),'raw renderer contact is not an original/current packet')
            need(number(r['position_error_m'])>=0 and number(r['angle_error_rad'])>=0,'invalid observed raw-wrist alignment error')
            need(-.0901<=number(r['rail_m'])<=.0501,'command left bounded authored rail range')
        need(all(a['now_ns']<b['now_ns'] and a['raw']<b['raw'] for a,b in zip(moving,moving[1:])),'raw geometry clock/sequence did not progress')
        need(any(abs(r['rail_m']+.09)<.0001 for r in moving),'outside approach not observed')
        need(any(abs(r['rail_m']+.02)<.0001 for r in moving),'entry target not observed')
        need(any(r['rail_m']>=.04 for r in moving),'terminal rail stroke not observed')
        need(any(r['position_error_m']<.003 and r['angle_error_rad']<.05 for r in moving),'no raw wrist convergence to authored target')
        for index,t in enumerate(consumer['transactions']):
            seats=[e for e in consumer['events'] if e['event']==10 and e['reason']==2 and
                   e.get('seat')==t['seat'] and e['cycle']==t['cycle']]
            need(len(seats)==1,'actual geometry seat does not join original submission')
            seat=seats[0]
            need(all(seat.get(k)==t[k] for k in ('item_id','item_generation','claim')),
                 'seated original item or hand claim differs from submission')
            need(integer(seat['source_sequence'])==integer(t['original_input']['sequence']),
                 'retained seat did not preserve its original geometry input sequence')
            need(t['original_input']['observed_ns']<=integer(seat['now_ns'])<=t['submitted_ns'] and
                 seat['now_ns']<t['original_input']['deadline_ns'],
                 'seat chronology does not preserve original contact and submission')
            acquired=[e for e in consumer['events'] if e['event']==1 and
                      all(e.get(k)==t[k] for k in ('item_id','item_generation','claim'))]
            need(len(acquired)==1,'per-round acquisition geometry join missing')
            samples=[r for r in moving if acquired[0]['now_ns']<=r['now_ns']<=t['resolved_ns']]
            need(any(abs(r['rail_m']+.09)<.0001 for r in samples) and
                 any(abs(r['rail_m']+.02)<.0001 for r in samples) and any(r['rail_m']>=.04 for r in samples),
                 'a shell lacks its own outside approach, entry and terminal stroke')
        if expected_rounds==2:
            rounds=probe['rounds'];need(len(rounds)==2,'per-round fixture evidence missing')
            for index,(r,t) in enumerate(zip(rounds,consumer['transactions'])):
                need(integer(r['number'])==index+1 and integer(r['acquired'])==index+1,'per-round acquisition count/ordinal invalid')
                need(t['resolved_ns']<=integer(r['completed_ns'])<t['reserve_after']['deadline_ns'],
                     'per-round observation does not follow the still-fresh native receipt')
                need((integer(r['loaded']),integer(r['reserve']))==counts(t['after'])[:2],'per-round counts differ from receipt')
                need(0<integer(r['grab_input'])<=integer(t['original_input']['sequence']),'per-round grab/source ordering invalid')
            a,b=rounds;first,second=consumer['transactions']
            next_acquire=next(e for e in consumer['events'] if e['event']==1 and
                              all(e.get(k)==second[k] for k in ('item_id','item_generation','claim')))
            need(a['completed_ns']<next_acquire['now_ns'],'second acquisition preceded first observed completion')
            need(first['current_input']['sequence']<integer(b['first_neutral_input'])<integer(b['last_neutral_input'])<b['grab_input']<=second['original_input']['sequence'],
                 'second pouch grab lacks distinct later neutral/source packets')
        return {'raw_samples':len(moving),'input_lag_min':min(r['input']-r['raw'] for r in moving),
                'input_lag_max':max(r['input']-r['raw'] for r in moving),'rounds':expected_rounds,
                'scope':'recorded authored trajectory and per-round source/seat join; repeated neutral packet ordering is checked, not invented from completion counts'}

    def palette():
        p=presentation['verified_private_packs'];paired=presentation['verified_paired_private_packs']
        need(len(p)==len(paired)==3 and all(type(n) is int and n>=0 for n in p+paired),'actual private palette Pack counters missing')
        need(p[0]>0 and p[1]>0 and paired[0]>0 and paired[1]>0,'Carried/Guided private palettes were not both verified in paired Pack calls')
        need(all(a>=b for a,b in zip(p,paired)),'paired Pack counters exceed total')
        need(rig['source_changes']==0 and rig['packing_failures']==0 and rig['native_animation_written'] is False and
             presentation['native_animation_written'] is False,'source preservation or actual destination Pack failed')
        need(presentation['native_hidden_contacts']>0 and presentation['owned_shell_visibility_poses']>0,'owned-shell visibility path was not exercised while native shell was hidden')
        return {'verified_private_packs':p,'verified_paired_private_packs':paired,
                'native_hidden_contacts':presentation['native_hidden_contacts'],'gpu_submission_proven_by_this_trace':False}

    def fallback():
        need(sections['private_palette_pack']['passed'],'private palette path not proven')
        need(integer(presentation['verified_fallback_packs'])>0 and integer(presentation['verified_paired_fallback_packs'])>0,
             'ownership-expiry ordinary-base fallback Pack was not observed in this run')
        return {'verified_fallback_packs':presentation['verified_fallback_packs'],'verified_paired_fallback_packs':presentation['verified_paired_fallback_packs']}

    for name,fn in [('run_identity',run_identity),('cleanup',cleanup),('stereo_delivery',delivery),('consumer_transaction',transaction),
                    ('native_receipt',receipt),('authored_raw_motion',geometry),('private_palette_pack',palette),('ownership_fallback',fallback)]:section(name,fn)
    native_names=('run_identity','cleanup','stereo_delivery','consumer_transaction','native_receipt','authored_raw_motion')
    cancellations=[{'now_ns':r.get('now_ns'),'reason':r.get('reason'),'meaning':CANCEL.get(r.get('reason'),'unknown cancellation code')}
                   for r in cycle.get('cancellations',[])[:16]]
    return {'schema':'fvr.bc2.physical_reload_audit.v1','passed':all(s['passed'] for s in sections.values()),
            'actual_native_consumer_verified':all(sections[n]['passed'] for n in native_names),
            'actual_private_palette_pack_verified':sections['private_palette_pack']['passed'],
            'ordinary_palette_fallback_verified':sections['ownership_fallback']['passed'],
            'headset_verified':False,'gpu_shell_submission_verified_by_this_audit':False,'synthetic_controller_fixture':True,
            'requested_rounds':expected_rounds,
            'sections':sections,'diagnosis':{'probe_failure':PROBE.get(probe.get('failure'),'missing/unknown probe failure'),
            'native_cycle_phase':cycle.get('phase'),'native_failure':FAILURE.get(cycle.get('failure'),'missing/unknown'),
            'cancellations':cancellations,'clock_failure':cycle.get('clock_failure'),
            'native_read_failures':cycle.get('read_failures'),'native_contention':cycle.get('contention')}}

def unique_object(pairs):
    out={}
    for k,v in pairs:
        if k in out:raise ValueError('duplicate JSON field: '+k)
        out[k]=v
    return out

def load(path):
    need(path.stat().st_size<=128*1024*1024,'report exceeds bounded 128 MiB limit')
    return json.loads(path.read_text(encoding='utf-8-sig'),object_pairs_hook=unique_object,
                      parse_constant=lambda x:(_ for _ in ()).throw(ValueError('non-finite JSON number')))

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--trace',type=Path,required=True)
    ap.add_argument('--receiver',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args();paths=[a.trace/'native-trace.json',a.trace/'manifest.json',a.trace/'completion.json',a.receiver/'result.json',a.receiver/'pairs.jsonl']
    try:
        values=[load(p) for p in paths[:4]]
        need(paths[4].stat().st_size<=16*1024*1024,'pair log exceeds 16 MiB bound')
        rows=[json.loads(s,object_pairs_hook=unique_object) for s in paths[4].read_text().splitlines() if s.strip()]
        need(len(rows)<=20000,'too many pair rows')
        report=audit(*values,rows)
        report['sources']=[{'path':str(p.resolve()),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in paths]
    except (OSError,ValueError,KeyError,TypeError) as e:
        report={'schema':'fvr.bc2.physical_reload_audit.v1','passed':False,'actual_native_consumer_verified':False,
                'headset_verified':False,'input_error':str(e)}
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps({k:report.get(k) for k in ('passed','actual_native_consumer_verified','actual_private_palette_pack_verified','ordinary_palette_fallback_verified','headset_verified','input_error')}))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
