"""Execute the installed BC2 reload state machine in an isolated x86 emulator.

Consumes a PRIVATE read-only configuration capture. Effects/listener lists are
explicitly mocked as empty. Engine scheduling, ownership, prediction, rendering,
controller policy and the headset are not emulated or verified by this tool.
No process is opened and no executable/configuration bytes are exported.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct

from probe_bc2_magazine_adjustment import Image,Emulator,discover

UPDATE='83 EC 08 56 8B F1 D9 46 48 57 8B 7C 24 14 D8 67 18 D9 5C 24 14 D9 EE D9'
STEP='F3 0F 10 44 24 08 53 8B 5C 24 08 55 56 8B F1 8B 46 08 8B 68 40 F3 0F 11'


def run_case(im,binding,source,mode,hitch,manual=False,initial_state=2,initial_next=2,initial_timer=0.,
             detached_rounds=0,return_frame=None,reserve_rounds=60):
    import unicorn
    em=Emulator(im,binding)
    update,step=im.find(UPDATE),im.find(STEP)
    mapped=set()
    for region in source['regions']:
        data=bytes.fromhex(region['bytes']);at=region['address']
        if not 0x10000<=at<=0xffffffff-len(data) or not 0<len(data)<=4096:
            raise ValueError('Invalid private fixture memory bounds')
        for page in range(at&~4095,(at+len(data)+4095)&~4095,4096):
            if page not in mapped:em.vm.mem_map(page,4096);mapped.add(page)
        em.vm.mem_write(at,data)
    raw=bytearray.fromhex(source['branches'][0]['raw_hex'])
    if len(raw)!=0xb0:raise ValueError('Wrong firing object size')
    for at,value in ((0x14,0),(0x18,0),(0x1c,0),(0x3c,initial_state),(0x40,2),(0x44,initial_next),(0x7c,detached_rounds),(0x80,reserve_rounds)):
        struct.pack_into('<I',raw,at,value)
    for at in (0x48,0x50,0x54,0x58,0x8c,0x90):struct.pack_into('<f',raw,at,0.)
    struct.pack_into('<f',raw,0x50,initial_timer)
    em.vm.mem_write(em.object,bytes(raw))
    adjustments=[]
    def adjust(delta,at_frame):
        before=bytes(em.vm.mem_read(em.object,0xb0))
        loaded=struct.unpack_from('<i',before,0x7c)[0]
        em.invoke(binding['rva'],em.object+4,delta)
        expected=bytearray(before);struct.pack_into('<i',expected,0x7c,loaded+delta)
        if em.writes!=[(em.object+0x7c,4)] or bytes(em.vm.mem_read(em.object,0xb0))!=expected:
            raise ValueError('Native adjustment changed unrelated state or clamped rounds')
        adjustments.append({'frame':at_frame,'delta':delta,'loaded':loaded+delta,'reserve':reserve_rounds})
    if detached_rounds:adjust(-detached_rounds,-1)
    context=em.ammo+0x400
    ctx=bytearray(0x30);struct.pack_into('<f',ctx,0x20,1.);ctx[0x24]=1
    pending=[];patches=0;restores=0;step_calls=0;observed_deltas=[];frame=0;zero_input_steps=0

    def intercept(vm,pc,size,user):
        nonlocal patches,restores,step_calls,zero_input_steps
        sp=vm.reg_read(em.x86.UC_X86_REG_ESP)
        if pending and (pc,sp)==pending[-1][:2]:
            _,_,address,before=pending.pop()
            current=bytes(vm.mem_read(address,0x30))
            expected=bytearray(before);expected[0x28]=1
            if current!=expected:raise ValueError('Original Step altered the scoped context')
            vm.mem_write(address+0x28,b'\0');restores+=1
        if pc!=im.base+step:return
        step_calls+=1
        self=vm.reg_read(em.x86.UC_X86_REG_ECX)
        ret,argument,delta=struct.unpack('<IIf',vm.mem_read(sp,12))
        if self!=em.object or argument!=context or ret!=im.base+update+0x168:
            raise ValueError('Unexpected native Step invocation boundary')
        current,next_=struct.unpack('<II',vm.mem_read(self+0x3c,4)+vm.mem_read(self+0x44,4))
        if current==next_==2 and delta==0:zero_input_steps+=1
        before=bytes(vm.mem_read(context,0x30));source_delta=struct.unpack_from('<f',before,0x18)[0]
        if len(observed_deltas)<8:observed_deltas.append(delta)
        limit=struct.unpack('<f',struct.pack('<f',.05 if mode=='legacy' else .1))[0]
        armed=manual and 180<=frame<186
        # This interceptor models only the existing scoped-byte mechanism and
        # its timing admission. Native owner/capability policy is tested in C++.
        if mode!='stock' and current==next_==2 and 0<delta<=limit and delta<=source_delta<=limit and not armed:
            if pending or before[0x28]!=0:raise ValueError('Unexpected nested/already inhibited Step')
            pending.append((ret,sp+12,context,before));vm.mem_write(context+0x28,b'\1');patches+=1

    em.vm.hook_add(unicorn.UC_HOOK_CODE,intercept)
    transitions=[]
    for frame in range(420):
        if return_frame==frame:adjust(detached_rounds,frame)
        dt=hitch if frame==0 else 1/60
        struct.pack_into('<f',ctx,0x18,dt)
        struct.pack_into('<I',ctx,0x2c,4 if manual and 180<=frame<186 else 0)
        em.vm.mem_write(context,bytes(ctx))
        em.invoke(update,em.object,context,0)
        if pending or bytes(em.vm.mem_read(context,0x30))!=ctx:
            raise ValueError('Context not exactly restored after original Update')
        # Native Update also stores its zero accumulator at context+0x14.
        # The complete context equality check above still applies.
        if any(not(em.object<=at and at+size<=em.object+0xb0) and (at,size)!=(context+0x14,4) for at,size in em.writes):
            raise ValueError(f'Native Update writes: {[(hex(at),size) for at,size in em.writes if not(em.object<=at and at+size<=em.object+0xb0)]}, frame={frame}, mode={mode}')
        after=bytes(em.vm.mem_read(em.object,0xb0))
        current,next_=struct.unpack_from('<I',after,0x3c)[0],struct.unpack_from('<I',after,0x44)[0]
        loaded,reserve=struct.unpack_from('<ii',after,0x7c)
        state={'frame':frame,'state':current,'next':next_,'loaded':loaded,'reserve':reserve}
        if not transitions or any(transitions[-1][k]!=state[k] for k in ('state','next','loaded','reserve')):
            transitions.append(state)
    return {'asset':source['weapon']['asset_name'],'mode':mode,'first_delta':hitch,'manual_pulse':manual,
            'transitions':transitions,'step_calls':step_calls,'patches':patches,'restores':restores,
            'original_context_restored':True,'observed_step_deltas':observed_deltas,
            'final_loaded':loaded,'final_reserve':reserve,'zero_input_steps':zero_input_steps,
            'initial_state':initial_state,'initial_next':initial_next,'initial_timer':initial_timer,
            'adjustments':adjustments}


def run(im,fixture):
    if hashlib.sha256(im.data).hexdigest()!=fixture['exe_sha256'] or fixture.get('read_only') is not True:
        raise ValueError('Private fixture provenance differs')
    binding=discover(im);rows=[]
    for source in fixture['weapons']:
        if 'weapon' not in source:continue
        asset=source['weapon']['asset_name']
        if asset not in ('XM8_sp_s','SPAS12_sp','40mmgl'):continue
        # The underbarrel remains stock automatic: manual inhibition is neither
        # proposed nor enabled for it by this experiment.
        modes=('stock',) if asset=='40mmgl' else ('stock','legacy','bounded')
        for mode in modes:
            for hitch in (1/60,.0596221,.1):
                row=run_case(im,binding,source,mode,hitch)
                expected_empty=mode=='bounded' or mode=='legacy' and hitch<=.05
                if (row['final_loaded']==0)!=expected_empty or row['patches']!=row['restores']:
                    raise ValueError(f'Native reload outcome differs: {row}')
                rows.append(row)
        if asset!='40mmgl':
            row=run_case(im,binding,source,'bounded',.0596221,True)
            if row['final_loaded']<=0 or row['patches']!=row['restores']:
                raise ValueError('Explicit manual reload did not advance')
            rows.append(row)
    if len(rows)!=23:raise ValueError('Expected rifle, tube and underbarrel fixture rows')
    magazine_cases=[]
    rifle=next(s for s in fixture['weapons'] if s.get('weapon',{}).get('asset_name')=='XM8_sp_s')
    for rounds in (15,30):
        for reserve in (0,60):
            returned=run_case(im,binding,rifle,'bounded',.0596221,
                              detached_rounds=rounds,return_frame=180,reserve_rounds=reserve)
            if (returned['final_loaded'],returned['final_reserve'])!=(rounds,reserve):
                raise ValueError('Original magazine return changed counts')
            if any(t['loaded']!=0 or t['reserve']!=reserve for t in returned['transitions'] if t['frame']<180):
                raise ValueError('Removed original magazine refilled itself')
            returned['scenario']='return_original';magazine_cases.append(returned)
        for mode in ('stock','bounded'):
            removed=run_case(im,binding,rifle,mode,.0596221,detached_rounds=rounds)
            expected=(0,60) if mode=='bounded' else (30,30)
            if (removed['final_loaded'],removed['final_reserve'])!=expected:
                raise ValueError('Removed-magazine empty inhibition negative control differs')
            removed['scenario']='discard_no_reload_input';magazine_cases.append(removed)
        replacement=run_case(im,binding,rifle,'bounded',.0596221,True,detached_rounds=rounds)
        if (replacement['final_loaded'],replacement['final_reserve'])!=(30,30):
            raise ValueError('Discard and deliberate replacement failed conservation')
        replacement['scenario']='discard_then_explicit_reload';magazine_cases.append(replacement)
    return {'schema':'fvr.bc2.reload_machine_emulation.v1','native_machine_code':True,
            'game_process_opened':False,'gameplay_verified':False,'headset_verified':False,
            'effects_listener_boundary':'empty mock','native_owner_and_scheduler_emulated':False,
            'cases':rows,'magazine_removal_cases':magazine_cases,'passed':True}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,required=True);p.add_argument('--fixture',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    if args.output.exists():p.error('Output exists; preserve prior evidence')
    fixture_bytes=args.fixture.read_bytes()
    result=run(Image(args.exe),json.loads(fixture_bytes))
    result['fixture_sha256']=hashlib.sha256(fixture_bytes).hexdigest()
    result['executable_sha256']=hashlib.sha256(args.exe.read_bytes()).hexdigest()
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'passed':True,'cases':len(result['cases']),
                     'magazine_removal_cases':len(result['magazine_removal_cases']),
                     'output':str(args.output),'gameplay_verified':False}))


if __name__=='__main__':main()
