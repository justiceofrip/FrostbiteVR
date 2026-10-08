"""Offline bolt-boundary experiment using original BC2 x86 instructions.

Uses exact extracted configuration fields in a synthetic firing object with
empty listener lists. No game process, native ownership or chamber is emulated.
No executable or configuration bytes are written to the report.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct

from probe_bc2_reload_machine import Image, Emulator, discover, UPDATE, STEP

EXPECTED_EXE='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
FUNCTIONS={
    'Update':(UPDATE,397,'0babdb25cdcc8f494a544d27cdf628c67f495215872b1ff5fa90f62fd5ca426e'),
    'Step':(STEP,1732,'122c98fefa4503c8c98688477f9cf5a1bfbbccb47e84ce6b303898c199ad908d')}

def bind(im):
    if hashlib.sha256(im.data).hexdigest()!=EXPECTED_EXE:raise ValueError('Unreviewed executable')
    result=discover(im)
    result['cycle']={}
    for name,(pattern,size,wanted) in FUNCTIONS.items():
        rva=im.find(pattern);actual=hashlib.sha256(im.read(rva,size)).hexdigest()
        if actual!=wanted:raise ValueError(name+' full function differs')
        result['cycle'][name]={'rva':rva,'size':size,'sha256':actual}
    return result

def value(row,key):return row['fields'][key]['value']

def setup(im,binding,row,state=7,loaded=None):
    em=Emulator(im,binding)
    primary=em.ammo+0x800;firing=em.ammo+0x600;context=em.ammo+0x400
    cap=value(row,'Ammo.MagazineCapacity')
    if type(cap)!=int or not 0<cap<=1000000:raise ValueError('Invalid authored capacity')
    if loaded is None:loaded=cap-1
    raw=bytearray(0xb0)
    for at,v in ((0,binding['primary_vtable']),(4,binding['secondary_vtable']),
                 (8,firing),(12,primary+0x170),(0x3c,state),(0x40,state-1),(0x44,state),
                 (0x7c,loaded),(0x80,20),(0x84,1),(0x98,cap)):
        struct.pack_into('<I',raw,at,v)
    for at in (0x74,0x78,0x9c):struct.pack_into('<f',raw,at,1.)
    raw[0xa4]=1
    config=bytearray(0x194)
    # Exact native reflected offsets already used by ReadReloadState.
    for offset,key in ((0xc,'FireLogic.PostReloadSequenceTime'),(0x10,'FireLogic.ReloadThreshold'),
                       (0x14,'FireLogic.ReloadDelay'),(0x18,'FireLogic.ReloadTime'),
                       (0x50,'FireLogic.BoltAction.BoltActionTime'),(0x54,'FireLogic.BoltAction.BoltActionDelay'),
                       (0x84,'FireLogic.RateOfFire')):
        struct.pack_into('<f',config,offset,value(row,key))
    for offset,v in ((0x20,1),(0x24,1),(0x4c,8),(0x8c,29),(0x180,value(row,'Ammo.NumberOfMagazines')),(0x184,cap)):
        struct.pack_into('<i',config,offset,v)
    config[0x58]=value(row,'FireLogic.BoltAction.HoldBoltActionUntilZoomRelease')
    config[0x59]=value(row,'FireLogic.BoltAction.HoldBoltActionUntilFireRelease')
    data=bytearray(0x48);struct.pack_into('<I',data,0x40,primary)
    ctx=bytearray(0x30);struct.pack_into('<f',ctx,0x20,1.);ctx[0x24]=1
    em.vm.mem_write(em.object,bytes(raw));em.vm.mem_write(primary,bytes(config));em.vm.mem_write(firing,bytes(data));em.vm.mem_write(context,bytes(ctx))
    return em,context,ctx,primary,bytes(config)

def snapshot(em):
    raw=bytes(em.vm.mem_read(em.object,0xb0))
    return dict(state=struct.unpack_from('<I',raw,0x3c)[0],previous=struct.unpack_from('<I',raw,0x40)[0],
                next=struct.unpack_from('<I',raw,0x44)[0],timer=struct.unpack_from('<f',raw,0x50)[0],
                loaded=struct.unpack_from('<i',raw,0x7c)[0],reserve=struct.unpack_from('<i',raw,0x80)[0])

def run_one(im,binding,row,dt,held=False,freeze=False):
    em,context,ctx,primary,config=setup(im,binding,row)
    initial=snapshot(em);transitions=[];held_samples=0;tail_seen=False;patches=restores=0
    for frame in range(300):
        # A synthetic real trigger release after 0.5s; optional native Update
        # dt override exercises the candidate state8 tail, not SPAS state7.
        before=snapshot(em)
        do_hold=freeze and before['state']==8 and before['next']==1 and frame<60
        if do_hold:tail_seen=True
        struct.pack_into('<f',ctx,0x18,dt)
        struct.pack_into('<I',ctx,0x2c,1 if held and frame<30 else 0)
        em.vm.mem_write(context,bytes(ctx))
        if do_hold:
            em.vm.mem_write(context+0x18,bytes(4));patches+=1
        em.invoke(binding['cycle']['Update']['rva'],em.object,context,0)
        if do_hold:
            if em.vm.mem_read(context+0x18,4)!=bytes(4):raise ValueError('Original Update changed scoped delta')
            em.vm.mem_write(context+0x18,bytes(ctx[0x18:0x1c]));restores+=1
        if bytes(em.vm.mem_read(context,0x30))!=ctx:raise ValueError('Context not exactly restored')
        if bytes(em.vm.mem_read(primary,len(config)))!=config:raise ValueError('Native configuration changed')
        allowed=lambda at,n:em.object<=at and at+n<=em.object+0xb0 or (at,n)==(context+0x14,4)
        if any(not allowed(at,n) for at,n in em.writes):raise ValueError('Native write outside bounded object/context')
        after=snapshot(em)
        if after['loaded']!=initial['loaded'] or after['reserve']!=initial['reserve']:raise ValueError('Cycle changed ammunition')
        if do_hold and any(after[k]!=before[k] for k in ('state','previous','next','timer')):raise ValueError('Tail hold failed')
        if held and frame<30:
            if after['state']!=7 or after['next']!=7:raise ValueError('Authored fire-release hold not honored')
            held_samples+=1
        state={k:after[k] for k in ('state','previous','next')}
        if not transitions or any(transitions[-1][k]!=state[k] for k in state):transitions.append(dict(frame=frame,**after))
    if (after['state'],after['next'])!=(2,2):raise ValueError('Native cycle did not reach idle input processing')
    if not any(t['state']==8 and t['next']==1 and t['previous']==7 and t['timer']>0 for t in transitions):raise ValueError('No observed bolt tail')
    if any(t['state']==7 and t['next']==8 and t['timer']>0 for t in transitions):raise ValueError('Unexpected SPAS-style positive pre-cycle window')
    if freeze and (not tail_seen or patches==0 or patches!=restores):raise ValueError('Tail patch not exercised/restored')
    return dict(asset=row['native_name'],configuration=row['resource'],configuration_sha256=row['resource_sha256'],
                instance_guid=row['instance_guid'],firing_resource=row['firing_resource'],firing_sha256=row['firing_sha256'],
                delta=dt,hold_trigger=held,freeze_tail=freeze,held_samples=held_samples,patches=patches,restores=restores,
                bolt_delay=value(row,'FireLogic.BoltAction.BoltActionDelay'),bolt_time=value(row,'FireLogic.BoltAction.BoltActionTime'),
                transitions=transitions,ammunition_unchanged=True,original_context_restored=True)

def shot_boundary_limit(im,binding,row,rounds):
    """Negative control: empty effect listeners cannot stand in for a shot."""
    em,context,ctx,primary,config=setup(im,binding,row,state=6,loaded=rounds)
    struct.pack_into('<f',ctx,0x18,1/60);struct.pack_into('<I',ctx,0x2c,1)
    em.vm.mem_write(context,bytes(ctx));em.invoke(binding['cycle']['Update']['rva'],em.object,context,0)
    after=snapshot(em)
    if after['loaded']!=rounds or after['next']!=1:raise ValueError('Empty-listener shot negative control differs')
    if bytes(em.vm.mem_read(primary,len(config)))!=config:raise ValueError('Negative control mutated configuration')
    return dict(asset=row['native_name'],configuration=row['resource'],initial_loaded=rounds,after=after,
                shot_receipt_established=False,reason='Empty listener mock did not consume ammunition; state7 entry remains experiment setup.')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True);parser.add_argument('--configurations',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    if args.output.exists():parser.error('Output exists; preserve earlier evidence')
    doc=json.loads(args.configurations.read_text(encoding='utf-8-sig'))
    rows=[r for r in doc['configurations']['resolved_weapons'] if r['weapon_class']=='wcSniper' and value(r,'FireLogic.FireLogicType')=='fltSingleFireWithBoltAction']
    if not rows:raise ValueError('No exact authored bolt configurations')
    for row in rows:
        if value(row,'FireLogic.ReloadType')!='rtMagazine':raise ValueError('Unexpected feed semantics')
    im=Image(args.exe);binding=bind(im);cases=[];negative=[]
    for row in rows:
        for dt in (1/60,.0596221,.1):
            for held,freeze in ((False,False),(True,False),(False,True)):
                cases.append(run_one(im,binding,row,dt,held,freeze))
        for rounds in (1,value(row,'Ammo.MagazineCapacity')):negative.append(shot_boundary_limit(im,binding,row,rounds))
    result=dict(schema='fvr.bc2.bolt_cycle_machine_emulation.v1',passed=True,
                executable_sha256=EXPECTED_EXE,configuration_input_sha256=hashlib.sha256(args.configurations.read_bytes()).hexdigest(),
                native_machine_code=True,synthetic_native_object=True,authored_fields=True,effect_listener_boundary='empty mock',
                native_owner_and_scheduler_emulated=False,chamber_knowledge_proved=False,game_process_opened=False,
                native_admitted=False,gameplay_verified=False,headset_verified=False,functions=binding['cycle'],cases=cases,
                shot_boundary_negative_controls=negative,
                candidate_native_boundary=dict(current=8,previous=7,next=1,positive_timer=True,
                    native_emulated=True,native_admitted=False,
                    missing=['Actual shot receipt and server/client owning-Update cohorts',
                             'Exact bound sniper configuration and native safe-context/lease admission',
                             'Presentation part/hand/closed-pose and native animation-tail correlation',
                             'Conserved semantic chamber/fire model; no separate native chamber count proved']))
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(passed=True,configurations=len(rows),cases=len(cases),native_admitted=False,output=str(args.output))))
if __name__=='__main__':main()
