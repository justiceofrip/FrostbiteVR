"""Audit one diagnostic native shell release/re-hold; no process access."""
import argparse
from collections import Counter
import hashlib,json,struct
from pathlib import Path
from audit_reload_restore import audit as audit_restore

def audit(trace):
    base=audit_restore(trace,require_restore=True,require_server_transfer=True,allow_diagnostic_round=True)
    errors=list(base['errors'])
    def check(ok,text):
        if not ok:errors.append(text)
    flow=trace['gameplay']['reload_flow'];gate=flow.get('diagnostic_round',{});delta=flow.get('diagnostic_hold',{})
    rows=flow['records'];byid={r['id']:r for r in rows}
    first=gate.get('first_begin_ns',0);first_end=gate.get('first_deadline_ns',0)
    request=gate.get('request_ns',0);second=gate.get('second_begin_ns',0);second_end=gate.get('second_deadline_ns',0)
    check(gate.get('code_verified') is True and gate.get('phase')==5 and gate.get('failure')==0,'round gate did not finish its diagnostic naturally')
    check(first>0 and first_end-first==350000000 and request>=first_end and second>=request and second_end-second==350000000,'invalid first/re-held350ms intervals')
    check(gate.get('advance_deadline_ns',0)-request==1500000000 and second<gate.get('advance_deadline_ns',0),'completion exceeded its1.5s bound')
    check(gate.get('contention')==0 and all(delta.get(k)==0 for k in ('patch_failures','restore_failures')),'gate/override failures')
    transfer_ids=gate.get('transfer_ids',[]);rehold_ids=gate.get('first_rehold_ids',[])
    check(len(transfer_ids)==len(rehold_ids)==3 and len(set(transfer_ids))==3 and len(set(rehold_ids))==3,'missing distinct per-branch transfer/rehold records')
    first_ammo=[];expected_ammo=[];transfers=[]
    for b in range(3):
        t=byid.get(transfer_ids[b]) if b<len(transfer_ids) else None
        h=byid.get(rehold_ids[b]) if b<len(rehold_ids) else None
        okay=t and t['kind']==2 and t['before']['branch']==b and t.get('transfer_path')==0 and request<=t['begin_ns']<=t['end_ns']<=second
        check(okay,f'branch{b}: missing owned ordinary transfer before acknowledgement')
        if not okay:continue
        old=[t['before']['loaded'],t['before']['reserve']];new=[t['after']['loaded'],t['after']['reserve']]
        first_ammo.append(old);expected_ammo.append(new);transfers.append(t)
        check(new==[old[0]+1,old[1]-1],f'branch{b}: requested transfer is not exactlyone conserved shell')
        check(h and h['kind']==0 and h['before']['branch']==b and h.get('hold_applied') is True and h.get('hold_restored') is True and t['end_ns']<=h['begin_ns']<=h['end_ns']<=second and [h['before']['loaded'],h['before']['reserve']]==new,f'branch{b}: first re-hold did not precede acknowledgment')
        during=[x for x in rows if x['kind']==2 and x['before']['branch']==b and request<=x['begin_ns']<second_end]
        check([x['id'] for x in during]==[t['id']],f'branch{b}: extra native shell before final release')
        resumed=[x for x in rows if x['kind']==2 and x['before']['branch']==b and x['begin_ns']>=second_end]
        check(bool(resumed),f'branch{b}: no ordinary native continuation after final release')
    check(len(first_ammo)==3 and first_ammo==[first_ammo[0]]*3 and expected_ammo==[expected_ammo[0]]*3,'native copies did not agree on requested ammo')
    ack=gate.get('acknowledgement')
    check(isinstance(ack,dict) and ack.get('request')==1 and ack.get('cycle')==1 and ack.get('diagnostic_only') is True and ack.get('server_invocation')==(transfer_ids[2] if len(transfer_ids)==3 else None) and ack.get('sample_sequence',0)>0,'missing exact diagnostic request/cycle/server acknowledgement')
    if isinstance(ack,dict) and transfers:
        owner=transfers[0]['before']
        check(all(ack.get(k)==owner.get(src) for k,src in [('actor','soldier'),('actor_generation','actor_generation'),('weapon','weapon'),('equip_generation','equip_generation'),('space','space')]),'acknowledgement owner differs from native request')
    held=[r for r in rows if r.get('hold_requested') or r.get('hold_applied')];counts=Counter(r['before']['branch'] for r in held)
    check(all(counts[b]>0 for b in range(3)) and delta.get('applied')==delta.get('restored')==[counts[b] for b in range(3)] and delta.get('original_calls')==len(held),'override counters disagree')
    for r in held:
        tag=f"record{r['id']}";b=r['before'];a=r.get('after');branch=b['branch']
        check(r['kind']==0 and r.get('hold_requested') is True and r.get('hold_applied') is True and r.get('hold_restored') is True and r.get('hold_unexpected_native_write') is False,tag+': override failed')
        check(a and all(b[k]==a[k] for k in ('current','next','timer','loaded','reserve')) and b['current']==11 and b['next']==12,tag+': held state advanced')
        original=r.get('hold_original_delta_bits');before=bytes(r['context_before']['bytes']);after=bytes(r['context_after']['bytes'])
        check(0<struct.unpack_from('<f',before,0x18)[0]<=.05 and struct.unpack_from('<I',before,0x18)[0]==original==struct.unpack_from('<I',after,0x18)[0] and r.get('hold_before_restore_bits')==0,tag+': four delta bytes were not exactly restored')
        if len(first_ammo)==3:
            ammo=[b['loaded'],b['reserve']]
            check((r['begin_ns']<first_end and r['end_ns']>=first and ammo==first_ammo[branch]) or (r['begin_ns']>=transfers[branch]['end_ns'] and r['begin_ns']<second_end and ammo==expected_ammo[branch]),tag+': override outside its owned round phase')
    for r in rows:
        if r['kind']!=0:continue
        if request<=r['begin_ns']<second_end:
            context=bytes(r['context_before']['bytes'])
            check(struct.unpack_from('<f',context,0x20)[0]==1 and 0<struct.unpack_from('<f',context,0x18)[0]<=.05,f"record{r['id']}: release/rehold timing was not bounded")
        if first<=r['begin_ns']<first_end or second<=r['begin_ns']<second_end:
            check(r.get('hold_applied') is True,f"record{r['id']}: an owned Update ran unheld inside the shared pause")
    check(not base['between_observed_calls_ammo_changes'] and not base['restore_round_reverts'],'unexplained ammo gap or restore reversal')
    check(all(t['single_conserved_round'] for t in base['transfers']),'non-conserved native transfer')
    return dict(schema='fvr.bc2.reload_round_audit.v1',passed=not errors,errors=errors,diagnostic=gate,held_records_per_branch=[counts[b] for b in range(3)],requested_before=first_ammo,requested_after=expected_ammo,transfers=base['transfers'],manual_reload_enabled=False,headset_verified=False)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();raw=a.trace.read_bytes()
    try:result=audit(json.loads(raw))
    except (KeyError,ValueError,TypeError,IndexError,struct.error) as exc:result=dict(schema='fvr.bc2.reload_round_audit.v1',passed=False,errors=['Malformed diagnostic: '+str(exc)])
    result['source']=dict(path=str(a.trace.resolve()),sha256=hashlib.sha256(raw).hexdigest());a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ('passed','errors')}));return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
