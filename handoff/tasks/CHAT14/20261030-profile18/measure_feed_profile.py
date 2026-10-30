"""Bounded file-only measurements of the already pinned M60 reload and mesh."""
from pathlib import Path
import json, math, struct, sys
sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE.parent / '20261030-implementation15'))
import measure_geometry as m

NAMES = ('jntWpn_6', 'jntWpn_12', 'jntWpn_13', 'jntWpnwpnJnt_14', 'jntWpn_15', 'jntWpn_9')
POSES = (('Resting', 0.), ('PouchDetachSource', 1.8), ('PouchWithdrawn', 2.1),
         ('PouchAttachSource', 3.), ('PouchAttachedFeedUnseated', 3.6),
         ('FeedSeatSource', 118/30), ('FeedSeated', 4.5))

def load():
    evidence = json.loads((m.HERE / 'geometry-evidence.json').read_text(encoding='utf-8'))
    a, b = m.Archive(m.GAME / m.PROOF['archive']), m.Archive(m.GAME / m.PROOF['authored_reload_animation']['archive'])
    assert a.index_sha256 == m.PROOF['archive_index_sha256']
    assert b.index_sha256 == m.PROOF['authored_reload_animation']['archive_index_sha256']
    sr = a.read_selected([m.SKEL])[m.SKEL]
    raw = b.read_selected([m.MESH, m.LOD, m.CLIP])
    assert m.sha(sr) == evidence['resources'][m.SKEL]
    assert all(m.sha(v) == evidence['resources'][k] for k, v in raw.items())
    sk, clip = m.Skeleton(sr), m.Clip(raw[m.CLIP])
    assert sk.fingerprint == evidence['rig_fingerprint'] and not clip.unsupported
    lod, = m.metadata(raw[m.MESH])
    parts = m.mag.rigid_parts(raw[m.LOD], lod, sk, strict_assembly_skin=True)
    assert len(parts) == 14
    return evidence, sk, clip, parts

def main():
    evidence, sk, clip, parts = load()
    names = sorted(set(NAMES) | {'LeftHand', *m.mag.FINGERS})
    rows = []
    for i in range(0, 151):
        time = i / 30
        rel = clip.evaluate(sk, time, names)['weapon_relative']
        contacts = {}
        for bone in NAMES[:5]:
            inv = m.inverse_rigid(rel[bone])
            fingers = {n:m.mag.point(rel[n][12:15], inv) for n in m.mag.FINGERS}
            nearest = min((math.dist(p, q), n, p, q) for n,p in fingers.items() for q in parts[bone]['points'])
            contacts[bone] = dict(origin=rel[bone][12:15], nearest_joint=nearest[1], joint_mesh_gap=nearest[0])
        rows.append(dict(time_s=time, parts=contacts))
    scratch = ROOT/'reports/coordinator-20261030-implementation18/feed-profile'
    scratch.mkdir(parents=True, exist_ok=True)
    (scratch/'sample-summary.json').write_text(json.dumps(m.vround(rows), indent=2)+'\n',encoding='utf-8')
    poses = []
    for label, time in POSES:
        rel = clip.evaluate(sk, time, names)['weapon_relative']
        locals = [m.multiply(rel[child], m.inverse_rigid(rel[parent]))
                  for parent,child in zip(NAMES[1:4],NAMES[2:5])]
        # Retain all four chain segments; composition must recover the exact clip.
        chain = [rel[NAMES[1]]]
        for local in locals: chain.append(m.multiply(local,chain[-1]))
        residual = max(abs(x-y) for bone,world in zip(NAMES[1:5],chain)
                       for x,y in zip(rel[bone],world))
        assert residual < 1e-10
        contact_bone = 'jntWpn_15' if label == 'FeedSeatSource' else 'jntWpn_6'
        poses.append(dict(label=label,time_s=time,weapon_from_pouch=rel[NAMES[0]],
            weapon_from_feed_root=rel[NAMES[1]],parent_from_chain_child=locals,
            source_from_wrist=m.multiply(rel['LeftHand'],m.inverse_rigid(rel[contact_bone])),
            contact_object_bone=contact_bone,weapon_from_feed_terminal=rel['jntWpn_15'],
            weapon_from_wrist=rel['LeftHand'],contact_frame='LeftHand wrist; fingers are proximity witnesses only',
            nearest_joint_measurement=rows[round(time*30)]['parts'],composition_max_error=residual))
    cover = next(p for p in evidence['parts'] if p['bone']=='jntWpn_9')
    grip = cover['stable_left_contact']['part_from_wrist']
    closed = clip.evaluate(sk,0,['jntWpn_9'])['weapon_relative']['jntWpn_9']
    opened = clip.evaluate(sk,1.5,['jntWpn_9'])['weapon_relative']['jntWpn_9']
    hinge = [0,0,1,0,0,1,0,0,-1,0,0,0,*closed[12:15],1]
    q0 = clip.tracks['jntWpn_9'][1].evaluate(0)
    q1 = clip.tracks['jntWpn_9'][1].evaluate(1.5)
    travel = 2*math.atan2(q1[0],q1[3])-2*math.atan2(q0[0],q0[3])
    opening_contact = m.multiply(m.multiply(grip,closed),m.inverse_rigid(hinge))
    closing_contact = m.multiply(m.multiply(grip,opened),m.inverse_rigid(hinge))
    def cpp_matrix(values):
        def scalar(x):
            value = format(x,'.10g')
            return value + ('' if '.' in value or 'e' in value else '.') + 'f'
        return 'MeasuredFrame({'+','.join(scalar(x) for x in values)+'})'
    literal = []
    for pose in poses:
        literal.append('    { // '+pose['label']+'; authored '+format(pose['time_s'],'.10g')+' seconds\n'+
            '      '+format(pose['time_s'],'.12g')+', '+cpp_matrix(pose['weapon_from_pouch'])+',\n'+
            '      '+cpp_matrix(pose['weapon_from_feed_root'])+',\n      {{'+
            ',\n        '.join(cpp_matrix(v) for v in pose['parent_from_chain_child'])+'}},\n'+
            '      '+cpp_matrix(pose['source_from_wrist'])+'}')
    literal_text = 'inline const std::array<Bc2M60FeedPose,7> M60MeasuredPoses{{\n'+',\n'.join(literal)+'\n}};\n'
    literal_text += 'inline const math::Matrix4 M60HingeFrame = '+cpp_matrix(hinge)+';\n'
    literal_text += 'inline const math::Matrix4 M60OpeningContact = '+cpp_matrix(opening_contact)+';\n'
    literal_text += 'inline const math::Matrix4 M60ClosingContact = '+cpp_matrix(closing_contact)+';\n'
    literal_text += 'inline constexpr float M60CoverTravel = '+format(travel,'.10g')+'f;\n'
    (scratch/'measured-frames.inc').write_text(literal_text,encoding='utf-8')
    by_label={p['label']:p for p in poses}
    operations={}
    for operation,source,destination,part,grip_label in (
        ('DetachContainer','PouchDetachSource','PouchWithdrawn','pouch','PouchDetachSource'),
        ('AttachContainer','PouchWithdrawn','PouchAttachedFeedUnseated','pouch','PouchWithdrawn'),
        ('SeatFeed','PouchAttachedFeedUnseated','FeedSeated','feed_terminal','FeedSeatSource')):
        src,dst=by_label[source]['weapon_from_'+part],by_label[destination]['weapon_from_'+part]
        operations[operation]=dict(source_pose=source,destination_pose=destination,object_bone='jntWpn_15' if part=='feed_terminal' else 'jntWpn_6',
            weapon_from_source=src,weapon_from_destination=dst,source_from_wrist=by_label[grip_label]['source_from_wrist'],
            grip_reference_pose=grip_label,source_destination_distance_m=math.dist(src[12:15],dst[12:15]),
            grip_reference_transport=grip_label!=source)
    # Numeric source and measured evidence must agree; no C++ execution here.
    header=ROOT/'src/games/bc2/Bc2FeedEquipmentProfile.h'
    if header.exists():
        numeric=header.read_text(encoding='utf-8').split('// BEGIN MEASURED FRAMES\n')[1].split('// END MEASURED FRAMES')[0]
        assert numeric.strip()==literal_text.strip()
    matrices=[v for p in poses for v in [p['weapon_from_pouch'],p['weapon_from_feed_root'],
        p['weapon_from_feed_terminal'],p['source_from_wrist'],*p['parent_from_chain_child']]]
    matrices += [hinge,opening_contact,closing_contact]
    float32_error=0.
    for matrix in matrices:
        q=list(struct.unpack('<16f',struct.pack('<16f',*matrix)))
        error=max(abs(sum(q[i*4+k]*q[j*4+k] for k in range(3))-(i==j)) for i in range(3) for j in range(3))
        assert error<1e-4 and all(math.isfinite(v) for v in q)
        float32_error=max(float32_error,error)
    result = dict(schema='fvr.bc2.m60_feed_profile_frames.v1',
        evidence_level='offline_pinned_mesh_and_authored_clip_measurement',
        selector=evidence['selector'],binding_digest=evidence['binding_digest'],
        resources=evidence['resources'],rig_fingerprint=evidence['rig_fingerprint'],
        prior_geometry_sha256=m.sha((m.HERE/'geometry-evidence.json').read_bytes()),
        role_review_sha256=m.sha((m.HERE/'role-review.json').read_bytes()),
        reader_sha256=m.sha(Path(__file__).read_bytes()),coordinate_convention=evidence['coordinate_convention'],
        bone_chain=[dict(bone=n,index=sk.names.index(n),parent=sk.names[sk.parents[sk.names.index(n)]]) for n in NAMES[:5]],
        poses=poses,operation_geometry=operations,measurement_checks=dict(float32_rotation_max_orthogonality_error=float32_error,
            parent_world_recomposition_max_error=max(p['composition_max_error'] for p in poses),generated_numeric_source_matches=True),
        cover=dict(weapon_from_motion=hinge,motion_from_opening_contact=opening_contact,
            motion_from_closing_contact=closing_contact,travel_rad=travel,grip_source_s=5/6,
            grip_transport='Same measured panel-local wrist frame transported to t0/t1.5; mod interaction reference, not native grip constraint.'),
        measured_operations=['OpenCover','CloseCover','DetachContainer','AttachContainer','SeatFeed'],
        policy=dict(tolerances='Caller-supplied; no numerical acceptance threshold is claimed authored.',
            selected_pose_endpoints='Explicit mod interaction selections among measured authored poses, not native stage markers.',
            caller_target='Root-authorized same-container replenishment: open cover; coupled pouch/feed detach; same-pouch reattach/refill; terminal belt seating; close cover. Separate replacement mode remains unavailable.',
            continuity='Attach source is prior detach destination t2.1, not authored approach t3.0. Seat source is retained t3.6 terminal with t3.9333 terminal-local wrist reference. Builder overload accepts actual retained source to avoid tolerance-endpoint snaps.',
            native_authority='None; no ammunition/chamber/fire readiness or native cover boolean inferred.',
            resource_identity='Supplied by immutable equipment custody; animation bone reuse cannot distinguish old/new resource instances.'),
        unavailable=dict(WithdrawFeed='No separately measured feed-withdrawal source/destination with pouch remaining attached. The observed outward interval carries pouch and chain together; do not reverse SeatFeed or call this absence.',
            ReleaseLatch='No measured independent latch contact/frame.',EngageLatch='No measured independent latch contact/frame.',
            CycleAction='Slider4 geometry exists, but charge/chamber semantics are unassigned.',
            replacement_stage_order='Independent replacement mode requests separate WithdrawFeed before replacing a seated old resource. This clip does not establish that prerequisite; it does not block the separately chosen coupled same-container policy.'),
        presentation_contract='SeatFeed carries terminal15, retaining the actual pickup offset. Given explicit current parent-local articulation, reconstruct15->14->13->12 backwards from that terminal; preserve separate pouch and exact supplied resource/container identity. No rigid belt substitute or automatic interpolation.',
        evidence_limits=['No runtime interpolation, native admission, C++ build/test execution or headset acceptance.',
            'Source grip is an authored wrist frame with measured finger proximity; no sustained scripted grip is required by free placement.',
            'No new archive metadata/asset survey or compare clip. Only the four previously pinned resources were read.'])
    result['implementation_sha256']={str(p.relative_to(ROOT)).replace('\\','/'):m.sha(p.read_bytes()) for p in
        (header,ROOT/'tests/Bc2FeedEquipmentProfileTests.cpp') if p.exists()}
    output=HERE/'proof01.json'
    output.write_text(json.dumps(m.vround(result),indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(dict(passed=True,output=str(output),bytes=output.stat().st_size,
        poses=len(poses),measured_operations=result['measured_operations'],unavailable=result['unavailable'],
        cover_travel_rad=travel)))

if __name__ == '__main__': main()
