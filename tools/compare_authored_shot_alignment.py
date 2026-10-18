"""Compare an authored render candidate with native pre-spread composition.

No process access. The native composition precedes spread, inherited velocity
and projectile dispatch. The render candidate is unexpired and owner-matched,
not proven to use the input consumed by this native invocation.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()


def unit(v):
    size = math.sqrt(sum(x*x for x in v))
    if not math.isfinite(size) or size < 1e-8:
        raise ValueError('Invalid direction')
    return [x/size for x in v]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def angle(a, b):
    return math.degrees(math.acos(max(-1., min(1., dot(unit(a), unit(b))))))


def direction(v, matrix):
    if len(matrix) != 16 or not all(math.isfinite(x) for x in matrix):
        raise ValueError('Invalid transform')
    return unit([sum(v[k]*matrix[k*4+c] for k in range(3)) for c in range(3)])


def canonical(native):
    if len(native) != 16:
        raise ValueError('Invalid native matrix')
    signs = [1, 1, -1, 1]
    return [native[r*4+c]*signs[r]*signs[c] for r in range(4) for c in range(4)]


def quat_product(a, b):
    x,y,z,w = a; X,Y,Z,W = b
    return [w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X,
            w*Z+x*Y-y*X+z*W, w*W-x*X-y*Y-z*Z]


def effective_aim_direction(evidence):
    reference = unit(evidence['reference_pose'][3:7])
    # Tracking contains the shared support policy's corrected render/aim input.
    aim = unit(evidence['effective_aim'][3:7])
    x,y,z,w = unit(quat_product([-reference[0],-reference[1],-reference[2],reference[3]], aim))
    local = [-2*(x*z+w*y), 2*(w*x-y*z), 1-2*(x*x+y*y)]
    return direction(local, evidence['body'])


def compare(native_path, axes_path, bindings_path, firing_path, asset):
    report = json.loads(native_path.read_text())
    axes = json.loads(axes_path.read_text())
    bindings = json.loads(bindings_path.read_text())
    entries = [e for e in axes['profiles'] if e['native_asset_name'] == asset]
    sources = [e for e in bindings['profiles'] if e['native_asset_name'] == asset]
    if len(entries) != 1 or len(sources) != 1:
        raise ValueError('One exact authored descriptor and binding required')
    axis, binding = entries[0], sources[0]
    if axis['binding_digest'] != binding['binding_digest'] or digest({k:v for k,v in binding.items() if k != 'binding_digest'}) != binding['binding_digest']:
        raise ValueError('Authored binding identity differs')
    raw_firing = firing_path.read_bytes()
    if hashlib.sha256(raw_firing).hexdigest() != axis['review']['firing_fields_sha256']:
        raise ValueError('Typed firing source differs from reviewed axes')
    firing = json.loads(raw_firing)
    raw_direction = firing['fields']['InitialDirection']['raw_xyzw'][:3]
    if raw_direction != [0, 0, 1]:
        raise ValueError('Only the reviewed neutral InitialDirection is admitted')
    # Native composition already includes the InitialDirection basis. Canonical
    # -Z is its forward axis; do not apply authored direction a second time.
    local_direction = [0, 0, -1]
    records, rejected = [], []
    for row in report['gameplay']['fire_origin_observation']['records']:
        if row['phase'] != 2 or row.get('weapon_name') != asset:
            continue
        try:
            shot = row['tracked_shot_candidate']; evidence = shot['aim_evidence']
            valid = (row['matrix_stage'] == 'native_pre_spread_composition' and
                     row['observed_candidate_unexpired'] and row['token_valid'] and
                     0 < row['observed_qpc'] < shot['deadline_qpc'] and
                     0 < shot['generation'] <= row['observed_current_generation'] and
                     row['observed_qpc'] < row['observed_current_deadline_qpc'] and
                     evidence['configuration_path_verified'] and evidence['authored_binding'] and evidence['authored_axes'] and
                     evidence['asset'] == asset and evidence['weapon'] == row['weapon'] and
                     evidence['weapon_data'] == row['weapon_data'] and
                     evidence['persistence'] == row['persistence'] and
                     evidence['configuration_path'] == binding['weapon']['resource'][:-4] and
                     evidence['mesh'] == axis['configured_mesh_path'] and
                     evidence['binding_digest'] == axis['binding_digest'] and
                     evidence['axis_evidence'] == digest(axis) and
                     evidence['rig_fingerprint'] == int(axis['rig_fingerprint'].split(':')[1],16) and
                     evidence['model_forward'] == axis['model_forward'] and evidence['model_up'] == axis['model_up'])
            if not valid:
                raise ValueError('Missing unexpired candidate or exact authored source proof')
            before, after = canonical(row['native_shot_matrix']), canonical(row['output_matrix'])
            native = direction(local_direction, before)
            barrel = direction(axis['model_forward'], shot['tracked_weapon'])
            up = direction(axis['model_up'], shot['tracked_weapon'])
            right = unit([up[1]*barrel[2]-up[2]*barrel[1],up[2]*barrel[0]-up[0]*barrel[2],up[0]*barrel[1]-up[1]*barrel[0]])
            units = shot['units_per_metre']
            if not math.isfinite(units) or units <= 0:
                raise ValueError('Invalid world scale')
            flash = shot['tracked_flash'][12:15]
            delta = [(before[12+k]-flash[k])/units for k in range(3)]
            along = dot(delta,native)
            records.append(dict(ms=row['ms'],weapon=row['weapon'],token=row['shot_token'],client=row['client_path'],
                space=shot['space'],render_candidate_generation=shot['generation'],owner_generation=shot['owner_generation'],
                composition_observed_qpc=row['observed_qpc'],candidate_original_deadline_qpc=shot['deadline_qpc'],
                observed_current_render_generation=row['observed_current_generation'],
                observed_current_render_deadline_qpc=row['observed_current_deadline_qpc'],
                render_generation_gap=row['observed_current_generation']-shot['generation'],
                candidate_remaining_qpc_ticks=shot['deadline_qpc']-row['observed_qpc'],
                candidate_age_known=False,native_consumed_input_generation=None,
                native_muzzle_supported=bool(row['native_muzzle_supported']),origin_written=row['origin_written'],
                native_orientation_unchanged=before[:12]==after[:12],native_output_unchanged=before==after,
                candidate_barrel_to_effective_aim_degrees=angle(barrel,effective_aim_direction(evidence)),
                pre_spread_composition_to_candidate_barrel_degrees=angle(native,barrel),
                pre_spread_composition_yaw_from_candidate_barrel_degrees=math.degrees(math.atan2(dot(native,right),dot(native,barrel))),
                pre_spread_composition_pitch_from_candidate_barrel_degrees=math.degrees(math.asin(max(-1.,min(1.,dot(native,up))))),
                composition_origin_from_candidate_muzzle_m=delta,composition_origin_distance_m=math.sqrt(dot(delta,delta)),
                candidate_muzzle_to_composition_axis_m=math.sqrt(max(0.,dot(delta,delta)-along*along)),
                output_composition_origin_from_candidate_muzzle_m=[(after[12+k]-flash[k])/units for k in range(3)]))
        except (KeyError, TypeError, ValueError) as error:
            rejected.append(dict(ms=row.get('ms'),reason=str(error)))
    spaces = []
    for space in sorted({r['space'] for r in records}):
        rows = sorted((r for r in records if r['space']==space), key=lambda r:r['ms'])
        event = lambda r: (r['owner_generation'], r['weapon'], r['token'])
        first_event = event(rows[0])
        spaces.append(dict(space=space,first_recorded_composition=[r for r in rows if event(r)==first_event],
                           other_recorded_compositions=[r for r in rows if event(r)!=first_event]))
    return dict(schema=2,asset=asset,native_report=str(native_path),observations=len(records),rejected=rejected,
                observation_boundary='native_pre_spread_composition',
                reference_change_observed=len(spaces)>1,spaces=spaces,
                native_verified=False,same_shot_input_join_verified=False,physical_controller_alignment_verified=False,
                spread_measured=False,final_projectile_velocity_measured=False,projectile_impact_tested=False,
                limitations=['Composition precedes native spread, inherited velocity and projectile dispatch; this is not the final bullet direction.',
                             'The render candidate retains its original deadline but can precede newer input; candidate age and consumed native input are not recorded.',
                             'Effective aim is the input after support correction; pre-support input can be supplied by a diagnostic receiver, so neither proves raw physical hardware.',
                             'The first recorded composition is not proof of a settled first shot or matched client/server projectile.',
                             'Read-only muzzle candidates are not event-latched write authorization.',
                             'Reference-space change is evidence of a change, not by itself proof of a manual recenter.'])


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native',type=Path,required=True)
    parser.add_argument('--axes',type=Path,required=True)
    parser.add_argument('--bindings',type=Path,required=True)
    parser.add_argument('--firing-fields',type=Path,required=True)
    parser.add_argument('--asset',required=True)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    text=json.dumps(compare(args.native,args.axes,args.bindings,args.firing_fields,args.asset),indent=2,allow_nan=False)+'\n'
    if args.output:args.output.write_text(text)
    print(text)
