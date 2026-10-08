"""Post-draw firing evidence from existing read-only/native-hook observations."""
from audit_weapon_visibility import need,positive,integer
import math

def _firing_observations(gameplay,probe,before,after,a,b):
    fire=probe['post_draw_fire']
    need(fire['requested'] is True and fire['shot_verified'] is False,'fixture confused input with shot proof')
    start,end=positive(fire['pulse_start_ns']),positive(fire['pulse_end_ns'])
    need(80_000_000<=end-start<=200_000_000,'unbounded/missing trigger pulse')
    first,last=positive(fire['first_commit_ms']),positive(fire['last_commit_ms'])
    need(first<=last<=first+200 and positive(fire['fire_cache_commits']) and positive(fire['release_cache_commits']),'committed press/release missing')
    restored=positive(fire['restored_claim']);need(restored!=probe['baseline_claim'],'old held claim reused')
    rows=probe['rows'];pressed=[r for r in rows if r['phase']==13 and r.get('consumed_trigger',0)>=.75]
    released=[r for r in rows if r['phase']==14 and r.get('consumed_trigger')==0]
    # Sparse rows supplement the per-callback counters; retain at least one of
    # each. A commanded trigger without real cache readback is not successful.
    need(pressed and released,'press/release observations missing')
    for r in pressed+released:
        need(r['body_phase']==1 and r['right_claim']==restored and r['left_claim']==0 and r['blocks_actions'] is False and r['free_right'] is False and r['suppressed'] is False,'fire did not retain restored Held ownership')
        need(r['fire_cache_read'] is True,'cache readback unavailable')
    need(all(r['fire_requested'] is True and r['fire_cache']==1 for r in pressed),'trigger did not reach native cache')
    need(all(r['fire_requested'] is False and r['fire_cache']==0 for r in released),'native trigger release missing')
    delta=a['loaded']-b['loaded'];need(a['asset']==b['asset']=='XM8_sp_s' and a['loaded']>=4 and 1<=delta<=3 and a['reserve']==b['reserve'],'expected bounded loaded decrease/reserve preservation missing')
    old,new=before['samples'][-1],after['samples'][0]
    for key in ('client_owner','server_player','server_soldier','server_item','server_firing','asset_name'):
        need(old[key]==new[key],'native firing owner changed: '+key)
    flow=gameplay['reload_flow'];need(flow['started'] is True and flow['drained'] is True and flow['server_binding_verified'] is True,'native observer unavailable')
    need(flow['native_state_writes'] is False,'diagnostic performed reload state writes')
    observations=[];missing=[]
    for r in flow['records']:
        need(r['transfer_path'] is None and not r['hold_requested'] and not r['hold_applied'],'unexpected native reload/hold')
        if not r['finished'] or not r['identity_retained'] or r['after'] is None:missing.append(r['id'])
        for which,stamp in (('before','begin_ns'),('after','end_ns')):
            s=r.get(which)
            if s is None:continue
            if which=='after' and (r['finished'] is not True or r['identity_retained'] is not True):continue
            t=positive(r[stamp]);branch=integer(s['branch']);need(branch in (0,1,2),'unknown native copy')
            for key,target in (('player','player'),('soldier','soldier'),('weak','weak'),('weapon','weapon'),('actor_generation','actor_generation'),('equip_generation','native_equip_generation'),('space','space')):
                need(s[key]==probe[target],'native observation belongs to another owner')
            need(b['loaded']<=s['loaded']<=a['loaded'] and s['reserve']==a['reserve'],'unexpected native count outside shot delta')
            if branch==2:
                need(s['firing']==old['server_firing'] and s['server_player']==old['server_player'] and s['server_soldier']==old['server_soldier'] and s['server_item']==old['server_item'],'server firing identity differs')
            observations.append((t,branch,s))
    boundaries=[]
    for branch in (0,1,2):
        pre=[(t,s) for t,n,s in observations if n==branch and start-500_000_000<=t<start and s['current']==2 and s['next']==2]
        post=[(t,s) for t,n,s in observations if n==branch and end+100_000_000<=t<=end+1_000_000_000 and s['current']==2 and s['next']==2]
        need(pre and post,'missing bounded idle native copy before/after pulse')
        x,y=max(pre,key=lambda v:v[0]),max(post,key=lambda v:v[0])
        need(x[1]['loaded']==a['loaded'] and y[1]['loaded']==b['loaded'],'native copy did not preserve measured shot decrement')
        boundaries.append(dict(branch=branch,before_ns=x[0],after_ns=y[0],loaded_before=x[1]['loaded'],loaded_after=y[1]['loaded']))
    native=gameplay['fire_origin_observation'];shots=[]
    need(native['origin_write_failures']==0 and native['shot_source_changes']==0,'native muzzle mapping failed')
    for r in native['records']:
        if r['phase']!=2:continue
        need(first-2<=r['ms']<=last+250,'shot outside single requested pulse')
        need(r['weapon']==probe['weapon'] and r['client_soldier']==probe['soldier'] and r['weapon_name']=='XM8_sp_s','shot weapon/actor mismatch')
        need(r['token_valid'] is True and 0<=integer(r['shot_token'])<=0xffffffff and r['origin_written'] is True and r['tracked_shot_candidate'] is not None,'shot lacked actual native token/muzzle receipt')
        if not r['client_path']:need(r['server_player']==old['server_player'] and r['server_soldier']==old['server_soldier'],'shot server owner mismatch')
        shots.append(r)
    need(shots and {r['client_path'] for r in shots}=={True,False},'matching client and authoritative server shot paths missing')
    return dict(asset='XM8_sp_s',loaded_before=a['loaded'],loaded_after=b['loaded'],reserve=b['reserve'],native_round_decrease=delta,
        native_boundaries=boundaries,native_observation_complete=not missing and flow['record_lock_drops']==0 and flow['dropped']==0,
        missing_after_records=missing,record_lock_drops=flow['record_lock_drops'],dropped=flow['dropped'],headset_verified=False,production_input_accepted=False),shots

def firing_evidence(gameplay,probe,before,after,a,b):
    """Original strict verdict: every observed effect token must pair exactly."""
    evidence,shots=_firing_observations(gameplay,probe,before,after,a,b)
    client_tokens={r['shot_token'] for r in shots if r['client_path']};server_tokens={r['shot_token'] for r in shots if not r['client_path']}
    need(client_tokens==server_tokens,'client/server native shot tokens do not match')
    for token in client_tokens:
        pair=[r['tracked_shot_candidate'] for r in shots if r['shot_token']==token]
        need(all(isinstance(p['event_muzzle'],list) and len(p['event_muzzle'])==16 and all(type(x) in (int,float) and math.isfinite(x) for x in p['event_muzzle']) for p in pair),'invalid event muzzle matrix')
        need(all(positive(p['event_generation'])==pair[0]['event_generation'] and p['event_muzzle']==pair[0]['event_muzzle'] for p in pair),'same native shot used different event pose')

    return dict(evidence,shot_verified=True,
        scope='same selected native server firing object, three observed count copies, and actual client/server shot callbacks',
        shot_tokens=sorted({r['shot_token'] for r in shots}),complete_client_server_composition_pairing=True)

def native_firing_evidence(gameplay,probe,before,after,a,b):
    """Separate positive firing proof; never supplies a missing effect callback.

    The Update context +0x1c event key is also read by both Shoot hooks. Require
    one exact event chain per native copy, actual server muzzle writes for every
    count event, and at least one genuinely paired client/server event. This is
    stronger than a final count difference, but is not all-effect-path coverage.
    """
    evidence,shots=_firing_observations(gameplay,probe,before,after,a,b)
    flow=gameplay['reload_flow'];fire=probe['post_draw_fire']
    start,end=fire['pulse_start_ns'],fire['pulse_end_ns']
    need(evidence['native_observation_complete'] and integer(flow['owner_lock_drops'])==0,
         'native event recording has an observation gap')
    events={0:[],1:[],2:[]};firing={0:set(),1:set(),2:set()}
    for row in flow['records']:
        x,y=row['before'],row['after'];branch=integer(x['branch'])
        need(branch==y['branch'] and positive(x['firing'])==positive(y['firing']),
             'native firing copy changed within callback')
        firing[branch].add(x['firing'])
        if x['loaded']==y['loaded']:continue
        need(integer(row['kind'])==0 and row['finished'] is True and row['identity_retained'] is True,
             'count change is not a retained native Update')
        need(start<=positive(row['begin_ns'])<=positive(row['end_ns'])<=end,
             'native decrement outside committed trigger pulse')
        need(x['loaded']-y['loaded']==1 and x['reserve']==y['reserve']==a['reserve'],
             'native shot did not consume exactly one loaded round')
        need(x['current'] in (2,9) and x['next']==x['current'] and y['current']==y['next']==9,
             'native decrement is not idle/firing to firing')
        ctx=row['context_before'];out=row['context_after']
        token=integer(ctx['raw_1c'])
        need(ctx['decoded_valid'] is True and out['decoded_valid'] is True and
             0<=token<=0xffffffff and out['raw_1c']==token and integer(ctx['input_flags'])==1 and integer(out['input_flags'])==1,
             'native decrement lacks stable Fire event context')
        events[branch].append(dict(record_id=positive(row['id']),token=token,branch=branch,
            begin_ns=row['begin_ns'],end_ns=row['end_ns'],begin_tick_ms=positive(row['begin_tick_ms']),
            end_tick_ms=positive(row['end_tick_ms']),loaded_before=x['loaded'],loaded_after=y['loaded']))
    for branch,rows in events.items():
        rows.sort(key=lambda r:r['begin_ns'])
        need(len(firing[branch])==1 and len(rows)==a['loaded']-b['loaded'],
             'native copy/event count differs from measured decrement')
        need(len({r['token'] for r in rows})==len(rows),'duplicate native event token')
        count=a['loaded'];previous=0
        for row in rows:
            need(previous<=row['begin_ns'] and row['loaded_before']==count,'native event count chain broke')
            count=row['loaded_after'];previous=row['end_ns']
        need(count==b['loaded'],'native event chain did not reach final count')
    order=[r['token'] for r in events[2]]
    need(all([r['token'] for r in events[n]]==order for n in (0,1)),
         'native copies did not consume matching ordered event tokens')
    client={r['shot_token'] for r in shots if r['client_path']}
    server={r['shot_token'] for r in shots if not r['client_path']}
    need(server==set(order) and client<=server and bool(client),
         'native count events lack server receipts or any matched client receipt')
    details=[]
    for index,token in enumerate(order):
        rows=[r for r in shots if r['shot_token']==token]
        poses=[r['tracked_shot_candidate'] for r in rows]
        need(all(isinstance(p['event_muzzle'],list) and len(p['event_muzzle'])==16 and
             all(type(x) in (int,float) and math.isfinite(x) for x in p['event_muzzle']) for p in poses),
             'invalid event muzzle matrix')
        need(all(positive(p['event_generation'])==poses[0]['event_generation'] and
             p['event_muzzle']==poses[0]['event_muzzle'] for p in poses),
             'same native shot used different event pose')
        for row in rows:
            boundary=events[0 if row['client_path'] else 2][index]
            need(boundary['begin_tick_ms']<=row['ms']<=boundary['end_tick_ms'],
                 'muzzle receipt did not occur inside its native event callback')
        details.append(dict(token=token,count_events=[events[n][index] for n in range(3)],
            client_composition_observed=token in client,server_composition_observed=True,
            exact_composition_pair=token in client,event_generation=poses[0]['event_generation'],
            missing_client_callback_reason=None if token in client else 'unobserved; native branch versus early owner rejection cannot be distinguished'))
    return dict(evidence,post_draw_native_firing_verified=True,
        scope='ordinary post-draw Fire consumed one round per matching native Update event on all three copies, with server muzzle receipts and at least one exact client/server muzzle pair',
        complete_client_server_composition_pairing=client==server,
        paired_composition_tokens=sorted(client),unpaired_server_tokens=sorted(server-client),
        unpaired_client_tokens=[],native_events=details,
        production_admission_decision=False,all_effect_paths_verified=False)
