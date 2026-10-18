"""Extract explicit support references; never promote a varying gun hand.

The default path requires constant controls. The opt-in bounded path recomputes
full-curve envelopes from exact clip/skeleton objects and labels them separately.
"""
import argparse
import hashlib
import json
from pathlib import Path
from bc2_authored_grip_bindings import load, digest, matrix, checked_hash, path_key
from bc2_authored_pose_bounds import relative_bounds, within_tolerance


def derive(bindings, assets, *, enable_contact=False, primary_hand_assets=frozenset()):
    return _derive(bindings,assets,enable_contact=enable_contact,primary_hand_assets=primary_hand_assets)


def derive_bounded(bindings, assets, resources, *, translation_m, rotation_rad,
                   enable_contact=False, primary_hand_assets=frozenset()):
    """Opt-in authoring from exact decoded resources, not a supplied pass flag.

    resources maps (animation SHA256, skeleton SHA256) to (Clip, Skeleton).
    Existing static references keep their original output. No runtime registry,
    right-hand pose or native mechanism is enabled by generating this document.
    """
    bounds={}
    for row in bindings.get('profiles',[]):
        if row.get('native_asset_name') not in assets or row.get('left_hand_status')=='static_authored_pose':
            continue
        if row.get('left_hand_status')!='decoded_spline_candidate':
            raise ValueError('Bounded support requires a decoded authored reference')
        key=(checked_hash(row['animation_sha256']),checked_hash(row['skeleton_sha256']))
        if key not in resources:
            raise ValueError('Exact decoded support resources absent')
        clip,skeleton=resources[key]
        if (clip.sha256,skeleton.sha256)!=key or skeleton.fingerprint!=row['rig_fingerprint']:
            raise ValueError('Decoded support resource identity mismatch')
        bound=relative_bounds(clip,skeleton,'LeftHand')
        pose=matrix(row['left_hand_in_weapon'])
        if max(abs(x-y) for x,y in zip(pose,bound['reference_pose']))>1e-8:
            raise ValueError('Bounded reference does not reproduce source support pose')
        if not within_tolerance(bound,translation_m,rotation_rad):
            raise ValueError('Full-curve support envelope exceeds explicit tolerance')
        bound.update(animation_sha256=key[0],skeleton_sha256=key[1],rig_fingerprint=skeleton.fingerprint,
                     source_binding_digest=row['binding_digest'],translation_tolerance_m=translation_m,
                     rotation_tolerance_rad=rotation_rad)
        bounds[row['binding_digest']]=bound
    return _derive(bindings,assets,enable_contact=enable_contact,
                   primary_hand_assets=primary_hand_assets,bounded_references=bounds)


def _derive(bindings, assets, *, enable_contact=False, primary_hand_assets=frozenset(), bounded_references=None):
    if bindings.get('schema') != 'fvr.bc2.authored_reload_reference_bindings' or bindings.get('runtime_admission') is not False:
        raise ValueError('Exact non-admitted authored reference binding required')
    if primary_hand_assets and (not enable_contact or not set(primary_hand_assets) <= set(assets)):
        raise ValueError('Primary-hand aim requires explicit enrolled contact assets')
    groups = {}
    for row in bindings['profiles']:
        if row['native_asset_name'] not in assets:
            continue
        if digest({k:v for k,v in row.items() if k != 'binding_digest'}) != row['binding_digest']:
            raise ValueError('Authored reference digest changed')
        bounded=(bounded_references or {}).get(row['binding_digest'])
        if row['state_count'] != 1 or row['state_index'] != 0 or (row['left_hand_status'] != 'static_authored_pose' and bounded is None):
            raise ValueError('Support requires constant left-hand/weapon ancestor chains and sole state')
        if enable_contact and row.get('weapon_class') not in ('wcAssault', 'wcSmg', 'wcLmg', 'wcSniper', 'wcShotgun'):
            raise ValueError('Support contact enrollment requires an exact reviewed handheld firearm configuration')
        matrix(row['left_hand_in_weapon'])
        path_key(row['configured_mesh_path'])
        for key in ('animation_sha256', 'skeleton_sha256'):
            checked_hash(row[key])
        key = (row['native_asset_name'], row['configured_mesh_path'], row['rig_fingerprint'])
        groups.setdefault(key, []).append(row)
    if {k[0] for k in groups} != set(assets):
        raise ValueError('Requested asset lacks proven static support')
    result = []
    for key, rows in sorted(groups.items()):
        signature = lambda r: (r['left_hand_in_weapon'], r['animation_sha256'], r['skeleton_sha256'])
        if any(signature(r) != signature(rows[0]) for r in rows):
            raise ValueError('Conflicting exact support key')
        r = rows[0]
        out = {k:r[k] for k in ('native_asset_name', 'configured_mesh_path', 'rig_fingerprint',
                                'animation_sha256', 'skeleton_sha256', 'left_hand_in_weapon')}
        out.update(left_hand_status='static_authored_pose', right_hand_retargeted=False,
                   native_admission=False, source_bindings=[x['binding_digest'] for x in rows],
                   source_weapons=[x['weapon'] for x in rows], animation_chain=r['animation_chain'],
                   right_hand_status_not_admitted=r['right_hand_status'])
        envelopes=[bounded_references[x['binding_digest']] for x in rows
                   if bounded_references and x['binding_digest'] in bounded_references]
        if envelopes:
            out.update(left_hand_status='bounded_decoded_reference',reference_envelopes=envelopes,
                       target_runtime_interpolation_verified=False)
        if enable_contact:
            if any(x.get('weapon_class') != r['weapon_class'] for x in rows):
                raise ValueError('Conflicting support weapon class')
            out.update(weapon_class=r['weapon_class'], support_contact_reference=True)
        if r['native_asset_name'] in primary_hand_assets:
            out['support_aim_mode'] = 'primary_hand'
        out['binding_digest'] = digest(out)
        result.append(out)
    scope='Static weapon-local support contact only; no gun-hand pose, native feature admission or grip claim'
    if bounded_references:
        scope='Explicit bounded decoded-curve support references; no gun-hand pose, native interpolation, reload or firing admission'
    return dict(schema='fvr.bc2.authored_support_bindings', schema_version=1, profiles=result,
                runtime_admission=False, scope=scope)


def header(document):
    lines = ['// Private measured support data; no native capability admission.', '#pragma once',
             '#include "Bc2AuthoredSupport.h"', 'namespace fvr::bc2::generated {',
             f'inline const std::array<AuthoredSupportProfile,{len(document["profiles"])}> AuthoredSupports{{{{']
    for r in document['profiles']:
        values = r['left_hand_in_weapon']
        mat = 'math::Matrix4{{{' + ','.join('{{'+','.join(f'{v:.10g}f' if ('.' in f'{v:.10g}' or 'e' in f'{v:.10g}') else f'{v:.10g}.f' for v in values[i:i+4])+'}}' for i in range(0,16,4)) + '}}}'
        text = ','.join(json.dumps(r[k]) for k in ('native_asset_name','configured_mesh_path','binding_digest','animation_sha256','skeleton_sha256'))
        mode=r.get('support_aim_mode','two_point')
        if mode not in ('two_point','primary_hand') or (mode=='primary_hand' and not r.get('support_contact_reference')):
            raise ValueError('Unsupported or unenrolled support aim mode')
        lines.append('AuthoredSupportProfile{'+text+',0x'+r['rig_fingerprint'].split(':')[1]+'ull,'+mat+
                     (',true' if r.get('support_contact_reference') else '')+
                     (',interaction::SupportAimMode::PrimaryHand' if mode=='primary_hand' else '')+'},')
    return '\n'.join(lines+['}};', '}'])+'\n'


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('game','archive','configurations','mesh-bindings','hand-poses','output','header'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--asset',action='append',required=True)
    p.add_argument('--reference-archive',action='append',type=Path,default=[],help='Explicit shared authored dependency archive')
    p.add_argument('--enable-support-contact',action='store_true',help='Enroll selected static firearm support contacts, preserving the gun hand')
    p.add_argument('--primary-hand-aim-asset',action='append',default=[],help='Explicit contact asset using primary-hand aim with close support hands')
    a=p.parse_args(argv)
    sources=[a.configurations,a.mesh_bindings,a.hand_poses]
    blobs=[x.read_bytes() for x in sources]
    bound=load(a.game,a.archive,*[json.loads(x.decode('utf-8-sig')) for x in blobs],reload_references_only=True,reference_archives=a.reference_archive)
    result=derive(bound,set(a.asset),enable_contact=a.enable_support_contact,primary_hand_assets=set(a.primary_hand_aim_asset))
    result['input_sha256']={p.name:hashlib.sha256(b).hexdigest() for p,b in zip(sources,blobs)}
    result['archive_index_sha256']=bound['archive_index_sha256']
    a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    a.header.write_text(header(result),encoding='utf-8')
    print(json.dumps({'profiles':len(result['profiles']),'runtime_admission':False}))


if __name__=='__main__':
    main()
