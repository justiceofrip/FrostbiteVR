"""Verify recorded native muzzle placement; no process access or input injection."""
import argparse,json,math
from pathlib import Path

def check(native,receiver,writes,weapon=None):
 native=Path(native);receiver=Path(receiver)
 d=json.loads((native/'native-trace.json').read_text());done=json.loads((native/'completion.json').read_text());delivery=json.loads((receiver/'result.json').read_text())
 def require(ok,message):
  if not ok:raise ValueError(message)
 require(d['state']=='observed' and d['hooks_disabled'],'native hook shutdown failed')
 require(done['bootstrap_exit']==0 and not done['game_exited'] and not done['new_crash_report'] and done['game_responding'],'game stability failed')
 require(delivery['consumed_pairs']==240 and delivery['native_tracking_transport_verified'] and not delivery['gpu_busy'],'stereo delivery failed')
 stream=d['native_stream'];require(stream['consumed']==240 and not stream['discarded'] and not stream['camera_restore_failures'] and not stream['gpu_failure_stage'] and not stream['gpu_failure_code'],'native stream failure')
 g=d['gameplay'];f=g['fire_origin_observation'];require(f['fire_writes_enabled']==writes,'wrong firing mode')
 for name in ['rejected_outputs','client_rejected','origin_write_failures','shot_source_changes']:require(f[name]==0,name)
 for name in ['source_changes','packing_failures','fallback_failures']:require(g['rig_publication'][name]==0,name)
 require(g['untracked_fire_samples']==0 and g['left_only_action_samples']>0,'tracking-loss control check absent or failed')
 rows=[x for x in f['records'] if x['phase']==2];require(rows,'no final shot compositions recorded')
 if weapon:require(all(row.get('weapon_name')==weapon for row in rows),'wrong weapon tested')
 if delivery.get('shot_schedule_version',1)>=2:
  require(g['next_weapon_commands']+g['previous_weapon_commands']==delivery['shot_primary_pulses'],'scripted weapon-selection pulse was not consumed')
 residual=0;counts={True:0,False:0};origins={True:[],False:[]}
 rig=g['rig'];bones=rig['bones']
 for row in rows:
  candidate=row['tracked_shot_candidate'];require(candidate and 'mapped_shot' in candidate,'missing supported tracked muzzle')
  index=candidate['flash_bone'];require(bones[index]['name']=='jntWpn_Flash','unverified attachment name')
  parent=bones[index]['parent'];seen=set()
  while parent!=-1 and parent!=rig['weapon_bone']:
   require(parent not in seen,'cyclic attachment ancestry');seen.add(parent);parent=bones[parent]['parent']
  require(parent==rig['weapon_bone'],'attachment outside equipped weapon subtree')
  before=row['native_shot_matrix'];after=row['output_matrix'];require(before[:12]==after[:12],'native angular basis changed')
  require(row['origin_written']==writes,'missing or unintended origin write')
  if not writes:require(before==after,'pass-through shot was changed')
  expected=candidate.get('event_muzzle',candidate['tracked_flash'])[12:15];mapped=candidate['mapped_shot'][12:15]
  require(math.dist(expected,mapped)<.0001,'muzzle offset applied twice')
  if writes:
   actual=after[12:15];actual[2]=-actual[2];error=math.dist(actual,expected);residual=max(residual,error);require(error<.0011,'output origin misses muzzle')
  counts[row['client_path']]+=1;origins[row['client_path']].append((row['ms'],expected))
  if delivery.get('shot_probe_requested'):
   elapsed=row['ms']-delivery['controls_started_ms'];loss=9000 if delivery.get('shot_schedule_version',1)>=2 else 6000;require(elapsed<loss or elapsed>=loss+400,'shot during scripted tracking loss')
 for side in [True,False]:
  require(counts[side]>=2,'both hand positions were not fired')
  movement=math.dist(origins[side][0][1],origins[side][-1][1]);require(.18<movement<.27,'shot origin did not follow 20cm sideways / 10cm downward hand travel')
 require(f['client_origin_writes']==(counts[True] if writes else 0) and f['server_origin_writes']==(counts[False] if writes else 0),'write count disagrees with records')
 latched=all('event_muzzle' in row['tracked_shot_candidate'] for row in rows)
 if latched:
  groups={}
  for row in rows:
   require(row.get('token_valid'),'missing native event identity');groups.setdefault((row['weapon'],row['shot_token']),[]).append(row)
  require(len(groups)>=2,'distinct native shots were not verified')
  agreement=0
  for group in groups.values():
   require({row['client_path'] for row in group}=={True,False},'native shot has no matching client/server consumer')
   first=group[0]['tracked_shot_candidate'];require(first['event_muzzle']==first['tracked_flash'],'first event pose differs from published gun')
   for row in group:
    candidate=row['tracked_shot_candidate'];require(candidate['event_generation']==first['generation'],'shot did not retain first published generation')
    agreement=max(agreement,math.dist(candidate['event_muzzle'][12:15],first['event_muzzle'][12:15]))
 else:
  agreement=max(min(math.dist(p,q) for when2,q in origins[False] if abs(when-when2)<300) for when,p in origins[True])
 require(agreement<.03,'client and server muzzle positions diverged')
 return {'passed':True,'native_report':str(native.resolve()),'receiver_report':str(receiver.resolve()),'origin_writes_enabled':writes,'weapon_names':sorted({row['weapon_name'] for row in rows if 'weapon_name' in row}),'client_compositions':counts[True],'server_compositions':counts[False],'maximum_origin_error_m':residual,'maximum_client_server_difference_m':agreement,'hand_travel_m':math.dist(origins[True][0][1],origins[True][-1][1]),'pairs':delivery['consumed_pairs'],'async_timeouts':delivery['async_timeouts'],'event_snapshot_matched':latched,'native_sources_unchanged':True,'native_angular_basis_unchanged':True,'headset_tested':False,'projectile_impact_tested':False}

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--receiver',required=True);p.add_argument('--writes',action='store_true');p.add_argument('--weapon');a=p.parse_args();print(json.dumps(check(a.native,a.receiver,a.writes,a.weapon),indent=2))
