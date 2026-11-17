"""Offline guard regressions. No compiler/game/configure/source mutation."""
import importlib.util
import copy
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import unittest
from unittest import mock
import tempfile

spec = importlib.util.spec_from_file_location('batch', Path(__file__).resolve().parents[1] / 'tools/native_batch.py')
batch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(batch)


class ScheduleTests(unittest.TestCase):
    def setUp(self):
        self.old = dict(tests=[dict(name='Retained')])
        self.schedule = dict(tests=[dict(name='Retained'), dict(name='New')])
        self.log = '1/2 Test #1: Retained .... Passed 0.01 sec\n2/2 Test #2: New .... Passed 0.01 sec\n100% tests passed, 0 tests failed out of 2\n'
        self.details = '1/2 Testing: Retained\nold output\nTest Passed.\n2/2 Testing: New\nnew output\nTest Passed.\n'

    def verify(self, **changes):
        kwargs = dict(schedule=self.schedule, log=self.log, details=self.details, old=self.old,
                      additions=['New'], focused=['Retained', 'New'], expectations=[dict(suite='New', contains=['new output'])])
        kwargs.update(changes)
        return batch.verify_schedule(**kwargs)

    def test_exact_retained_and_added_pass(self):
        self.assertEqual(self.verify(), ['Retained', 'New'])

    def test_removed_retained_refuses(self):
        with self.assertRaises(ValueError): self.verify(schedule=dict(tests=[dict(name='New')]))

    def test_unreviewed_addition_refuses(self):
        with self.assertRaises(ValueError): self.verify(additions=[])

    def test_duplicate_pass_refuses(self):
        with self.assertRaises(ValueError): self.verify(log=self.log.replace('2/2 Test #2: New', '2/2 Test #2: Retained'))

    def test_missing_details_refuses(self):
        with self.assertRaises(ValueError): self.verify(details=self.details.replace('2/2 Testing: New', '2/2 Testing: Other'))

    def test_duplicate_details_refuses(self):
        with self.assertRaises(ValueError): self.verify(details=self.details + self.details)

    def test_missing_output_refuses(self):
        with self.assertRaises(ValueError): self.verify(details=self.details.replace('new output', ''))

    def test_missing_focused_refuses(self):
        with self.assertRaises(ValueError): self.verify(focused=['Unavailable'])

    def test_normal_and_private_campaign_flags_are_distinct(self):
        frozen = dict(normal_flags={'x86': {name: dict(type='STRING', value='old') for name in
            ('BC2_NATIVE_OPERATION_CAPABILITIES_HEADER', 'BC2_NATIVE_OPERATION_SOURCE_RECEIPT', 'BC2_NATIVE_OPERATION_SOURCE_SHA256')}})
        config = dict(reviewed_header=dict(path='header'), source_receipt=dict(path='receipt'), source_sha256='hash')
        normal = batch.flags(config, frozen, 'x86')
        private = batch.flags(config, frozen, 'x86', True)
        self.assertEqual(normal[batch.PRIVATE]['value'], 'OFF')
        self.assertEqual(private[batch.PRIVATE]['value'], 'ON')
        self.assertEqual({k for k in normal if normal[k] != private[k]}, {batch.PRIVATE})

    def test_campaign_on_cannot_be_normal(self):
        with self.assertRaises(ValueError):
            batch.verify_flag_text(batch.PRIVATE + ':BOOL=ON\n', {batch.PRIVATE: dict(type='BOOL', value='OFF')})

    def test_unknown_fvr_flag_refuses(self):
        with self.assertRaises(ValueError):
            batch.verify_flag_text(batch.PRIVATE + ':BOOL=OFF\nFVR_EXTRA_PROBE:BOOL=ON\n', {batch.PRIVATE: dict(type='BOOL', value='OFF')})


class FrozenEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.stage = self.root / 'normal-x86'
        self.stage.mkdir()
        self.log = self.stage / 'LastTest.log'
        self.log.write_text('passing evidence', encoding='utf-8')
        self.binary = self.root / 'shared-probe.dll'
        self.binary.write_bytes(b'normal')
        config = self.root / 'config.json'
        config.write_text('{}', encoding='utf-8')
        self.config = batch.pin(config)
        normal = dict(files=[batch.pin(self.log), batch.pin(self.binary)])
        self.receipt = self.root / 'frozen-receipt.json'
        batch.save(self.receipt, dict(config=self.config,
            files=batch.retained_stage_evidence(normal, self.stage)))

    def test_changed_retained_test_evidence_refuses(self):
        self.log.write_text('changed after freeze', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Pinned file changed'):
            batch.verified_receipt(self.receipt, self.config)

    def test_missing_retained_test_evidence_refuses(self):
        self.log.unlink()
        with self.assertRaises(FileNotFoundError):
            batch.verified_receipt(self.receipt, self.config)

    def test_later_private_binary_does_not_invalidate_frozen_normal_evidence(self):
        self.binary.write_bytes(b'private')
        receipt = batch.verified_receipt(self.receipt, self.config)
        self.assertEqual(receipt['files'], [batch.pin(self.log)])


class AuxiliarySourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.proof = batch.module('auxiliary_recipe',
            Path(__file__).resolve().parents[1] / 'tools/bc2_native_operation_receipts.py')

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.bootstrap = batch.AUXILIARY_ROOTS[0]
        self.cleanup = 'src/games/bc2/Bc2ResolutionCleanup.h'
        self.support = 'include/fvr/ResolutionSupport.h'
        self.implementation = 'src/runtime/ResolutionSupport.cpp'
        self.native = 'src/games/bc2/NativeProbe.cpp'
        self.host = batch.HOST_ROOTS[0]
        for name, source in (
            (self.bootstrap, '#include "Bc2ResolutionCleanup.h"\n'),
            (self.cleanup, '#include "fvr/ResolutionSupport.h"\n'),
            (self.support, '// transitive project header\n'),
            (self.implementation, '// matching implementation\n'),
            (self.native, '// native operation source\n'),
            (self.host, '// host source\n'),
            ('src/Unrelated.cpp', '// not a bootstrap dependency\n'),
        ):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding='utf-8')
        self.config = dict(source_commit='a' * 40)
        self.receipt = self.root / 'auxiliary-source.json'
        self.recorded = self.proof.source_receipt(self.root, roots=batch.AUXILIARY_ROOTS)
        self.bind()

    def bind(self):
        self.receipt.write_text(json.dumps(self.recorded), encoding='utf-8')
        self.config['auxiliary_source_receipt'] = batch.pin(self.receipt)

    def verify(self):
        return batch.verify_auxiliary_source(self.config, self.proof, self.root)

    def verify_candidates(self, names, changed=None):
        self.config['candidate_files'] = [dict(relative_path=name,
            sha256=batch.pin(self.root / name)['sha256']) for name in names]
        with mock.patch.object(batch, 'git', return_value='\n'.join(names if changed is None else changed)):
            batch.verify_candidate_files(self.config, dict(built_source_commit='b' * 40), self.root,
                dict(files=[dict(path=self.native)]), dict(files=[dict(path=self.host)]), self.verify())

    def test_exact_packaged_closure_covers_root_and_transitive_dependencies(self):
        expected = {self.bootstrap, self.cleanup, self.support, self.implementation}
        self.assertEqual({row['path'] for row in self.verify()['files']}, expected)
        self.assertFalse(self.verify()['native_admission_granted'])
        self.verify_candidates(sorted(expected))

    def test_changed_receipt_pin_refuses(self):
        self.receipt.write_bytes(self.receipt.read_bytes() + b'\n')
        with self.assertRaisesRegex(ValueError, 'Pinned file changed'):
            self.verify()

    def test_missing_receipt_refuses(self):
        self.receipt.unlink()
        with self.assertRaises(FileNotFoundError):
            self.verify()

    def test_missing_or_unapproved_declared_roots_refuse(self):
        for roots in (None, [], [self.cleanup], [self.bootstrap, self.native], [self.bootstrap, self.bootstrap]):
            with self.subTest(roots=roots):
                if roots is None:
                    self.recorded.pop('roots', None)
                else:
                    self.recorded['roots'] = roots
                self.bind()
                with self.assertRaisesRegex(ValueError, 'Unsupported auxiliary dependency roots'):
                    self.verify()

    def test_missing_actual_root_refuses(self):
        (self.root / self.bootstrap).unlink()
        with self.assertRaisesRegex(ValueError, 'source dependency missing'):
            self.verify()

    def test_changed_transitive_dependencies_refuse(self):
        for name in (self.cleanup, self.support, self.implementation):
            with self.subTest(dependency=name):
                path = self.root / name
                original = path.read_bytes()
                path.write_bytes(original + b'// changed\n')
                with self.assertRaisesRegex(ValueError, 'Auxiliary dependency closure changed'):
                    self.verify()
                path.write_bytes(original)

    def test_missing_transitive_dependency_refuses(self):
        (self.root / self.support).unlink()
        with self.assertRaisesRegex(ValueError, 'missing project include'):
            self.verify()

    def test_repinned_incomplete_or_forged_receipt_refuses(self):
        original = copy.deepcopy(self.recorded)
        for field, value in (
            ('files', [row for row in original['files'] if row['path'] != self.implementation]),
            ('source_sha256', '0' * 64),
            ('recipe_sha256', '0' * 64),
            ('native_admission_granted', True),
        ):
            with self.subTest(field=field):
                self.recorded = copy.deepcopy(original)
                self.recorded[field] = value
                self.bind()
                with self.assertRaisesRegex(ValueError, 'Auxiliary dependency closure changed'):
                    self.verify()

    def test_old_config_keeps_existing_native_and_host_coverage(self):
        del self.config['auxiliary_source_receipt']
        self.assertIsNone(self.verify())
        self.verify_candidates([self.native, self.host])

    def test_old_config_cannot_cover_auxiliary_source(self):
        del self.config['auxiliary_source_receipt']
        with self.assertRaisesRegex(ValueError, 'Candidate missing from'):
            self.verify_candidates([self.bootstrap, self.cleanup])

    def test_auxiliary_receipt_cannot_cover_unrelated_source(self):
        with self.assertRaisesRegex(ValueError, 'Candidate missing from'):
            self.verify_candidates([self.bootstrap, 'src/Unrelated.cpp'])

    def test_auxiliary_receipt_does_not_replace_candidate_review(self):
        with self.assertRaisesRegex(ValueError, 'Applied review omits changed tracked files'):
            self.verify_candidates([self.bootstrap], changed=[self.bootstrap, self.cleanup])


class ClosedResourceReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.now = datetime.now(timezone.utc) + timedelta(seconds=2)
        # Resource outcome of normal84/xr-game02; reload and headset remain unaccepted.
        self.session = dict(status='observed_and_closed', failures=[], normal_game_close=True,
            native_retirement_proven=True, exact_three_profiles_restored=True,
            launcher_active_settings_preserved=True, reload_success_claimed=False, headset_accepted=False,
            processes=[dict(name=name, exited=True, exit_code=0,
                identity=dict(pid=index, start_utc=(self.now - timedelta(minutes=5)).isoformat(),
                    path=str(self.root / (name + '.exe'))))
                for index, name in enumerate(('entry', 'host', 'supervisor', 'resolution_helper', 'game', 'trace'), 1)])
        self.config = dict(source_commit='a' * 40, run=str(self.root / 'run'),
            root_leases=self.json('leases.json', {}), owners_relinquished=True,
            closure_and_restoration_verified=True)
        originals = dict(profiles=[], launcher=[])
        for kind, names in (('profiles', ('GameSettings.bin', 'GameSettings.ini', 'settings.ini')),
                            ('launcher', ('active.json', 'data/settings.json'))):
            for name in names:
                path = self.root / kind / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'installed84 original')
                originals[kind].append(dict(original=batch.pin(path)))
        self.observation = dict(checked_utc=self.now.isoformat(), all_owned_processes_absent=True,
            shared_build_processes_absent=True, processes=[dict(name=row['name'],
                identity=copy.deepcopy(row['identity']), absent=True, replacement_process=None,
                observed_exit_code=0) for row in self.session['processes']])
        self.release = dict(schema='fvr.root-offline-resource-release.v1', root_accepted=True,
            offline_build_only=True, gameplay_accepted=False, headset_accepted=False,
            source_commit=self.config['source_commit'], run=self.config['run'], root_leases=self.config['root_leases'],
            status='observed_and_closed', failures=[], normal_game_close=True, native_retirement_proven=True,
            exact_three_profiles_restored=True, launcher_active_settings_preserved=True,
            checked_utc=self.now.isoformat(), expires_utc=(self.now + timedelta(hours=1)).isoformat(),
            session_originals=self.json('normal84/xr-game02/originals.json', originals),
            restoration=[row['original'] for kind in ('profiles', 'launcher') for row in originals[kind]])
        self.bind()

    def json(self, name, value):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding='utf-8')
        return batch.pin(path)

    def bind(self):
        self.release['last_session'] = self.json('normal84/xr-game02/result.json', self.session)
        self.release['process_observation'] = self.json('processes.json', self.observation)
        self.config['offline_resource_release'] = self.json('release.json', self.release)
        self.config['owner_evidence'] = [self.release['last_session'], self.config['offline_resource_release']]

    def verify(self):
        batch.verify_owner_release(self.config, self.now)

    def test_normal84_closed_resource_release_preserves_gameplay_limits(self):
        self.verify()
        self.assertEqual(batch.load(self.release['last_session']['path']), self.session)
        self.assertFalse(self.session['reload_success_claimed'])
        self.assertFalse(self.release['gameplay_accepted'])
        self.assertFalse(self.release['headset_accepted'])

    def test_mismatched_review_outcome_refuses(self):
        for field, value in (('status', 'failed'), ('normal_game_close', False), ('native_retirement_proven', False)):
            with self.subTest(field=field):
                previous = self.release[field]
                self.release[field] = value
                self.bind()
                with self.assertRaisesRegex(ValueError, 'relabels session outcome'):
                    self.verify()
                self.release[field] = previous

    def test_matching_but_unfinished_closure_or_restoration_refuses(self):
        for field in ('normal_game_close', 'native_retirement_proven',
                      'exact_three_profiles_restored', 'launcher_active_settings_preserved'):
            for value in (False, 1):
                with self.subTest(field=field, value=value):
                    self.session[field] = self.release[field] = value
                    self.bind()
                    with self.assertRaises(ValueError):
                        self.verify()
                    self.session[field] = self.release[field] = True

    def test_failures_must_be_explicitly_empty_in_session_and_review(self):
        for target in (self.session, self.release):
            for value in (['incomplete'], None, False, ''):
                with self.subTest(session=target is self.session, value=value):
                    target['failures'] = value
                    self.bind()
                    with self.assertRaisesRegex(ValueError, 'requires no session or review failures'):
                        self.verify()
            del target['failures']
            self.bind()
            with self.assertRaisesRegex(ValueError, 'requires no session or review failures'):
                self.verify()
            target['failures'] = []

    def test_closure_review_must_match_session_kind(self):
        self.config['closure_and_restoration_verified'] = False
        with self.assertRaisesRegex(ValueError, 'ROOT must verify closed-session'):
            self.verify()
        self.config['closure_and_restoration_verified'] = True
        self.session['status'] = self.release['status'] = 'failed'
        self.bind()
        with self.assertRaisesRegex(ValueError, 'must not assert normal session closure'):
            self.verify()

    def test_unknown_or_pending_session_status_refuses(self):
        for status in ('running', 'passed', ''):
            with self.subTest(status=status):
                self.session['status'] = self.release['status'] = status
                self.bind()
                with self.assertRaisesRegex(ValueError, 'requires a failed or observed_and_closed session'):
                    self.verify()

    def test_closed_status_does_not_bypass_process_expiry_or_original_pins(self):
        self.observation['processes'][0]['absent'] = False
        self.bind()
        with self.assertRaisesRegex(ValueError, 'has not released resources'):
            self.verify()
        self.observation['processes'][0]['absent'] = True
        self.release['expires_utc'] = self.now.isoformat()
        self.bind()
        with self.assertRaisesRegex(ValueError, 'Fresh ROOT offline release'):
            self.verify()
        self.release['expires_utc'] = (self.now + timedelta(hours=1)).isoformat()
        self.bind()
        active = next(row for row in self.release['restoration'] if Path(row['path']).name == 'active.json')
        Path(active['path']).write_bytes(b'changed installed version')
        with self.assertRaisesRegex(ValueError, 'Pinned file changed'):
            self.verify()


if __name__ == '__main__': unittest.main()
