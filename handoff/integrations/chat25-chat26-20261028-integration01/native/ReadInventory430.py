"""One bounded repeated-identity native inventory read; no writes or calls."""
import argparse, hashlib, json, sys, time
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path('G:/Unity/VRMMO/FrostbiteVR');BASE=ROOT/'reports/integration-chat25-chat26-20261028-01/native'
sys.path.insert(0,str(ROOT/'tools'))
from read_bc2 import Process
from capture_reload_state import Image,Inspector
from capture_bc2_emulation_fixture import creation_filetime
class BoundedProcess(Process):
 def __init__(self,pid,path):
  self.calls=0;self.bytes=0;self.started=time.monotonic();super().__init__(pid,path)
 def read(self,at,n):
  self.calls+=1;self.bytes+=n
  if self.calls>65536 or self.bytes>2*1024*1024 or time.monotonic()-self.started>10:raise ValueError('Read budget exceeded')
  return super().read(at,n)
ap=argparse.ArgumentParser();ap.add_argument('--process',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--selected-knife-server',action='store_true');a=ap.parse_args()
assert a.output.resolve().is_relative_to(BASE) and not a.output.exists()
identity=json.loads(a.process.read_text(encoding='utf-8-sig'));image=Image(Path(identity['path']))
assert hashlib.sha256(image.data).hexdigest()=='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
p=BoundedProcess(identity['pid'],image.path)
try:
 assert creation_filetime(p)==int(identity['creation_filetime'])
 ins=Inspector(p,image);before=ins.owner();rows=[]
 for slot,at in enumerate(before['items']):
  if not at:continue
  data=p.u32(at+4);ins.require_type(data,'SoldierWeaponData')
  row={'slot':slot,'weapon':at,'data':data,'persistence':p.u32(data+0x64),'name':ins.text(p.u32(data+12)),'path':ins.text(p.u32(data+0x40))}
  try:
   config=ins.weapon(at,slot);row['configuration']=config;row['mesh_links']=ins.mesh_links(config)
   row['states']={hex(branch):ins.state(config,branch) for branch in (0x3c,0x40)}
  except (OSError,ValueError,KeyError) as e:row['detail_error']=str(e)
  assert p.u32(at+4)==data and p.u32(data+0x64)==row['persistence']
  rows.append(row)
 melee=None
 if a.selected_knife_server:
  sys.path.insert(0,str(Path(r'G:\Unity\VRMMO\bc2vr-recovery\overnight426\knife')))
  from SelectedKnifeIdentity426 import capture
  selected=[r for r in rows if r['slot']==0 and r['weapon']==before['selected_weapon']]
  assert len(selected)==1 and 'configuration' in selected[0]
  melee=capture(ins,before,selected[0]['configuration'])
 assert ins.owner()==before and creation_filetime(p)==int(identity['creation_filetime'])
 result={'read_only':True,'native_calls':False,'process_writes':False,'atomic_snapshot':False,'pid':identity['pid'],'creation_filetime':identity['creation_filetime'],'owner':before,'items':rows,'read_calls':p.calls,'read_bytes':p.bytes,'reader_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
 if melee is not None:result['selected_melee_identity']=melee
 a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({'items':[{'slot':r['slot'],'name':r['name'],'path':r['path'],'persistence':r['persistence'],'error':r.get('detail_error')} for r in rows]}))
finally:p.close()
