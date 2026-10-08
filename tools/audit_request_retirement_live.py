"""Audit cancellation recovery independently of insertion success."""
import json
import sys
from pathlib import Path

def audit(trace):
    errors=[]
    def require(ok, why):
        if not ok: errors.append(why)
    flow=trace['gameplay']['reload_flow']
    probe=trace['gameplay']['reload_request_probe']
    recovery=probe.get('retirement',{})
    starts=[r for r in probe['journal'] if r['event']==1]
    ends=[r for r in probe['journal'] if r['event'] in (5,6)]
    require(len(starts)==1 and len(ends)==1,'unique start and cancellation required')
    if len(starts)!=1 or len(ends)!=1: return {'passed':False,'errors':errors}
    start,end=starts[0],ends[0]
    identity=recovery.get('expected_identity',{})
    native=start.get('native',{})
    require(identity.get('owner')==native.get('owner') and identity.get('server')==native.get('server') and
            identity.get('firing')==[b['firing'] for b in native.get('branches',[])], 'different original owner')
    receipt,source=recovery.get('receipt',{}),recovery.get('reserve',{})
    require(recovery.get('enabled') is True and recovery.get('verified') is True and recovery.get('reason')==0,'recovery failed')
    require(receipt.get('identity')==identity==source.get('identity'),'receipt/source identity mismatch')
    require(receipt.get('cycle')==start['cycle']==end['cycle'] and receipt.get('event',0)>0 and receipt.get('verified') is True,'invalid retirement')
    require(0<recovery.get('stopped_ns',0)<=end['now_ns']<=receipt.get('observed_ns',0)<=receipt.get('checked_ns',0)<receipt.get('deadline_ns',0), 'retirement ordering')
    require(receipt.get('deadline_ns',0)-receipt.get('observed_ns',0)<=200_000_000,'retirement freshness widened')
    require(source.get('verified') is True and source.get('sequence',0)>0 and
            receipt.get('checked_ns',0)<=source.get('observed_ns',0)<=source.get('checked_ns',0)<min(source.get('deadline_ns',0),receipt.get('deadline_ns',0)), 'reserve not fresh after retirement')
    require(source.get('deadline_ns',0)-source.get('observed_ns',0)<=200_000_000,'reserve freshness widened')
    branches=native.get('branches',[])
    require(len(branches)==3 and len({b['loaded']+b['reserve'] for b in branches})==1,'incoherent initial ammo')
    require(branches and source.get('loaded',-1)+source.get('count',-1)==branches[0]['loaded']+branches[0]['reserve'],'ammo not conserved')
    require(0<=source.get('loaded',-1)<=source.get('capacity',-1) and source.get('count',-1)>=0,'invalid fresh counts')
    require(trace.get('hooks_disabled') is True and flow.get('drained') is True,'hooks not drained')
    return {'passed':not errors,'errors':errors,'scope':'exact-cycle cancellation/drain plus fresh reserve only',
            'insertion_accepted':False,'headset_tested':False,'cycle':start['cycle'],
            'event':receipt.get('event'),'loaded':source.get('loaded'),'reserve':source.get('count')}

if __name__=='__main__':
    result=audit(json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig')))
    output=json.dumps(result,indent=2)+'\n'
    Path(sys.argv[2]).write_text(output,encoding='utf-8',newline='\n')
    print(output,end='')
    raise SystemExit(0 if result['passed'] else 1)
