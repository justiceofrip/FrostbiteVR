"""Translate genuine frozen normal evidence into shipping metadata; no artifact copies.

Only --emit writes (three new JSON files). No subprocess, build, Git, installation,
profile or game actions. Historical stage build paths are compared, never opened.
Promoted from normal83's Adapt-NormalDelivery.py; accepts an explicitly reviewed
host source update only with a fresh host from both full normal builds and freeze.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ARTIFACTS = {
    'probe': ('x86', 'BC2NativeProbe.dll'),
    'trace': ('x86', 'BC2NativeTrace.exe'),
    'receiver': ('x64', 'BC2NativeIpcProbe.exe'),
    'host': ('x64', 'BC2XrHost.exe'),
    'resolution': ('x86', 'BC2NativeResolution.exe'),
    'resolution_bootstrap': ('x86', 'BC2ResolutionBootstrap.dll'),
    'graphics_creation32': ('x86', 'Bc2GraphicsCreation32.dll'),
    'graphics_creation64': ('x64', 'Bc2GraphicsCreation64.dll'),
}


def need(ok, message):
    if not ok:
        raise ValueError(message)


def load(path):
    def unique(rows):
        result = {}
        for key, value in rows:
            need(key not in result, 'Duplicate JSON key: ' + key)
            result[key] = value
        return result
    return json.loads(Path(path).read_text(encoding='utf-8-sig'), object_pairs_hook=unique)


def pin(path):
    path = Path(path).resolve()
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest.hexdigest())


def checked(row):
    need(isinstance(row, dict) and set(row) == {'path', 'bytes', 'sha256'}, 'Exact file pin required')
    need(Path(row['path']).is_absolute(), 'Absolute file pin required')
    need(pin(row['path']) == row, 'Changed pinned file: ' + row['path'])
    return Path(row['path']).resolve()


def module(name, row):
    spec = importlib.util.spec_from_file_location(name, checked(row))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def stage_identity(stage, arch, config_pin, config):
    need(stage['schema'] == 'fvr.native-batch-normal.v1' and stage['normal'] is True,
         'Genuine normal-stage receipt required')
    need(stage['architecture'] == arch and stage['config'] == config_pin,
         'Stage architecture/config differs')
    need(stage['source_commit'] == config['source_commit'] and
         stage['source_sha256'] == config['source_sha256'], 'Stage source differs')
    need(stage['flags']['FVR_BC2_CAMPAIGN_PROVISIONING']['value'] == 'OFF',
         'Private provisioning in normal receipt')
    need(stage['flags']['BC2_PROJECTILE_TARGET_OBSERVER']['value'] ==
         {'x86': 'ON', 'x64': 'OFF'}[arch], 'Observer compile contract differs')
    need(len(stage['suites']) == len(set(stage['suites'])), 'Duplicate normal suite')


def artifact_identity(row, stages, frozen_dir):
    key = row['key']
    need(key in ARTIFACTS, 'Unknown artifact')
    arch, filename = ARTIFACTS[key]
    need(row['architecture'] == row['pe']['architecture'] == arch, 'Artifact architecture differs')
    need((row['pe']['guid'], row['pe']['age']) ==
         (row['symbols']['guid'], row['symbols']['age']), 'PE/PDB identity differs')
    for kind, name in (('binary', filename), ('pdb', Path(filename).stem + '.pdb')):
        need(Path(row[kind]['path']).resolve() == frozen_dir / name,
             'Artifact escapes genuine normal freeze')
        prior = [p for p in stages[arch]['files'] if Path(p['path']).name == name]
        need(len(prior) == 1 and all(prior[0][k] == row[kind][k] for k in ('bytes', 'sha256')),
             'Frozen artifact differs from tested normal output')


def derive(config_path, config_sha, normal_sha, version):
    need(re.fullmatch(r'\d+\.\d+\.\d+', version) is not None, 'Explicit package version required')
    config_pin = pin(config_path)
    need(config_pin['sha256'] == config_sha, 'Config SHA differs')
    c = load(config_path)
    need(c['schema'] == 'fvr.native-batch.v1' and c['root_accepted'] is True, 'Accepted native config required')
    need(c['operation_records_changed'] is False and c['normal_feature_grants_changed'] is False,
         'Operation/feature changes require a different review')
    root, run = Path(c['repository']).resolve(), Path(c['run']).resolve()
    need(run.is_relative_to(root / 'reports'), 'Canonical retained run required')
    frozen_dir = run / 'normal-runtime'
    normal_pin = pin(frozen_dir / 'receipt.json')
    need(normal_pin['sha256'] == normal_sha, 'Normal freeze SHA differs')
    n = load(normal_pin['path'])
    need(n['schema'] == 'fvr.native-batch-artifacts.v1' and n['normal'] is True and
         n['private_campaign_provisioning'] is False, 'Genuine normal freeze required')
    need(n['config'] == config_pin and n['source_commit'] == c['source_commit'] and
         n['source_sha256'] == c['source_sha256'], 'Frozen source/config differs')
    need(n['runtime_default_enabled'] is False, 'Private runtime marker enabled')
    allowed = {frozen_dir, run / 'normal-x86', run / 'normal-x64'}
    frozen_files = {}
    for row in n['files']:
        p = Path(row['path']).resolve()
        need(p.parent in allowed and p not in frozen_files, 'Unexpected/duplicate frozen evidence path')
        checked(row)
        frozen_files[p] = row
    baseline = load(checked(c['baseline_receipt']))
    baseline_freeze = load(checked(baseline['source_freeze']))
    need(baseline['normal'] is True and baseline['full_normal_builds'] is True, 'Full normal baseline required')
    old_header = checked(baseline['reviewed_header']).read_bytes()
    header = checked(c['reviewed_header'])
    need(old_header.count(baseline['source_sha256'].encode()) == 1 and header.read_bytes() ==
         old_header.replace(baseline['source_sha256'].encode(), c['source_sha256'].encode()),
         'Reviewed operation table changed')
    source_recipe = module('delivery_source_recipe', c['source_recipe'])
    source_recipe.verify_source(root, checked(c['source_receipt']), c['source_sha256'], header)
    # Import only pinned, pure validators; never call native_batch.context/main/git.
    batch_pin = next(p for p in c['tool_pins'] if Path(p['path']).resolve() == root / 'tools/native_batch.py')
    batch = module('delivery_batch_validators', batch_pin)
    host = batch.verify_host_source(c, baseline, source_recipe, root)
    if 'host_source_update' in c:
        need(n['host_source_receipt'] == c['host_source_receipt'], 'Freeze host closure differs')
    stages, validations, caches = {}, [], {}
    for row in n['prerequisite_receipts']:
        path = Path(row['path']).resolve()
        need(path in {run / 'normal-x86/receipt.json', run / 'normal-x64/receipt.json'},
             'Unexpected prerequisite path')
        stage = load(checked(row))
        arch = stage['architecture']
        need(arch in ('x86', 'x64') and arch not in stages, 'Duplicate/unknown normal stage')
        stage_identity(stage, arch, config_pin, c)
        if 'host_source_update' in c:
            need(stage['host_source_receipt'] == c['host_source_receipt'], 'Stage host closure differs')
        directory = run / ('normal-' + arch)
        local = {}
        for evidence in stage['files']:
            p = Path(evidence['path']).resolve()
            if p.parent == directory:
                need(p in frozen_files and frozen_files[p] == evidence, 'Stage evidence absent from freeze')
                need(p.name not in local, 'Duplicate stage-local evidence')
                local[p.name] = evidence  # Already hash checked through n.files.
        required = {'build.json', 'test-list.json', 'tests.log', 'LastTest.log', 'CMakeCache.txt',
                    'Bc2NativeOperationBuild.generated.h', 'chest-mesh.log', 'chest-attack.log'}
        need(required <= local.keys(), 'Incomplete retained normal evidence')
        build = load(local['build.json']['path'])
        need(build['built'] is True and build['tests_passed'] is True and build['architecture'] == arch,
             'Actual successful normal Build.ps1 receipt required')
        prior = next(v for v in baseline['validation'] if v['architecture'] == arch)
        previous_schedule = load(checked(prior['test_schedule']))
        details = Path(local['LastTest.log']['path']).read_text(encoding='utf-8-sig')
        names = batch.verify_schedule(load(local['test-list.json']['path']),
                    Path(local['tests.log']['path']).read_text(encoding='utf-8-sig'), details,
                    previous_schedule, c['test_additions'][arch], c['focused_suites'][arch],
                    c['output_expectations'][arch])
        need(names == stage['suites'], 'Stage suite list differs from retained schedule')
        flags = batch.verify_flags(local['CMakeCache.txt']['path'], batch.flags(c, baseline_freeze, arch))
        need(flags == stage['flags'], 'Retained flags differ')
        source_recipe.verify_build_config(Path(local['Bc2NativeOperationBuild.generated.h']['path']),
                                          header, c['source_sha256'])
        muzzle = {}
        for suite, old in prior['muzzle_suites'].items():
            block = re.search(r'^\d+/\d+ Testing: ' + re.escape(suite) +
                              r'\n(.*?)(?=^\d+/\d+ Testing: |\Z)', details, re.M | re.S)
            need(block is not None, 'Retained muzzle suite missing')
            if 'groups' in old:
                groups = re.findall(r'^([A-Z] .+ passed)$', block[1], re.M)
                need(groups == old['groups'], 'Retained muzzle groups differ')
                muzzle[suite] = dict(groups=groups)
            else:
                need('optional observer disabled: normal shot layout has no observer member' in block[1],
                     'Disabled normal observer layout missing')
                muzzle[suite] = dict(disabled_layout=True)
        host_checks = None
        if arch == 'x86':
            block = re.search(r'^\d+/\d+ Testing: Bc2ProjectileTargetHostTests\n(.*?)(?=^\d+/\d+ Testing: |\Z)',
                              details, re.M | re.S)
            match = re.search(r'(\d+) checks, 0 failures', block[1]) if block else None
            need(match is not None, 'Retained host checks missing')
            host_checks = int(match[1])
        need('private cache parser passed' in Path(local['chest-mesh.log']['path']).read_text(), 'Chest mesh check absent')
        need('chest-attack-transaction exited 0' in Path(local['chest-attack.log']['path']).read_text(), 'Chest transaction absent')
        bindings = [row for row in c['test_main_bindings'] if arch in row['architectures']]
        for binding in bindings:
            source = root / binding['path']
            need(source.resolve().is_relative_to(root) and pin(source)['sha256'] == binding['applied_sha256'] and
                 binding['entry_call'] in source.read_text(encoding='utf-8-sig') and
                 binding['behavior_assertions_root_reviewed'] is True, 'Reviewed test-main binding differs')
        validations.append(dict(architecture=arch, suites=len(names), baseline_preserved=True,
            baseline_suites=len(previous_schedule['tests']), reviewed_added_suites=c['test_additions'][arch],
            reviewed_output_checks=c['output_expectations'][arch], focused_suites_once=c['focused_suites'][arch],
            diagnostic_test_mains=bindings,
            muzzle_suites=muzzle, host_checks_observed=host_checks, chest_mesh=local['chest-mesh.log'],
            chest_transaction=local['chest-attack.log'], tests=local['tests.log'],
            test_schedule=local['test-list.json'], test_details=local['LastTest.log'],
            build=local['build.json'], result=row))
        stages[arch] = stage
        caches[arch] = local['CMakeCache.txt']
    need(set(stages) == {'x86', 'x64'}, 'Both normal stages required')
    pairs, binaries, symbols = [], {}, {}
    for row in n['artifacts']:
        artifact_identity(row, stages, frozen_dir)
        key = row['key']
        need(key not in binaries, 'Duplicate artifact key')
        for kind in ('binary', 'pdb'):
            need(frozen_files.get(Path(row[kind]['path']).resolve()) == row[kind], 'Artifact absent from frozen file set')
        binaries[key], symbols[key] = row['binary'], row['pdb']
        pairs.append(copy.deepcopy(row))
    need(set(binaries) == set(ARTIFACTS), 'All eight frozen binary/PDB pairs required')
    batch.verify_updated_host_binary(c, baseline, binaries['host'])
    observer = dict(x86='ON', x64='OFF', runtime_request_required=True, default_cmake='OFF',
                    passive_observer_read_only=True, native_origin_acceptance=False)
    provenance = dict(normal_receipt=normal_pin, config=config_pin, adapter=pin(__file__),
                      source_recipe=c['source_recipe'], baseline_receipt=c['baseline_receipt'])
    host_metadata = dict(host_source_receipt=c['host_source_receipt'], host_source_sha256=host['source_sha256'],
                         host_source_review=dict(basis='Exact pinned config/baseline/actual source closure comparison',
                                                 config=config_pin, baseline_receipt=c['baseline_receipt']))
    if 'host_source_update' in c:
        host_metadata['host_source_review'].update(
            basis='ROOT-reviewed host closure update; fresh host from actual full normal x86/x64 builds and freeze',
            review=c['host_source_update'], previous_host_source_receipt=baseline['host_source_receipt'],
            tested_host_binary=binaries['host'])
    freeze = dict(source_commit=c['source_commit'], validated_source_commit=c['source_commit'],
                  source_sha256=c['source_sha256'], source_receipt=c['source_receipt'],
                  reviewed_header=c['reviewed_header'], normal_flags={a: s['flags'] for a, s in stages.items()},
                  optional_observer=observer, provenance=provenance, **host_metadata)
    audit = dict(passed=True, basis='Hash-verified frozen files and genuine retained PE/PDB identity records',
                 pairs=pairs, provenance=provenance, new_symbol_scan=False)
    receipt = dict(schema='fvr.checkpoint436.normal-build.v1', checkpoint=436, version=version,
        built_source_commit=c['source_commit'], validated_source_commit=c['source_commit'],
        native_compiled_source_commit_by_architecture={a: s['source_commit'] for a, s in stages.items()},
        validated_source_commit_by_architecture={a: s['source_commit'] for a, s in stages.items()},
        source_sha256=c['source_sha256'], source_receipt=c['source_receipt'], reviewed_header=c['reviewed_header'],
        normal_cache=caches, binaries=binaries, symbols_by_binary=symbols, validation=validations,
        optional_observer=observer, provenance=provenance, normal=True, full_normal_builds=True,
        diagnostic_only=False, diagnostic_kind='normal', input_protocol=4, operation_records_changed=False,
        fixture_validation=None, installed=False, published=False, game_tested=False, headset_accepted=False,
        **host_metadata)
    return receipt, freeze, audit, stages


def serialized(value):
    return (json.dumps(value, indent=2) + '\n').encode('utf-8')


def metadata_set(output, receipt, freeze, audit):
    result = {'source-freeze.json': serialized(freeze), 'symbol-audit.json': serialized(audit)}
    for key, name in (('source_freeze', 'source-freeze.json'), ('symbol_audit', 'symbol-audit.json')):
        raw = result[name]
        receipt[key] = dict(path=str(output / name), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
    result['build-receipt.json'] = serialized(receipt)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', type=Path, required=True)
    p.add_argument('--config-sha256', required=True)
    p.add_argument('--normal-sha256', required=True)
    p.add_argument('--version', required=True)
    p.add_argument('--output', type=Path)
    modes = p.add_mutually_exclusive_group()
    modes.add_argument('--emit', action='store_true')
    modes.add_argument('--verify-output', action='store_true')
    modes.add_argument('--self-test', action='store_true')
    args = p.parse_args()
    receipt, freeze, audit, stages = derive(args.config, args.config_sha256, args.normal_sha256, args.version)
    rejected = []
    if args.self_test:
        c = load(args.config)
        cp = pin(args.config)
        need(all(v['baseline_suites'] + len(v['reviewed_added_suites']) == v['suites']
                 for v in receipt['validation']), 'Normal schedule arithmetic differs')
        preview = metadata_set(Path(__file__).resolve().parent / 'schema-preview',
                               copy.deepcopy(receipt), freeze, audit)
        shipping = json.loads(preview['build-receipt.json'])
        need(shipping['schema'] == 'fvr.checkpoint436.normal-build.v1' and shipping['checkpoint'] == 436
             and set(shipping['binaries']) == set(shipping['symbols_by_binary']) == set(ARTIFACTS)
             and shipping['normal'] is True and shipping['full_normal_builds'] is True
             and shipping['diagnostic_only'] is False and shipping['installed'] is False,
             'Shipping metadata schema differs')
        for field, filename in (('source_freeze', 'source-freeze.json'), ('symbol_audit', 'symbol-audit.json')):
            need(shipping[field]['bytes'] == len(preview[filename]) and shipping[field]['sha256'] ==
                 hashlib.sha256(preview[filename]).hexdigest(), 'Derived metadata pin differs')
        mutations = [('private', lambda s: s['flags']['FVR_BC2_CAMPAIGN_PROVISIONING'].update(value='ON')),
                     ('wrong-source', lambda s: s.update(source_commit='0' * 40)),
                     ('false-normal', lambda s: s.update(normal=False)),
                     ('duplicate-suite', lambda s: s['suites'].append(s['suites'][0])),
                     ('wrong-observer', lambda s: s['flags']['BC2_PROJECTILE_TARGET_OBSERVER'].update(value='OFF'))]
        for name, mutation in mutations:
            s = copy.deepcopy(stages['x86']); mutation(s)
            try:
                stage_identity(s, 'x86', cp, c)
            except ValueError:
                rejected.append(name)
            else:
                raise ValueError('Negative check accepted: ' + name)
        for name, bad_config, bad_normal in [('wrong-config-pin', '0' * 64, args.normal_sha256),
                                             ('wrong-normal-pin', args.config_sha256, '0' * 64)]:
            try:
                derive(args.config, bad_config, bad_normal, args.version)
            except ValueError:
                rejected.append(name)
            else:
                raise ValueError('Negative check accepted: ' + name)
        frozen_dir = Path(receipt['provenance']['normal_receipt']['path']).parent
        for name, mutation in [('mutable-binary-path', lambda a: a['binary'].update(path=str(Path(c['repository']) / 'build/x86/BC2NativeProbe.dll'))),
                               ('wrong-pdb', lambda a: a['symbols'].update(age=-1)),
                               ('untested-hash', lambda a: a['binary'].update(sha256='0' * 64))]:
            row = copy.deepcopy(audit['pairs'][0]); mutation(row)
            try:
                artifact_identity(row, stages, frozen_dir)
            except ValueError:
                rejected.append(name)
            else:
                raise ValueError('Negative check accepted: ' + name)
    outputs = None
    if args.emit or args.verify_output:
        need(args.output is not None, 'Output directory required')
        output = args.output.resolve()
        need(not output.is_relative_to(Path(receipt['provenance']['normal_receipt']['path']).parent),
             'Never write into genuine normal freeze')
        outputs = metadata_set(output, receipt, freeze, audit)
        if args.emit:
            output.mkdir(parents=True, exist_ok=True)
            need(all(not (output / name).exists() for name in outputs), 'Preserve existing metadata')
            for name, raw in outputs.items():
                with (output / name).open('xb') as stream:
                    stream.write(raw)
        else:
            for name, raw in outputs.items():
                need((output / name).read_bytes() == raw, 'Derived metadata differs: ' + name)
    print(json.dumps(dict(verified=True, version=args.version,
        suites={v['architecture']: v['suites'] for v in receipt['validation']},
        binary_pdb_pairs=len(audit['pairs']), rejected_negative_checks=rejected,
        outputs=[pin(args.output / n) for n in outputs] if outputs else [],
        installed=False, packaged=False, game_tested=False, headset_accepted=False), indent=2))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, StopIteration) as error:
        print(json.dumps(dict(verified=False, refused=str(error))), file=sys.stderr)
        sys.exit(1)
