"""Reuse unchanged normal-test passes after two test-only fixture corrections."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[3];RUN=ROOT/'reports/coordinator-20261030-integration55'
SUITES={'Bc2PhysicalFeed','StagedFrameProducer'}
ALLOWED={'tests/Bc2PhysicalFeedTests.cpp','tests/StagedFrameProducerTests.cpp'}
SOURCE=('include','src','profiles','cmake','tests','tools','launcher','config','third_party','licenses','CMakeLists.txt','Build.ps1','Build-Checkpoint.ps1')
ARTIFACTS=(('x86','BC2NativeProbe.dll'),('x86','BC2NativeTrace.exe'),('x64','BC2NativeIpcProbe.exe'),('x64','BC2XrHost.exe'),('x86','BC2NativeResolution.exe'),('x86','BC2ResolutionBootstrap.dll'))
def load(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def pin(p):
 p=Path(p);return dict(path=str(p.resolve()),sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def save(p,v):
 with Path(p).open('x',encoding='utf-8',newline='\n') as f:json.dump(v,f,indent=2);f.write('\n')
def statuses(p):
 text=Path(p).read_text(encoding='utf-8-sig');result={}
 for m in re.finditer(r'^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+(.+)$',text,re.M):
  assert m[1] not in result;result[m[1]]=bool(re.search(r'\bPassed\s+[\d.]+ sec\s*$',m[2]))
 return result
state=load(RUN/'source-state.json');head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
if sys.argv[1]=='snapshot':
 data=dict(built_source_commit=state['source_commit'],source_sha256=state['source_sha256'],architectures={},native=[])
 for arch,attempt in [('x86','03'),('x64','01')]:
  prefix=RUN/f'{arch}-{attempt}';result=load(str(prefix)+'-result.json');assert result['source_commit']==state['source_commit']
  buildlog=Path(str(prefix)+'-build.log');steps=re.findall(r'^\[(\d+)/(\d+)\]',buildlog.read_text(encoding='utf-8-sig'),re.M);assert steps and steps[-1][0]==steps[-1][1]
  cache=(ROOT/'build'/arch/'CMakeCache.txt').read_text();ctest=re.search(r'^CMAKE_CTEST_COMMAND:INTERNAL=(.+)$',cache,re.M)[1].strip()
  definitions=json.loads(subprocess.check_output([ctest,'--test-dir',str(ROOT/'build'/arch),'--show-only=json-v1'],text=True))['tests']
  observed=statuses(str(prefix)+'-tests.log');assert len(observed)==len(definitions)==335
  assert {n for n,p in observed.items() if not p}<=SUITES
  rows={}
  for test in definitions:
   assert test['name'] in observed;inputs=[]
   for arg in test['command']:
    p=Path(arg)
    if p.is_file():inputs.append(pin(p))
   assert inputs;rows[test['name']]=dict(passed=observed[test['name']],inputs=inputs,command=test['command'])
  data['architectures'][arch]=dict(tests=rows,normal_result=pin(str(prefix)+'-result.json'),normal_build=pin(buildlog),normal_tests=pin(str(prefix)+'-tests.log'))
 for arch,name in ARTIFACTS:
  for p in (ROOT/'build'/arch/name,ROOT/'build'/arch/'symbols'/(Path(name).stem+'.pdb')):data['native'].append(pin(p))
 save(RUN/'fixture-baseline.json',data);print('Pinned 335 normal tests per architecture and six binary/symbol pairs.')
else:
 baseline=load(RUN/'fixture-baseline.json');assert baseline['built_source_commit']==state['source_commit']
 changed=set(subprocess.check_output(['git','diff','--name-only',state['source_commit'],head,'--',*SOURCE],cwd=ROOT,text=True).splitlines());assert changed==ALLOWED,changed
 subprocess.run(['git','diff','--exit-code',head,'--',*SOURCE],cwd=ROOT,check=True)
 pins=[pin(RUN/'fixture-baseline.json')];checks={}
 for row in baseline['native']:assert pin(row['path'])==row;pins.append(row)
 for arch,old in baseline['architectures'].items():
  buildlog=RUN/f'{arch}-fixtures-build.log';assert 'FAILED:' not in buildlog.read_text(encoding='utf-8-sig')
  observed=statuses(RUN/f'{arch}-fixtures-tests.log');assert set(observed)==SUITES and all(observed.values()),observed
  for name,row in old['tests'].items():
   if name not in SUITES:
    assert row['passed'];
    for item in row['inputs']:assert pin(item['path'])==item
   for item in row['inputs']:pins.append(pin(item['path']))
  for field in ('normal_result','normal_build','normal_tests'):
   assert pin(old[field]['path'])==old[field];pins.append(old[field])
  pins.extend([pin(buildlog),pin(RUN/f'{arch}-fixtures-tests.log')])
  checks[arch]=dict(architecture=arch,suites=335,passed=335,reused_unchanged=333,rerun=sorted(SUITES),normal_result=old['normal_result'],normal_build=old['normal_build'],normal_tests=old['normal_tests'],targeted_tests=pin(RUN/f'{arch}-fixtures-tests.log'))
 save(RUN/'fixture-validation.json',dict(passed=True,built_source_commit=state['source_commit'],validated_commit=head,source_sha256=state['source_sha256'],allowed_test_only_changes=sorted(changed),checks=checks,pins=list({p['path']:p for p in pins}.values()),native_artifacts_unchanged=True,normal_builds_complete=True,game_started=False))
 print('335/335 accepted per architecture:333 unchanged passes reused,2 corrected fixtures rerun; native binaries/symbols unchanged.')
