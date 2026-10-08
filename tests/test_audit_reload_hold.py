import copy
import struct
import unittest
from test_audit_reload_restore import boundary,record,restore,trace
from audit_reload_hold import audit
from audit_reload_restore import audit as strict_observer


def fixture():
    rows=[];begin=1000000000;deadline=begin+350000000;bits=struct.unpack('<I',struct.pack('<f',.005))[0]
    for branch in range(3):
        b=boundary(branch,2,8);b['timer']=.2
        if branch==2:b.update(wrapper_offset=0x10,server_player=0x30000,server_soldier=0x31000,server_item=0x32000)
        h=record(len(rows)+1,0,begin+branch*1000,b,b);context=bytearray(48);struct.pack_into('<I',context,0x18,bits)
        h.update(context_before={'bytes':list(context)},context_after={'bytes':list(context)},hold_requested=True,hold_applied=True,hold_restored=True,hold_unexpected_native_write=False,hold_original_delta_bits=bits,hold_before_restore_bits=0);rows.append(h)
        a=copy.deepcopy(b);a.update(loaded=3,reserve=7)
        t=record(len(rows)+1,2,deadline+1000+branch*1000,b,a);t.update(caller=0x6e6831,transfer_path=0);rows.append(t)
        if branch==1:rows.append(restore(len(rows)+1,deadline+10000,a,a))
    result=trace(rows);flow=result['gameplay']['reload_flow'];flow.update(observation_only=False,native_state_writes=True,server_binding_verified=True,server_matched=[1,0,1,0],server_read_misses=0)
    flow['diagnostic_hold']=dict(enabled=True,code_verified=True,phase=3,reason=1,begin_ns=begin,deadline_ns=deadline,duration_ns=350000000,contention=0,patch_failures=0,restore_failures=0,applied=[1,1,1],restored=[1,1,1],original_calls=3,loaded=2,reserve=8,firing=[0x14000,0x15000,0x16000])
    return result


class HoldAuditTests(unittest.TestCase):
    def test_three_branch_wait_and_native_resumption(self):
        t=fixture();r=audit(t);self.assertTrue(r['passed'],r['errors']);self.assertEqual(r['final_ammo_by_branch'],[[3,7]]*3);self.assertFalse(strict_observer(t)['valid_observation'])
    def test_no_silent_partial_hold_or_restore_failure(self):
        for key,value in [('phase',4),('contention',1),('restore_failures',1),('code_verified',False),('applied',[1,1,0])]:
            t=fixture();t['gameplay']['reload_flow']['diagnostic_hold'][key]=value;self.assertFalse(audit(t)['passed'],key)
    def test_exact_context_and_reload_state_invariant(self):
        for change in ('delta','zero','timer','applied'):
            t=fixture();r=t['gameplay']['reload_flow']['records'][0]
            if change=='delta':r['context_after']['bytes'][0x18]^=1
            if change=='zero':r['hold_before_restore_bits']=1
            if change=='timer':r['after']['timer']=.19
            if change=='applied':r['hold_applied']=False
            self.assertFalse(audit(t)['passed'],change)
    def test_native_transfer_must_wait_for_release(self):
        t=fixture();t['gameplay']['reload_flow']['records'][1]['begin_ns']=1000005000;self.assertFalse(audit(t)['passed'])
if __name__=='__main__':unittest.main()
