"""Compile exact shotgun43 configurations and evaluated authored geometry.

This creates physical mod policy, not native animation or ammunition authority.
Four independently evaluated content groups retain 25 exact configuration joins.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import struct
import uuid
from pathlib import Path
from bc2_authored_grip_bindings import digest
from bc2_weapon_animation_pipeline import identity, inverse_rigid, multiply

ACTION_HASH='c1e775329c56c32bec9ed49392d5b080f4b9f50bbd66c0df15945f98d9dbd7b8'
# Explicit semantic review. Keys are exact configured meshes, never name prefixes.
ROLES={
 'Objects/Weapons/Handheld/BU_shg_Neostead/BU_shg_Neostead_Mesh':
     dict(pump=189,shell=190,pump_bone='jntWpn_4',shell_bone='jntWpn_13',grasp=12,entry=24,seat=30,pump_grasp=12),
 'Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh':
     dict(pump=200,shell=201,pump_bone='jntWpn_4',shell_bone='jntWpn_7',grasp=18,entry=21,seat=26,pump_grasp=14),
 'Objects/Weapons/Handheld/US_shg_Remington870MCS/US_shg_Remington870MCS_Mesh':
     dict(pump=205,shell=206,pump_bone='jntWpn_4',shell_bone='jntWpn_3',grasp=18,entry=21,seat=27,pump_grasp=26),
 'Objects/Weapons/Handheld/MEC_shg_TOZ194/MEC_shg_TOZ194_Mesh':
     dict(pump=386,shell=387,pump_bone='jntWpn_5',shell_bone='jntWpn_6',grasp=18,entry=21,seat=27,pump_grasp=0),
}
FINGERS=['LeftHand'+f+str(j) for f in ('Thumb','Index','Middle','Ring','Pinky') for j in (1,2,3)]
UX=dict(captureDistanceMeters=.06,releaseDistanceMeters=.12,postCaptureTravelMeters=.02,
        captureAngleRadians=math.radians(35),releaseAngleRadians=math.radians(70),
        seatToleranceMeters=.003,maxStepMeters=.04,maxStepRadians=math.pi/3,
        alignmentNs=120000000,seatDwellNs=60000000,maxSampleGapNs=100000000,maxGuidedNs=5000000000)

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def rigid(m):
    if len(m)!=16 or not all(math.isfinite(v) for v in m):raise ValueError('Finite rigid transform required')
    inverse_rigid(m)
    return m
def translation(v):
    m=identity();m[12:15]=v;return m
def unit(v):
    length=math.hypot(*v)
    if length<1e-8:raise ValueError('Zero motion direction')
    return [x/length for x in v]
def finger_shape(poses):
    inv=inverse_rigid(poses['LeftHand'])
    return {n:rigid(multiply(poses[n],inv))for n in FINGERS}
def content(record):
    src=record['source']
    return (record['mesh'],src['mesh_sha256'],src['lod_sha256'],src['sha256'],src['reference_clip_sha256'])
def group_content(group):
    src=group['identity']
    return tuple(src[k] for k in ('mesh','mesh_sha256','lod_sha256','sha256','reference_clip_sha256'))
def checked_samples(samples):
    if samples.get('schema')!='fvr.bc2.shotgun_exact_samples.v1' or samples.get('actions_sha256')!=ACTION_HASH:
        raise ValueError('Pinned action samples required')
    rows=samples.get('records',[])
    expected={v[k]for v in ROLES.values()for k in ('pump','shell')}
    if len(rows)!=8 or {r['record_index']for r in rows}!=expected:raise ValueError('Exact eight sampled actions required')
    for r in rows:
        if r.get('digest')!=digest({k:v for k,v in r.items() if k!='digest'}):raise ValueError('Sample receipt digest differs')
        if not 2<=len(r['samples'])<=128:raise ValueError('Sample bound')
        for i,s in enumerate(r['samples']):
            if abs(s['time']-min(i/60,r['duration_s']))>1e-8:raise ValueError('Sample cadence differs')
            for m in s['poses'].values():rigid(m)
    return {r['record_index']:r for r in rows}

def shell_geometry(record,role):
    name=role['shell_bone'];parts={p['bone']:p for p in record['parts']};part=parts[name]
    if not part['independent_rigid_geometry'] or part['mixed_triangles'] or part['triangles']<90:raise ValueError('Independent shell geometry required')
    lo,hi=part['bounds_m'];extent=[b-a for a,b in zip(lo,hi)]
    if not .05<extent[2]<.09 or not all(.015<x<.03 for x in extent[:2]):raise ValueError('Reviewed round shell extent differs')
    center=[(a+b)/2 for a,b in zip(lo,hi)];center_in_bone=translation(center)
    source=record['samples'][role['grasp']];poses=source['poses']
    item=rigid(multiply(center_in_bone,poses[name]))
    hand=rigid(multiply(poses['LeftHand'],inverse_rigid(item)))
    entry_source=record['samples'][role['entry']];seat_source=record['samples'][role['seat']]
    entry_center=rigid(multiply(center_in_bone,entry_source['poses'][name]))
    seat_center=rigid(multiply(center_in_bone,seat_source['poses'][name]))
    displacement=[seat_center[12+k]-entry_center[12+k]for k in range(3)]
    travel=math.hypot(*displacement);axis=unit(displacement)
    if not .04<travel<.15:raise ValueError('Bounded authored loading displacement required')
    # The nose follows the observed terminal approach (-local Z). Rotation is
    # terminal orientation; the straight center displacement is explicit VR
    # assistance and does not claim the stock curved trajectory or open socket.
    landmark=identity();landmark[0]=landmark[10]=-1.;landmark[14]=-extent[2]/2
    target=rigid(multiply(landmark,seat_center));entry=target.copy()
    for k in range(3):entry[12+k]-=displacement[k]
    direction=[sum(axis[k]*entry[j*4+k]for k in range(3))for j in range(3)]
    if sum(axis[k]*seat_center[8+k] for k in range(3))>=0:raise ValueError('Shell approach disagrees with leading end')
    descendants=[];pending=[name]
    while pending:
        parent=pending.pop()
        for n,p in parts.items():
            if p['parent']==parent:descendants.append(n);pending.append(n)
    nearest={}
    for f in ('Thumb','Index','Middle','Ring','Pinky'):
        nearest[f]=min(math.dist(multiply(poses['LeftHand'+f+str(j)],inverse_rigid(item))[12:15],[0,0,0])for j in (1,2,3))
    if sum(d<.055 for d in nearest.values())<2:raise ValueError('Selected shell anatomy is remote from the item')
    result=dict(bone=name,parent=part['parent'],centerInBone=center_in_bone,bounds=part['bounds_m'],
                descendantPolicy='hide_other_rounds_for_one_shell_token',descendants=descendants,
                handShape=finger_shape(poses),itemFromHand=hand,itemFromInsertion=landmark,
                weaponFromEntry=entry,travelDirection=direction,travelMeters=travel,terminalItem=seat_center,
                source=dict(grasp_frame=role['grasp'],grasp_time=source['time'],entry_frame=role['entry'],
                            entry_time=entry_source['time'],terminal_frame=role['seat'],terminal_time=seat_source['time'],
                            nearest_finger_joint_to_center_m=nearest,stable_contact_windows=part['contact']['windows']),
                policy=dict(kind='explicit_authored_loading_port_and_hand_contact_mod_policy',
                            grasp='One complete evaluated source frame; no stable-window/native-contact claim.',
                            entry='Exact center displacement between two evaluated approach frames, held terminal orientation.',
                            seat='Exact evaluated terminal approach pose selected for this VR loading port.',
                            socket_opening_verified=False,native_transfer_frame_verified=False,ux=UX),**UX)
    # The existing recognizer must reach this exact item pose algebraically.
    final=entry.copy()
    for k in range(3):final[12+k]+=axis[k]*travel
    recovered=multiply(inverse_rigid(landmark),final)
    if max(abs(a-b)for a,b in zip(recovered,seat_center))>1e-7:raise ValueError('Insertion endpoint round-trip')
    return result

def pump_geometry(record,role,shell):
    parts={p['bone']:p for p in record['parts']};name=role['pump_bone'];part=parts[name]
    phase=record.get('pump_phase_samples')
    if not phase or phase['part']!=name:raise ValueError('Exact physical-progress companion samples required')
    rail=phase['rail'];closed=rigid(rail['reference_part']);axis=rail['axis'];stroke=rail['decoded_travel']['maximum_m']
    if part['parent']!='jntWpn_1' or any(p['parent']==name for p in parts.values()) or not part['independent_rigid_geometry']:
        raise ValueError('Independent direct-leaf fore-end required')
    if not .04<stroke<.25 or part['triangles']<900:raise ValueError('Reviewed fore-end role geometry differs')
    pose=record['samples'][role['pump_grasp']]['poses'];relative=multiply(pose['LeftHand'],inverse_rigid(pose[name]))
    wrist=rigid(multiply(relative,closed));fingers=finger_shape(pose)
    if math.dist(wrist[12:15],closed[12:15])>.3:raise ValueError('Pump hand remote')
    companions=[];moving=[]
    for p in parts.values():
        if p['bone']==name or p['motion']['kind']=='static':continue
        if p['parent']!='jntWpn_1' or any(q['parent']==p['bone']for q in parts.values()) or not p['independent_rigid_geometry']:
            raise ValueError('Companion topology unsupported')
        paths={leg:[rigid(s['poses'][p['bone']])for s in phase[leg]] for leg in ('rear','forward')}
        if any(len(path)!=17 for path in paths.values()):raise ValueError('Companion sample count')
        if paths['rear'][0]!=p['static_authored_closed_candidate'] or paths['forward'][0]!=paths['rear'][0] or paths['rear'][-1]!=paths['forward'][-1]:
            raise ValueError('Companion endpoint discontinuity')
        for leg in ('rear','forward'):
            for i,s in enumerate(phase[leg]):
                if s['progress']!=i/16 or s.get('pump_error_m',0)>1e-6:raise ValueError('Companion physical-progress mismatch')
        moving.append(p['bone']);companions.append(dict(name=p['bone'],parent=p['parent'],closed=paths['rear'][0],**paths))
    excluded={'jntWpn_1',name,shell['bone'],*shell['descendants'],*moving}
    fixed=[dict(name=p['bone'],parent=p['parent'],inWeapon=rigid(p['static_authored_closed_candidate']))for p in parts.values() if p['bone']not in excluded]
    return dict(root='jntWpn_1',part=name,axis=axis,stroke=stroke,closedPart=closed,closedWrist=wrist,
                handShape=fingers,companions=companions,fixedParts=fixed,rearDirection=1.,measured=False,authoredReviewed=True,
                policy=dict(kind='explicit_authored_pump_mod_policy',source_frame=role['pump_grasp'],
                            role_basis='Rigid fore-end mesh extent/material topology and same-frame hand anatomy; small independent pieces are companions.',
                            closed_basis='Exact static authored reference; native boundary remains an independent runtime gate.',
                            stop_basis='Exact decoded positive extremum from the static reference; negative animation overshoot is excluded.',
                            companion_basis='17 exact evaluated poses on each first physical-progress crossing; closed endpoint uses static authored reference.',
                            native_boundary_verified=False,negative_overshoot_m=rail['decoded_travel']['minimum_m'],
                            source_rail=rail,phase_source_times={leg:[s['time']for s in phase[leg]]for leg in ('rear','forward')}))

def load_actions(path):
    if sha(path)!=ACTION_HASH:raise ValueError('Pinned action379 identities required')
    return json.loads(Path(path).read_text())

def audio_profile(row,sampled,actions,mesh):
    result=dict(provenance=ACTION_HASH,cue_policy='generic_physical_cues_from_owner_spas_cache',roles={})
    for role,key in (('1p_boltaction','pump'),('1p_reload','insertion'),('1p_reload_out','finish')):
        matches=[p for p in row['mechanical_inputs']if p['role']==role]
        result['roles'][key]=dict(status='unsupported_missing_exact_authored_binding',guid=None)
        if len(matches)!=1:continue
        binding=matches[0]
        if binding['source']['sha256']!=ACTION_HASH:raise ValueError('Audio action source differs')
        index=int(binding['source']['pointer'].removeprefix('/records/'))
        record=sampled.get(index)
        if record:
            source=record['source'];record_digest=record['record_digest'];binding_digest=record['binding_digest']
        elif actions is not None:
            record=actions['records'][index];source=record['source'];record_digest=record['digest'];binding_digest=record['binding_digest']
        else:continue
        if source['role']!=role or record_digest!=binding['mechanism_digest'] or binding_digest!=binding['binding_digest']:
            raise ValueError('Exact audio role identity differs')
        if key=='finish':
            # A valid action/binding pair can still belong to another weapon.
            # Join the unsampled finish to this configuration and reviewed mesh.
            reference=sampled[ROLES[mesh]['pump']]
            tree=source.get('animation_tree',{});configuration=record.get('weapon_configuration',{})
            if (not all(tree.get(k)for k in ('resource','sha256','instance_guid')) or
                not configuration.get('resource') or not record.get('configured_mesh')):continue
            states=[s for s in row.get('authored_states',[])if s['index']==record.get('state_index')]
            if len(states)!=1 or not states[0].get('animation_tree_references',{}).get('AnimTree1p'):continue
            tree_reference=(tree['resource'].removesuffix('.dbx')+'/'+tree['instance_guid']).casefold()
            selected_bindings=[p['binding_digest']for p in row['mechanical_inputs']
                               if p['role']in ('1p_boltaction','1p_reload')]
            if (record.get('asset')!=row['asset'] or configuration['resource']!=row['resource'] or
                record['configured_mesh']!=mesh or record.get('rig_fingerprint')!=reference['rig_fingerprint'] or
                any(binding_digest!=value for value in selected_bindings) or
                tree!=reference['source'].get('animation_tree') or
                tree_reference!=states[0]['animation_tree_references']['AnimTree1p'].casefold() or
                any(source.get(k)!=reference['source'][k]for k in ('mesh_sha256','lod_sha256','reference_clip_sha256'))):
                raise ValueError('Exact finish audio equipment identity differs')
        guid=str(uuid.UUID(source['asset_guid']))
        result['roles'][key]=dict(status='exact_authored_animation',guid=guid,record=index,
            asset_document=source['asset_document'],asset_document_sha256=source['asset_document_sha256'])
    return result

def build(batch,samples,actions=None):
    if batch.get('schema')!='fvr.bc2.shotgun_reload_batch.v1' or batch['inputs']['actions']['sha256']!=ACTION_HASH:
        raise ValueError('Pinned shotgun43 batch required')
    sampled=checked_samples(samples);groups={};content_groups={}
    for mesh,role in ROLES.items():
        pump=sampled[role['pump']];shell=sampled[role['shell']]
        if pump['mesh']!=mesh or shell['mesh']!=mesh or pump['rig_fingerprint']!=shell['rig_fingerprint']:raise ValueError('Exact mesh/rig join differs')
        sg=shell_geometry(shell,role);pg=pump_geometry(pump,role,sg)
        value=dict(mesh=mesh,rig_fingerprint=pump['rig_fingerprint'],shell=sg,pump=pg,
                   sources=dict(pump_record=role['pump'],shell_record=role['shell'],pump_digest=pump['record_digest'],
                                shell_digest=shell['record_digest'],pump_sample_digest=pump['digest'],shell_sample_digest=shell['digest']))
        value['geometry_digest']=digest(value);groups[mesh]=value
        content_groups[(content(pump),content(shell))]=mesh
    descriptors=batch['tube_descriptors'];inventory=batch['inventory'];rows=[]
    if len(descriptors)!=25 or len({r['key']for r in descriptors})!=25:raise ValueError('Exactly 25 distinct descriptors required')
    for index,d in enumerate(descriptors):
        selected=[r for r in inventory if r['key']==d['key']]
        if len(selected)!=1 or selected[0]['route']!='SingleBulletPump':raise ValueError('Exact tube inventory missing')
        row=selected[0];binding={}
        for role in ('1p_boltaction','1p_reload'):
            found=[p for p in row['mechanical_inputs']if p['role']==role]
            if len(found)!=1:raise ValueError('Exact unique action binding required')
            found=found[0];g=batch['geometry_groups'][found['group']]
            if g['gaps'] or g['identity']['role']!=role:raise ValueError('Action group incomplete')
            binding[role]=(found,g)
        key=tuple(group_content(binding[r][1])for r in ('1p_boltaction','1p_reload'))
        if key not in content_groups:raise ValueError('Unreviewed independent content group')
        mesh=content_groups[key];geometry=groups[mesh]
        if any(g['rig_fingerprint']!=geometry['rig_fingerprint']for _,g in binding.values()):raise ValueError('Rig fingerprint differs')
        if row['configuration']!=d['configuration']:raise ValueError('Configuration identity differs')
        value=dict(native_index=index,key=d['key'],asset=d['identity']['assetName'],asset_path=d['identity']['assetPath'],
                   descriptor_digest=d['descriptor_digest'],geometry_digest=geometry['geometry_digest'],mesh=mesh,
                   rig_fingerprint=geometry['rig_fingerprint'],physicalReviewed=True,
                   native_admission_changed=False,bindings={r:binding[r][0]for r in binding})
        value['profile_id']=int(digest(value)[:16],16);rows.append(value)
        # Audio is a presentation join, never part of physical/native identity.
        value['audio']=audio_profile(row,sampled,actions,mesh)
    for source in sampled.values():
        bindings=[p for r in inventory for p in r['mechanical_inputs']
                  if p['source']['pointer']=='/records/'+str(source['record_index'])]
        if not bindings or any(p['mechanism_digest']!=source['record_digest'] or p['binding_digest']!=source['binding_digest']for p in bindings):
            raise ValueError('Sample action identity differs from exact inventory binding')
    return dict(schema='fvr.bc2.shotgun_equipment.v1',actions_sha256=ACTION_HASH,
                review_level='source_geometry_and_explicit_mod_policy',native_admission_changed=False,
                actual_game_checked=False,headset_checked=False,groups=list(groups.values()),profiles=rows,
                summary=dict(completed_configurations=len(rows),independent_equipment_geometry=len(groups),
                             exact_sampled_actions=8,shell_contact_windows_required=False))

def number(v):
    bits=struct.unpack('<I',struct.pack('<f',v))[0]
    return f'std::bit_cast<float>(0x{bits:08x}u)'
def matrix(m):
    return 'math::Matrix4{{{'+','.join('{'+','.join(number(v)for v in m[i:i+4])+'}'for i in range(0,16,4))+'}}}'
def string(v):return json.dumps(v)
def audio_guid(value):
    return '{'+','.join(f'0x{x:02x}'for x in uuid.UUID(value).bytes_le)+'}' if value else '{}'
def hand(name,shape):
    lines=[f'inline const interaction::AuthoredHandShape {name}= []{{interaction::AuthoredHandShape out{{}};out.counts={{3,3,3,3,3}};']
    for i,n in enumerate(FINGERS):lines.append(f'out.wristFromJoint[{i//3}][{i%3}]={matrix(shape[n])};')
    return '\n'.join([*lines,'return out;}();'])

def header(result):
    lines=['#pragma once','// Generated source geometry + explicit VR policy. No native qualification.',
           '#include "Bc2ShotgunReloadRegistry.h"','#include "Bc2PhysicalPump.h"','#include "Bc2ShotgunAudioProfile.h"','#include <bit>',
           'namespace fvr::bc2::generated {',
           'struct ShotgunShellGeometry {',
           ' const ShotgunReloadEquipmentProfile* equipment=nullptr;',
           ' std::string_view bone,parent; math::Matrix4 centerInBone{};',
           ' const interaction::AuthoredHandShape* handShape=nullptr;',
           ' std::span<const std::string_view> hiddenDescendants{};',
           '};']
    group_index={g['mesh']:i for i,g in enumerate(result['groups'])}
    for i,g in enumerate(result['groups']):
        p=g['pump'];s=g['shell'];pre=f'ShotgunGeometry{i}'
        lines.extend([hand(pre+'PumpHand',p['handShape']),hand(pre+'ShellHand',s['handShape'])])
        fixed=','.join('{'+string(x['name'])+','+string(x['parent'])+','+matrix(x['inWeapon'])+'}' for x in p['fixedParts'])
        lines.append(f'inline const std::array<Bc2PumpFixedPart,{len(p["fixedParts"])}> {pre}Fixed{{{{{fixed}}}}};')
        for j,c in enumerate(p['companions']):
            for leg in ('rear','forward'):
                values=','.join(matrix(m)for m in c[leg])
                lines.append(f'inline const std::array<math::Matrix4,17> {pre}Companion{j}{leg}{{{{{values}}}}};')
        companions=','.join('{'+string(c['name'])+','+string(c['parent'])+','+matrix(c['closed'])+f',{pre}Companion{j}rear,{pre}Companion{j}forward'+'}'for j,c in enumerate(p['companions']))
        lines.append(f'inline const std::array<Bc2PumpCompanionPart,{len(p["companions"])}> {pre}Companions{{{{{companions}}}}};')
        descendants=','.join(map(string,s['descendants']))
        lines.append(f'inline constexpr std::array<std::string_view,{len(s["descendants"])}> {pre}Hidden{{{{{descendants}}}}};')
    for i,row in enumerate(result['profiles']):
        g=result['groups'][group_index[row['mesh']]];p=g['pump'];s=g['shell'];pre=f'ShotgunGeometry{group_index[row["mesh"]]}'
        lines.append(f'static_assert(ShotgunReloadRegistrations[{i}].descriptorDigest=={string(row["descriptor_digest"])});')
        lines.append(f'inline const interaction::ReloadInsertionProfile ShotgunInsertion{i}= []{{interaction::ReloadInsertionProfile out{{}};')
        lines.extend([f'out.id={row["profile_id"]}ull;out.revision=1;',
                      'out.family=interaction::ReloadInsertionFamily::SingleShell;out.orientation=interaction::ReloadInsertionOrientation::AxialSymmetry;',
                      'out.approach=interaction::ReloadInsertionApproach::RailContact;'])
        for key in ('itemFromHand','itemFromInsertion','weaponFromEntry'):lines.append(f'out.{key}={matrix(s[key])};')
        lines.append('out.travelDirection={'+','.join(number(v)for v in s['travelDirection'])+'};')
        for key in ('travelMeters',*UX):lines.append(f'out.{key}='+ (str(s[key])+'ll' if key.endswith('Ns') else number(s[key]))+';')
        lines.extend(['return out;}();',f'inline const Bc2PumpCalibration ShotgunPump{i}= []{{Bc2PumpCalibration out{{}};',
                      f'out.revision=1;out.rigFingerprint=0x{row["rig_fingerprint"].split(":")[1]}ull;out.profileId={row["profile_id"]}ull;',
                      f'out.asset={string(row["asset"])};out.measured=false;out.authoredReviewed=true;out.rearDirection=1.f;',
                      f'out.closedPart={matrix(p["closedPart"])};out.closedWrist={matrix(p["closedWrist"])};',
                      f'out.handShape=&{pre}PumpHand;out.fixedParts={pre}Fixed;out.companions={pre}Companions;',
                      'out.geometry={'+string(p['root'])+','+string(p['part'])+',{'+','.join(number(v)for v in p['axis'])+'},'+number(p['stroke'])+'};',
                      'return out;}();'])
    lines.append('inline const std::array<ShotgunReloadEquipmentProfile,25> ShotgunEquipmentProfiles{{')
    for i,r in enumerate(result['profiles']):
        lines.append('{&ShotgunReloadRegistrations['+str(i)+'],'+string(r['mesh'])+','+string(r['geometry_digest'])+',0x'+r['rig_fingerprint'].split(':')[1]+f'ull,&ShotgunInsertion{i},&ShotgunPump{i},true'+'},')
    lines.extend(['}};','inline const std::array<ShotgunShellGeometry,25> ShotgunShellGeometries{{'])
    for i,r in enumerate(result['profiles']):
        gi=group_index[r['mesh']];s=result['groups'][gi]['shell'];pre=f'ShotgunGeometry{gi}'
        lines.append('{&ShotgunEquipmentProfiles['+str(i)+'],'+string(s['bone'])+','+string(s['parent'])+','+matrix(s['centerInBone'])+f',&{pre}ShellHand,{pre}Hidden'+'},')
    lines.extend(['}};',
        'inline const ShotgunShellGeometry* FindShotgunShellGeometry(const ShotgunReloadEquipmentProfile* equipment)noexcept {',
        ' for(const auto& shell:ShotgunShellGeometries)if(shell.equipment==equipment)return &shell;return nullptr;',
        '}'])
    lines.append('inline constexpr std::array<ShotgunAudioProfile,25> ShotgunAudioProfiles{{')
    for row in result['profiles']:
        audio=row['audio'];roles=audio['roles']
        lines.append('{'+','.join(audio_guid(roles[role]['guid'])for role in ('pump','insertion','finish'))+','+string(audio['provenance'])+'},')
    lines.extend(['}};',
        'inline const ShotgunAudioProfile* FindShotgunAudioProfile(const ShotgunReloadEquipmentProfile* equipment)noexcept {',
        ' for(std::size_t i=0;i<ShotgunEquipmentProfiles.size();++i)if(equipment==&ShotgunEquipmentProfiles[i])return &ShotgunAudioProfiles[i];return nullptr;',
        '}', '} // namespace fvr::bc2::generated',''])
    return '\n'.join(lines)

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('batch','samples','output','header'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--actions',type=Path)
    args=parser.parse_args(argv);batch=json.loads(args.batch.read_text())
    actions_path=args.actions or Path(batch['inputs']['actions']['path'])
    result=build(batch,json.loads(args.samples.read_text()),load_actions(actions_path))
    result['inputs']={k:dict(path=str(v),sha256=sha(v)) for k,v in (('batch',args.batch),('samples',args.samples))}
    result['inputs']['actions']=dict(path=str(actions_path),sha256=ACTION_HASH)
    data=header(result);result['header_sha256']=hashlib.sha256(data.encode()).hexdigest()
    for path,text in ((args.output,json.dumps(result,indent=2,allow_nan=False)+'\n'),(args.header,data)):
        path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text,encoding='utf-8')
    print(json.dumps(result['summary']))

if __name__=='__main__':main()
