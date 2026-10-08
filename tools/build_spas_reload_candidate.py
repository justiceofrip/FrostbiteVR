"""Build a disabled BC2 SPAS reload draft from measured native observations.

Measured grasp/terminal poses remain separate from interaction design tolerances.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import numpy as np
import weapon_profile_pipeline as profiles


def build(contacts,observations):
    if contacts.get('schema')!='fvr.bc2.reload_contact_candidates' or observations.get('schema')!='fvr.bc2.reload_shell_observations':raise ValueError('Unexpected evidence schema')
    if observations.get('asset_name')!='SPAS12_sp' or len(contacts['groups'])!=1 or len(observations['groups'])!=1:raise ValueError('Exact SPAS capture group required')
    contact=contacts['groups'][0];observed=observations['groups'][0]
    if any(contact[k]!=observed[k] for k in ('actor','weapon','space')):raise ValueError('Capture ownership mismatch')
    grasp=contact['best_authored_grasp_observation']
    if not grasp or grasp['terminal_before_transfer']:raise ValueError('Pre-insertion grasp observation required')
    candidates=[r for r in observed['native_transfer_neighbors'] if r['wrapper_offset']==60 and r['previous_visible_pose']]
    if not candidates:raise ValueError('Native single-shell transfer neighbor required')
    transfer=max(candidates,key=lambda r:r['tick_ms']);terminal=transfer['previous_visible_pose']
    row=next(o for o in observed['observations'] if o['row']==terminal['row'])
    center_in_weapon=profiles.matrix(row['shell_center_in_weapon'],1e-4)
    item_from_hand=profiles.matrix(grasp['item_from_hand_candidate'],1e-4)
    length=observed['mapped_asset_bounds_in_shell_bone']['axis_length_m']
    if not math.isfinite(length) or not .02<length<.15:raise ValueError('Shell length outside draft domain')
    # Deliberate profile construction: item origin is measured shell bounds center;
    # the inferred nose is the positive asset-Z end, canonical item -Z.
    item_from_insertion=np.diag([-1.,1.,-1.,1.]);item_from_insertion[3,2]=-length/2
    tip_target=profiles.matrix(item_from_insertion@center_in_weapon,1e-4)
    travel=.05;entry=tip_target.copy();entry[3,:3]-=entry[2,:3]*travel
    at_seat=item_from_insertion@center_in_weapon@np.linalg.inv(entry)
    expected=np.eye(4);expected[3,2]=travel
    if not np.allclose(at_seat,expected,atol=1e-5,rtol=0):raise ValueError('Constructed rail does not reach observed terminal pose')
    raw_grasp=profiles.matrix(grasp['observed_center_frame_in_wrist'],1e-4)
    if not np.allclose(item_from_hand@raw_grasp,np.eye(4),atol=1e-5,rtol=0):raise ValueError('Authored grasp inverse mismatch')
    surface=next(r for r in contact['receiver_surface_observations'] if r['row']==terminal['row'])
    profile={'id':0x535041530001,'revision':1,'family':'SingleShell','orientation':'Keyed',
        'itemFromHand':item_from_hand.reshape(-1).tolist(),'itemFromInsertion':item_from_insertion.reshape(-1).tolist(),
        'weaponFromEntry':entry.reshape(-1).tolist(),'travelMeters':travel,
        'captureDistanceMeters':.03,'releaseDistanceMeters':.06,
        'captureAngleRadians':math.radians(35),'releaseAngleRadians':math.radians(70),
        'seatToleranceMeters':.003,'maxStepMeters':.04,'maxStepRadians':math.radians(60),
        'alignmentNs':120000000,'seatDwellNs':60000000,'maxSampleGapNs':100000000,'maxGuidedNs':5000000000}
    return {'schema':'fvr.bc2.reload_insertion_candidate','schema_version':1,'stable_id':'bc2:SPAS12_sp:single_shell:draft1',
        'enabled':False,'status':'derived draft awaiting native gate and visual test','game':'bc2','asset_name':'SPAS12_sp',
        'shell_bone':'jntWpn_7','native_written':False,'coordinate_convention':'canonical LH row-vector; translations in metres',
        'portable_profile_candidate':profile,
        'measured_evidence':{'grasp_row':grasp['row'],'grasp_captured_ms':grasp['captured_ms'],
            'shell_center_to_wrist':grasp['observed_center_frame_in_wrist'],
            'joint_surface_probes':grasp['joint_surface_probes'],
            'terminal_row':terminal['row'],'terminal_relative_to_transfer_ms':terminal['relative_to_transfer_ms'],
            'terminal_shell_center_in_weapon':row['shell_center_in_weapon'],'shell_length_m':length,
            'observed_last_approach':surface['last_observed_motion'],'nearest_receiver_surface':surface['nearest_receiver_surface'],
            'receiver_coplanar_patch':surface['coplanar_surface_patch']},
        'constructed_geometry':{'item_frame':'Actual shell asset AABB center with canonical asset axes',
            'nose_landmark':'Positive asset-Z extent / negative canonical item-Z endpoint; semantic direction inferred from brass/plastic extents',
            'rail_start_tip_in_weapon':entry[3,:3].tolist(),'rail_target_tip_in_weapon':tip_target[3,:3].tolist(),
            'rail_inward_axis_in_weapon':entry[2,:3].tolist(),'terminal_shell_center_in_weapon':center_in_weapon[3,:3].tolist(),
            'rail_start_shell_center_in_weapon':(center_in_weapon[3,:3]-entry[2,:3]*travel).tolist(),
            'rail_frame_basis':'Use the measured terminal shell orientation, reframed so entry +Z follows inferred shell nose',
            'travel_basis':'Chosen50mm interaction stroke, not a measured native rail length',
            'actual_open_aperture':False,'native_loading_socket_verified':False},
        'design_choices':{'orientation':'Keyed retains measured roll for the first draft; axial freedom may be reviewed separately.',
            'capture_release_radius_m':[.03,.06],'capture_release_cone_degrees':[35,70],
            'alignment_ms':120,'seat_dwell_ms':60,'seat_tolerance_m':.003,'maximum_pose_step_m':.04,
            'maximum_pose_step_degrees':60,'maximum_sample_gap_ms':100,'maximum_guidance_ms':5000,
            'basis':'Deliberate interaction tolerances for initial preview; these values were not extracted from BC2.'},
        'pending':['Native hold/commit/reconcile gate and exact ammo resource ownership.','Renderer/skin section visibility and headset grasp/rail acceptance.'],
        'limitations':['The current native receiver has closed render faces near this area; retain as asset occlusion/polish work, not an invented open loading aperture.',
            'Finger terminal joints are anatomical proximity proxies rather than measured fingertip skin contact.',
            'The native terminal shell pose was sampled before ammo transfer; target placement is a deliberate initial interaction candidate.',
            'This JSON is not loaded by the runtime and grants no ammo or native reload operation.']}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--contacts',type=Path,required=True);p.add_argument('--observations',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();contacts=profiles.load_json(a.contacts);observations=profiles.load_json(a.observations)
    if contacts['sources']['observations']['sha256']!=profiles.digest(a.observations):raise ValueError('Contact observation hash mismatch')
    result=build(contacts,observations)
    result['sources']={name:{'path':str(path.resolve()),'sha256':profiles.digest(path)} for name,path in [('contacts',a.contacts),('observations',a.observations)]}
    result['builder_sha256']=profiles.digest(__file__);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf8')
    print(json.dumps({'output':str(a.output),'enabled':False,'grasp_row':result['measured_evidence']['grasp_row'],'terminal_row':result['measured_evidence']['terminal_row'],'travel_m':result['portable_profile_candidate']['travelMeters']}))

if __name__=='__main__':main()
