"""Validate saved BC2 support-grip evidence. No process access."""
import argparse,json,math
from pathlib import Path

def check(native,receiver=None,host=None,weapon_kind=None):
 native=Path(native);d=json.loads((native/'native-trace.json').read_text())
 done=json.loads((native/'completion.json').read_text());g=d['gameplay'];s=g['two_hand_support'];rig=g['rig_publication']
 def require(ok,message):
  if not ok:raise ValueError(message)
 require(d['state']=='observed' and d['hooks_disabled'],'hooks did not retire')
 require(done['bootstrap_exit']==0 and done['game_responding'] and not done['game_exited'] and not done['new_crash_report'],'native stability failed')
 require(s['requested'] and s['held_samples']>100,'support was not exercised')
 expected=4 if host else 3
 require(s['grabs']==expected and s['releases']==expected,'unexpected support transitions')
 require(s['preservation_failures']==0,'support altered input outside gun-hand orientations')
 for key in ('source_changes','packing_failures','fallback_failures'):require(rig[key]==0,key)
 require(not rig['native_animation_written'],'native animation was modified')
 require(g['untracked_fire_samples']==0,'untracked controller fired')
 stream=d['native_stream']
 for key in ('camera_restore_failures','gpu_failure_stage','gpu_failure_code'):require(stream[key]==0,key)
 rows=s['records'];require(rows,'missing support records')
 if weapon_kind is not None:require(all(r.get('weapon_kind')==weapon_kind for r in rows if r['holding']),'wrong supported firearm')
 for row in rows:
  if not row['holding']:require(row['aim']==row['raw_aim'],'release did not restore raw aim')
  if row['engaged']:require(row['holding'] and row['correction_radians']<.002,'grab snapped the aim')
 held=[r for r in rows if r['holding'] and r['correction_radians']>.05]
 require(len(held)>=8,'support never steered the weapon')
 require(any(r['released'] and r['squeeze']>.7 for r in rows),'tracking loss did not release support')
 require(any(not r['holding'] and r['squeeze']>.7 and r['contact'] for r in rows),'held squeeze automatically reattached after loss')
 maximum=max(a['error'] for r in rig['hand_evidence'] for a in r['arms'] if a['tracked'])
 require(maximum<.005,'tracked hand missed independent target')
 if receiver:
  delivery=json.loads((Path(receiver)/'result.json').read_text())
  require(delivery['support_grip_requested'] and delivery['consumed_pairs']==240 and stream['consumed']==240,'bounded stereo coverage failed')
  require(delivery['native_tracking_transport_verified'] and delivery['gpu_busy']==0,'GPU delivery failed')
  require(g['fire_origin_observation']['client_origin_writes']==0 and g['fire_origin_observation']['server_origin_writes']==0,'non-firing sequence fired')
  direction=delivery.get('support_primary_direction',0)
  require(g['next_weapon_commands']==int(direction>0) and g['previous_weapon_commands']==int(direction<0),'selection pulse mismatch')
  pairs=240;timeouts=delivery['async_timeouts']
 else:
  from check_native_xr_muzzle import check as check_muzzle
  delivery=check_muzzle(host);pairs=delivery['pairs'];timeouts=delivery['async_timeouts']
  require(s['fire_samples']>30 and s['aim_samples']>=s['held_samples'],'supported aim/fire path was not exercised')
  require(s['max_aim_residual_radians']<.0001,'native aim diverged from assisted aim')
  shots=[r for r in g['fire_origin_observation']['records'] if r['phase']==2 and r['origin_written']]
  for shot in shots:
   prior=[r for r in rows if r['ms']<=shot['ms']]
   require(prior and prior[-1]['holding'] and prior[-1]['correction_radians']>.05,'recorded shot was not two-hand steered')
 return {'passed':True,'native_report':str(native.resolve()),'host_report':str(Path(host).resolve()) if host else None,'receiver_report':str(Path(receiver).resolve()) if receiver else None,'grabs':s['grabs'],'releases':s['releases'],'supported_samples':s['held_samples'],'supported_fire_samples':s.get('fire_samples',0),'maximum_aim_residual_radians':s.get('max_aim_residual_radians'),'maximum_wrist_residual_m':maximum,'maximum_correction_degrees':max(r['correction_radians'] for r in rows)*180/math.pi,'offhand_and_positions_preserved':True,'native_sources_unchanged':True,'pairs':pairs,'async_timeouts':timeouts,'headset_tested':False,'projectile_impact_tested':False}

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--native');p.add_argument('--receiver');p.add_argument('--host');p.add_argument('--output');p.add_argument('--weapon-kind',type=int,choices=(1,2));a=p.parse_args()
 if a.host:a.native=json.loads((Path(a.host)/'reports.json').read_text())['native_report']
 if not a.native or not(a.receiver or a.host):p.error('choose --native/--receiver or --host')
 result=check(a.native,a.receiver,a.host,a.weapon_kind);text=json.dumps(result,indent=2)
 if a.output:Path(a.output).write_text(text+'\n',encoding='utf8')
 print(text)
