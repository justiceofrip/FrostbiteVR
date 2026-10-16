"""Bounded WASAPI endpoint loopback for BC2 mechanism-audio timing.

Run through Measure-BC2MechanismAudio.ps1 for storage admission. This records the
endpoint mix (possibly other applications), not a microphone or a process-only
stream. It changes no endpoint, session volume, game state or input.
"""
from pathlib import Path
import argparse,array,ctypes as c,json,math,os,struct,time,uuid,wave
from datetime import datetime,timezone
from read_bc2 import Process

P=c.c_void_p;U=c.c_uint32;Q=c.c_uint64;H=c.c_long
ole=c.OleDLL('ole32');kernel=c.WinDLL('kernel32')
ole.CoInitializeEx.argtypes=[P,U];ole.CoInitializeEx.restype=H
ole.CoCreateInstance.argtypes=[P,P,U,P,c.POINTER(P)];ole.CoCreateInstance.restype=H
ole.CoTaskMemFree.argtypes=[P];ole.CoUninitialize.argtypes=[]
kernel.QueryPerformanceCounter.argtypes=[c.POINTER(c.c_int64)]
kernel.QueryPerformanceFrequency.argtypes=[c.POINTER(c.c_int64)]
MAX_BYTES=3*1024*1024;MAX_PACKETS=2000

def guid(value):return (c.c_ubyte*16).from_buffer_copy(uuid.UUID(value).bytes_le)
def hr(value,label):
    if value<0:raise OSError(f'{label}: HRESULT 0x{value&0xffffffff:08x}')
    return value
def invoke(pointer,index,types,*args):
    if not pointer:raise ValueError('Null COM interface')
    table=c.cast(pointer,c.POINTER(c.POINTER(P))).contents
    return c.WINFUNCTYPE(H,P,*types)(table[index])(pointer,*args)
def release(pointer):
    if pointer:invoke(pointer,2,[])
def query(pointer,iid):
    out=P();hr(invoke(pointer,0,[P,c.POINTER(P)],guid(iid),c.byref(out)),'QueryInterface');return out
def activate(device,iid):
    out=P();hr(invoke(device,3,[P,U,P,c.POINTER(P)],guid(iid),23,None,c.byref(out)),'Activate');return out
def qpc_ns():
    value=c.c_int64();kernel.QueryPerformanceCounter(c.byref(value));return value.value*1_000_000_000//QPC_FREQUENCY
frequency=c.c_int64();kernel.QueryPerformanceFrequency(c.byref(frequency));QPC_FREQUENCY=frequency.value

def selected_endpoint(pid):
    """Return one retained IMMDevice only when the game endpoint is unambiguous."""
    enum=P();devices=P();found=None;endpoint=None
    hr(ole.CoCreateInstance(guid('bcde0395-e52f-467c-8e3d-c4579291692e'),None,23,
        guid('a95664d2-9614-4f35-a746-de8db63617e6'),c.byref(enum)),'Create enumerator')
    try:
        hr(invoke(enum,3,[c.c_int,U,c.POINTER(P)],0,1,c.byref(devices)),'Enum endpoints')
        count=U();hr(invoke(devices,3,[c.POINTER(U)],c.byref(count)),'Endpoint count')
        if count.value>128:raise ValueError('Endpoint count bound')
        for n in range(count.value):
            device=P();manager=P();sessions=P();keep=False
            try:
                hr(invoke(devices,4,[U,c.POINTER(P)],n,c.byref(device)),'Endpoint item')
                manager=activate(device,'77aa99a0-1bd6-484f-8bc7-2c654c9a9b6f')
                hr(invoke(manager,5,[c.POINTER(P)],c.byref(sessions)),'Session enumeration')
                size=c.c_int();hr(invoke(sessions,3,[c.POINTER(c.c_int)],c.byref(size)),'Session count')
                if not 0<=size.value<=4096:raise ValueError('Session count bound')
                matched=False
                for j in range(size.value):
                    control=P();process=P()
                    try:
                        hr(invoke(sessions,4,[c.c_int,c.POINTER(P)],j,c.byref(control)),'Session item')
                        process=query(control,'bfb7ff88-7239-4fc9-8fa2-07c950be9c6d')
                        owner=U();state=c.c_int()
                        hr(invoke(process,14,[c.POINTER(U)],c.byref(owner)),'Session PID')
                        hr(invoke(control,3,[c.POINTER(c.c_int)],c.byref(state)),'Session state')
                        if owner.value==pid and state.value!=2:matched=True
                    finally:release(process);release(control)
                if matched:
                    name=P();hr(invoke(device,5,[c.POINTER(P)],c.byref(name)),'Endpoint ID')
                    try:value=c.wstring_at(name)
                    finally:ole.CoTaskMemFree(name)
                    if found:raise ValueError('BC2 has sessions on multiple endpoints; no capture')
                    endpoint=value;found=device;keep=True
            finally:
                release(sessions);release(manager)
                if not keep:release(device)
        if not found:raise ValueError('No existing BC2 audio session on an active endpoint')
        result=found;found=None;return result,endpoint
    finally:release(found);release(devices);release(enum)

def parse_format(data):
    if len(data)<18:raise ValueError('Short audio format')
    tag,channels,rate,average,align,bits,extra=struct.unpack_from('<HHIIHHH',data)
    if channels not in (1,2) or not 8000<=rate<=48000:raise ValueError('Only mono/stereo at <=48kHz admitted')
    if tag==0xfffe:
        if extra!=22 or len(data)!=40:raise ValueError('Invalid extensible format')
        valid,mask=struct.unpack_from('<HI',data,18)
        subtype=str(uuid.UUID(bytes_le=bytes(data[24:40])))
        if subtype not in ('00000001-0000-0010-8000-00aa00389b71','00000003-0000-0010-8000-00aa00389b71'):
            raise ValueError('Unsupported extensible subtype')
        tag=int(subtype[:8],16)
        if valid!=bits:raise ValueError('Partial-bit containers not admitted')
    if (tag,bits) not in ((1,16),(3,32)) or align!=channels*bits//8 or average!=rate*align:
        raise ValueError('Expected PCM16 or IEEE float32 endpoint mix')
    return {'tag':tag,'channels':channels,'rate':rate,'align':align,'bits':bits,'original_hex':data.hex()}

def pcm16(data,fmt):
    if len(data)%fmt['align']:raise ValueError('Misaligned audio packet')
    if fmt['tag']==1:return data
    out=array.array('h')
    for (value,) in struct.iter_unpack('<f',data):
        if not math.isfinite(value):raise ValueError('Nonfinite endpoint sample')
        out.append(max(-32768,min(32767,round(value*32768))))
    if os.sys.byteorder!='little':out.byteswap()
    return out.tobytes()

def capture(pid,seconds,output,inspect=False):
    process=Process(pid);process.close() # Verify the configured executable before endpoint access.
    hr(ole.CoInitializeEx(None,0),'Initialize COM')
    device=P();client=P();reader=P();format_pointer=P();started=False
    try:
        device,endpoint=selected_endpoint(pid);client=activate(device,'1cb9ad4c-dbfa-4c32-b178-c2f568a703b2')
        hr(invoke(client,8,[c.POINTER(P)],c.byref(format_pointer)),'Get mix format')
        header=c.string_at(format_pointer,18);extra=struct.unpack_from('<H',header,16)[0]
        if extra>22:raise ValueError('Mix format extension bound')
        fmt=parse_format(c.string_at(format_pointer,18+extra))
        result={'schema':'fvr.bc2.audio_loopback.v1','pid':pid,'endpoint':endpoint,'format':fmt,
            'qpc_frequency':QPC_FREQUENCY,'clock':'QPC; packet positions use100ns, native receipts use ns',
            'endpoint_mix_includes_other_apps':True,'requested_seconds':seconds,'inspect_only':inspect}
        if inspect:return result
        # Shared, polling loopback. No input endpoint, playback or volume API.
        hr(invoke(client,3,[c.c_int,U,c.c_int64,c.c_int64,P,P],0,0x20000,1_000_000,0,format_pointer,None),'Initialize loopback')
        hr(invoke(client,14,[P,c.POINTER(P)],guid('c8adbd64-e71e-48a0-a4de-185c395cd317'),c.byref(reader)),'Capture service')
        max_frames=int(fmt['rate']*seconds);frames=0;packets=[];lost=0;gaps=0;timestamp_errors=0
        result['utc_start']=datetime.now(timezone.utc).isoformat();result['start_qpc_ns']=qpc_ns()
        hr(invoke(client,10,[]),'Start capture');started=True
        print(json.dumps({'capture_ready':True,'pid':pid,'endpoint':endpoint,'start_qpc_ns':result['start_qpc_ns']}),flush=True)
        with output.with_suffix('.wav').open('xb') as destination,wave.open(destination,'wb') as wav:
            wav.setnchannels(fmt['channels']);wav.setsampwidth(2);wav.setframerate(fmt['rate'])
            while qpc_ns()-result['start_qpc_ns']<int(seconds*1e9) and frames<max_frames:
                packet_size=U();hr(invoke(reader,5,[c.POINTER(U)],c.byref(packet_size)),'Next packet')
                if not packet_size.value:time.sleep(.004);continue
                if packet_size.value>fmt['rate']:raise ValueError('Packet exceeds one second')
                data=P();count=U();flags=U();position=Q();stamp=Q()
                hr(invoke(reader,3,[c.POINTER(P),c.POINTER(U),c.POINTER(U),c.POINTER(Q),c.POINTER(Q)],
                    c.byref(data),c.byref(count),c.byref(flags),c.byref(position),c.byref(stamp)),'Get packet')
                try:
                    if count.value>fmt['rate']:raise ValueError('Acquired packet bound')
                    take=min(count.value,max_frames-frames);read_ns=qpc_ns()
                    raw=bytes(take*fmt['align']) if flags.value&2 else c.string_at(data,take*fmt['align'])
                    encoded=pcm16(raw,fmt)
                finally:hr(invoke(reader,4,[U],count.value),'Release packet')
                if len(packets)<MAX_PACKETS:packets.append([frames,take,stamp.value,position.value,flags.value,read_ns])
                else:lost+=1
                gaps+=bool(flags.value&1);timestamp_errors+=bool(flags.value&4)
                wav.writeframesraw(encoded);frames+=take
        hr(invoke(client,11,[]),'Stop capture');started=False
        result.update(end_qpc_ns=qpc_ns(),frames=frames,pcm_bytes=frames*fmt['channels']*2,
            packet_columns=['wav_frame','frames','qpc_100ns','device_frame','flags','read_qpc_ns'],packets=packets,
            packet_rows_omitted=lost,discontinuities=gaps,timestamp_errors=timestamp_errors)
        return result
    finally:
        if started:invoke(client,11,[])
        release(reader);release(client);release(device)
        if format_pointer:ole.CoTaskMemFree(format_pointer)
        ole.CoUninitialize()

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--pid',type=int,required=True)
    ap.add_argument('--seconds',type=float,default=12);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--inspect',action='store_true');ap.add_argument('--admission',type=Path)
    args=ap.parse_args();output=args.output.resolve();repo=Path(__file__).resolve().parents[1]
    if not 0<args.seconds<=15 or output.drive.upper()!='G:' or output.is_relative_to(repo):
        raise ValueError('Duration must be <=15s; output must be outside source on G:')
    for ancestor in [output.parent,*output.parent.parents]:
        if ancestor.exists() and ancestor.is_symlink():raise ValueError('Linked output path')
    if output.with_suffix('.json').exists() or output.with_suffix('.wav').exists():raise ValueError('Refusing overwrite')
    if not args.inspect:
        if not args.admission:raise ValueError('Use Measure-BC2MechanismAudio.ps1 for storage admission')
        receipt=json.loads(args.admission.read_text(encoding='utf-8-sig'))
        age=(datetime.now(timezone.utc)-datetime.fromisoformat(receipt['checked_utc'].replace('Z','+00:00'))).total_seconds()
        if receipt['operation']!='Capture' or not 0<=age<=90 or receipt['planned_bytes']<MAX_BYTES or \
           Path(receipt['output_path']).resolve()!=output.parent or receipt['raw_capture_bytes']<2_880_044:
            raise ValueError('Missing/current capture storage admission')
    output.parent.mkdir(parents=True,exist_ok=True)
    result=capture(args.pid,args.seconds,output,args.inspect)
    data=json.dumps(result,separators=(',',':'))+'\n'
    if len(data.encode())>200_000:raise ValueError('Metadata bound')
    with output.with_suffix('.json').open('x',encoding='utf-8',newline='\n') as stream:stream.write(data)
    total=len(data.encode())+(output.with_suffix('.wav').stat().st_size if not args.inspect else 0)
    if total>MAX_BYTES:raise ValueError('Combined artifact byte bound')
    print(json.dumps({'complete':True,'output':str(output),'bytes':total,'frames':result.get('frames',0),
        'endpoint':result['endpoint'],'inspect_only':args.inspect}),flush=True)
    return 0
if __name__=='__main__':raise SystemExit(main())
