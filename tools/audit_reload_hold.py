"""Audit the explicit one-shot native reload hold; never attaches to a process."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
from audit_reload_restore import audit as audit_restore


def audit(trace):
    base=audit_restore(trace,require_restore=True,require_server_transfer=True,allow_diagnostic_hold=True)
    errors=list(base['errors'])
    def check(condition,text):
        if not condition:errors.append(text)
    flow=trace['gameplay']['reload_flow'];hold=flow.get('diagnostic_hold',{})
    rows=flow['records'];begin=hold.get('begin_ns',0);deadline=hold.get('deadline_ns',0)
    check(hold.get('code_verified') is True,'hold code was not verified')
    check(begin>0 and deadline-begin==350000000 and hold.get('duration_ns')==350000000,'hold interval is not exactly350ms')
    check(hold.get('phase')==3 and hold.get('reason')==1,'hold did not release naturally at its deadline')
    check(all(hold.get(k)==0 for k in ('contention','patch_failures','restore_failures')),'hold policy or restoration failed')
    held=[r for r in rows if r.get('hold_requested') or r.get('hold_applied')]
    counts=Counter(r['before']['branch'] for r in held)
    check(all(counts[b]>0 for b in range(3)),'not all three branches were held')
    check(hold.get('applied')==hold.get('restored')==[counts[b] for b in range(3)] and hold.get('original_calls')==len(held),'hold counters disagree with recorded invocations')
    for r in held:
        tag='record '+str(r['id'])
        check(r['kind']==0 and r.get('hold_requested') is True and r.get('hold_applied') is True and r.get('hold_restored') is True and r.get('hold_unexpected_native_write') is False,tag+': override did not apply/restore cleanly')
        before,after=r['before'],r.get('after')
        check(r['begin_ns']<deadline and r['end_ns']>=begin,tag+': held invocation outside active interval')
        check(isinstance(after,dict) and all(before[k]==after[k] for k in ('current','next','timer','loaded','reserve')),tag+': reload state advanced inside a held original')
        check(before['current']==11 and before['next']==12 and [before['loaded'],before['reserve']]==[hold.get('loaded'),hold.get('reserve')],tag+': held wrong state/ammo')
        expected=r.get('hold_original_delta_bits');raw_before=bytes(r['context_before']['bytes']);raw_after=bytes(r['context_after']['bytes'])
        dt=struct.unpack_from('<f',raw_before,0x18)[0]
        check(0<dt<=.05 and struct.unpack_from('<I',raw_before,0x18)[0]==expected and struct.unpack_from('<I',raw_after,0x18)[0]==expected and r.get('hold_before_restore_bits')==0,tag+': original delta bytes were not exactly restored from positive zero')
        check(before['firing']==hold.get('firing',[0,0,0])[before['branch']],tag+': held firing identity changed')
    transfers=[r for r in rows if r['kind']==2]
    check(all(r['begin_ns']>=deadline for r in transfers),'a round transferred before automatic hold release')
    check(all(t['single_conserved_round'] for t in base['transfers']),'non-conserved native shell transfer')
    check(not base['between_observed_calls_ammo_changes'] and not base['restore_round_reverts'],'unexplained ammo changes or prediction reverts')
    per_branch=[[t for t in base['transfers'] if t['branch']==b] for b in range(3)]
    check(all(per_branch) and all([(t['before'],t['after']) for t in branch]==[(t['before'],t['after']) for t in per_branch[0]] for branch in per_branch),'native resumption did not converge through the same shell sequence')
    after_by_branch=[]
    for b in range(3):
        branch=[r for r in rows if r['before']['branch']==b and isinstance(r.get('after'),dict)]
        latest=max(branch,key=lambda r:r['end_ns']) if branch else None
        after_by_branch.append([latest['after']['loaded'],latest['after']['reserve']] if latest else None)
    check(after_by_branch[0] is not None and all(x==after_by_branch[0] for x in after_by_branch),'final native ammunition differs between branches')
    return {'schema':'fvr.bc2.reload_hold_audit.v1','passed':not errors,'errors':errors,'manual_reload_enabled':False,'headset_verified':False,'hold':hold,'held_records_per_branch':[counts[b] for b in range(3)],'transfers':base['transfers'],'final_ammo_by_branch':after_by_branch,'restore_count':len(base['restores']),'limits':['One bounded native wait/resume experiment; no physical insertion capability is implied.']}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();raw=a.trace.read_bytes()
    try:r=audit(json.loads(raw))
    except (KeyError,ValueError,TypeError,IndexError,struct.error) as e:r={'schema':'fvr.bc2.reload_hold_audit.v1','passed':False,'errors':['Malformed hold trace: '+str(e)],'manual_reload_enabled':False}
    r['source']={'path':str(a.trace.resolve()),'sha256':hashlib.sha256(raw).hexdigest()};a.output.write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:r[k] for k in ('passed','errors')}));return 0 if r['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
