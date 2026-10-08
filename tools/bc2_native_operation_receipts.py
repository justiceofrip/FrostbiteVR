"""Compute/check operation source provenance and audit native diagnostic evidence.

This read-only tool NEVER reviews/enables native capabilities or emits C++ tables.
"""
import argparse,hashlib,json,re,struct,sys
from pathlib import Path

SCHEMA='fvr.bc2.native-operation-source.v1'
ROOTS=tuple('src/games/bc2/'+n+'.cpp' for n in (
 'Bc2SelectedMeshes1p','Bc2WeaponVisibility','Bc2HolsterInput','Bc2BodyHolster',
 'Bc2BodyInventory','Bc2InputBinding','Bc2RigPublication','Bc2Gameplay','NativeProbe','Bc2RigWorkerRuntime','Bc2ReloadFlowRuntime'))+tuple(
 'third_party/minhook-1.3.4/src/'+n for n in ('buffer.c','hook.c','trampoline.c','hde/hde32.c'))
DATA_EXCLUSIONS=frozenset('src/games/bc2/'+n for n in (
 'Bc2VisibilityDescriptorData.h','Bc2BodyEquipmentProfiles.h','Bc2BodyAmmoAssetProfiles.h','Bc2BeltPropProfiles.h'))
INCLUDE=re.compile(r'^\s*#\s*include\s*["<]([^">]+)[">]',re.M)
def require(ok,message):
 if not ok:raise ValueError(message)
def sha(raw):return hashlib.sha256(raw).hexdigest()
def canonical_json(value):return json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode('utf8')
def load(path):
 def unique(pairs):
  out={}
  for k,v in pairs:
   require(k not in out,'duplicate JSON key: '+k);out[k]=v
  return out
 return json.loads(Path(path).read_text(encoding='utf-8-sig'),object_pairs_hook=unique)
def source_receipt(root,reviewed_header=None,roots=None):
 roots=ROOTS if roots is None else roots
 root=Path(root).resolve();require(root.is_dir(),'source root missing')
 # Local quoted dependencies and same-name implementation files are included.
 # Unknown local references fail, instead of silently dropping a dependency.
 implementation={}
 for p in (root/'src').rglob('*.cpp'):implementation.setdefault(p.stem,[]).append(p)
 headers={}
 for p in (root/'include').rglob('*.h'):headers.setdefault(p.name,[]).append(p)
 pending=[root/p for p in roots];seen=set();excluded=set()
 while pending:
  p=pending.pop().resolve();require(p.is_relative_to(root),'source dependency escapes root')
  rel=p.relative_to(root).as_posix()
  if rel in DATA_EXCLUSIONS:excluded.add(rel);continue
  if rel in seen:continue
  require(p.is_file(),'source dependency missing: '+rel);seen.add(rel)
  text=p.read_text(encoding='utf-8-sig')
  for name in INCLUDE.findall(text):
   candidates=[p.parent/name,root/'include'/name,root/'src/games/bc2'/name]
   found=next((q for q in candidates if q.is_file()),None)
   if found is None and '/' not in name:
    matches=headers.get(name,[]);require(len(matches)<=1,'ambiguous project include: '+name)
    if matches:found=matches[0]
   # Angle standard/system headers are outside the reviewed project source set.
   if found is None:
    require('/' not in name or not name.startswith('fvr/'),'missing project include: '+name)
    if re.search(r'#\s*include\s*"'+re.escape(name)+r'"',text):raise ValueError('missing local include: '+name)
    continue
   pending.append(found)
   if found.suffix=='.h':
    matches=implementation.get(found.stem,[])
    require(len(matches)<=1,'ambiguous implementation for '+name)
    pending.extend(matches)
 rows=[{'path':p,'sha256':sha((root/p).read_bytes())} for p in sorted(seen)]
 wiring=root/'cmake/Bc2NativeOperationCapability.cmake'
 body={'schema':SCHEMA,'roots':list(roots),'files':rows,'excluded_generated_data':sorted(excluded),
       'recipe_sha256':sha(Path(__file__).read_bytes()),
       'build_wiring_sha256':sha(wiring.read_bytes()) if wiring.is_file() else None}
 body['source_sha256']=sha(canonical_json(body))
 # Pin data separately; adding data must not require new native ABI evidence.
 body['excluded_data_sha256']=[{'path':p,'sha256':sha((root/p).read_bytes())} for p in sorted(excluded)]
 body['reviewed_header_sha256']=sha(Path(reviewed_header).read_bytes()) if reviewed_header else None
 body['native_admission_granted']=False
 return body
def verify_source(root,receipt,expected,header=None):
 require(re.fullmatch('[0-9a-f]{64}',expected) is not None,'expected source SHA256 invalid')
 recorded=load(receipt);fresh=source_receipt(root,header)
 require(recorded==fresh,'source receipt differs from current exact dependency closure/header')
 require(fresh['source_sha256']==expected,'source SHA256 mismatch')
 return fresh
def find_unique(tree,key):
 found=[]
 def visit(node):
  if isinstance(node,dict):
   if key in node:found.append(node[key])
   for v in node.values():visit(v)
  elif isinstance(node,list):
   for v in node:visit(v)
 visit(tree);require(len(found)<=1,'ambiguous diagnostic '+key)
 return found[0] if found else None
def audit_trace(tree):
 visibility=find_unique(tree,'weapon_visibility_probe') or {}
 neutral=find_unique(tree,'weapon_visibility_action_suppression') or {}
 body=find_unique(tree,'body_holster_probe') or {}
 selected=find_unique(tree,'selected_meshes_observer') or {}
 rows=body.get('rows',[]);challenge=body.get('challenge') or {}
 masks=(0xffffffff,0xffffffff,(1<<12)|(1<<14)|(1<<29),(1<<1)|(1<<4)|(1<<5)|(1<<6))
 arrays=[challenge.get(k) for k in ('before','challenged','written','observed')]
 words_valid=all(isinstance(a,list) and len(a)==4 and all(type(v) is int and 0<=v<=0xffffffff for v in a) for a in arrays)
 challenged=False
 if words_valid:
  before,inserted,written,observed=arrays
  challenged=(inserted[:2]==[0x3f800000]*2 and all((inserted[n]&masks[n])==masks[n] for n in (2,3)) and
    all(written[n]==(before[n]&~masks[n]) and (observed[n]&masks[n])==0 and
        (observed[n]&~masks[n])==(before[n]&~masks[n]) for n in range(4)))
 row_match=any(r.get('input')==challenge.get('input') and r.get('native_tick')==challenge.get('native_tick') and
  r.get('request')==challenge.get('request') and r.get('suppressed') is True and r.get('free_right') is True and
  r.get('hidden') is True and r.get('blocks_actions') is True for r in rows)
 suppression=(body.get('failure')==0 and body.get('phase')==11 and challenge.get('staged') is True and
  challenge.get('committed') is True and challenge.get('unrelated_preserved') is True and challenged and row_match)
 receipts=visibility.get('receipts',[])
 paired=(visibility.get('paired_private_palette_sequence_verified') is True and visibility.get('failure')==0 and
  len(receipts)==3 and all(type(n) is int and n>=2 for n in receipts))
 fire=body.get('post_draw_fire') or {}
 fire_proof=(fire.get('requested') is True and fire.get('fire_cache_commits',0)>0 and fire.get('release_cache_commits',0)>0 and
  fire.get('restored_claim',0)>0 and 0<fire.get('pulse_end_ns',0)-fire.get('pulse_start_ns',0)<=200000000)
 body_pair=(body.get('diagnostic_profile') in (4,5) and body.get('phase')==11 and body.get('failure')==0 and
   body.get('hidden_request',0)>0 and body.get('show_request',0)>0 and body.get('show_request')!=body.get('hidden_request') and
   any(r.get('free_right') is True and r.get('hidden') is True and r.get('paired_free_copies',0)>0 and r.get('draw_serial',0)>0 for r in rows) and
   any(r.get('phase') in (10,11) and r.get('hidden') is False and r.get('right_claim',0)>0 and r.get('draw_serial',0)>0 for r in rows))
 owner={k:body.get(v) for k,v in (('player','player'),('soldier','soldier'),('weak','weak'),('weapon','weapon'),
   ('actor_generation','actor_generation'),('equip_generation','native_equip_generation'),('space','space'))}
 physical=challenge.get('physical_owner') or {}
 observed,deadline,now=(challenge.get(k,0) for k in ('input_observed_ns','input_deadline_ns','input_now_ns'))
 identity=(all(type(v) is int and v>0 for v in owner.values()) and challenge.get('native_owner')==owner and
  physical==dict(actor=(owner['weak']<<32)|owner['soldier'],actor_generation=owner['actor_generation'],
   equip_generation=body.get('physical_equip_generation'),space=owner['space']) and
  challenge.get('focused') is True and challenge.get('tracked')==[True,True] and
  type(observed) is int and type(deadline) is int and type(now) is int and 0<observed<=now<deadline and deadline-observed<=150000000 and
  challenge.get('input',0)>0 and challenge.get('native_tick',0)>0 and challenge.get('cache',0)>=0x10000 and challenge.get('request')==body.get('hidden_request'))
 suppression=(body.get('failure')==0 and body.get('phase')==11 and challenge.get('staged') is True and
  challenge.get('committed') is True and challenge.get('unrelated_preserved') is True and challenged and identity)
 cohorts=[r for r in selected.get('configuration_identity_samples',[]) if r.get('owner')==owner and
   r.get('configuration_path_verified') is True and r.get('configuration_path') and r.get('operation_binding')]
 return {'schema':'fvr.bc2.native-operation-trace-audit.v1','native_admission_granted':False,
  'visibility_paired_palette_diagnostic':paired,'neutral_visibility_cache_observations':neutral.get('verified_commits',0),
  'configured_body_hide_show_diagnostic':body_pair,
  'source_digest_is_binary_equivalence_proof':False,
  'neutral_observations_are_challenged_suppression':False,'challenged_suppression_diagnostic':suppression,
  'challenge_words_valid':words_valid,'challenge_row_joined':row_match,'challenge_request_identity_verified':identity,
  'post_draw_fire_cache_diagnostic':fire_proof,'actual_shot_verified':False,'headset_verified':False,
  'body_diagnostic_profile':body.get('diagnostic_profile'),'configuration_identity_samples':cohorts,
  'gaps':(['Configured non-fire profile4 has no post-draw fire pulse; opt-in profile5 adds verified-ammo preflight.'] if body.get('diagnostic_profile')==4 else [])+[
   'Visibility probe neutral counters cannot replace BodyHolsterCacheChallenge.',
   'Native shot/ammo outcome, executable SHA256 provenance and reviewed build receipt require independent evidence.']}
def fnv64(raw):
 h=14695981039346656037
 for byte in raw:h=((h^byte)*1099511628211)&0xffffffffffffffff
 return h
def verify_binding(raw,b):
 require(isinstance(b,dict),'native binding missing')
 require(len(raw)>=64 and raw[:2]==b'MZ','not a PE executable')
 nt=struct.unpack_from('<I',raw,0x3c)[0];require(nt+84<=len(raw) and raw[nt:nt+4]==b'PE\0\0','PE header invalid')
 machine,count=struct.unpack_from('<HH',raw,nt+4);optional_bytes=struct.unpack_from('<H',raw,nt+20)[0];optional=nt+24
 require(optional_bytes>=60 and optional+optional_bytes<=len(raw),'optional PE header truncated')
 require(machine==0x14c and struct.unpack_from('<H',raw,optional)[0]==0x10b,'unsupported executable ABI')
 base=struct.unpack_from('<I',raw,optional+28)[0];image_bytes=struct.unpack_from('<I',raw,optional+56)[0]
 require(base==0x400000 and b.get('image_base')==base and b.get('image_bytes')==image_bytes,'native PE binding differs')
 require(b.get('executable_bytes')==len(raw) and b.get('content_fingerprint_fnv64')==fnv64(raw),'native executable content binding differs')
 code=b.get('code');require(isinstance(code,list) and len(code)==4,'four code proofs required')
 sections=optional+optional_bytes;require(sections+count*40<=len(raw),'PE sections truncated')
 for proof in code:
  require(isinstance(proof,dict),'invalid code proof record')
  rva,size=proof.get('rva'),proof.get('bytes');require(type(rva) is int and type(size) is int and rva>0 and 0<size<=4096,'invalid code proof bounds')
  matches=[]
  for n in range(count):
   at=sections+n*40;virtual_size,address,raw_size,offset=struct.unpack_from('<IIII',raw,at+8);flags=struct.unpack_from('<I',raw,at+36)[0]
   if flags&0x20000000 and address<=rva and rva+size<=address+raw_size and rva+size<=image_bytes:matches.append(offset+rva-address)
  require(len(matches)==1 and matches[0]+size<=len(raw),'code proof has no unique executable file range')
  require(fnv64(raw[matches[0]:matches[0]+size])==proof.get('fingerprint_fnv64'),'native code proof differs')
 return True
def binding_receipt(trace,executable,adapter,build_receipt,source):
 tree=load(trace);selected=find_unique(tree,'selected_meshes_observer') or {};bindings=[]
 for row in selected.get('configuration_identity_samples',[]):
  if row.get('operation_binding') and row['operation_binding'] not in bindings:bindings.append(row['operation_binding'])
 require(len(bindings)==1,'native trace must contain one unambiguous actual operation binding')
 raw=Path(executable).read_bytes();verify_binding(raw,bindings[0])
 return {'schema':'fvr.bc2.native-operation-binding-receipt.v1','native_admission_granted':False,
  'pid':tree.get('pid'),'binding':bindings[0],'executable_sha256':sha(raw),'adapter_sha256':sha(Path(adapter).read_bytes()),
  'trace_sha256':sha(Path(trace).read_bytes()),'build_receipt_sha256':sha(Path(build_receipt).read_bytes()),
  'source_receipt_sha256':sha(Path(source).read_bytes()),'diagnostic_audit':audit_trace(tree),
  'loaded_adapter_identity_verified':False,'compiler_options_abi_equivalence_verified':False,
  'reviewed_native_operation':False}
def main():
 p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='mode',required=True)
 s=sub.add_parser('source');s.add_argument('--root',type=Path,required=True);s.add_argument('--reviewed-header',type=Path);s.add_argument('--output',type=Path,required=True)
 v=sub.add_parser('verify-source');v.add_argument('--root',type=Path,required=True);v.add_argument('--reviewed-header',type=Path);v.add_argument('--receipt',type=Path,required=True);v.add_argument('--expected',required=True)
 a=sub.add_parser('audit-trace');a.add_argument('--trace',type=Path,required=True);a.add_argument('--output',type=Path,required=True)
 b=sub.add_parser('binding');
 for name in ('trace','executable','adapter','build-receipt','source-receipt','output'):b.add_argument('--'+name,type=Path,required=True)
 args=p.parse_args()
 try:
  if args.mode=='verify-source':print(verify_source(args.root,args.receipt,args.expected,args.reviewed_header)['source_sha256']);return
  result=source_receipt(args.root,args.reviewed_header) if args.mode=='source' else binding_receipt(args.trace,args.executable,args.adapter,args.build_receipt,args.source_receipt) if args.mode=='binding' else audit_trace(load(args.trace))
  if args.mode=='binding':
   require(args.output.resolve() not in {getattr(args,n).resolve() for n in ('trace','executable','adapter','build_receipt','source_receipt')},'output overwrites binding input')
  if args.mode=='audit-trace':result['trace_sha256']=sha(args.trace.read_bytes());require(args.output.resolve()!=args.trace.resolve(),'output overwrites trace')
  elif args.mode=='source':
   protected={ (args.root/r['path']).resolve() for r in result['files'] }
   if args.reviewed_header:protected.add(args.reviewed_header.resolve())
   require(args.output.resolve() not in protected,'output overwrites input source/header')
  require(not args.output.exists(),'output already exists')
  args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
 except (ValueError,OSError,UnicodeError) as exc:p.error(str(exc))
if __name__=='__main__':main()
