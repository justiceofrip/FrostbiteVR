"""Offline normal delivery guards; synthetic artifacts never establish native acceptance."""
import copy
from datetime import datetime, timedelta, timezone
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parents[1] / 'tools'


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


batch = module('normal_batch_test', TOOLS / 'native_batch.py')
delivery = module('normal_delivery_test', TOOLS / 'normal_native_delivery.py')
proof = module('normal_source_test', TOOLS / 'bc2_native_operation_receipts.py')


class DeliveryTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        self.run = self.root / 'reports/run'
        self.frozen = self.run / 'normal-runtime'
        self.frozen.mkdir(parents=True)
        for name in ('native_batch.py', 'bc2_native_operation_receipts.py'):
            self.write('tools/' + name, (TOOLS / name).read_bytes())
        for name in proof.ROOTS:
            self.write(name, b'// native source\n')
        self.write('cmake/Bc2NativeOperationCapability.cmake', b'# wiring\n')
        self.write('src/xr/OpenXrHost.cpp', b'#include "fvr/D3D11BodyPropCompositor.h"\n')
        self.write('include/fvr/D3D11BodyPropCompositor.h', b'#pragma once\n')
        self.write('src/graphics/D3D11BodyPropCompositor.cpp', b'// old graphics\n')
        old_host = proof.source_receipt(self.root, roots=('src/xr/OpenXrHost.cpp',))
        old_source = proof.source_receipt(self.root)['source_sha256']
        old_header = self.write('baseline/header.h', ('// ' + old_source).encode())
        self.old_host = self.json('baseline/host-source.json', old_host)
        self.old_binary = self.write('baseline/BC2XrHost.exe', b'old host')
        flags = {name: dict(type='STRING', value='old') for name in
                 ('BC2_NATIVE_OPERATION_CAPABILITIES_HEADER', 'BC2_NATIVE_OPERATION_SOURCE_RECEIPT',
                  'BC2_NATIVE_OPERATION_SOURCE_SHA256')}
        self.baseline = dict(normal=True, full_normal_builds=True, source_sha256=old_source,
            host_source_receipt=self.old_host, host_source_sha256=old_host['source_sha256'],
            reviewed_header=old_header, binaries=dict(host=self.old_binary), validation=[])
        normal_flags = {}
        self.suites = {'x86': ['Retained', 'Bc2ProjectileTargetHostTests'], 'x64': ['Retained']}
        for arch, names in self.suites.items():
            normal_flags[arch] = dict(flags, BC2_PROJECTILE_TARGET_OBSERVER=dict(type='BOOL', value='ON' if arch == 'x86' else 'OFF'))
            schedule = self.json('baseline/' + arch + '-schedule.json', dict(tests=[dict(name=n) for n in names]))
            self.baseline['validation'].append(dict(architecture=arch, test_schedule=schedule, muzzle_suites={}))
        self.baseline_freeze = dict(normal_flags=normal_flags)
        self.baseline['source_freeze'] = self.json('baseline/freeze.json', self.baseline_freeze)
        self.write(proof.ROOTS[0], b'// updated native\n')
        self.write('src/graphics/D3D11BodyPropCompositor.cpp', b'#include "SpentCaseAsset.h"\n')
        self.write('src/graphics/SpentCaseAsset.h', b'// new asset\n')
        source_sha = proof.source_receipt(self.root)['source_sha256']
        header = self.write('review/header.h', ('// ' + source_sha).encode())
        self.host = proof.source_receipt(self.root, roots=('src/xr/OpenXrHost.cpp',))
        self.config = dict(schema='fvr.native-batch.v1', root_accepted=True,
            repository=str(self.root), run=str(self.run), source_commit='a' * 40,
            source_sha256=source_sha, reviewed_header=header,
            source_recipe=batch.pin(self.root / 'tools/bc2_native_operation_receipts.py'),
            source_receipt=self.json('review/source.json', proof.source_receipt(self.root, header['path'])),
            host_source_receipt=self.json('review/host-source.json', self.host),
            baseline_receipt=self.json('baseline/receipt.json', self.baseline),
            tool_pins=[batch.pin(self.root / 'tools/native_batch.py')], operation_records_changed=False,
            normal_feature_grants_changed=False, candidate_files=[],
            test_main_bindings=[],
            test_additions={a: [] for a in self.suites}, focused_suites=self.suites,
            output_expectations={a: [] for a in self.suites})
        before = {r['path']: r['sha256'] for r in old_host['files']}
        after = {r['path']: r['sha256'] for r in self.host['files']}
        changes = [dict(relative_path=p, before_sha256=before.get(p), sha256=after.get(p))
                   for p in sorted(before.keys() | after.keys()) if before.get(p) != after.get(p)]
        self.config['candidate_files'] = [dict(relative_path=r['relative_path'], sha256=r['sha256']) for r in changes]
        self.review = dict(schema='fvr.normal-host-source-update.v1', root_accepted=True,
            source_commit=self.config['source_commit'], source_sha256=source_sha,
            baseline_receipt=self.config['baseline_receipt'], previous_host_source_receipt=self.old_host,
            installed_normal_receipt=self.config['baseline_receipt'], previous_host_binary=self.old_binary,
            host_source_receipt=self.config['host_source_receipt'], full_normal_builds_required=True,
            native_capability_grants_changed=False, changed_files=changes)
        self.bind_review()
        self.stages = {}
        self.normal = dict(schema='fvr.native-batch-artifacts.v1', config=self.config_pin,
            normal=True, private_campaign_provisioning=False, runtime_default_enabled=False,
            source_commit=self.config['source_commit'], source_sha256=source_sha,
            host_source_receipt=self.config['host_source_receipt'], files=[], artifacts=[], prerequisite_receipts=[])
        for arch, names in self.suites.items():
            stage = dict(schema='fvr.native-batch-normal.v1', architecture=arch, config=self.config_pin,
                source_commit=self.config['source_commit'], source_sha256=source_sha, normal=True,
                host_source_receipt=self.config['host_source_receipt'], suites=names,
                flags=batch.flags(self.config, self.baseline_freeze, arch), files=[])
            local = {'build.json': dict(built=True, tests_passed=True, architecture=arch),
                     'test-list.json': dict(tests=[dict(name=n) for n in names])}
            for name, value in local.items():
                stage['files'].append(self.json('reports/run/normal-' + arch + '/' + name, value))
            text = {'tests.log': ''.join(f'{i}/{len(names)} Test #{i}: {n} .... Passed 0.01 sec\n' for i, n in enumerate(names, 1)) + f'100% tests passed, 0 tests failed out of {len(names)}\n',
                    'LastTest.log': ''.join(f'{i}/{len(names)} Testing: {n}\n1 checks, 0 failures\nTest Passed.\n' for i, n in enumerate(names, 1)),
                    'CMakeCache.txt': ''.join(f'{k}:{v["type"]}={v["value"]}\n' for k, v in stage['flags'].items()),
                    'Bc2NativeOperationBuild.generated.h': proof.build_config_bytes(header['path'], source_sha).decode(),
                    'chest-mesh.log': 'private cache parser passed', 'chest-attack.log': 'chest-attack-transaction exited 0'}
            for name, value in text.items():
                stage['files'].append(self.write('reports/run/normal-' + arch + '/' + name, value.encode()))
            self.normal['files'].extend(stage['files'])
            self.stages[arch] = stage
        for key, (arch, name) in delivery.ARTIFACTS.items():
            row = dict(key=key, architecture=arch, pe=dict(architecture=arch, guid=key, age=1), symbols=dict(guid=key, age=1))
            for kind, filename in (('binary', name), ('pdb', Path(name).stem + '.pdb')):
                row[kind] = self.write('reports/run/normal-runtime/' + filename, (key + kind + ' new').encode())
                self.normal['files'].append(row[kind])
                self.stages[arch]['files'].append(dict(row[kind], path=str(self.root / 'build' / arch / filename)))
            self.normal['artifacts'].append(row)
        self.bind_stages()

    def write(self, path, raw):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
        return batch.pin(target)

    def json(self, path, value):
        return self.write(path, (json.dumps(value, indent=2) + '\n').encode())

    def bind_review(self):
        self.config['host_source_update'] = self.json('review/host-update.json', self.review)
        self.config_pin = self.json('review/config.json', self.config)

    def bind_stages(self):
        self.normal['prerequisite_receipts'] = [self.json('reports/run/normal-' + a + '/receipt.json', s) for a, s in self.stages.items()]
        self.normal_pin = self.json('reports/run/normal-runtime/receipt.json', self.normal)

    def derive(self):
        return delivery.derive(self.config_pin['path'], self.config_pin['sha256'], self.normal_pin['sha256'], '0.1.84')

    def check_host(self):
        return batch.verify_host_source(self.config, self.baseline, proof, self.root)

    def test_full_normal_translation_and_metadata_pins(self):
        receipt, freeze, audit, stages = self.derive()
        output = self.root / 'delivery'
        metadata = delivery.metadata_set(output, receipt, freeze, audit)
        for name, raw in metadata.items():
            self.write('delivery/' + name, raw)
        emitted = batch.load(output / 'build-receipt.json')
        batch.checked(emitted['source_freeze'])
        batch.checked(emitted['symbol_audit'])
        self.assertEqual(len(emitted['binaries']), 8)
        self.assertEqual(emitted['host_source_review']['review'], self.config['host_source_update'])
        self.assertEqual(emitted['binaries']['host'], emitted['host_source_review']['tested_host_binary'])
        self.assertFalse(emitted['game_tested'])
        self.assertEqual(set(stages), {'x86', 'x64'})

    def test_default_retained_host_still_allowed(self):
        self.write('src/graphics/D3D11BodyPropCompositor.cpp', b'// old graphics\n')
        self.config.pop('host_source_update')
        self.config['host_source_receipt'] = self.old_host
        self.assertEqual(self.check_host()['source_sha256'], self.baseline['host_source_sha256'])

    def test_cli_emit_verify_and_self_test(self):
        args = [sys.executable, '-B', str(TOOLS / 'normal_native_delivery.py'),
                '--config', self.config_pin['path'], '--config-sha256', self.config_pin['sha256'],
                '--normal-sha256', self.normal_pin['sha256'], '--version', '0.1.84', '--output', str(self.root / 'cli-output')]
        for mode in ('--self-test', '--emit', '--verify-output'):
            result = subprocess.run(args + [mode], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(json.loads(result.stdout)['verified'])

    def test_changed_host_without_review_refuses(self):
        self.config.pop('host_source_update')
        with self.assertRaisesRegex(ValueError, 'explicit host_source_update'):
            self.check_host()

    def test_changed_review_pin_refuses(self):
        Path(self.config['host_source_update']['path']).write_text('{}')
        with self.assertRaisesRegex(ValueError, 'Pinned file changed'):
            self.check_host()

    def test_forged_update_review_refuses(self):
        mutations = [lambda r: r.update(root_accepted=False), lambda r: r.update(source_commit='b' * 40),
            lambda r: r.update(previous_host_source_receipt=self.config['host_source_receipt']),
            lambda r: r.update(previous_host_binary=self.config['host_source_receipt']),
            lambda r: r.update(native_capability_grants_changed=True), lambda r: r.update(changed_files=[]),
            lambda r: r['changed_files'][0].update(before_sha256='0' * 64),
            lambda r: r.update(full_normal_builds_required=False)]
        original = copy.deepcopy(self.review)
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.review = copy.deepcopy(original)
                mutate(self.review)
                self.bind_review()
                with self.assertRaises(ValueError):
                    self.check_host()

    def test_candidate_coverage_refuses_omitted_new_header(self):
        self.config['candidate_files'] = [r for r in self.config['candidate_files'] if not r['relative_path'].endswith('SpentCaseAsset.h')]
        with self.assertRaisesRegex(ValueError, 'candidate file review'):
            self.check_host()

    def test_source_closure_cannot_be_relabeled(self):
        self.config['host_source_receipt'] = self.old_host
        with self.assertRaisesRegex(ValueError, 'Host dependency closure changed'):
            self.check_host()

    def test_stage_rejects_different_host_pin(self):
        self.stages['x64']['host_source_receipt'] = self.old_host
        self.bind_stages()
        with self.assertRaisesRegex(ValueError, 'Stage host closure differs'):
            self.derive()

    def test_retained_host_even_with_rebound_stage_is_refused(self):
        row = next(r for r in self.normal['artifacts'] if r['key'] == 'host')
        old = row['binary']
        new = self.write('reports/run/normal-runtime/BC2XrHost.exe', b'old host')
        row['binary'] = new
        self.normal['files'] = [new if r == old else r for r in self.normal['files']]
        for r in self.stages['x64']['files']:
            if Path(r['path']).name == 'BC2XrHost.exe':
                r.update(bytes=new['bytes'], sha256=new['sha256'])
        self.bind_stages()
        with self.assertRaisesRegex(ValueError, 'freshly built host binary'):
            self.derive()

    def test_untested_host_hash_refused(self):
        row = next(r for r in self.normal['artifacts'] if r['key'] == 'host')
        row['binary']['sha256'] = '0' * 64
        self.bind_stages()
        with self.assertRaises(ValueError):
            self.derive()

    def test_missing_x64_full_normal_stage_refuses(self):
        del self.stages['x64']
        self.bind_stages()
        with self.assertRaisesRegex(ValueError, 'Both normal stages required'):
            self.derive()

    def test_forged_host_symbols_refuse(self):
        next(r for r in self.normal['artifacts'] if r['key'] == 'host')['symbols']['age'] = 2
        self.bind_stages()
        with self.assertRaisesRegex(ValueError, 'PE/PDB identity differs'):
            self.derive()

    def test_failed_normal_test_refuses(self):
        row = next(r for r in self.stages['x64']['files'] if Path(r['path']).name == 'tests.log')
        old = dict(row)
        new = self.write('reports/run/normal-x64/tests.log', b'0% tests passed, 1 test failed\n')
        row.update(new)
        self.normal['files'] = [new if r == old else r for r in self.normal['files']]
        self.bind_stages()
        with self.assertRaisesRegex(ValueError, 'Full normal suite did not pass'):
            self.derive()


class OfflineReleaseTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        self.now = datetime.now(timezone.utc) + timedelta(seconds=2)
        self.session = dict(status='failed', normal_game_close=False, native_retirement_proven=False,
            exact_three_profiles_restored=True, launcher_active_settings_preserved=True, processes=[])
        for i, name in enumerate(('entry', 'host', 'supervisor', 'resolution_helper', 'game', 'trace'), 1):
            self.session['processes'].append(dict(name=name, exited=True, exit_code=0,
                identity=dict(pid=i, start_utc=(self.now - timedelta(minutes=5)).isoformat(), path=str(self.root / (name + '.exe')))))
        self.session_pin = self.json('session/result.json', self.session)
        self.config = dict(source_commit='a' * 40, run=str(self.root / 'run'), root_leases=self.json('leases.json', {}),
            owners_relinquished=True, closure_and_restoration_verified=False)
        originals = dict(profiles=[], launcher=[])
        for kind, names in (('profiles', ('GameSettings.bin', 'GameSettings.ini', 'settings.ini')),
                            ('launcher', ('active.json', 'data/settings.json'))):
            for name in names:
                p = self.root / kind / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(b'original')
                originals[kind].append(dict(original=batch.pin(p)))
        self.observation = dict(checked_utc=self.now.isoformat(), all_owned_processes_absent=True,
            shared_build_processes_absent=True, processes=[dict(name=r['name'], identity=r['identity'], absent=True,
            replacement_process=None, observed_exit_code=0) for r in self.session['processes']])
        self.release = dict(schema='fvr.root-offline-resource-release.v1', root_accepted=True, offline_build_only=True,
            gameplay_accepted=False, headset_accepted=False, source_commit=self.config['source_commit'], run=self.config['run'],
            root_leases=self.config['root_leases'], last_session=self.session_pin, status='failed',
            normal_game_close=False, native_retirement_proven=False, exact_three_profiles_restored=True,
            launcher_active_settings_preserved=True, checked_utc=self.now.isoformat(),
            expires_utc=(self.now + timedelta(hours=1)).isoformat(),
            session_originals=self.json('session/originals.json', originals),
            restoration=[r['original'] for k in ('profiles', 'launcher') for r in originals[k]])
        self.bind()

    def json(self, path, value):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(value), encoding='utf-8')
        return batch.pin(target)

    def bind(self):
        self.release['process_observation'] = self.json('processes.json', self.observation)
        self.config['offline_resource_release'] = self.json('release.json', self.release)
        self.config['owner_evidence'] = [self.session_pin, self.config['offline_resource_release']]

    def verify(self):
        return batch.verify_owner_release(self.config, self.now)

    def test_failed_session_released_for_offline_build_only(self):
        self.verify()
        self.assertFalse(self.release['normal_game_close'])
        self.assertFalse(self.release['native_retirement_proven'])
        self.assertEqual(batch.load(self.session_pin['path'])['status'], 'failed')

    def test_reused_pid_with_later_birth_releases_original_only(self):
        row = self.observation['processes'][0]
        for path in (row['identity']['path'], str(self.root / 'unrelated.exe')):
            for started in (self.now - timedelta(minutes=1), self.now):
                with self.subTest(path=path, started=started):
                    row['replacement_process'] = dict(pid=row['identity']['pid'], path=path,
                        start_utc=started.isoformat().replace('+00:00', 'Z'))
                    self.bind()
                    self.verify()
                    self.assertEqual(row['identity'], self.session['processes'][0]['identity'])
                    self.assertFalse(self.release['normal_game_close'])
                    self.assertFalse(self.release['native_retirement_proven'])
                    self.assertEqual(batch.load(self.session_pin['path'])['status'], 'failed')

    def test_same_older_or_future_replacement_birth_refuses(self):
        row = self.observation['processes'][0]
        original = row['identity']['start_utc']
        for started in (original, original.replace('+00:00', 'Z'),
                        (self.now - timedelta(minutes=6)).isoformat(),
                        (self.now + timedelta(microseconds=1)).isoformat()):
            with self.subTest(started=started):
                row['replacement_process'] = dict(row['identity'], start_utc=started)
                self.bind()
                with self.assertRaisesRegex(ValueError, 'Replacement process birth'):
                    self.verify()

    def test_malformed_or_forged_replacement_identity_refuses(self):
        row = self.observation['processes'][0]
        valid = dict(row['identity'], start_utc=(self.now - timedelta(minutes=1)).isoformat())
        invalid = [[], '', False, {}, dict(valid, extra=True)]
        invalid.extend({key: value for key, value in valid.items() if key != missing} for missing in valid)
        invalid.extend(dict(valid, pid=value) for value in (99, 0, True, '1', 1.0, None))
        invalid.extend(dict(valid, path=value) for value in ('', 'relative.exe', None, 123, []))
        invalid.extend(dict(valid, start_utc=value) for value in ('', 'invalid', None, 123, [],
            (self.now - timedelta(minutes=1)).replace(tzinfo=None).isoformat(),
            (self.now - timedelta(minutes=1)).astimezone(timezone(timedelta(hours=1))).isoformat()))
        for replacement in invalid:
            with self.subTest(replacement=replacement):
                row['replacement_process'] = replacement
                self.bind()
                with self.assertRaises(ValueError):
                    self.verify()

    def test_pid_reuse_does_not_relax_original_absence_or_resource_gates(self):
        original = copy.deepcopy(self.observation)
        for mode in ('identity', 'present', 'exit_code', 'owned_busy', 'build_busy'):
            with self.subTest(mode=mode):
                self.observation = copy.deepcopy(original)
                row = self.observation['processes'][0]
                row['replacement_process'] = dict(row['identity'],
                    start_utc=(self.now - timedelta(minutes=1)).isoformat())
                if mode == 'identity': row['identity']['path'] = str(self.root / 'wrong.exe')
                elif mode == 'present': row['absent'] = False
                elif mode == 'exit_code': row['observed_exit_code'] = 1
                elif mode == 'owned_busy': self.observation['all_owned_processes_absent'] = False
                else: self.observation['shared_build_processes_absent'] = False
                self.bind()
                with self.assertRaises(ValueError):
                    self.verify()

    def test_fabricated_session_success_refuses(self):
        for field in ('normal_game_close', 'native_retirement_proven'):
            with self.subTest(field=field):
                self.release[field] = True
                self.bind()
                with self.assertRaisesRegex(ValueError, 'relabels session outcome'):
                    self.verify()
                self.release[field] = False

    def test_missing_or_wrong_identity_refuses(self):
        original = copy.deepcopy(self.observation)
        for mode in ('missing', 'pid', 'path', 'start', 'replacement', 'busy'):
            with self.subTest(mode=mode):
                self.observation = copy.deepcopy(original)
                row = self.observation['processes'][0]
                if mode == 'missing': self.observation['processes'].pop()
                elif mode == 'pid': row['identity']['pid'] += 100
                elif mode == 'path': row['identity']['path'] = str(self.root / 'wrong.exe')
                elif mode == 'start': row['identity']['start_utc'] = self.now.isoformat()
                elif mode == 'replacement': row['replacement_process'] = dict(pid=99)
                else: self.observation['shared_build_processes_absent'] = False
                self.bind()
                with self.assertRaises(ValueError): self.verify()

    def test_expired_or_long_lived_admission_refuses(self):
        for hours in (-1, 25):
            self.release['expires_utc'] = (self.now + timedelta(hours=hours)).isoformat()
            self.bind()
            with self.assertRaisesRegex(ValueError, 'Fresh ROOT offline release'): self.verify()

    def test_changed_settings_refuse(self):
        Path(self.release['restoration'][-1]['path']).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Pinned file changed'): self.verify()

    def test_rebound_restoration_pins_cannot_hide_changed_settings(self):
        path = Path(self.release['restoration'][-1]['path'])
        path.write_bytes(b'changed')
        self.release['restoration'][-1] = batch.pin(path)
        self.bind()
        with self.assertRaisesRegex(ValueError, 'Exact original profile/settings'): self.verify()

    def test_wrong_leases_refuse(self):
        self.release['root_leases'] = self.json('wrong-leases.json', {})
        self.bind()
        with self.assertRaisesRegex(ValueError, 'source/run/leases differ'): self.verify()

    def test_default_still_requires_closure(self):
        self.config.pop('offline_resource_release')
        with self.assertRaisesRegex(ValueError, 'closure and restoration'): self.verify()
        self.config['closure_and_restoration_verified'] = True
        self.verify()


if __name__ == '__main__':
    unittest.main()
