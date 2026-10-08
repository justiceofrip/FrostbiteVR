"""Audit actual CB bytes across exact same native frame; no render authority."""
from pathlib import Path
import argparse, collections, hashlib, json, math, struct

GEOMETRY={1:(270,'e05c8e35ec306bd2','5fd96edbcded25ba'),2:(90,'ce6e99d2c3cd267e','a9888be789a5c902')}
RIG=0xa7f219a1426216ab
OWNER=('actor','weak','weapon','owner_generation','space','rig_pose','rig_fingerprint')

def read_draw(draw):
    section=draw.get('geometry_section');expected=GEOMETRY.get(section)
    if not expected or (draw.get('count'),draw.get('vertex_skin_fnv1a64'),draw.get('position_fnv1a64'))!=expected:return None
    if not draw.get('complete') or draw.get('abandoned') or not draw.get('geometry_valid') or draw.get('eye') not in (0,1):return None
    key=tuple(draw.get(k,0) for k in ('world','request','frame'))
    if not key[0] or not key[1] or not draw.get('view'):return None
    slot=draw.get('shell_slot_observation',{});buffers=draw.get('buffers',[])
    if not slot.get('captured') or (slot.get('constant_buffer_slot'),slot.get('byte_offset'),slot.get('byte_count'))!=(0,272,48):return None
    if len(buffers)<3 or not buffers[2].get('complete') or buffers[2].get('copied_bytes',0)<320:return None
    try: raw=bytes.fromhex(slot.get('bytes_hex',''))
    except (TypeError,ValueError):return None
    if len(raw)!=48 or not all(math.isfinite(v) for v in struct.unpack('<12f',raw)):return None
    return key,raw

def source_owner(draw):
    p=draw.get('producer',{});now=draw.get('qpc_ns',0)
    if not p.get('exact_request_association') or not p.get('packed_shell_valid') or p.get('shell_hidden'):return None
    if p.get('request')!=draw['request'] or p.get('native_frame')!=draw['frame'] or p.get('rig_fingerprint')!=RIG:return None
    observed=p.get('observed_ns',0);deadline=p.get('deadline_ns',0)
    if not 0<observed<=now<deadline or deadline-observed>250000000:return None
    owner=tuple(p.get(k,0) for k in OWNER)
    if not all(owner) or not p.get('input_generation'):return None
    buffers=draw['buffers'];cb=buffers[2]
    if cb.get('packed_shell_matches')!=1 or cb.get('packed_shell_offsets')!=[272]:return None
    if sum(b.get('packed_shell_matches',0) for b in buffers[2:])!=1:return None
    return owner

def audit(trace):
    draws=trace.get('reload_draw_evidence',{}).get('draws',[])
    if len(draws)>4096:raise ValueError('Draw bound exceeded')
    grouped=collections.defaultdict(list);rejected=0
    for draw in draws:
        parsed=read_draw(draw)
        if not parsed:rejected+=1;continue
        key,raw=parsed;grouped[key].append((draw,raw,source_owner(draw)))
    result={'schema':'fvr.bc2.shell_slot_join_audit','schema_version':1,
            'render_authority':False,'shader_consumption_verified':False,
            'native_state_written':False,'draws':len(draws),'eligible_shell_draws':sum(map(len,grouped.values())),
            'rejected_or_non_shell_draws':rejected,'source_seeds':0,'matched_left_draws':0,'matched_right_draws':0,
            'complete_stereo_shell_frames':0,'conflicting_frames':0,'frames_without_source':0,
            'slot_mismatch_draws':0,'incomplete_stereo_frames':0,'samples':[]}
    for key,entries in sorted(grouped.items()):
        seeds={(owner,raw) for _,raw,owner in entries if owner}
        result['source_seeds']+=sum(bool(owner) for _,_,owner in entries)
        if not seeds:result['frames_without_source']+=1;continue
        if len(seeds)!=1:result['conflicting_frames']+=1;continue
        owner,source=next(iter(seeds));matched=[]
        for draw,raw,_ in entries:
            if raw!=source:result['slot_mismatch_draws']+=1;continue
            matched.append(draw)
            result['matched_left_draws' if draw['eye']==0 else 'matched_right_draws']+=1
        pairs={(d['eye'],d['geometry_section']) for d in matched}
        complete=pairs=={(0,1),(0,2),(1,1),(1,2)} and len(matched)==len(entries)
        result['complete_stereo_shell_frames']+=complete
        result['incomplete_stereo_frames']+=not complete
        if len(result['samples'])<16:result['samples'].append({'world':key[0],'request':key[1],'frame':key[2],
            'source_owner':dict(zip(OWNER,owner)),'source_slot_sha256':hashlib.sha256(source).hexdigest(),
            'complete_stereo_shell_frame':complete,'matched_draws':len(matched)})
    result['interpretation']='Exact bytes carried by each sampled SPAS draw, joined only within the same world/request/native frame; no shader instruction or current native section authority inferred.'
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('trace',type=Path);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();result=audit(json.loads(args.trace.read_text(encoding='utf-8-sig')))
    result['source_trace']=str(args.trace);args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k not in ('samples','interpretation')},indent=2))
