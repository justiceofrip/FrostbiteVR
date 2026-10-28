"""400 scheduled slots in a12s read-only current client/server health window.

No native calls, hooks, input, process writes, process discovery, or retry. An
actor/read gap is never death. Current-health transitions require later paired
presentation evidence; this isolated observation never claims actual death.
"""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import datetime
import hashlib
import json
import math
import struct
import sys
import time

ROOT = Path(r'G:\Unity\VRMMO\bc2vr-recovery\headset391-investigation\actual-death395-preparation')
TRANSITION_ROOT = Path(r'G:\Unity\VRMMO\bc2vr-recovery\headset391-investigation\fade-file-window400-private')
if hashlib.sha256((TRANSITION_ROOT/'HealthTransition395.py').read_bytes()).hexdigest() != '101240b8dd86d9db16760f3cf37f460f7b07c852af88ff2b99cf94518a468b52':
    raise ValueError('Historical transition policy changed')
sys.path.insert(0,str(TRANSITION_ROOT))
from HealthTransition395 import summarize
from HealthReadPolicy436 import load_proof, verify_spans
from HealthBaselineEvidence436 import publish_baseline
HELPERS = Path(r'G:\Unity\VRMMO\FrostbiteVR\tools')
EXE = Path(r'G:\New folder (2)\Battlefield Bad Company 2\BFBC2Game.exe')
IMAGE_HASH = '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
PROOF_HASH = 'b7b64b1a796697e824cdad18d0e7e9243093359fd62791d7dea8aada4acb75a9'


def require(value, message):
    if not value:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument('--process-start-filetime', required=True, type=int)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--start-tick-ms', required=True, type=int)
    parser.add_argument('--ready', required=True, type=Path)
    args = parser.parse_args()
    require(args.pid > 0 and args.process_start_filetime > 0, 'Exact process identity required')
    require(args.output.parent.is_dir() and not args.output.exists(), 'New output in existing private directory required')
    require(args.ready.parent == args.output.parent and not args.ready.exists(), 'New ready receipt beside output required')
    rows_path = args.output.with_suffix('.jsonl')
    require(not rows_path.exists(), 'Health series JSONL must be new')
    pins = json.loads(Path(__file__).with_name('helper-pins-r3.json').read_text())
    for name, digest in pins.items():
        require(hashlib.sha256((HELPERS/name).read_bytes()).hexdigest() == digest, 'Helper changed: '+name)
    proof_raw = (ROOT/'native-health-consumer395.json').read_bytes()
    require(hashlib.sha256(proof_raw).hexdigest() == PROOF_HASH, 'Offline health proof changed')
    proof = load_proof(proof_raw)
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(HELPERS))
    from read_bc2 import Process
    from capture_reload_state import Image, Inspector
    from capture_reload_server import discover
    report = dict(schema='fvr.private.health_series395.v1', pid=args.pid,
                  process_start_filetime=args.process_start_filetime,
                  utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  helper_pins=pins, offline_proof_sha256=PROOF_HASH,
                  read_only=True, process_writes=False, native_calls=False, os_input=False,
                  atomic=False, actual_death=False, admission=False, samples=[], errors=[],
                  native_damage_consumer_live_verified=False,
                  baseline_evidence_policy_sha256=hashlib.sha256(Path(__file__).with_name('HealthBaselineEvidence436.py').read_bytes()).hexdigest(),
                  health_read_policy_sha256=hashlib.sha256(Path(__file__).with_name('HealthReadPolicy436.py').read_bytes()).hexdigest(),
                  start_tick_ms=args.start_tick_ms,
                  limits=dict(samples=400, interval_ms=30, whole_process_read_seconds=30,
                              read_operations=300000, read_bytes=16*1024*1024),
                  unavailable=['Godmode controlling instance and damage consumer are unverified.',
                               'Native MaxHealth multipliers are not evaluated or called.',
                               'This snapshot has no correlated paired-eye death presentation.'])
    process = None
    began = None
    operations = total_bytes = 0
    try:
        image = Image(EXE)
        require(hashlib.sha256(image.data).hexdigest() == IMAGE_HASH, 'Installed executable changed')
        process = Process(args.pid, expected_path=EXE)
        require(process.base == image.base == 0x400000, 'Relocated executable unsupported')
        began = time.perf_counter_ns()
        original_read = process.read

        def read(address, count):
            nonlocal operations, total_bytes
            operations += 1
            total_bytes += count
            require(operations <= 300000 and total_bytes <= 16*1024*1024 and
                    time.perf_counter_ns()-began <= 30_000_000_000, 'Whole observation budget exceeded')
            require(0x10000 <= address and 0 < count <= 65536 and address+count <= 0x100000000,
                    'Read outside bounded x86 address span')
            return original_read(address, count)

        process.read = read
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        get_times = kernel.GetProcessTimes
        get_times.argtypes = [wintypes.HANDLE]+[ctypes.POINTER(wintypes.FILETIME)]*4
        get_times.restype = wintypes.BOOL

        def identity():
            times = [wintypes.FILETIME() for _ in range(4)]
            require(get_times(process.h, *(ctypes.byref(x) for x in times)), 'Process identity read failed')
            require((times[0].dwHighDateTime << 32 | times[0].dwLowDateTime) == args.process_start_filetime,
                    'Process creation identity changed')

        identity()
        inspector = Inspector(process, image)
        binding = discover(image, process)
        code_proof = verify_spans(proof,image,read)
        report['health_read_code_policy'] = code_proof
        report['verified_native_spans'] = code_proof['verified_native_spans']
        report['server_binding'] = binding

        def owner():
            # Same verified actor/player routes as the pinned readers, without
            # requiring selected equipment to survive a future death transition.
            cm = process.u32(inspector.context+8)
            require(process.u32(cm) == inspector.manager_table, 'Client manager differs')
            cp = process.u32(cm+0xb4)
            require(read(cp+0xccd, 1)[0] & 8, 'Player is not local')
            require(process.u32(cp+0xc60) == 0, 'Attached entity present; on-foot only')
            weak = process.u32(cp+0xc54)
            weak_value = process.u32(weak)
            require(weak_value >= 0x10004, 'Client actor weak reference absent')
            ca = weak_value-4
            require(process.u32(ca) == 0x1437950 and process.u32(ca+0x2c) == 0x1437924,
                    'Client main/embedded health table differs')
            inspector.require_type(ca, 'ClientSoldierEntity')
            require(process.u32(cp+0xc68) == ca and process.u32(ca+0x220) == cp,
                    'Client player/actor owner disagreement')
            pid = process.u32(cp+0x154)
            sm = process.u32(binding['context']+8)
            require(process.u32(sm) == binding['manager_table'], 'Server manager differs')
            capacity, players = process.u32(sm+4), process.u32(sm+0x6c)
            require(0 < capacity <= 256 and pid < capacity and 0x10000 <= players and
                    players+capacity*4 <= 0x100000000, 'Server player array bounds')
            sp = process.u32(players+pid*4)
            require(process.u32(sp+0x154) == pid, 'Server/client player index differs')
            table = inspector.image_pointer(process.u32(sp), 0x34)
            require(process.u32(table+0x30) == binding['controlled_getter'], 'Server actor getter differs')
            sa = process.u32(sp+0xc3c)
            require(process.u32(sa) == 0x1424850 and process.u32(sa+0x2c) == 0x1424824,
                    'Server main/embedded health table differs')
            inspector.require_type(sa, 'ServerSoldierEntity')
            require(process.u32(sp+0xc6c) == sa and process.u32(sa+0x220) == sp,
                    'Server player/actor owner disagreement')
            data = process.u32(ca+0xc)
            require(process.u32(sa+0xc) == data, 'Client/server soldier data differs')
            info = inspector.require_type(data, 'SoldierEntityData')
            require(info['address'] == 0x1c00d3c, 'Soldier data type identity differs')
            fields = [f for f in info['fields'] if f['name'] == 'MaxHealth']
            require(len(fields) == 1 and fields[0]['offset'] == 0xe8 and
                    fields[0]['type'] == 'Float32' and fields[0]['type_info'] == 0x1bf03e4,
                    'Reflected MaxHealth layout differs')
            return dict(client_manager=cm, client_player=cp, weak=weak, client_actor=ca,
                        player_id=pid, server_manager=sm, server_player=sp, server_actor=sa,
                        player_array=players, capacity=capacity, soldier_data=data,
                        client_health_table=0x1437924, server_health_table=0x1424824)

        def snapshot():
            before = owner()
            addresses = [before['client_actor']+0x34, before['server_actor']+0x34,
                         before['soldier_data']+0xe8]
            first = [read(at, 4) for at in addresses]
            after = owner()
            second = [read(at, 4) for at in addresses]
            require(before == after and first == second and owner() == before,
                    'Health/owner changed during repeated snapshot')
            client, server, maximum = [struct.unpack('<f', raw)[0] for raw in first]
            require(all(math.isfinite(x) for x in (client, server, maximum)) and maximum > 0,
                    'Health or reflected MaxHealth is nonfinite/invalid')
            # Conservative bound, not a claim to have evaluated the native multipliers.
            bounded = 0 <= client <= maximum and 0 <= server <= maximum
            return dict(monotonic_ns=time.perf_counter_ns(), windows_tick_ms=int(kernel.GetTickCount64()),
                        owner=before, client_health=client, server_health=server,
                        static_max_health=maximum, raw_hex=[raw.hex() for raw in first],
                        repeated_equal=True, static_config_bound_pass=bounded,
                        native_max_health_evaluated=False, both_positive=client > 0 and server > 0,
                        both_zero=client == 0 and server == 0, health_copies_equal=first[0] == first[1])

        kernel.GetTickCount64.argtypes = []
        kernel.GetTickCount64.restype = ctypes.c_ulonglong
        require(2000 <= args.start_tick_ms-int(kernel.GetTickCount64()) <= 13000,
                'Health window must start2..13s after bounded binding')
        identity()
        baseline=snapshot()
        publish_baseline(baseline, report, args.ready, pid=args.pid,
                         process_start_filetime=args.process_start_filetime,
                         start_tick_ms=args.start_tick_ms)
        with rows_path.open('x',encoding='utf-8') as rows:
            for index in range(400):
                due=args.start_tick_ms+index*30
                while int(kernel.GetTickCount64()) < due:
                    time.sleep(max(0,min(.010,(due-int(kernel.GetTickCount64()))/1000)))
                tick=int(kernel.GetTickCount64())
                row=dict(index=index,scheduled_tick_ms=due,observed_tick_ms=tick)
                if tick-due >= 30:
                    row['gap']='missed_scheduled_slot'
                else:
                    try:
                        identity()
                        row['sample']=snapshot()
                    except (OSError, ValueError, KeyError, RuntimeError, AttributeError) as exc:
                        row['gap']=str(exc)
                # Fixed-size publication is filesystem-only, with the complete
                # original row preserved below. Invalid/missing is explicit0.
                sample=row.get('sample');valid=bool(sample and sample['repeated_equal'] and sample['static_config_bound_pass'] and sample['health_copies_equal'] and sample['owner']==baseline['owner'])
                b=baseline['owner'];current=sample or baseline
                fields=[1,args.pid,args.process_start_filetime,tick,index,int(valid),
                        b['client_player'],b['client_actor'],b['weak'],b['server_player'],b['server_actor'],
                        int.from_bytes(bytes.fromhex(baseline['raw_hex'][0]),'little'),int.from_bytes(bytes.fromhex(baseline['raw_hex'][1]),'little'),
                        int.from_bytes(bytes.fromhex(current['raw_hex'][0]),'little'),int.from_bytes(bytes.fromhex(current['raw_hex'][1]),'little'),
                        int.from_bytes(bytes.fromhex(baseline['raw_hex'][2]),'little')]
                # A replacement owner's values never masquerade as the pin.
                if sample:
                    o=sample['owner'];fields[6:11]=[o['client_player'],o['client_actor'],o['weak'],o['server_player'],o['server_actor']]
                pending=args.output.parent/'two-grenade-health.pending'
                pending.write_bytes(struct.pack('<16Q',*fields));pending.replace(args.output.parent/'two-grenade-health.bin')
                row['receiver_witness']=fields
                report['samples'].append(row)
                rows.write(json.dumps(row,allow_nan=False)+'\n');rows.flush()
        identity()
        report['health_evidence']=summarize(report['samples'])
        report['reflection'] = list(inspector.types.values())
    except (OSError, ValueError, KeyError, RuntimeError, AttributeError) as exc:
        report['errors'].append(str(exc))
    finally:
        if process is not None:
            process.close()
        report['process_read_operations'] = operations
        report['process_read_bytes'] = total_bytes
        report['elapsed_ns'] = time.perf_counter_ns()-began if began is not None else 0
    passed = len(report['samples']) == 400 and not report['errors']
    report['collection_complete']=passed
    report.setdefault('health_evidence',summarize(report['samples']))
    report['observation_pass'] = passed
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write('\n')
    print(json.dumps(dict(output=str(args.output), observation_pass=passed,
                          samples=len(report['samples']), errors=report['errors'], actual_death=False)))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
