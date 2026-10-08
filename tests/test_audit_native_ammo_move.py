import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_native_ammo_move import audit


def fixture(refill=False):
    owner=dict(player=65536,soldier=65537,weak=65538,weapon=65539,actor_generation=1,equip_generation=2,space=3)
    firings=[70000,71000,72000]
    rows=[]
    def row(ident,branch,begin,end,before,after):
        def state(counts):
            return dict(**owner,firing=firings[branch],branch=branch,loaded=counts[0],reserve=counts[1],current=2,next=2,timer=0,
                        server_player=80000 if branch==2 else 0,server_soldier=81000 if branch==2 else 0,server_item=82000 if branch==2 else 0)
        return dict(kind=0,depth=1,native_invocation=ident,begin_ns=begin,end_ns=end,finished=True,identity_retained=True,
                    before=state(before),after=state(after),hold_applied=False,context_before=dict(input_flags=0))
    expected=(1,22) if refill else (8,23)
    rows.append(row(1,2,90,120,(8,23),(0,23)))
    rows.extend(row(2+b,b,140,150,(0,23),(0,23)) for b in range(2))
    rows.append(row(4,2,400,420,(0,23),expected))
    rows.extend(row(5+b,b,440,450,expected,expected) for b in range(2))
    operations=[dict(invocation=ident,firing=firings[2],begin_ns=begin,end_ns=end,delta=delta,called=1,exact=1,context_unchanged=1)
                for ident,begin,end,delta in [(1,100,110,-8),(4,405,410,expected[0])]]
    probe=dict(diagnostic_only=True,drained=True,phase=5,failure=0,calls=2,exact_calls=2,empty_mask=7,returned_mask=7,
               firing=firings,original_loaded=8,original_reserve=23,refill=refill,expected_loaded=expected[0],expected_reserve=expected[1],
               removed_ns=110,returned_ns=410,empty_invocations=[2,3,1],returned_invocations=[5,6,4],operations=operations)
    return dict(hooks_disabled=True,gameplay=dict(reload_flow=dict(drained=True,ammo_move_probe=probe,records=rows)))


class NativeAmmoAuditTests(unittest.TestCase):
    def test_both_native_operations(self):
        for refill in (False,True):self.assertEqual(audit(fixture(refill))['status'],'bounded_native_verified')

    def test_reject_false_or_missing_evidence(self):
        for case in range(12):
            with self.subTest(case=case):
                d=fixture();f=d['gameplay']['reload_flow'];p=f['ammo_move_probe'];r=f['records']
                if case==0:d['hooks_disabled']=False
                if case==1:p['phase']=0
                if case==2:p['operations'][0]['exact']=0
                if case==3:r[0]['depth']=2
                if case==4:r[0]['context_before']['input_flags']=4
                if case==5:r[1]['after']['loaded']=8
                if case==6:r[2]['identity_retained']=False
                if case==7:r[3]['after']['reserve']=24
                if case==8:r[3]['after']['weapon']+=1
                if case==9:p['empty_invocations'][0]=999
                if case==10:r.append(copy.deepcopy(r[0]))
                if case==11:p['refill']=True
                self.assertEqual(audit(d)['status'],'inconclusive')

    def test_empty_report_is_not_success(self):
        self.assertEqual(audit({})['status'],'inconclusive')

    def test_live_resource_ledger_is_separate_required_evidence(self):
        d=fixture();self.assertEqual(audit(d,require_resource_ledger=True)['status'],'inconclusive')
        p=d['gameplay']['reload_flow']['ammo_move_probe']
        p['resource_ledger']=dict(private_magazine_scope=True,phase=1,completed=2,failure=0,pending=0,
                                  well_empty=0,resource_state=3,loaded=8,reserve=23,original_rounds=8)
        self.assertEqual(audit(d,require_resource_ledger=True)['status'],'bounded_native_verified')
        self.assertEqual(audit(d,require_handoff=True)['status'],'inconclusive')
        p['resource_ledger'].update(completion_retained=2,completion_drained=2,completion_deferred=2)
        self.assertEqual(audit(d,require_handoff=True)['status'],'bounded_native_verified')
        self.assertEqual(audit(d,require_inventory_binding=True)['status'],'inconclusive')
        p['resource_ledger'].update(inventory_rebinds=1,weapon_generation=1,equip_generation=4)
        self.assertEqual(audit(d,require_inventory_binding=True)['status'],'bounded_native_verified')
        for field,value in [('inventory_rebinds',0),('weapon_generation',0),('equip_generation',2)]:
            changed=copy.deepcopy(d);changed['gameplay']['reload_flow']['ammo_move_probe']['resource_ledger'][field]=value
            self.assertEqual(audit(changed,require_inventory_binding=True)['status'],'inconclusive')
        for field in ('completion_retained','completion_drained','completion_deferred'):
            changed=copy.deepcopy(d);changed['gameplay']['reload_flow']['ammo_move_probe']['resource_ledger'][field]=1
            self.assertEqual(audit(changed,require_handoff=True)['status'],'inconclusive')
        for field in ('phase','completed','failure','pending','well_empty','resource_state','loaded','reserve','original_rounds'):
            changed=copy.deepcopy(d);changed['gameplay']['reload_flow']['ammo_move_probe']['resource_ledger'][field]+=1
            self.assertEqual(audit(changed,require_resource_ledger=True)['status'],'inconclusive',field)

    def test_intended_family_requires_observed_configuration(self):
        d=fixture();s=dict(reload_type=0,weapon=65539,branches=[dict(address=70000),dict(address=71000)])
        d['gameplay']['reload_observer']=dict(samples=[s])
        self.assertEqual(audit(d,0)['status'],'bounded_native_verified')
        self.assertEqual(audit(d,1)['status'],'inconclusive')
        s['weapon']+=1
        self.assertEqual(audit(d,0)['status'],'inconclusive')

if __name__=='__main__':unittest.main()
