"""Check completed private remove/return or remove/refill native evidence.

No native calls or process access. This correlates the diagnostic receipts with
independent completed Update records; it does not grant player dispatch.
"""
import argparse
import hashlib
import json
from pathlib import Path


def audit(document, expected_reload_type=None, require_resource_ledger=False, require_handoff=False, require_inventory_binding=False):
    reasons=[]
    def need(ok, reason):
        if not ok: reasons.append(reason)
    flow=document.get('gameplay',{}).get('reload_flow',{})
    p=flow.get('ammo_move_probe',{})
    need(document.get('hooks_disabled') is True and flow.get('drained') is True,'hooks_not_drained')
    need(p.get('diagnostic_only') is True and p.get('drained') is True,'private_probe_missing')
    need(p.get('phase')==5 and p.get('failure')==0,'probe_incomplete')
    need(p.get('calls')==p.get('exact_calls')==2,'native_call_count')
    need(p.get('empty_mask')==p.get('returned_mask')==7,'cohort_incomplete')
    metrics=[]
    try:
        original,reserve=p['original_loaded'],p['original_reserve']
        expected=p.get('expected_loaded',original),p.get('expected_reserve',reserve)
        refill=bool(p.get('refill',False))
        if require_resource_ledger or require_handoff or require_inventory_binding:
            ledger=p.get('resource_ledger',{})
            need(ledger.get('private_magazine_scope') is True,'resource_scope_missing')
            need(ledger.get('phase')==1 and ledger.get('completed')==2 and ledger.get('failure')==0 and
                 ledger.get('pending')==0,'resource_commands_incomplete')
            need(ledger.get('well_empty')==0 and ledger.get('resource_state')==(2 if refill else 3),
                 'resource_custody_mismatch')
            need((ledger.get('loaded'),ledger.get('reserve'))==expected and ledger.get('original_rounds')==original,
                 'resource_accounting_mismatch')
            if require_handoff:
                need(ledger.get('completion_retained')==ledger.get('completion_drained')==
                     ledger.get('completion_deferred')==2,'resource_completion_handoff_incomplete')
        need(all(type(v) is int and v>=0 for v in (original,reserve,*expected)) and original>0,'invalid_counts')
        need(sum(expected)==reserve if refill else expected==(original,reserve),'ammunition_conservation')
        firings=p['firing'];ops=p['operations']
        need(len(firings)==3 and len(set(firings))==3 and all(type(v) is int and v>=65536 for v in firings),'firing_identity')
        need(len(ops)==2,'operation_receipts')
        rows=flow['records']
        owners=[]
        # Client Update records carry the local owner and their firing address.
        # Server linkage is populated only on the server branch.
        keys=('player','soldier','weak','weapon','actor_generation','equip_generation','space')
        def own(row, branch):
            b,a=row.get('before'),row.get('after')
            return (row.get('kind')==0 and row.get('depth')==1 and row.get('finished') is True and
                    row.get('identity_retained') is True and b and a and
                    b['branch']==a['branch']==branch and b['firing']==a['firing']==firings[branch] and
                    all(b[k]==a[k] and type(a[k]) is int and a[k]>0 for k in keys))
        for n,op in enumerate(ops):
            need(op['called']==op['exact']==op['context_unchanged']==1,'native_postcondition')
            need(op['firing']==firings[2] and op['delta']==(-original if n==0 else expected[0]),'server_operation')
            matching=[r for r in rows if r.get('native_invocation')==op['invocation'] and r.get('kind')==0]
            need(len(matching)==1,'operation_update_missing_or_duplicate')
            if len(matching)!=1:continue
            r=matching[0];need(own(r,2),'operation_not_owned_server_update')
            need(all(type(r['after'].get(k)) is int and r['after'][k]>=65536 and r['before'][k]==r['after'][k]
                     for k in ('server_player','server_soldier','server_item')),'server_linkage_missing')
            need(r['begin_ns']<=op['begin_ns']<=op['end_ns']<=r['end_ns'],'operation_not_inside_update')
            need(op['end_ns']==p['removed_ns' if n==0 else 'returned_ns'],'operation_timestamp')
            need((r['before']['loaded'],r['before']['reserve'])==((original,reserve) if n==0 else (0,reserve)),'operation_before_counts')
            need((r['after']['loaded'],r['after']['reserve'])==((0,reserve) if n==0 else expected),'operation_after_counts')
            need(not r.get('hold_applied') and r['context_before']['input_flags']==0,'nonneutral_or_animation_hold')
            owners.append(tuple(r['after'][k] for k in keys))
        need(len(owners)==2 and owners[0]==owners[1],'operation_owner_changed')
        if require_inventory_binding:
            ledger=p.get('resource_ledger',{})
            need(ledger.get('inventory_rebinds')==1 and type(ledger.get('weapon_generation')) is int and
                 ledger['weapon_generation']>0 and type(ledger.get('equip_generation')) is int and
                 ledger['equip_generation']>0 and owners and ledger['equip_generation']!=owners[0][5],
                 'inventory_lifetime_rebind_missing')
        if expected_reload_type is not None:
            samples=document.get('gameplay',{}).get('reload_observer',{}).get('samples',[])
            need(any(s.get('reload_type')==expected_reload_type and owners and s.get('weapon')==owners[0][3] and
                     [b.get('address') for b in s.get('branches',[])]==firings[:2] for s in samples),
                 'expected_reload_family_not_observed')
        need(0<p['removed_ns']<p['returned_ns'],'operation_order')
        for branch in range(3):
            own_rows=[r for r in rows if own(r,branch) and owners and tuple(r['after'][k] for k in keys)==owners[0]]
            observed=[]
            for label,start,end,counts in [('empty',p['removed_ns'],ops[1]['begin_ns'],(0,reserve)),
                                           ('returned',p['returned_ns'],p['returned_ns']+2_000_000_000,expected)]:
                selected=[r for r in own_rows if start<=r['end_ns']<=end and
                          (r['after']['loaded'],r['after']['reserve'])==counts and
                          r['after']['current']==r['after']['next']==2 and r['after']['timer']==0]
                need(bool(selected),f'{label}_own_update_missing_{branch}')
                if selected:
                    first=min(selected,key=lambda r:r['end_ns'])
                    observed.append((first['end_ns']-start)/1e6)
                    need(p[label+'_invocations'][branch] in [r['native_invocation'] for r in selected],f'{label}_receipt_not_correlated_{branch}')
            metrics.append(dict(branch=branch,first_empty_ms=observed[0] if observed else None,
                                first_returned_ms=observed[1] if len(observed)>1 else None))
    except (KeyError,TypeError,ValueError,IndexError) as e:
        reasons.append('malformed_evidence:'+str(e))
    return dict(schema=1,status='bounded_native_verified' if not reasons else 'inconclusive',reasons=reasons,branches=metrics,
                expected_reload_type=expected_reload_type,resource_ledger_required=require_resource_ledger,
                completion_handoff_required=require_handoff,
                inventory_binding_required=require_inventory_binding,
                player_dispatch_verified=False,headset_verified=False,
                limits=['One diagnostic owner only; no grip, holster, persistent magazine inventory or weapon-family coverage claim.',
                        'Native ABI and code admission remain runtime proofs; this audit correlates serialized observations.'])


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('trace',type=Path);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--reload-type',type=int,choices=(0,1));ap.add_argument('--resource-ledger',action='store_true')
    ap.add_argument('--completion-handoff',action='store_true');ap.add_argument('--inventory-binding',action='store_true');a=ap.parse_args()
    if a.output.exists():ap.error('Preserve existing evidence')
    raw=a.trace.read_bytes();r=audit(json.loads(raw.decode('utf-8-sig')),a.reload_type,a.resource_ledger,a.completion_handoff,a.inventory_binding)
    r['source']={'path':str(a.trace.resolve()),'sha256':hashlib.sha256(raw).hexdigest()}
    a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r));return r['status']!='bounded_native_verified'

if __name__=='__main__':raise SystemExit(main())
