"""Explicit semantic role + exact authored frame review; no native authority.

The review selects among already measured geometric roles. The complete stock
frame is sampled again from the original clip, while the existing rigid contact
window remains mandatory. Temporal finger motion is recorded separately.
"""
import math
from bc2_authored_grip_bindings import digest,checked_hash

def validate_review(review):
    if (review.get('schema')!='fvr.bc2.reviewed-authored-magazine-contact.v1' or
        review.get('review_digest')!=digest({k:v for k,v in review.items() if k!='review_digest'}) or
        review.get('semantic_role')!='detachable_magazine' or review.get('contact')!='complete_same_authored_frame' or
        review.get('native_role_verified') is not False or review.get('headset_tested') is not False):
        raise ValueError('Explicit authored contact review required')
    for name in ('binding_digest','mesh_sha256','lod_sha256','skeleton_sha256','clip_sha256','render_receipt_sha256','contact_sheet_sha256','pose_digest'):
        checked_hash(review.get(name))
    checked_hash(review.get('weapon',{}).get('sha256'))
    for name in ('native_asset_name','magazine_bone','rig_fingerprint','clip_resource'):
        if not isinstance(review.get(name),str) or not review[name] or '\0' in review[name]:raise ValueError('Contact review identity')
    time=review.get('frame_seconds')
    if isinstance(time,bool) or not isinstance(time,(int,float)) or not math.isfinite(time) or time<0 or abs(time*60-round(time*60))>1e-8:
        raise ValueError('Contact review must select one exact sampled stock frame')
    return review

def index_reviews(reviews,assets):
    if len(reviews)>128:raise ValueError('Contact review count bound')
    result={}
    for review in reviews:
        validate_review(review);key=review['weapon']['resource']
        if key in result or review['native_asset_name'] not in assets:raise ValueError('Duplicate or unselected contact review')
        result[key]=review
    return result

def verify_context(review,profile,clip,skeleton,clip_resource,mesh_sha,lod_sha):
    validate_review(review)
    expected=dict(native_asset_name=profile['native_asset_name'],weapon=profile['weapon'],binding_digest=profile['binding_digest'],
        mesh_sha256=mesh_sha,lod_sha256=lod_sha,skeleton_sha256=skeleton.sha256,rig_fingerprint=skeleton.fingerprint,
        clip_resource=clip_resource,clip_sha256=clip.sha256)
    if any(review[k]!=v for k,v in expected.items()):raise ValueError('Contact review exact authored identity differs')

def select_reviewed_pair(review,candidates,parts,closed,clip,skeleton):
    import bc2_authored_magazine_geometry as g
    validate_review(review);bone=review['magazine_bone'];matched=[c for c in candidates if c['bone']==bone]
    if len(matched)!=1:raise ValueError('Reviewed root did not pass shared geometric role checks')
    diagnostics={}
    try:g.paired_reload_grasp(clip,skeleton,bone,closed['weapon_relative'][bone],parts[bone],diagnostics)
    except ValueError as error:
        if str(error)!='No stable authored magazine contact interval':raise
    time=review['frame_seconds'];windows=[w for w in diagnostics['windows'] if abs((w['begin_seconds']+w['end_seconds'])/2-time)<1e-8 and set(w['failed'])<={'finger_angle_rad'}]
    if len(windows)!=1:raise ValueError('Reviewed frame lacks unchanged rigid wrist/item contact window')
    window=windows[0]
    # Removed magazine proof already available here. Short co-motion contacts
    # require the separate entry witness path and are deliberately not inferred.
    if window['minimum_item_motion_m']<=g.PAIRED_GRASP['min_item_motion_m']:raise ValueError('Reviewed frame has no removed-item witness')
    pose=clip.evaluate(skeleton,time,['LeftHand',bone,*g.FINGERS])['weapon_relative'];wrist=g.matrix(pose['LeftHand']);item=g.matrix(pose[bone])
    pair=dict(item_from_hand=g.multiply(wrist,g.inverse_rigid(item)),wrist_from_fingers={n:g.multiply(g.matrix(pose[n]),g.inverse_rigid(wrist)) for n in g.FINGERS})
    if digest(pair)!=review['pose_digest']:raise ValueError('Reviewed complete authored pose changed')
    metrics=window['metrics'];evidence=dict(begin_seconds=window['begin_seconds'],end_seconds=window['end_seconds'],time_seconds=time,
        max_wrist_translation_m=metrics['wrist_translation_m'],max_wrist_angle_rad=metrics['wrist_angle_rad'],
        max_finger_angle_rad=metrics['finger_angle_rad'],max_knuckle_distance_to_bounds_m=metrics['contact_distance_m'],minimum_item_motion_m=window['minimum_item_motion_m'])
    pair['receipt']=dict(source='same_frame_authored_reload_contact_candidate',selection_mode='reviewed_authored_frame',
        clip_sha256=clip.sha256,skeleton_sha256=skeleton.sha256,rig_fingerprint=skeleton.fingerprint,magazine_bone=bone,
        sample_count=diagnostics['sample_count'],eligible_windows=1,window=evidence,criteria=dict(g.PAIRED_GRASP),
        temporal_finger_motion_is_pose_selection_not_rigid_contact=True,semantic_contact_review=review,
        runtime_interpolation_verified=False,active_native_animation_verified=False,grasp_digest=digest(pair))
    role=dict(matched[0],ambiguity_resolution='exact_reviewed_semantic_magazine_root',geometric_candidate_count=len(candidates),review_digest=review['review_digest'])
    rejected=[dict(c,reasons=['other measured role preserved; exact semantic annotation selects another root']) for c in candidates if c['bone']!=bone]
    return role,pair,rejected

def validate_export(profile,paired):
    import bc2_authored_magazine_geometry as g
    review=validate_review(paired.get('semantic_contact_review',{}));window=paired.get('window',{})
    expected=dict(native_asset_name=profile['native_asset_name'],weapon=profile['weapon'],binding_digest=profile['grip_binding_digest'],
        mesh_sha256=profile['mesh_sha256'],lod_sha256=profile['lod_sha256'],skeleton_sha256=profile['skeleton_sha256'],rig_fingerprint=profile['rig_fingerprint'],
        clip_resource=profile['reload_clip']['resource'],clip_sha256=profile['reload_clip']['sha256'],magazine_bone=profile['bones']['magazine'],pose_digest=paired.get('grasp_digest'),frame_seconds=window.get('time_seconds'))
    if any(review[k]!=v for k,v in expected.items()) or profile['role_evidence'].get('review_digest')!=review['review_digest']:
        raise ValueError('Exported contact does not match reviewed authored pose')
    if paired.get('criteria')!=g.PAIRED_GRASP or paired.get('temporal_finger_motion_is_pose_selection_not_rigid_contact') is not True:
        raise ValueError('Reviewed contact criteria changed')
    checks={'max_wrist_translation_m':'max_wrist_translation_m','max_wrist_angle_rad':'max_wrist_angle_rad','max_knuckle_distance_to_bounds_m':'max_contact_distance_m'}
    if any(not math.isfinite(window.get(key,float('nan'))) or not 0<=window[key]<g.PAIRED_GRASP[limit] for key,limit in checks.items()):
        raise ValueError('Reviewed rigid contact bounds changed')
    if (window.get('minimum_item_motion_m',0)<=g.PAIRED_GRASP['min_item_motion_m'] or
        abs(window.get('end_seconds',0)-window.get('begin_seconds',0)-g.PAIRED_GRASP['window_seconds'])>1e-8 or
        abs((window['end_seconds']+window['begin_seconds'])/2-review['frame_seconds'])>1e-8):
        raise ValueError('Reviewed contact window changed')

def validate_contact_candidate(candidate):
    """Validate modular contact data without supplying missing insertion data."""
    import bc2_authored_magazine_geometry as g
    if (candidate.get('schema')!='fvr.bc2.reviewed-contact-candidate.v1' or
        candidate.get('candidate_digest')!=digest({k:v for k,v in candidate.items() if k!='candidate_digest'}) or
        candidate.get('runtime_admitted') is not False or candidate.get('native_role_verified') is not False or
        candidate.get('rail_status') not in ('unresolved','separately_derived_experimental_geometry')):
        raise ValueError('Invalid modular reviewed contact candidate')
    pair=candidate['pair'];receipt=pair['receipt'];pose={k:pair[k] for k in ('item_from_hand','wrist_from_fingers')}
    if receipt.get('selection_mode')!='reviewed_authored_frame' or receipt.get('grasp_digest')!=digest(pose) or set(pose['wrist_from_fingers'])!=set(g.FINGERS):
        raise ValueError('Incomplete same-frame authored contact pose')
    for matrix in [candidate['closed_item'],pose['item_from_hand'],*pose['wrist_from_fingers'].values()]:g.inverse_rigid(g.matrix(matrix))
    if candidate['bones']['fingers']!=g.FINGERS or receipt.get('runtime_interpolation_verified') is not False or receipt.get('active_native_animation_verified') is not False:
        raise ValueError('Contact candidate may not claim native pose acceptance')
    validate_export(candidate,receipt)
    if 'assembly' in candidate:
        from bc2_magazine_assembly import validate_receipt
        validate_receipt(candidate['assembly'],candidate)
    return candidate
