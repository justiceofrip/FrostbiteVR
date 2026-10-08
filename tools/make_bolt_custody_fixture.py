"""Generate bounded test-only M24/SV98 pose landmarks, never runtime profiles."""
import argparse,hashlib,json,math
from pathlib import Path

def extract(data):
    if data['schema']!='fvr.bc2.bolt_authored_calibration.v1' or data['runtime_admission'] or data['semantic_closed_verified']:raise ValueError('Offline authored calibration required')
    rows=[]
    for name in ('M24','SV98'):
        profiles=[p for p in data['profiles'] if p['asset']==name and p['source']['role']=='1p_boltaction']
        if len({p['source']['sha256'] for p in profiles})!=1:raise ValueError('Exact animation variant ambiguous')
        p=profiles[0];parts=[part for part in p['parts'] if part['bone']=='jntWpn_3']
        if len(parts)!=1:raise ValueError('Expected exact rigid part absent')
        part=parts[0];m=part['measured_stops_axes'];right=part['contacts']['right']
        if not part['independent_rigid_geometry'] or not right['pose'] or right['native_contact_verified'] or m['frame_zero_is_native_closed']:raise ValueError('Measured candidate receipt required')
        first=part['motion']['baseline_parent_from_part'];rear=m['translation']['maximum']['parent_from_part'];advanced=m['translation']['minimum']['parent_from_part']
        delta=[rear[n]-advanced[n] for n in (12,13,14)];length=math.sqrt(sum(v*v for v in delta));axis=[v/length for v in delta]
        poses=[first,m['rotation']['maximum']['parent_from_part'],rear,advanced,m['sampled_dwell_candidates'][-1]['parent_from_part'],right['pose']['part_from_wrist']]
        config=next(c['configuration'] for c in data['configuration_variants'] if c['asset']==name and c['configuration']['resource']==p['weapon_configuration']['resource'])
        rows.append(dict(name=name,axis=axis,stroke=length,unlock=-m['rotation']['maximum_radians'],poses=poses,
            native_bolt_time=config['fields']['FireLogic.BoltAction.BoltActionTime']['value'],source=p['source'],configuration=p['weapon_configuration']))
    return rows

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():p.error('Preserve prior generated fixture')
    raw=a.input.read_bytes();rows=extract(json.loads(raw));sha=hashlib.sha256(raw).hexdigest()
    def number(v):
        s=format(v,'.10g');return s+('f' if '.' in s or 'e' in s else '.0f')
    def matrix(v):return 'math::Matrix4{{{'+','.join('{'+','.join(number(x) for x in v[i:i+4])+'}' for i in range(0,16,4))+'}}}'
    lines=['#pragma once','#include "fvr/math/StereoMath.h"','namespace bolt_custody_fixture {','using namespace fvr;',
        '// Generated authored landmarks only. Initial pose is NOT native closed.',
        'inline constexpr bool NativeHandleVerified=false,NativeClosedVerified=false;',
        f'inline constexpr const char* EvidenceSha256="{sha}";',
        'struct MeasuredBolt {const char* name; std::array<float,3> axis; float stroke,unlock,nativeBoltTime; math::Matrix4 initial,rotated,rear,advanced,finalPose,contactFromWrist;};']
    for r in rows:
        lines.append('// '+r['source']['resource']+' SHA256 '+r['source']['sha256'])
        lines.append('inline const MeasuredBolt '+r['name']+'{"'+r['name']+'",{'+','.join(map(number,r['axis']))+'},'+','.join(number(r[k]) for k in ('stroke','unlock','native_bolt_time'))+','+','.join(map(matrix,r['poses']))+'};')
    lines+=['}'];a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text('\n'.join(lines)+'\n')
    print(json.dumps(dict(output=str(a.output),sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),source_sha256=sha,fixtures=len(rows),native_admission=False)))
if __name__=='__main__':main()
