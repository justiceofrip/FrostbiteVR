"""Prepared root-only read observer. Default --check is offline; --capture is explicit.

Read-only native phase/counter evidence, never an operation acknowledgement.
Use Run-Observe.ps1 for storage admission when PC access and root session resume.
"""
import argparse, hashlib, json, math, struct, sys, time
from pathlib import Path
from prepare import HERE, ROOT, TOOLS, RESOURCE, Image, code_proof, sha

PHASE = {1:'ReturnWait',2:'InputProcessing',5:'ShotStep',6:'ShotStep',7:'BoltHold',
         8:'BoltCycle',10:'ReloadBegin',11:'ReloadWait',12:'ReloadTransfer'}

def state(raw, base_capacity):
    if len(raw) != 0xb0: raise ValueError('Firing object size differs')
    current, previous, following = struct.unpack_from('<III', raw, 0x3c)
    timer, = struct.unpack_from('<f', raw, 0x50)
    cm, rm = struct.unpack_from('<ff', raw, 0x74)
    loaded, reserve = struct.unpack_from('<ii', raw, 0x7c)
    override, = struct.unpack_from('<i', raw, 0x98); flags = raw[0xa8]
    if max(current,previous,following) > 15 or not math.isfinite(timer) or abs(timer)>1000000:
        raise ValueError('Malformed native phase/timer')
    if any(not math.isfinite(v) or not 0<=v<=1024 for v in (cm,rm)) or any(not -1<=v<=1000000 for v in (loaded,reserve,override)):
        raise ValueError('Malformed ammo fields')
    product = base_capacity*cm
    capacity = override if override>=0 else int(product) if 0<=product<=1000000 and product.is_integer() else None
    return dict(current=current,previous=previous,next=following,phase=PHASE.get(current,'Unknown'),
        phase_timer=timer,loaded=loaded,reserve=reserve,capacity_multiplier=cm,reserve_multiplier=rm,
        capacity_override=override,effective_capacity=capacity,flags_a8=flags)

def idle_partial(states):
    return len(states)==3 and len({(s['loaded'],s['reserve'],s['effective_capacity']) for s in states})==1 and all(
        s['current']==s['next']==2 and s['effective_capacity'] is not None and
        0<s['loaded']<s['effective_capacity'] and s['reserve']>0 and not s['flags_a8']&8 for s in states)

def transfer(before, after):
    # This is sampled conservation only. The enclosing report keeps authority false.
    delta = after['loaded']-before['loaded']
    return delta>0 and before['reserve']-after['reserve']==delta and min(before['loaded'],before['reserve'],after['loaded'],after['reserve'])>=0

def check():
    raw = bytearray(0xb0)
    struct.pack_into('<III',raw,0x3c,2,1,2); struct.pack_into('<ffii',raw,0x74,1.,1.,30,200)
    struct.pack_into('<i',raw,0x98,-1)
    sample = state(raw,100)
    assert idle_partial([sample]*3)
    assert not idle_partial([sample,sample,{**sample,'current':11}])
    assert not idle_partial([sample,sample,{**sample,'loaded':31}])
    assert transfer(sample,{**sample,'loaded':100,'reserve':130})
    assert not transfer(sample,{**sample,'loaded':100,'reserve':200})
    struct.pack_into('<f',raw,0x74,float('nan'))
    try: state(raw,100)
    except ValueError: pass
    else: raise AssertionError('Nonfinite state accepted')
    proof=json.loads((HERE/'proof.json').read_text()); jobs=json.loads((HERE/'jobs.json').read_text())
    for name, expected in proof['reader_hashes'].items():
        if sha((TOOLS/name).read_bytes())!=expected: raise ValueError('Prepared reader changed: '+name)
    if sha((ROOT/'src/games/bc2/Bc2ReloadState.cpp').read_bytes())!=proof['reload_state_source_sha256']:
        raise ValueError('Production ReloadState source changed')
    from capture_lmg_native import select_job
    model,definition=select_job(jobs,RESOURCE)
    if definition['selector']['asset_name']!='M60_sp': raise ValueError('Job selector differs')
    return proof,model,definition

def capture(args,proof,model,definition):
    from capture_lmg_native import open_readers, read_sample, cohort, configured_mesh_matches
    from capture_reload_state import Inspector
    from capture_reload_server import capture as server_capture, discover
    from read_bc2 import Process
    if args.output.exists(): raise ValueError('Preserve prior capture')
    image=Image(args.executable)
    if sha(image.data)!=proof['executable_sha256'] or code_proof(image)!=proof['reload_state_functions']:
        raise ValueError('Prepared executable/complete ReloadState proof differs')
    result=dict(schema='fvr.bc2.m60_stage_observation.v1',read_only=True,process_writes=False,native_calls=False,
        input_or_focus_changes=False,native_authority=False,fire_ready_verified=False,chamber_known=False,
        mechanism_roles={'cover':'Unknown','latch':'Unknown','container':'Unknown','feed':'Unknown','charge':'Unknown'},
        proof_sha256=sha((HERE/'proof.json').read_bytes()),jobs_sha256=sha((HERE/'jobs.json').read_bytes()),
        executable_sha256=sha(image.data),pid=args.pid,resource=RESOURCE,samples=[],rejected=[],
        atomic_snapshot=False,complete_event_coverage=False,pose_capture=False,input_edge_captured=False)
    process=None; baseline=None; settled_start=None; ready_ns=None; previous=None; post_start=None; transfers=set()
    start=time.perf_counter_ns(); deadline=start+20_000_000_000
    try:
        image,process,inspector,binding=open_readers(args.executable,args.pid,Image,Process,Inspector,discover)
        # Complete production function proof must match in live memory as well.
        for region in proof['reload_state_functions']:
            if sha(process.read(image.base+region['rva'],region['size']))!=region['sha256']:
                raise ValueError('Live ReloadState function differs')
        def server(inspect,bound):
            row=server_capture(inspect,bound)
            row['state']['observed']=state(bytes.fromhex(row['state']['raw_hex']),100)
            return row
        for _ in range(1100):
            if time.perf_counter_ns()>=deadline: break
            try:
                sample=read_sample(inspector,binding,definition,server)
                identity=cohort(sample)
                if baseline is None:
                    links=inspector.mesh_links(sample['configuration'])
                    if not configured_mesh_matches(links,sample['configuration'],model['mesh_resource']):
                        raise ValueError('Exact configured body mesh absent')
                    result.update(configuration=sample['configuration'],mesh_links=links,native_owner=sample['native_owner'],server_binding=binding)
                    baseline=identity
                elif identity!=baseline:
                    result['rejected'].append(dict(ns=time.perf_counter_ns(),reason='Session owner/configuration/firing cohort changed')); break
                capacity=sample['configuration']['ammo_config']['MagazineCapacity']
                if capacity!=100: raise ValueError('Exact configured base capacity changed from authored 100')
                states=[state(bytes.fromhex(sample['client_states'][str(b)]['raw_hex']),capacity) for b in (0x3c,0x40)]
                states.append(sample['server']['state']['observed'])
                row=dict(begin_ns=sample['begin_ns'],end_ns=sample['end_ns'],states=states)
                result['samples'].append(row); now=row['end_ns']
                continuous=previous is not None and row['begin_ns']-previous['end_ns']<=100_000_000
                if ready_ns is None:
                    same=previous is not None and [(s['loaded'],s['reserve']) for s in previous['states']]==[(s['loaded'],s['reserve']) for s in states]
                    if idle_partial(states):
                        if settled_start is None or not continuous or not same: settled_start=now
                        if now-settled_start>=2_000_000_000:
                            ready_ns=now; result['operator_reload_window_ns']=now
                            print(json.dumps(dict(ready_for_operator_ordinary_reload=True,input_sent=False)),flush=True)
                    else: settled_start=None
                elif previous:
                    for branch,(before,after) in enumerate(zip(previous['states'],states)):
                        if transfer(before,after):
                            transfers.add(branch)
                            result.setdefault('observed_conserved_increases',[]).append(dict(branch=branch,begin_ns=previous['begin_ns'],end_ns=now,
                                loaded_before=before['loaded'],loaded_after=after['loaded'],reserve_before=before['reserve'],reserve_after=after['reserve'],native_acknowledgement=False))
                    idle=all(s['current']==s['next']==2 and s['loaded']>0 for s in states)
                    agreed=len({(s['loaded'],s['reserve']) for s in states})==1
                    same=[(s['loaded'],s['reserve']) for s in previous['states']]==[(s['loaded'],s['reserve']) for s in states]
                    if transfers=={0,1,2} and idle and agreed:
                        if post_start is None or not continuous or not same: post_start=now
                        if now-post_start>=2_000_000_000:
                            result['observation_complete']=True; break
                    else: post_start=None
                previous=row
            except (OSError,ValueError,KeyError) as exc:
                result['rejected'].append(dict(ns=time.perf_counter_ns(),reason=str(exc)))
                settled_start=None; post_start=None; previous=None
            time.sleep(.020)
    except (OSError,ValueError,KeyError,RuntimeError) as exc:
        result['rejected'].append(dict(ns=time.perf_counter_ns(),reason=str(exc)))
    finally:
        if process is not None: process.close()
    result.setdefault('observation_complete',False); result['duration_ns']=time.perf_counter_ns()-start
    result['limit']='Sampled ordinary reload only. Transfer intervals and input-processing return are not manual stage dispatch, resource ownership, cover/feed completion, chamber or fire-readiness proof.'
    data=json.dumps(result,indent=2,allow_nan=False)+'\n'
    if len(data.encode())>8*1024*1024: raise ValueError('8 MiB metadata bound exceeded')
    with args.output.open('x',encoding='utf-8') as output:
        output.write(data)
    print(json.dumps(dict(observation_complete=result['observation_complete'],samples=len(result['samples']),rejections=len(result['rejected']),output=str(args.output))))
    return 0 if result['observation_complete'] else 1

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',action='store_true'); parser.add_argument('--check',action='store_true')
    parser.add_argument('--pid',type=int); parser.add_argument('--executable',type=Path); parser.add_argument('--output',type=Path)
    args=parser.parse_args(); proof,model,definition=check()
    if not args.capture:
        print(json.dumps(dict(passed=True,offline_checks=6,reader_pins_checked=True,game_process_opened=False))); return 0
    if args.check or not args.pid or args.pid<=0 or args.executable is None or args.output is None:
        parser.error('Capture requires positive PID, executable and fresh output; --check cannot capture')
    return capture(args,proof,model,definition)

if __name__=='__main__': raise SystemExit(main())
