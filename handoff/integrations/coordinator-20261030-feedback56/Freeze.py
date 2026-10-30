"""Freeze a tested combined runtime and matching local symbols; no launch/install."""
from pathlib import Path
import argparse, importlib.util, json, re, shutil, subprocess, sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[3]
RUN=ROOT/'reports/coordinator-20261030-feedback56'
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
audit=module('retained_symbol_audit',ROOT/'handoff/integrations/chat25-chat26-20261028-integration01/FreezeRuntime.py')
proof=module('operation_proof',ROOT/'tools/bc2_native_operation_receipts.py')
load=audit.load;pin=audit.pin;need=audit.require
def save(path,value):
    with path.open('x',encoding='utf-8') as f:json.dump(value,f,indent=2);f.write('\n')
def main():
    p=argparse.ArgumentParser();p.add_argument('--x86',required=True);p.add_argument('--x64',required=True);p.add_argument('--storage',type=Path,required=True);p.add_argument('--fixture-validation',type=Path);a=p.parse_args()
    lease=load(RUN/'leases.json')['INTEGRATION'];need(load(Path(lease['path']))['owner_token']==lease['owner_token'],'Lease differs')
    state=load(RUN/'source-state.json');head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    repair=load(a.fixture_validation) if a.fixture_validation else None
    if repair:
        need(repair['passed'] and repair['native_artifacts_unchanged'] and repair['normal_builds_complete'],'Fixture validation failed')
        need(repair['built_source_commit']==state['source_commit'] and repair['validated_commit']==head and repair['source_sha256']==state['source_sha256'],'Fixture source differs')
        for row in repair['pins']:need(pin(Path(row['path']))['sha256']==row['sha256'],'Reused validation input changed:'+row['path'])
    else:need(head==state['source_commit'],'Built source commit changed')
    subprocess.run(['git','diff','--exit-code',head,'--',*audit.SOURCE_PATHS],cwd=ROOT,check=True)
    proof.verify_source(ROOT,Path(state['source_receipt']),state['source_sha256'],Path(state['reviewed_header']))
    checks=[];flags={};caches={}
    for arch,attempt in [('x86',a.x86),('x64',a.x64)]:
        if repair:
            check=repair['checks'][arch];need(check['passed']==check['suites']==335,'Incomplete combined test validation');checks.append(check)
        else:
            result=load(RUN/f'{arch}-{attempt}-result.json');build=load(RUN/f'{arch}-{attempt}-build.json')
            need(result['passed'] and result['source_commit']==head and result['source_sha256']==state['source_sha256'],'Combined attempt failed')
            need(build['built'] and build['tests_passed'] and build['architecture']==arch,'Normal build missing')
            need(Path(build['build_directory']).resolve()==(ROOT/'build'/arch).resolve(),'Wrong build tree')
            log=RUN/f'{arch}-{attempt}-tests.log';text=log.read_text(encoding='utf-8-sig')
            count=re.search(r'100% tests passed, 0 tests failed out of (\d+)',text)
            need(count and int(count[1])>=305 and '***Failed' not in text,'Normal tests incomplete')
            checks.append(dict(architecture=arch,suites=int(count[1]),result=pin(RUN/f'{arch}-{attempt}-result.json'),build=pin(RUN/f'{arch}-{attempt}-build.json'),tests=pin(log)))
        cache=ROOT/'build'/arch/'CMakeCache.txt';flags[arch]={}
        for line in cache.read_text().splitlines():
            m=re.match(r'(BC2_[^:]+):([^=]+)=(.*)',line)
            if m:flags[arch][m[1]]=dict(type=m[2],value=m[3])
        need(not any(v['value']=='ON' for k,v in flags[arch].items() if k.endswith('_PROBE')),'Diagnostic flag enabled')
        config=ROOT/'build'/arch/'generated/bc2-operation/Bc2NativeOperationBuild.generated.h'
        proof.verify_build_config(config,Path(state['reviewed_header']),state['source_sha256'])
        caches[arch]=dict(cache=pin(cache),operation_config=pin(config))
    rows=[]
    for key,arch,name in audit.ARTIFACTS:
        binary=ROOT/'build'/arch/name;pdb=ROOT/'build'/arch/'symbols'/(Path(name).stem+'.pdb')
        pe=audit.pe_identity(binary,arch);symbols=audit.pdb_identity(pdb,pe['image_size'])
        need((pe['guid'],pe['age'])==(symbols['guid'],symbols['age']),'Symbols differ:'+key)
        rows.append(dict(key=key,binary=pin(binary),pdb=pin(pdb),pe=pe,pdb_identity=symbols))
    budget=load(a.storage);need(budget['planned_bytes']>=sum(r[k]['bytes'] for r in rows for k in ('binary','pdb'))+4*1024*1024,'Freeze budget too small')
    output=RUN/'normal-runtime';output.mkdir();binaries={};symbols={}
    for row in rows:
        for kind,target in [('binary',binaries),('pdb',symbols)]:
            original=row[kind];destination=output/Path(original['path']).name;shutil.copyfile(original['path'],destination)
            target[row['key']]=pin(destination);need(target[row['key']]['sha256']==original['sha256'],'Frozen bytes changed')
    save(output/'symbol-audit.json',dict(passed=True,pairs=rows))
    save(output/'source-freeze.json',dict(source_commit=state['source_commit'],validated_source_commit=head,source_sha256=state['source_sha256'],reviewed_header=pin(Path(state['reviewed_header'])),source_receipt=pin(Path(state['source_receipt'])),normal_flags=flags,enrollment=pin(ROOT/'profiles/weapons43/Enrollment.cmake')))
    save(output/'build-receipt.json',dict(schema='fvr.checkpoint436.normal-build.v1',checkpoint=436,version='0.1.20',built_source_commit=state['source_commit'],validated_source_commit=head,fixture_validation=pin(a.fixture_validation) if repair else None,task_commit=state['task_commit'],source_sha256=state['source_sha256'],source_receipt=state['source_receipt'],reviewed_header=pin(Path(state['reviewed_header'])),source_freeze=pin(output/'source-freeze.json'),normal_cache=caches,binaries=binaries,symbols_by_binary=symbols,symbol_audit=pin(output/'symbol-audit.json'),validation=checks,storage=pin(a.storage),normal=True,full_normal_builds=True,diagnostic_only=False,diagnostic_kind='normal',input_protocol=4,operation_records_changed=False,installed=False,published=False,headset_accepted=False))
    print(json.dumps(dict(receipt=pin(output/'build-receipt.json'),checks=checks)))
if __name__=='__main__':main()
