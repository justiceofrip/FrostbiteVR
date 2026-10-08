"""Independent numeric audit of ControlsVehicle native camera observations.

Uses only Python's standard library and raw reported poses/matrices. It does not
load FvrCore, run the production camera policy or access a game process.
"""
from __future__ import annotations
import argparse,json,math
from pathlib import Path

def ident():return [[float(r==c) for c in range(4)] for r in range(4)]
def multiply(a,b):return [[sum(a[r][k]*b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]
def inverse_rigid(m):
    out=ident()
    for r in range(3):
        for c in range(3):out[r][c]=m[c][r]
    for c in range(3):out[3][c]=-sum(m[3][k]*out[k][c] for k in range(3))
    return out
def pose_rh(pose,units=1.):
    assert len(pose)==7 and all(math.isfinite(v) for v in pose)
    x,y,z,w=pose[3:];length=math.sqrt(x*x+y*y+z*z+w*w);assert length>1e-8
    x,y,z,w=(v/length for v in (x,y,z,w))
    # Standard active quaternion column matrix, transposed for row vectors.
    column=[[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
            [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
            [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
    out=ident()
    for r in range(3):
        for c in range(3):out[r][c]=column[c][r]
    out[3][:3]=[v*units for v in pose[:3]];return out
def relative_lh(reference,current,units):
    relative=multiply(pose_rh(current,units),inverse_rigid(pose_rh(reference,units)))
    reflection=[1,1,-1,1]
    return [[relative[r][c]*reflection[r]*reflection[c] for c in range(4)] for r in range(4)]
def matrix(values):
    assert len(values)==16 and all(math.isfinite(v) for v in values)
    return [values[r*4:r*4+4] for r in range(4)]
def flat(m):return [v for row in m for v in row]
def distance(a,b):return math.sqrt(sum((x-y)**2 for x,y in zip(a,b)))
def rotation_error(a,b):return max(abs(a[r][c]-b[r][c]) for r in range(3) for c in range(3))
def camera_rows(value):
    if isinstance(value,dict):
        if 'vehicle_camera_evidence' in value:return value['vehicle_camera_evidence']
        for item in value.values():
            found=camera_rows(item)
            if found is not None:return found
    elif isinstance(value,list):
        for item in value:
            found=camera_rows(item)
            if found is not None:return found
    return None

def audit(rows):
    assert rows and len(rows)<=16,'Missing or unbounded camera evidence'
    assert all(r['applied_mask']==3 for r in rows),'A sampled stereo pair lacks native post-setter readback'
    first=rows[0];units=first['units_per_metre'];assert math.isfinite(units) and units>0
    first_head=relative_lh(first['reference_pose'],first['head_pose'],units)
    # ControlsVehicle intentionally enters with pure yaw. General headset
    # pitch/roll anchoring is outside this fixture's claim.
    assert abs(first['head_pose'][3])<1e-6 and abs(first['head_pose'][5])<1e-6
    entry_offset=distance(first['head_pose'][:3],first['reference_pose'][:3])
    entry_yaw=abs(math.degrees(math.atan2(first_head[2][0],first_head[2][2])))
    assert abs(entry_offset-.6)<1e-5 and abs(entry_yaw-20)<1e-3,'Fixture did not capture the .6m/20deg entry'
    anchor=inverse_rigid(first_head)
    errors={name:0. for name in ('adjusted_position_m','adjusted_rotation','planned_position_m','planned_rotation','applied_position_m','applied_rotation','center_position_m','ipd_m','seat_relative_lean_m')}
    observations=[];saw_lean=False;saw_return=False;previous_frame=0;initial_native=matrix(first['native_camera']);native_motion=0.
    key=lambda row:tuple(row[n] for n in ('actor','actor_generation','vehicle','entry','space'))
    for row in rows:
        assert key(row)==key(first),'Owner/reference changed during this bounded fixture'
        assert row['native_frame']>previous_frame;previous_frame=row['native_frame']
        assert row['units_per_metre']==units and row['tracking_sequence']>0
        assert row['reference_pose']==first['reference_pose'],'Reference changed without a space generation'
        native=matrix(row['native_camera']);adjusted=matrix(row['adjusted_camera'])
        expected_base=multiply(anchor,native)
        errors['adjusted_position_m']=max(errors['adjusted_position_m'],distance(adjusted[3][:3],expected_base[3][:3])/units)
        errors['adjusted_rotation']=max(errors['adjusted_rotation'],rotation_error(adjusted,expected_base))
        native_motion=max(native_motion,distance(native[3][:3],initial_native[3][:3])/units)
        actual=[]
        for side in range(2):
            expected=multiply(relative_lh(row['reference_pose'],row['eye_poses'][side],units),expected_base)
            for label,field in [('planned','planned_eyes'),('applied','applied_eyes')]:
                observed=matrix(row[field][side])
                errors[label+'_position_m']=max(errors[label+'_position_m'],distance(observed[3][:3],expected[3][:3])/units)
                errors[label+'_rotation']=max(errors[label+'_rotation'],rotation_error(observed,expected))
            actual.append(matrix(row['applied_eyes'][side]))
        expected_head=multiply(relative_lh(row['reference_pose'],row['head_pose'],units),expected_base)
        center=[(actual[0][3][k]+actual[1][3][k])/2 for k in range(3)]
        errors['center_position_m']=max(errors['center_position_m'],distance(center,expected_head[3][:3])/units)
        source_ipd=distance(row['eye_poses'][0][:3],row['eye_poses'][1][:3]);native_ipd=distance(actual[0][3][:3],actual[1][3][:3])/units
        assert abs(source_ipd-.064)<1e-5,'Fixture eye separation differs from64mm'
        errors['ipd_m']=max(errors['ipd_m'],abs(native_ipd-source_ipd))
        lean=distance(row['head_pose'][:3],first['head_pose'][:3]);seat_lean=distance(center,native[3][:3])/units
        assert min(abs(lean),abs(lean-.1))<1e-5,'Unexpected fixture displacement'
        errors['seat_relative_lean_m']=max(errors['seat_relative_lean_m'],abs(seat_lean-lean))
        if lean>.09:saw_lean=True
        elif saw_lean:saw_return=True
        observations.append({'native_frame':row['native_frame'],'tracking_sequence':row['tracking_sequence'],'head_delta_m':lean,'native_seat_relative_center_m':seat_lean,'native_ipd_m':native_ipd})
    assert saw_lean,'The 10cm lean phase was not captured'
    assert saw_return,'The return from lean was not captured'
    for name,error in errors.items():
        tolerance=2e-5 if name.endswith('rotation') else .002
        assert error<=tolerance,f'{name}: {error} exceeds {tolerance}'
    return {'passed':True,'rows':len(rows),'entry_offset_m':entry_offset,'entry_yaw_degrees':entry_yaw,'later_lean_and_return_observed':True,
            'max_errors':errors,'native_seat_translation_observed_m':native_motion,'observations':observations,
            'verified':'matching raw tracking, native seat, adjusted base, planned eyes and native post-setter matrices','headset_tested':False,'physics_movement_verified':False}

def self_test():
    first_head=[.6,0,0,0,math.sin(math.radians(10)),0,math.cos(math.radians(10))];reference=[0,0,0,0,0,0,1];anchor=inverse_rigid(relative_lh(reference,first_head,1))
    rows=[]
    for n,x in enumerate((.6,.6,.7,.7,.6)):
        head=list(first_head);head[0]=x;native=pose_rh([120+n,.7,-310,0,math.sin(n*.04),0,math.cos(n*.04)])
        base=multiply(anchor,native);eyes=[head.copy(),head.copy()];eyes[0][0]-=.032;eyes[1][0]+=.032
        outputs=[flat(multiply(relative_lh(reference,eye,1),base)) for eye in eyes]
        rows.append({'actor':1,'actor_generation':2,'vehicle':3,'entry':4,'space':5,'native_frame':n+1,'tracking_sequence':n+10,'units_per_metre':1,'applied_mask':3,'reference_pose':reference,'head_pose':head,'eye_poses':eyes,'native_camera':flat(native),'adjusted_camera':flat(base),'planned_eyes':outputs,'applied_eyes':[v.copy() for v in outputs]})
    audit(rows)
    # Independently corrupt actual native eye position/IPD and require failure.
    bad=json.loads(json.dumps(rows));bad[2]['applied_eyes'][1][12]+=.02
    try:audit(bad)
    except AssertionError:pass
    else:raise AssertionError('Audit accepted a corrupt native eye')
    bad=json.loads(json.dumps(rows));bad[2]['adjusted_camera']=bad[2]['native_camera']
    try:audit(bad)
    except AssertionError:pass
    else:raise AssertionError('Audit accepted inherited-offset regression')
    return {'self_test_passed':True,'native_capture_tested':False}

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('report',nargs='?',type=Path);parser.add_argument('--output',type=Path);parser.add_argument('--self-test',action='store_true');args=parser.parse_args()
    try:
        result=self_test() if args.self_test else audit(camera_rows(json.loads(args.report.read_text(encoding='utf-8-sig'))))
    except (AssertionError,ValueError,TypeError,KeyError) as error:
        result={'passed':False,'error':str(error),'headset_tested':False}
    text=json.dumps(result,indent=2);print(text)
    if args.output:args.output.write_text(text)
    raise SystemExit(0 if result.get('passed') or result.get('self_test_passed') else 1)
