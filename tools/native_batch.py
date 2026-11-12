"""Reusable reviewed normal/private build evidence, with no delivery shim imports.

Invoke through Invoke-NativeBatch.ps1 so every
write/build has fresh storage admission. This does not install or launch BC2.
"""
from pathlib import Path
from datetime import datetime, timedelta, timezone
import argparse
import hashlib
import importlib.util
import json
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
PRIVATE = 'FVR_BC2_CAMPAIGN_PROVISIONING'
ARTIFACTS = (
    ('probe', 'x86', 'BC2NativeProbe.dll'),
    ('trace', 'x86', 'BC2NativeTrace.exe'),
    ('receiver', 'x64', 'BC2NativeIpcProbe.exe'),
    ('host', 'x64', 'BC2XrHost.exe'),
    ('resolution', 'x86', 'BC2NativeResolution.exe'),
    ('resolution_bootstrap', 'x86', 'BC2ResolutionBootstrap.dll'),
    ('graphics_creation32', 'x86', 'Bc2GraphicsCreation32.dll'),
    ('graphics_creation64', 'x64', 'Bc2GraphicsCreation64.dll'),
)


def need(ok, message):
    if not ok:
        raise ValueError(message)


def load(path):
    def unique(rows):
        out = {}
        for key, value in rows:
            need(key not in out, 'Duplicate JSON key: ' + key)
            out[key] = value
        return out
    return json.loads(Path(path).read_text(encoding='utf-8-sig'), object_pairs_hook=unique)


def pin(path):
    path = Path(path).resolve()
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest.hexdigest())


def checked(row):
    need(isinstance(row, dict) and Path(row['path']).is_absolute(), 'Absolute pin required')
    actual = pin(row['path'])
    need(actual == row, 'Pinned file changed: ' + row['path'])
    return Path(actual['path'])


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def save(path, value):
    with Path(path).open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def verify_host_source(c, baseline, proof, root):
    """Default to the installed closure; a changed closure needs a pinned ROOT review."""
    host = load(checked(c['host_source_receipt']))
    old = load(checked(baseline['host_source_receipt']))
    need(old['source_sha256'] == baseline['host_source_sha256'], 'Baseline host source digest differs')
    need(host == proof.source_receipt(root, roots=('src/xr/OpenXrHost.cpp',)),
         'Host dependency closure changed')
    if 'host_source_update' not in c:
        need(c['host_source_receipt'] == baseline['host_source_receipt'],
             'This route retains the installed host closure; explicit host_source_update review required')
        return host
    review = load(checked(c['host_source_update']))
    need(review['schema'] == 'fvr.normal-host-source-update.v1' and review['root_accepted'] is True,
         'Explicit ROOT host source update review required')
    need(review['source_commit'] == c['source_commit'] and review['source_sha256'] == c['source_sha256'],
         'Host review final source differs')
    need(review['baseline_receipt'] == c['baseline_receipt'] and
         review['previous_host_source_receipt'] == baseline['host_source_receipt'] and
         review['host_source_receipt'] == c['host_source_receipt'], 'Host review receipt pins differ')
    installed = load(checked(review['installed_normal_receipt']))
    need(installed['normal'] is True and installed['full_normal_builds'] is True and
         installed['host_source_receipt'] == baseline['host_source_receipt'] and
         installed['host_source_sha256'] == old['source_sha256'] and
         review['previous_host_binary'] == installed['binaries']['host'], 'Installed normal host binding differs')
    checked(review['previous_host_binary'])
    need(review['full_normal_builds_required'] is True and review['native_capability_grants_changed'] is False and
         c['operation_records_changed'] is False and c['normal_feature_grants_changed'] is False,
         'Host update requires full normal builds without capability grant changes')
    need(old['source_sha256'] != host['source_sha256'], 'Host update must change the source closure')
    # Recipe/build wiring changes need their own review, not a graphics-source exemption.
    for field in set(old) | set(host):
        if field not in ('files', 'source_sha256'):
            need(old.get(field) == host.get(field), 'Host closure metadata changed: ' + field)
    before = {row['path']: row['sha256'] for row in old['files']}
    after = {row['path']: row['sha256'] for row in host['files']}
    need(len(before) == len(old['files']) and len(after) == len(host['files']), 'Duplicate host source file')
    changes = [dict(relative_path=p, before_sha256=before.get(p), sha256=after.get(p))
               for p in sorted(before.keys() | after.keys()) if before.get(p) != after.get(p)]
    need(changes and review['changed_files'] == changes, 'Exact changed host closure file review required')
    candidates = {row['relative_path']: row['sha256'] for row in c['candidate_files']}
    need(len(candidates) == len(c['candidate_files']), 'Duplicate candidate file review')
    for row in changes:
        need(row['sha256'] is not None, 'Host source deletion requires separate review')
        need(candidates.get(row['relative_path']) == row['sha256'], 'Host update missing from candidate file review')
    return host


def verify_updated_host_binary(c, baseline, binary):
    if 'host_source_update' in c:
        old = baseline['binaries']['host']
        checked(old)
        installed = load(checked(c['host_source_update']))['previous_host_binary']
        checked(installed)
        need(binary['sha256'] not in (old['sha256'], installed['sha256']),
             'Changed host source requires a freshly built host binary')


def verify_owner_release(c, now=None):
    """Offline resource release preserves a failed session; it is not gameplay acceptance."""
    need(c['owners_relinquished'] is True, 'ROOT must verify shared owners relinquished')
    need(c['owner_evidence'], 'Actual owner-closure evidence required')
    for row in c['owner_evidence']:
        checked(row)
    if 'offline_resource_release' not in c:
        need(c['closure_and_restoration_verified'] is True,
             'ROOT must verify shared-owner closure and restoration')
        return
    need(c['closure_and_restoration_verified'] is False,
         'Offline resource release must not assert normal session closure')
    release = load(checked(c['offline_resource_release']))
    need(c['offline_resource_release'] in c['owner_evidence'], 'Offline release absent from owner evidence')
    need(release['schema'] == 'fvr.root-offline-resource-release.v1' and release['root_accepted'] is True and
         release['offline_build_only'] is True and release['gameplay_accepted'] is False and
         release['headset_accepted'] is False, 'ROOT offline-only resource review required')
    need(release['source_commit'] == c['source_commit'] and release['run'] == c['run'] and
         release['root_leases'] == c['root_leases'], 'Offline release source/run/leases differ')
    checked(release['root_leases'])
    need(release['last_session'] in c['owner_evidence'], 'Failed session absent from owner evidence')
    session_path = checked(release['last_session'])
    session = load(session_path)
    need(session['status'] == 'failed', 'Offline release must preserve the failed session')
    for field in ('status', 'normal_game_close', 'native_retirement_proven'):
        need(release[field] == session[field], 'Offline release relabels session outcome: ' + field)
    for field in ('exact_three_profiles_restored', 'launcher_active_settings_preserved'):
        need(session[field] is True and release[field] is True, 'Exact session restoration required')
    def utc(value):
        need(isinstance(value, str) and value, 'UTC observation time required')
        parsed = datetime.fromisoformat(value.replace('Z', '+00:00'))
        need(parsed.utcoffset() == timedelta(0), 'UTC observation time required')
        return parsed
    observed, expires = utc(release['checked_utc']), utc(release['expires_utc'])
    now = now or datetime.now(timezone.utc)
    need(observed <= now < expires and timedelta(0) < expires - observed <= timedelta(hours=24),
         'Fresh ROOT offline release required (maximum 24 hours)')
    need(observed.timestamp() >= session_path.stat().st_mtime, 'Offline observation predates session result')
    observation = load(checked(release['process_observation']))
    need(observation['checked_utc'] == release['checked_utc'] and
         observation['all_owned_processes_absent'] is True and observation['shared_build_processes_absent'] is True,
         'Fresh exact process/resource absence observation required')
    expected = {row['name']: row for row in session['processes']}
    actual = {row['name']: row for row in observation['processes']}
    need(expected and len(expected) == len(session['processes']) and
         len(actual) == len(observation['processes']) and actual.keys() == expected.keys(),
         'Exact complete session process observation required')
    for name, row in actual.items():
        prior = expected[name]
        identity = row['identity']
        need(isinstance(identity, dict) and identity == prior['identity'] and
             set(identity) == {'pid', 'start_utc', 'path'} and type(identity['pid']) is int and identity['pid'] > 0 and
             isinstance(identity['path'], str) and identity['path'] and Path(identity['path']).is_absolute(),
             'Exact recorded process identity required: ' + name)
        started = utc(identity['start_utc'])
        need(started < observed and row['absent'] is True and
             prior['exited'] is True and prior['exit_code'] == row['observed_exit_code'] == 0,
             'Session process has not released resources: ' + name)
        replacement = row['replacement_process']
        if replacement is not None:
            need(isinstance(replacement, dict) and set(replacement) == {'pid', 'start_utc', 'path'} and
                 type(replacement['pid']) is int and replacement['pid'] == identity['pid'] and
                 isinstance(replacement['path'], str) and replacement['path'] and Path(replacement['path']).is_absolute(),
                 'Exact replacement process identity required: ' + name)
            need(started < utc(replacement['start_utc']) <= observed,
                 'Replacement process birth must follow original and not exceed observation: ' + name)
    originals_path = checked(release['session_originals'])
    need(originals_path == session_path.parent / 'originals.json', 'Same-session original restoration pins required')
    originals = load(originals_path)
    need(len(originals['profiles']) == 3 and len(originals['launcher']) == 2, 'Three profiles and two launcher files required')
    expected_files = [row['original'] for key in ('profiles', 'launcher') for row in originals[key]]
    need(len({row['path'] for row in expected_files}) == 5 and release['restoration'] == expected_files,
         'Exact original profile/settings restoration pins required')
    for row in release['restoration']:
        checked(row)


def context(path, digest):
    need(pin(path)['sha256'] == digest, 'Configuration hash differs')
    c = load(path)
    need(c['schema'] == 'fvr.native-batch.v1' and c['root_accepted'] is True,
         'Complete and explicitly accept the review before execution')
    root, run = Path(c['repository']).resolve(), Path(c['run']).resolve()
    need(run.is_relative_to(root / 'reports') and run != root / 'reports', 'Fresh canonical report run required')
    need(re.fullmatch('[0-9a-f]{40}', c['source_commit'] or '') is not None,
         'Final applied source commit required; do not infer it from a draft')
    need(re.fullmatch('[0-9a-f]{64}', c['source_sha256'] or '') is not None, 'Final applied source closure required')
    for checkout in (root, Path(c['task_checkout'])):
        need(git(checkout, 'rev-parse', 'HEAD') == c['source_commit'], 'Checkout source differs')
        need(not git(checkout, 'status', '--porcelain', '--untracked-files=no'), 'Tracked checkout is dirty')
    verify_owner_release(c)
    leases = load(checked(c['root_leases']))
    for name in ('COORDINATOR', 'INTEGRATION'):
        lease = leases[name]
        need(load(lease['path'])['owner_token'] == lease['owner_token'], 'Current ROOT lease differs: ' + name)
        need(Path(lease['path']).resolve() == root.parent / 'bc2vr-task-worktrees' / (name + '.lock'),
             'Unexpected lease path')
    for row in c['tool_pins']:
        checked(row)
    checked(c['chest_mesh'])
    tool_paths = {Path(row['path']).resolve() for row in c['tool_pins']}
    need(Path(__file__).resolve() in tool_paths and Path(__file__).with_name('Invoke-NativeBatch.ps1').resolve() in tool_paths,
         'Both actual runner files must be pinned')
    for name in ('Build.ps1', 'tools/StorageBudget.ps1', 'launcher/Launcher.Core.ps1'):
        need(root / name in tool_paths, 'Operational repository helper not pinned: ' + name)
    baseline = load(checked(c['baseline_receipt']))
    need(baseline['normal'] and baseline['full_normal_builds'], 'Passing normal baseline required')
    for row in baseline['validation']:
        checked(row['test_schedule'])
        checked(row['test_details'])
    frozen = load(checked(baseline['source_freeze']))
    need(c['operation_records_changed'] is False and c['normal_feature_grants_changed'] is False,
         'This route cannot review a native capability grant change')
    header = checked(c['reviewed_header'])
    old_header = checked(baseline['reviewed_header']).read_bytes()
    old_digest, new_digest = baseline['source_sha256'].encode(), c['source_sha256'].encode()
    need(old_header.count(old_digest) == 1 and header.read_bytes() == old_header.replace(old_digest, new_digest),
         'Only explicit final-source digest rebinding may differ in the retained operation table')
    proof = module('operation_recipe', checked(c['source_recipe']))
    need(Path(c['source_recipe']['path']).resolve() == root / 'tools/bc2_native_operation_receipts.py',
         'Source recipe must be the final repository recipe')
    operation = proof.verify_source(root, checked(c['source_receipt']), c['source_sha256'], header)
    host = verify_host_source(c, baseline, proof, root)
    need(c['candidate_files'], 'Explicit applied candidate file review required')
    changed = set(git(root, 'diff', '--name-only', baseline['built_source_commit'], c['source_commit']).splitlines())
    paths = {row['relative_path'] for row in c['candidate_files']}
    need(changed <= paths, 'Applied review omits changed tracked files: ' + ', '.join(sorted(changed - paths)))
    covered = {row['path'] for row in operation['files']} | {row['path'] for row in host['files']}
    for row in c['candidate_files']:
        target = root / row['relative_path']
        need(target.resolve().is_relative_to(root), 'Candidate path escapes repository')
        need(pin(target)['sha256'] == row['sha256'], 'Applied candidate bytes changed')
        if row['relative_path'].startswith(('src/', 'include/')):
            need(row['relative_path'] in covered, 'Candidate missing from native/host source closures')
    need(c['private_targets'] == ['BC2NativeProbe', 'Bc2CampaignProvisioningCommand'], 'Only reviewed private targets allowed')
    need(c['private_tests'] == [], 'Core/source-wait checks must execute in normal CTest, once per architecture')
    expected_private = {'Bc2CampaignProvisioningTests', 'Bc2CampaignProvisioningSourceTests'}
    need(set(c['private_registered_suites']) == expected_private, 'Exact private-only suite registration required')
    coverage = set()
    for row in c['reused_private_focused_evidence']:
        need(row['root_accepted'] is True, 'ROOT must accept the scoped prior focused evidence')
        result = load(checked(row['result']))
        need(result['compile_exit'] == result['test_exit'] == 0 and result['source_files'], 'Actual passing compiler/test receipt required')
        arch = 'x64' if result['architecture'] == 'amd64' else result['architecture']
        need(arch == row['architecture'], 'Focused evidence architecture differs')
        for source in result['source_files']:
            path = Path(source['path']).resolve()
            source_root = next((p for p in (root, Path(c['task_checkout']).resolve()) if path.is_relative_to(p)), None)
            need(source_root is not None, 'Focused test source outside reviewed checkouts')
            need(pin(root / path.relative_to(source_root))['sha256'] == source['sha256'], 'Previously tested source bytes changed')
        need(row['unchanged_dependencies_root_reviewed'] is True, 'ROOT must confirm unchanged focused-test dependencies')
        coverage.add((row['suite'], arch))
    need(coverage == {('Bc2CampaignProvisioningTests', 'x86'), ('Bc2CampaignProvisioningTests', 'x64'),
                      ('Bc2CampaignProvisioningSourceTests', 'x86')}, 'Explicit service-both/source-x86 focused evidence required')
    need(c['required_new_normal_suites'], 'Explicit final core/source-wait suite list required')
    need(c['runtime_marker'] == dict(name='campaign-provision.enable', content='campaign-provision-v1\n', default_enabled=False),
         'Exact default-OFF report-local runtime request required')
    for arch in ('x86', 'x64'):
        old = next(row for row in baseline['validation'] if row['architecture'] == arch)
        need(set(c['required_new_normal_suites']) <= set(c['test_additions'][arch]),
             'Core/source-wait tests must be added to both normal architectures')
        need(set(c['required_new_normal_suites']) <= set(c['focused_suites'][arch]),
             'New core/source-wait tests need focused normal coverage')
        need(set(c['focused_suites'][arch]) >= set(old['focused_suites_once']), 'Retained focused suites omitted')
        need(all(row in c['output_expectations'][arch] for row in old['reviewed_output_checks']), 'Retained output checks omitted')
        tuples = {(row['suite'], row['path'], row['entry_call']) for row in c['test_main_bindings']}
        need(all((row['suite'], row['path'], row['entry_call']) in tuples for row in old['diagnostic_test_mains']),
             'Retained test-main call omitted; update applied hashes, never drop old calls')
    for row in c['test_main_bindings']:
        source = root / row['path']
        need(pin(source)['sha256'] == row['applied_sha256'] and row['entry_call'] in source.read_text(encoding='utf-8-sig'),
             'Reviewed test main binding changed')
        need(row['behavior_assertions_root_reviewed'] is True, 'ROOT must review the behavior assertions')
    need(set(c['required_new_normal_suites']) <= {row['suite'] for row in c['test_main_bindings']},
         'New normal suites require final applied source/main bindings')
    need(c['budget']['root_authorized'] is True and c['budget']['explicit_private_exception'] is True,
         'Fresh explicit private storage decision required')
    need(isinstance(c['budget']['reason'], str) and c['budget']['reason'].strip(), 'Actual sizing/retention reason required')
    need(c['budget']['floor_bytes'] == 15 * 1024**3 and c['budget']['build_reservation_bytes'] == 3 * 1024**3,
         'Keep the free floor and build reservation')
    need(all(type(c['budget'][name]) is int and c['budget'][name] >= 40 * 1024**3
             for name in ('build_ceiling_bytes', 'freeze_ceiling_bytes')), 'Explicit reviewed build/freeze ceilings required')
    inventory = load(checked(c['budget']['fresh_storage_inspection']))
    expected_roots = {str(Path(row['path']).resolve()).casefold() for row in inventory['generated']['roots']}
    need(len(expected_roots) == 13, 'Complete thirteen-root storage review required')
    need({str(Path(p).resolve()).casefold() for p in c['budget']['roots']} == expected_roots, 'Storage roots differ')
    return c, root, run, baseline, frozen, proof


def flags(c, frozen, arch, private=False):
    values = {name: dict(row) for name, row in frozen['normal_flags'][arch].items()}
    for name, value in (
        ('BC2_NATIVE_OPERATION_CAPABILITIES_HEADER', c['reviewed_header']['path']),
        ('BC2_NATIVE_OPERATION_SOURCE_RECEIPT', c['source_receipt']['path']),
        ('BC2_NATIVE_OPERATION_SOURCE_SHA256', c['source_sha256']),
    ):
        values[name]['value'] = value.replace('\\', '/')
    values[PRIVATE] = dict(type='BOOL', value='ON' if private else 'OFF')
    values['FVR_TEST_DEBUG_SYMBOLS'] = dict(type='BOOL', value='OFF')
    values['FVR_TEST_GPU'] = dict(type='BOOL', value='OFF')
    return values


def verify_flag_text(text, expected):
    values = {}
    for line in text.splitlines():
        match = re.match(r'((?:BC2_|FVR_)[^:]+):([^=]+)=(.*)', line)
        if match:
            values[match[1]] = dict(type=match[2], value=match[3])
    need(set(values) == set(expected), 'Unexpected or missing BC2/FVR feature flag')
    for name, value in expected.items():
        need(values[name]['value'].replace('\\', '/').casefold() == value['value'].replace('\\', '/').casefold(),
             'Feature value differs: ' + name)
    return values


def verify_flags(cache, expected):
    return verify_flag_text(Path(cache).read_text(encoding='utf-8-sig'), expected)


def verify_schedule(schedule, log, details, old, additions, focused, expectations):
    names = [row['name'] for row in schedule['tests']]
    old_names = [row['name'] for row in old['tests']]
    need(len(set(names)) == len(names) and len(set(additions)) == len(additions), 'Duplicate schedule entry')
    need(not set(old_names) & set(additions) and set(names) == set(old_names) | set(additions), 'Unreviewed suite delta')
    need([name for name in names if name in set(old_names)] == old_names, 'Retained suite order changed')
    passed = re.findall(r'\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.+\s+Passed\s+', log)
    total = re.search(r'100% tests passed, 0 tests failed out of (\d+)', log)
    need(total and int(total[1]) == len(names) == len(passed) and set(passed) == set(names), 'Full normal suite did not pass exactly once')
    blocks = {}
    for name in names:
        matches = re.findall(r'^\d+/\d+ Testing: ' + re.escape(name) + r'\n(.*?)(?=^\d+/\d+ Testing: |\Z)', details, re.M | re.S)
        need(len(matches) == 1 and 'Test Passed.' in matches[0], 'Exact passing CTest detail absent: ' + name)
        blocks[name] = matches[0]
    need(set(focused) <= set(names), 'Focused suite missing from normal run')
    for row in expectations:
        need(row['suite'] in blocks and all(text in blocks[row['suite']] for text in row['contains']), 'Reviewed output missing: ' + row['suite'])
    return names


def freeze_rows(root, private=False):
    rows = [('probe', 'x86', 'BC2NativeProbe.dll'), ('provision_command', 'x86', 'Bc2CampaignProvisioningCommand.exe')] if private else ARTIFACTS
    return [(key, arch, root / 'build' / arch / name,
             root / 'build' / arch / 'symbols' / (Path(name).stem + '.pdb')) for key, arch, name in rows]


def verified_receipt(path, config_pin):
    value = load(path)
    need(value['config'] == config_pin, 'Receipt belongs to another exact review')
    for row in value['files']:
        checked(row)
    return value


def retained_stage_evidence(receipt, directory):
    """Freeze test/cache evidence, excluding mutable shared build outputs."""
    directory = Path(directory).resolve()
    return [row for row in receipt['files'] if Path(row['path']).resolve().parent == directory]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('check', 'check-frozen', 'flags', 'verify-normal', 'freeze-size', 'freeze', 'verify-private'))
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--config-sha256', required=True)
    parser.add_argument('--architecture', choices=('x86', 'x64'))
    parser.add_argument('--private', action='store_true')
    parser.add_argument('--directory', type=Path)
    a = parser.parse_args()
    c, root, run, baseline, frozen, proof = context(a.config, a.config_sha256)
    config_pin = pin(a.config)
    if a.mode == 'check':
        print(json.dumps(dict(checked=True, source_commit=c['source_commit'], normal_builds_required=['x86', 'x64'])))
        return
    if a.mode == 'check-frozen':
        value = verified_receipt(run / 'normal-runtime/receipt.json', config_pin)
        need(value['normal'] is True and value['private_campaign_provisioning'] is False, 'Frozen normal artifacts required')
        for row in value['prerequisite_receipts']:
            checked(row)
        print(json.dumps(dict(verified=True, normal_receipt=pin(run / 'normal-runtime/receipt.json'))))
        return
    if a.mode == 'flags':
        need(a.architecture is not None and (not a.private or a.architecture == 'x86'), 'Explicit architecture required')
        print(json.dumps([f'-D{name}' + ('' if row['type'] == 'UNINITIALIZED' else ':' + row['type']) + '=' + row['value']
                          for name, row in flags(c, frozen, a.architecture, a.private).items() if row['type'] != 'INTERNAL']))
        return
    if a.mode == 'freeze-size':
        print(sum(path.stat().st_size for row in freeze_rows(root, a.private) for path in row[2:]) + 4 * 1024**2)
        return
    directory = a.directory.resolve()
    expected = run / ('private-x86' if a.mode == 'verify-private' else 'normal-runtime' if a.mode == 'freeze' else 'normal-' + str(a.architecture))
    need(directory == expected, 'Stage output directory differs')
    if a.mode == 'verify-normal':
        arch = a.architecture
        normal = load(directory / 'build.json')
        need(normal['built'] and normal['tests_passed'] and normal['architecture'] == arch,
             'Actual Build.ps1 success receipt required')
        need(Path(normal['build_directory']).resolve() == root / 'build' / arch, 'Canonical build tree required')
        previous = next(row for row in baseline['validation'] if row['architecture'] == arch)
        names = verify_schedule(load(directory / 'test-list.json'), (directory / 'tests.log').read_text(encoding='utf-8-sig'),
                                (directory / 'LastTest.log').read_text(encoding='utf-8-sig'), load(previous['test_schedule']['path']),
                                c['test_additions'][arch], c['focused_suites'][arch], c['output_expectations'][arch])
        details = (directory / 'LastTest.log').read_text(encoding='utf-8-sig')
        for suite, retained in previous['muzzle_suites'].items():
            block = re.search(r'^\d+/\d+ Testing: ' + re.escape(suite) + r'\n(.*?)(?=^\d+/\d+ Testing: |\Z)', details, re.M | re.S)
            need(block is not None, 'Retained muzzle suite missing')
            if 'groups' in retained:
                need(re.findall(r'^([A-Z] .+ passed)$', block[1], re.M) == retained['groups'], 'Retained muzzle groups changed')
            else:
                need('optional observer disabled: normal shot layout has no observer member' in block[1], 'Normal disabled observer layout missing')
        if arch == 'x86':
            host = re.search(r'^\d+/\d+ Testing: Bc2ProjectileTargetHostTests\n(.*?)(?=^\d+/\d+ Testing: |\Z)', details, re.M | re.S)
            need(host and re.search(r'\d+ checks, 0 failures', host[1]), 'Retained observer host checks missing')
        need('private cache parser passed' in (directory / 'chest-mesh.log').read_text(encoding='utf-8-sig'), 'Retained chest mesh check missing')
        need('chest-attack-transaction exited 0' in (directory / 'chest-attack.log').read_text(encoding='utf-8-sig'), 'Retained chest transaction check missing')
        actual_flags = verify_flags(directory / 'CMakeCache.txt', flags(c, frozen, arch))
        proof.verify_build_config(directory / 'Bc2NativeOperationBuild.generated.h', Path(c['reviewed_header']['path']), c['source_sha256'])
        files = [pin(p) for p in sorted(directory.iterdir()) if p.is_file()]
        # Bind actual outputs now; Freeze rejects build-tree changes between tests and copying.
        files += [pin(p) for row in freeze_rows(root) if row[1] == arch for p in row[2:]]
        if arch == 'x64':
            verify_updated_host_binary(c, baseline, pin(root / 'build/x64/BC2XrHost.exe'))
        save(directory / 'receipt.json', dict(schema='fvr.native-batch-normal.v1', config=config_pin,
             architecture=arch, source_commit=c['source_commit'], source_sha256=c['source_sha256'],
             host_source_receipt=c['host_source_receipt'], normal=True, suites=names, flags=actual_flags, files=files))
        return
    retained_normal_files = []
    if a.mode == 'freeze':
        normal_receipts = []
        for arch in ('x86', 'x64'):
            path = run / ('normal-' + arch) / 'receipt.json'
            normal_stage = verified_receipt(path, config_pin)
            retained_normal_files.extend(retained_stage_evidence(normal_stage, path.parent))
            normal_receipts.append(pin(path))
    else:
        normal_receipts = [pin(run / 'normal-runtime' / 'receipt.json')]
        normal = verified_receipt(normal_receipts[0]['path'], config_pin)
        normal_probe = next(row for row in normal['artifacts'] if row['key'] == 'probe')['binary']
        need(pin(root / 'build/x86/BC2NativeProbe.dll')['sha256'] != normal_probe['sha256'],
             'Private probe must be a distinct compiled-ON artifact')
        verify_flags(directory / 'CMakeCache.txt', flags(c, frozen, 'x86', True))
        private_names = [row['name'] for row in load(directory / 'test-list.json')['tests']]
        normal_names = [row['name'] for row in load(run / 'normal-x86/test-list.json')['tests']]
        need(len(private_names) == len(set(private_names)) and set(private_names) == set(normal_names) | set(c['private_registered_suites'])
             and [name for name in private_names if name in set(normal_names)] == normal_names,
             'Private configure changed retained normal schedule or added unexpected suites')
        proof.verify_build_config(root / 'build/x86/generated/bc2-operation/Bc2NativeOperationBuild.generated.h', Path(c['reviewed_header']['path']), c['source_sha256'])
    audit = module('symbol_audit', checked(c['symbol_audit_tool']))
    records, files = [], [pin(p) for p in sorted(directory.iterdir()) if p.is_file()]
    files.extend(retained_normal_files)
    for key, arch, binary, pdb in freeze_rows(root, a.mode == 'verify-private'):
        if key == 'host':
            verify_updated_host_binary(c, baseline, pin(binary))
        pe, symbols = audit.pe_identity(binary, arch), audit.pdb_identity(pdb, audit.pe_identity(binary, arch)['image_size'])
        need((pe['guid'], pe['age']) == (symbols['guid'], symbols['age']), 'PE/PDB mismatch: ' + key)
        pair = dict(key=key, architecture=arch, pe=pe, symbols=symbols)
        for kind, source in (('binary', binary), ('pdb', pdb)):
            target = directory / source.name
            need(not target.exists(), 'Preserve existing artifact')
            shutil.copyfile(source, target)
            need(pin(target)['sha256'] == pin(source)['sha256'], 'Frozen artifact differs')
            pair[kind] = pin(target)
            files.append(pair[kind])
        records.append(pair)
    save(directory / 'receipt.json', dict(schema='fvr.native-batch-artifacts.v1', config=config_pin,
         source_commit=c['source_commit'], source_sha256=c['source_sha256'], normal=a.mode == 'freeze',
         host_source_receipt=c['host_source_receipt'],
         private_campaign_provisioning=a.mode == 'verify-private', runtime_default_enabled=False,
         prerequisite_receipts=normal_receipts, artifacts=records, files=files,
         reused_private_focused_evidence=c['reused_private_focused_evidence'] if a.mode == 'verify-private' else [],
         installed=False, game_tested=False, headset_accepted=False))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as exc:
        print('REFUSED: ' + str(exc), file=sys.stderr)
        sys.exit(2)
