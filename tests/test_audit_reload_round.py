import copy,struct,unittest
from test_audit_reload_restore import boundary,record,restore,trace
from audit_reload_round import audit

def fixture():
    rows=[];first=1000000000;firstend=first+350000000;second=firstend+1000000;secondend=second+350000000;bits=struct.unpack('<I',struct.pack('<f',.005))[0]
    transfers=[];reholds=[]
    def held(b,when):
        h=record(len(rows)+1,0,when,b,b);context=bytearray(48);struct.pack_into('<I',context,0x18,bits);struct.pack_into('<f',context,0x20,1)
        h.update(context_before={'bytes':list(context)},context_after={'bytes':list(context)},hold_requested=True,hold_applied=True,hold_restored=True,hold_unexpected_native_write=False,hold_original_delta_bits=bits,hold_before_restore_bits=0);rows.append(h);return h['id']
    for branch in range(3):
        b=boundary(branch,2,8);b['timer']=.2
        if branch==2:b.update(wrapper_offset=0x10,server_player=0x30000,server_soldier=0x31000,server_item=0x32000)
        held(b,first+1000+branch*1000)
        a=copy.deepcopy(b);a.update(loaded=3,reserve=7)
        t=record(len(rows)+1,2,firstend+10000+branch*10000,b,a);t.update(caller=0x6e6831,transfer_path=0);rows.append(t);transfers.append(t['id'])
        reholds.append(held(a,firstend+12000+branch*10000));held(a,second+10000+branch*10000)
        z=copy.deepcopy(a);z.update(loaded=4,reserve=6);t=record(len(rows)+1,2,secondend+10000+branch*10000,a,z);t.update(caller=0x6e6831,transfer_path=0);rows.append(t)
        if branch==1:rows.append(restore(len(rows)+1,secondend+500000,z,z))
    result=trace(rows);f=result['gameplay']['reload_flow'];f.update(observation_only=False,native_state_writes=True,server_binding_verified=True,server_matched=[3,0,2,0],server_read_misses=0)
    f['diagnostic_hold']=dict(enabled=False,patch_failures=0,restore_failures=0,applied=[3]*3,restored=[3]*3,original_calls=9)
    owner=rows[0]['before'];ack=dict(request=1,cycle=1,sample_sequence=50,server_invocation=transfers[2],diagnostic_only=True,actor=owner['soldier'],actor_generation=owner['actor_generation'],weapon=owner['weapon'],equip_generation=owner['equip_generation'],space=owner['space'])
    f['diagnostic_round']=dict(enabled=True,code_verified=True,phase=5,failure=0,contention=0,first_begin_ns=first,first_deadline_ns=firstend,request_ns=firstend,advance_deadline_ns=firstend+1500000000,second_begin_ns=second,second_deadline_ns=secondend,transfer_ids=transfers,first_rehold_ids=reholds,acknowledgement=ack)
    return result

class RoundAuditTests(unittest.TestCase):
    def test_one_native_round_reheld_and_natural_continuation(self):
        r=audit(fixture());self.assertTrue(r['passed'],r['errors']);self.assertEqual(r['requested_after'],[[3,7]]*3)
    def test_exact_ack_and_actual_reholds(self):
        for key,value in [('cycle',2),('request',2),('server_invocation',1),('space',999)]:
            t=fixture();t['gameplay']['reload_flow']['diagnostic_round']['acknowledgement'][key]=value;self.assertFalse(audit(t)['passed'],key)
        t=fixture();f=t['gameplay']['reload_flow'];f['diagnostic_round']['first_rehold_ids'][2]=f['diagnostic_round']['transfer_ids'][2];self.assertFalse(audit(t)['passed'])
    def test_wrong_hold_bytes_or_phase_fail(self):
        for change in ('delta','timer','phase','unheld'):
            t=fixture();f=t['gameplay']['reload_flow'];row=f['records'][0]
            if change=='delta':row['context_after']['bytes'][24]^=1
            if change=='timer':row['after']['timer']=.1
            if change=='phase':f['diagnostic_round']['phase']=6
            if change=='unheld':row['hold_applied']=False
            self.assertFalse(audit(t)['passed'],change)
if __name__=='__main__':unittest.main()
