"""Prepare tiny private mechanism cues from the owner's installed BC2 audio.

No audio is shipped in source. Decode through the caller's existing FFmpeg;
never invoke a native game function or change an archive. The two SPAS ranges
are an experimental waveform-derived split, not headset-accepted calibration.
EA Audio Core block identification: vgmstream/src/meta/ea_eaac.c (upstream).
XAS decoding remains in FFmpeg, not copied into this repository.
"""
from __future__ import annotations
import argparse, hashlib, io, json, math, struct, subprocess, wave
from pathlib import Path
import inspect_bc2_mesh_asset as archives

COMMON = 'Dist/win32/levels/mp_common/level-00.fbrb'
INDEX = '6ec10fba796c14d2a05f86d3079e819c38a4aaab3ec9205d7345bd2869554965'
SPAS = 'Sound/Weapons/Handheld/UL_shg_SPAS12/Spas-12_Pump_1p_Wave.res'
SPAS_SHA = '7e3cc4f14e0839f6dce1b3b6d78bf3ef38e82ca056b282557c64a0e3689f3dd5'

def sha(data): return hashlib.sha256(data).hexdigest()

def packets(data):
    """Accept only the observed bounded PC wave wrapper + mono XAS1 blocks."""
    if len(data)<48 or len(data)>1024*1024 or data[:4]!=b'\0\0\x03\0':
        raise ValueError('Unsupported wave resource wrapper')
    expected=struct.unpack_from('<I',data,4)[0]
    if not 0<expected<=48000*8: raise ValueError('Sample bound')
    if data[8:12]!=bytes(4) or data[12:16]!=b'\x01\0\0\0' or data[16:28]!=bytes(12):
        raise ValueError('Unknown wrapper fields')
    length=struct.unpack_from('<I',data,28)[0]
    if length<16 or 32+length>len(data) or any(data[32+length:]):
        raise ValueError('Wave stream length/padding')
    stream=data[32:32+length]
    if stream[:4]!=b'H\0\0\x0c': raise ValueError('Header block')
    info,count=struct.unpack_from('>II',stream,4)
    if info>>24!=0x14 or (info>>18)&0x3f or count>>30!=1:
        raise ValueError('Expected mono version1 XAS1 RAM sample')
    rate=info&0x3ffff
    if rate!=48000 or count&0x3fffffff!=expected: raise ValueError('Rate/sample mismatch')
    at=12;total=0;blocks=[]
    while at<len(stream):
        if at+4>len(stream): raise ValueError('Truncated block')
        header=int.from_bytes(stream[at:at+4],'big');kind=header>>24;size=header&0xffffff
        if size<4 or at+size>len(stream): raise ValueError('Block length')
        if kind==0x45:
            if size!=4 or at+size!=len(stream): raise ValueError('End block')
            break
        if kind!=0x44 or size<8 or (size-8)%76: raise ValueError('XAS packet alignment')
        samples=int.from_bytes(stream[at+4:at+8],'big')
        capacity=(size-8)//76*128
        if not capacity-127<=samples<=capacity: raise ValueError('Block sample count')
        total+=samples;blocks.append(stream[at+8:at+size]);at+=size
    else: raise ValueError('Missing end block')
    if total!=expected: raise ValueError('Total sample count')
    return rate,expected,b''.join(blocks)

def decode(data,ffmpeg):
    rate,count,payload=packets(data)
    # WAV provides packet boundaries and channel/rate metadata; explicit input
    # decoder overrides its PCM tag. 76-byte packets are one mono XAS frame.
    fmt=struct.pack('<HHIIHH',1,1,rate,rate*76//128,76,4)
    framed=b'RIFF'+struct.pack('<I',36+len(payload))+b'WAVEfmt '+struct.pack('<I',16)+fmt+b'data'+struct.pack('<I',len(payload))+payload
    run=subprocess.run([str(ffmpeg),'-v','error','-f','wav','-max_size','76','-c:a','adpcm_ea_xas',
        '-i','pipe:0','-f','s16le','pipe:1'],input=framed,capture_output=True,timeout=30)
    if run.returncode or run.stderr or not count*2<=len(run.stdout)<=(count+127)*2:
        raise ValueError('Decoder failed or returned unexpected PCM length: '+run.stderr.decode(errors='replace')[:512])
    return rate,run.stdout[:count*2]

def cue(pcm,rate,start,end,gain=.30):
    if not 0<=start<end<=len(pcm)//2 or not 0<gain<=.5: raise ValueError('Cue range/gain')
    values=list(struct.unpack('<'+'h'*(end-start),pcm[start*2:end*2]))
    fade=min(rate//500,len(values)//4)
    for i,v in enumerate(values):
        envelope=min(1.,i/max(1,fade),(len(values)-1-i)/max(1,fade))
        values[i]=round(v*gain*envelope)
    out=io.BytesIO()
    with wave.open(out,'wb') as w:
        w.setnchannels(1);w.setsampwidth(2);w.setframerate(rate);w.writeframes(struct.pack('<'+'h'*len(values),*values))
    return out.getvalue()

def prepare(game,ffmpeg,output):
    output=output.resolve()
    if output.drive.upper()!='G:' or output==Path(__file__).resolve().parents[1] or Path(__file__).resolve().parents[1] in output.parents:
        raise ValueError('Private audio cache must be outside the source repository on G:')
    previous=archives.MAX_PAYLOAD
    try:
        archives.MAX_PAYLOAD=640*1024*1024
        ar=archives.Archive(game/COMMON)
    finally: archives.MAX_PAYLOAD=previous
    if ar.index_sha256!=INDEX: raise ValueError('Common archive index differs')
    raw=ar.read_selected([SPAS])[SPAS]
    if sha(raw)!=SPAS_SHA: raise ValueError('Unreviewed SPAS wave resource')
    rate,pcm=decode(raw,ffmpeg)
    if len(pcm)!=28864*2: raise ValueError('SPAS authored sample count differs')
    output.mkdir(parents=True,exist_ok=True)
    profile={'schema':'fvr.bc2.mechanism_audio_cache.v1','asset':'SPAS12_sp','sample_rate':rate,
        'archive':COMMON,'index_sha256':INDEX,'resource':SPAS,'resource_sha256':sha(raw),'pcm_sha256':sha(pcm),
        'animation_guid':'2d9cf41b-3aba-4a22-82a8-64051c180432',
        'sound_template_guid':'9515f482-793e-420d-b107-234f1c7ece0d',
        'gain':.30,'range_basis':'10ms RMS envelope: onset145ms; valley355ms; second onset365ms',
        'headset_accepted':False,'suppression_verified':False,'cues':{}}
    for key,first,last in [('rear',6960,17040),('closure',17520,28864)]:
        encoded=cue(pcm,rate,first,last);name='spas-'+key+'.wav';(output/name).write_bytes(encoded)
        profile['cues'][key]={'file':name,'sha256':sha(encoded),'bytes':len(encoded),
            'first_sample':first,'end_sample_exclusive':last,'duration_s':(last-first)/rate}
    (output/'mechanism-audio.json').write_text(json.dumps(profile,indent=2)+'\n',encoding='utf-8',newline='\n')
    return profile

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',type=Path,required=True)
    p.add_argument('--ffmpeg',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=prepare(a.game,a.ffmpeg,a.output)
    print(json.dumps({'asset':result['asset'],'cues':result['cues'],'headset_accepted':False}));return 0
if __name__=='__main__':raise SystemExit(main())
