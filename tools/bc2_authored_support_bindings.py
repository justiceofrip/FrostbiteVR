"""Extract only proven constant left support anchors; never promote a varying gun hand."""
import argparse
import hashlib
import json
from pathlib import Path
from bc2_authored_grip_bindings import load, digest, matrix, checked_hash, path_key


def derive(bindings, assets):
    if bindings.get('schema') != 'fvr.bc2.authored_reload_reference_bindings' or bindings.get('runtime_admission') is not False:
        raise ValueError('Exact non-admitted authored reference binding required')
    groups = {}
    for row in bindings['profiles']:
        if row['native_asset_name'] not in assets:
            continue
        if digest({k:v for k,v in row.items() if k != 'binding_digest'}) != row['binding_digest']:
            raise ValueError('Authored reference digest changed')
        if row['state_count'] != 1 or row['state_index'] != 0 or row['left_hand_status'] != 'static_authored_pose':
            raise ValueError('Support requires constant left-hand/weapon ancestor chains and sole state')
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
        out['binding_digest'] = digest(out)
        result.append(out)
    return dict(schema='fvr.bc2.authored_support_bindings', schema_version=1, profiles=result,
                runtime_admission=False, scope='Static weapon-local support contact only; no gun-hand pose, native feature admission or grip claim')


def header(document):
    lines = ['// Private measured support data; no native capability admission.', '#pragma once',
             '#include "Bc2AuthoredSupport.h"', 'namespace fvr::bc2::generated {',
             f'inline const std::array<AuthoredSupportProfile,{len(document["profiles"])}> AuthoredSupports{{{{']
    for r in document['profiles']:
        values = r['left_hand_in_weapon']
        mat = 'math::Matrix4{{{' + ','.join('{{'+','.join(f'{v:.10g}f' if ('.' in f'{v:.10g}' or 'e' in f'{v:.10g}') else f'{v:.10g}.f' for v in values[i:i+4])+'}}' for i in range(0,16,4)) + '}}}'
        text = ','.join(json.dumps(r[k]) for k in ('native_asset_name','configured_mesh_path','binding_digest','animation_sha256','skeleton_sha256'))
        lines.append('AuthoredSupportProfile{'+text+',0x'+r['rig_fingerprint'].split(':')[1]+'ull,'+mat+'},')
    return '\n'.join(lines+['}};', '}'])+'\n'


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('game','archive','configurations','mesh-bindings','hand-poses','output','header'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--asset',action='append',required=True)
    a=p.parse_args(argv)
    sources=[a.configurations,a.mesh_bindings,a.hand_poses]
    blobs=[x.read_bytes() for x in sources]
    bound=load(a.game,a.archive,*[json.loads(x.decode('utf-8-sig')) for x in blobs],reload_references_only=True)
    result=derive(bound,set(a.asset))
    result['input_sha256']={p.name:hashlib.sha256(b).hexdigest() for p,b in zip(sources,blobs)}
    result['archive_index_sha256']=bound['archive_index_sha256']
    a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    a.header.write_text(header(result),encoding='utf-8')
    print(json.dumps({'profiles':len(result['profiles']),'runtime_admission':False}))


if __name__=='__main__':
    main()
