"""Offline guard regressions. No compiler/game/configure/source mutation."""
import importlib.util
from pathlib import Path
import unittest
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


if __name__ == '__main__': unittest.main()
