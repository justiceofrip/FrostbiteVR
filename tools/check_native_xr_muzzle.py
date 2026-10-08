"""Validate saved full-host BC2 firing evidence, without process access."""
import argparse,json,math,re
from pathlib import Path

def check(folder):
 folder=Path(folder);paths=json.loads((folder/'reports.json').read_text());native=Path(paths['native_report'])
 d=json.loads((native/'native-trace.json').read_text());done=json.loads((native/'completion.json').read_text());host=json.loads((folder/'host.json').read_text())
 def require(ok,message):
  if not ok:raise ValueError(message)
 require(d['state']=='observed' and d['hooks_disabled'],'native hooks did not retire')
 require(done['bootstrap_exit']==0 and done['game_responding'] and not done['game_exited'] and not done['new_crash_report'],'native stability failed')
 require(host['state']=='session_observed' and host['runtime']=='FVR NATIVE CAMPAIGN TEST - no headset' and not host['error'],'wrong or failed XR runtime')
 require(host['submitted_pairs']>=240 and host['rejected_pairs']==0 and host['async_timeouts']==0,'host delivery failed')
 runtime=re.search(r'NativeCampaignTest frames=(\d+) layers=(\d+) errors=(\d+)',(folder/'runtime.stderr.log').read_text())
 require(runtime and int(runtime[3])==0,'test runtime validation failed')
 stream=d['native_stream'];require(stream['consumed']==host['submitted_pairs'] and stream['published']==stream['consumed']+stream['discarded'],'native/host pair count mismatch')
 require(stream['discarded']<=1 and stream['discarded']<=host['async_requests']-host['async_completed'],'unexplained dropped pair (only the final outstanding request may retire)')
 for key in ('camera_restore_failures','gpu_failure_stage','gpu_failure_code'):require(stream[key]==0,key)
 g=d['gameplay'];f=g['fire_origin_observation'];rig=g['rig_publication'];require(f['fire_writes_enabled'],'muzzle placement disabled')
 for key in ('rejected_outputs','client_rejected','origin_write_failures','shot_source_changes','origin_fallbacks'):require(f[key]==0,key)
 for key in ('source_changes','packing_failures','fallback_failures','shot_frame_expired','shot_frame_unavailable'):require(rig[key]==0,key)
 require(g['untracked_fire_samples']==0 and g['left_only_action_samples']>0,'tracking-loss suppression failed')
 require(f['client_origin_writes']>=8 and f['server_origin_writes']>=8 and f['origin_writes_after_log_full']>0 and len(f['records'])==32,'firing did not continue beyond log capacity')
 groups={};residual=0
 for row in f['records']:
  if row['phase']!=2:continue
  require(row['weapon_name']=='XM8_sp_s' and row['token_valid'] and row['origin_written'],'unverified rifle event')
  candidate=row['tracked_shot_candidate'];require(candidate and 'event_muzzle' in candidate,'event snapshot missing')
  require(row['native_shot_matrix'][:12]==row['output_matrix'][:12],'native angular basis changed')
  actual=row['output_matrix'][12:15];actual[2]=-actual[2];error=math.dist(actual,candidate['event_muzzle'][12:15]);residual=max(residual,error);require(error<.0011,'shot origin mismatch')
  groups.setdefault((row['weapon'],row['shot_token']),[]).append(row)
 pairs=0
 for group in groups.values():
  first=group[0]['tracked_shot_candidate'];require(first['event_muzzle']==first['tracked_flash'],'first consumer did not use published pose')
  for row in group:
   sample=row['tracked_shot_candidate'];require(sample['event_muzzle']==first['event_muzzle'] and sample['event_generation']==first['generation'],'event consumers disagree')
  pairs+=int({row['client_path'] for row in group}=={True,False})
 require(pairs>=3,'too few matched client/server shot events')
 return {'passed':True,'native_report':str(native),'host_report':str(folder.resolve()),'pairs':host['submitted_pairs'],'retired_outstanding_pairs':stream['discarded'],'xr_presented_frames':host['presented_frames'],'xr_reused_frames':host['reused_frames'],'xr_blank_frames':host['blank_frames'],'async_timeouts':host['async_timeouts'],'client_origin_writes':f['client_origin_writes'],'server_origin_writes':f['server_origin_writes'],'origin_writes_after_log_full':f['origin_writes_after_log_full'],'origin_fallbacks':f['origin_fallbacks'],'matched_recorded_events':pairs,'maximum_origin_error_m':residual,'native_sources_unchanged':True,'headset_tested':False,'projectile_impact_tested':False}

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('folder');a=p.parse_args();print(json.dumps(check(a.folder),indent=2))
