"""Emit exact authored bolt gestures; preserve separate native closed-pose evidence."""
import argparse,json,math,hashlib
from pathlib import Path
import bc2_authored_magazine_geometry as m
import bc2_authored_mechanism_geometry as mechanism

def fl(x):
    s=format(float(x),'.10g')
    return s+('f' if '.' in s or 'e' in s.lower() else '.f')
def mat(v):return 'math::Matrix4{{{'+','.join('{'+','.join(fl(x)for x in v[i:i+4])+'}' for i in range(0,16,4))+'}}}'
def stable(value):return int(hashlib.sha256(value.encode()).hexdigest()[:16],16)

def derive(group):
    rows=group['samples'];ref=m.matrix(group['reference']['jntWpn_3']);inverse=m.inverse_rigid(ref)
    grips=[];travel=[];angles=[]
    for row in rows:
        poses=row['poses'];part=m.matrix(poses['jntWpn_3']);wrist=m.matrix(poses['RightHand'])
        delta=m.multiply(part,inverse);axis,angle=mechanism.axis_angle(delta)
        travel.append(delta[14]);angles.append(-angle if axis and axis[2]<0 else angle)
        if abs(delta[12])>.002 or abs(delta[13])>.002:raise ValueError('Bolt translation leaves authored axial envelope')
        if angle>.05 and (axis is None or abs(axis[2])<.999):raise ValueError('Bolt rotation leaves authored axial envelope')
        grips.append((m.multiply(wrist,m.inverse_rigid(part)),
            [m.multiply(m.matrix(poses[n.replace('Left','Right')]),m.inverse_rigid(wrist))for n in m.FINGERS]))
    choices=[]
    for i in range(2,len(rows)-2):
        indices=range(i-2,i+3)
        dt=max(m.grasp_separation(grips[i][0],grips[j][0])[0] for j in indices)
        da=max(m.grasp_separation(grips[i][0],grips[j][0])[1] for j in indices)
        finger=max(m.grasp_separation(grips[i][1][n],grips[j][1][n])[1] for j in indices for n in range(15))
        contact=max(rows[j]['knuckle_to_part_m'] for j in indices)
        if dt<.004 and da<.05 and finger<.06 and contact<.05 and .2<rows[i]['time']<1.5:
            choices.append((dt,da,finger,contact,i))
    if choices:
        chosen=min(choices);index=chosen[-1];policy='same_frame_stable_authored_grasp';metrics=chosen[:-1]
    else:
        # The GOL authored wrist changes during the stroke. Freeze one real
        # contact as explicit mod policy; do not claim a stable native grasp.
        eligible=[i for i,r in enumerate(rows)if r['knuckle_to_part_m']<.05 and abs(angles[i])>.1]
        if not eligible:raise ValueError('No source contact for fixed mod grasp')
        index=min(eligible,key=lambda i:rows[i]['knuckle_to_part_m']);policy='explicit_fixed_contact_from_exact_authored_frame';metrics=None
    stroke=max(travel);unlock=min(angles)
    if not .02<stroke<.25 or not -2<unlock<-.1:raise ValueError('Bounded axial bolt stroke required')
    return dict(closed=ref,stroke=stroke,unlock=unlock,wrist=grips[index][0],fingers=grips[index][1],
        source_seconds=rows[index]['time'],policy=policy,stable_window_metrics=metrics,
        source_translation_minimum=min(travel),native_closed_pose_measured=False)

def generate(packet):
    lines=['#pragma once','#include "Bc2BoltRuntimeGeometry.h"','namespace fvr::bc2::generated {']
    derived={}
    for key,group in packet['groups'].items():
        if 'reuse'in group:continue
        data=derived[key]=derive(group)
        lines.append(f'inline const interaction::AuthoredHandShape BoltHand{key}=[]{{interaction::AuthoredHandShape h;h.counts={{3,3,3,3,3}};')
        for i,pose in enumerate(data['fingers']):lines.append(f'h.wristFromJoint[{i//3}][{i%3}]={mat(pose)};')
        lines.append('return h;}();')
    lines.append('inline const interaction::AuthoredHandShape ReusedM95BoltHand=[]{interaction::AuthoredHandShape h;h.counts={3,3,3,3,3};for(unsigned i=0;i<15;++i)h.wristFromJoint[i/3][i%3]=M95BoltFingers[i].wristFromFinger;return h;}();')
    selected=sorted(packet['selected'],key=lambda r:r['configuration']['assetPath'])
    if len(selected)!=17 or len({r['configuration']['assetPath'] for r in selected})!=17:raise ValueError('Exact seventeen configuration roster required')
    lines.append(f'inline const std::array<Bc2BoltCalibration,{len(selected)}> BoltEquipment=[]{{std::array<Bc2BoltCalibration,{len(selected)}> out;')
    for i,row in enumerate(selected):
        cfg=row['configuration'];key=row['content'];g=packet['groups'][key]
        lines.append(f'{{auto& c=out[{i}];')
        if 'reuse'in g:
            if g['mesh']!='Objects/Weapons/Handheld/BU_sni_M95/BU_sni_M95_Mesh' or g['rig']!='fnv1a64:a7f219a1426216ab':raise ValueError('M95 geometry reuse requires exact mesh/rig')
            lines.append('c=M95ObservedBoltCalibration235();c.handShape=&ReusedM95BoltHand;')
            lines.append('c.nativeJoined='+('true' if cfg['assetPath']=='Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95' else 'false')+';')
        else:
            d=derived[key]
            lines.append('c.partName="jntWpn_3";c.rigFingerprint=0x'+g['rig'].split(':')[1]+'ull;')
            lines.append('c.profile.family=interaction::WeaponCycleFamily::Bolt;c.profile.hands={interaction::InteractionHand::Right,interaction::InteractionHand::Left};')
            lines.append('c.profile.axis={0,0,1};c.profile.closedContact='+mat(d['closed'])+';')
            lines.append(f'c.profile.stroke={fl(d["stroke"])};c.profile.unlockRadians={fl(d["unlock"])};c.wristFromPart={mat(d["wrist"])};')
            lines.append('c.profile.frontTolerance=c.profile.rearTolerance=.004f;c.profile.contactRadius=.04f;c.profile.lateralTolerance=.015f;c.profile.rotationTolerance=.035f;c.profile.maxStepMeters=.025f;c.profile.endpointDwellNs=30000000;c.profile.maximumCycleNs=30000000000ll;')
            lines.append(f'c.handShape=&BoltHand{key};')
        lines.append(f'c.asset={json.dumps(cfg["assetName"])};c.mesh={json.dumps(row["mesh"])};c.configurationPath={json.dumps(cfg["assetPath"])};')
        lines.append(f'c.profile.id=0x{stable(cfg["assetPath"]+key):016x}ull;c.profile.revision=47;c.authoredReviewed=true;}}')
    lines.extend(['return out;}();','} // namespace fvr::bc2::generated',''])
    return '\n'.join(lines),derived

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--samples',type=Path,required=True);p.add_argument('--header',type=Path,required=True);p.add_argument('--receipt',type=Path,required=True);a=p.parse_args()
    packet=json.loads(a.samples.read_text());header,derived=generate(packet)
    a.header.parent.mkdir(parents=True,exist_ok=True);a.header.write_text(header,encoding='utf-8')
    a.receipt.write_text(json.dumps({'schema':'fvr.bc2.bolt_equipment_review.v1','samples_sha256':hashlib.sha256(a.samples.read_bytes()).hexdigest(),
        'configurations':17,'physical_authoring':derived,'native_runtime_acceptance':False},indent=2)+'\n',encoding='utf-8')
    print('17 exact bolt profiles generated; native/headset acceptance remains untested')
if __name__=='__main__':main()
