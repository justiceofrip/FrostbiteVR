"""Close the exact source roster using retained combined checks, without installation."""
from pathlib import Path
import hashlib
import importlib.util
import json

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]

def load(path):
    return json.loads(path.read_text(encoding='utf-8'))

def write(path, value):
    path.write_text(json.dumps(value, indent=2)+'\n', encoding='utf-8')

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

reports = ROOT/'reports'
x86_runs = ['coordinator-20261030-weapons50-x86-05',
            'coordinator-20261030-weapons51-x86-06',
            'coordinator-20261030-weapons51-fixed08']
x64_run = 'coordinator-20261030-weapons51-x64-02'
checks = {}
for architecture, runs in [('x86', x86_runs), ('x64', [x64_run])]:
    latest = {}
    for run in runs:
        for check in load(reports/run/'results.json')['checks']:
            latest[check['suite']] = dict(check, report=run)
    assert len(latest) == 30 and all(c['compiled'] and c['passed'] for c in latest.values())
    checks[architecture] = latest

coverage_path = HERE.parent/'20261030-coverage50/coverage.json'
coverage = load(coverage_path)
assert coverage['complete'] and coverage['summary']['metadata_complete'] == 181
joined = {r['configuration']: r for r in coverage['rows']}
roster_path = HERE/'roster.json'
roster = load(roster_path)
assert len(roster['rows']) == len(joined) == 181
for row in roster['rows']:
    exact = joined[row['configuration']]
    assert exact['asset'] == row['asset'] and exact['metadata_complete']
    row.update(two_hand='source_complete_exact_selector',
               manual_reload='source_complete_family_runtime_geometry',
               source_complete=True, installed=False, headset_accepted=False)
roster['summary']['source_complete'] = 181
roster['completion'] = dict(evidence='combined offline source checks; native/headset acceptance pending',
                            receipt='completion.json', integrated=False, installed=False)
write(roster_path, roster)
# Refresh the coverage input hash after closing status fields in the roster.
script = coverage_path.with_name('check_coverage.py')
spec = importlib.util.spec_from_file_location('weapon_coverage', script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
assert module.run() == 0

cache = reports/'coordinator-20261030-cache48-enrollment01/body-ammo.fvrprop'
assert cache.stat().st_size == 54281907
assert digest(cache) == 'd0135f2f44f31322ac5e3971b82b26d665c22c48d9b6a39a04dd489f95b396aa'
write(HERE/'completion.json', dict(
    schema='fvr.bc2.firearm_source_completion.v1', source_complete=True,
    configurations=181, asset_names=77,
    evidence='exact configuration joins, changed-component compilation, applicable production syntax and offline tests',
    checks=checks, coverage=dict(path=str(coverage_path.relative_to(ROOT)), sha256=digest(coverage_path)),
    production_syntax=dict(x86='coordinator-20261030-weapons51-x86-06',
                           x64=x64_run, x64_excludes='Gameplay/RigPublication/NativeProbe are x86-only build targets'),
    cache=dict(path=str(cache.relative_to(ROOT)), records=252, bytes=cache.stat().st_size, sha256=digest(cache),
               verification='actual CPU cache consumers passed on x86 and x64; incremental48 replay passed'),
    python_checks=dict(report='coordinator-20261030-assetchecks50', passed=11,
                       reused='prior family authoring and cache completion passes remain applicable'),
    enrollment='profiles/weapons43/Enrollment.cmake',
    normal_dll_build=False, merged=False, pushed=False, installed=False, headset_accepted=False,
    installed_version='0.1.17 unchanged',
    next='Integrate the complete source batch and cache only when user requests integration; then use normal existing build trees.'))
print('Source complete:181 configurations;30/30 focused suites per architecture; installation unchanged.')
