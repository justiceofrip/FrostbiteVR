"""Exact shared SingleFire admission tests; no game process or chamber claims."""
import copy
import json
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAIN = Path(os.environ.get('FVR_TEST_MAIN_ROOT', ROOT))
sys.path[:0] = [str(ROOT/'tools'), str(MAIN/'tools')]
import bc2_magazine_registry_header as registry
import bc2_magazine_descriptor_manifest as descriptor
import bc2_singlefire_magazine_proof as proof


def jobs():
    return json.loads((MAIN/'profiles/singlefire238/descriptor-jobs.json').read_text())


def review(row):
    return dict(key=row['key'], descriptor_digest=row['descriptor_digest'],
                family_proof=registry.SINGLE_FIRE_FAMILY,
                instruction_proof_digest=proof.PROOF_DIGEST,
                evidence_sha256=proof.EXPECTED_PROOF['instruction_evidence_sha256'])


class SingleFireNativeFamily(unittest.TestCase):
    def test_exact_two_profiles_use_independent_capability(self):
        j = jobs(); header, receipt = registry.build(j, [review(r) for r in j['descriptors']])
        self.assertEqual(receipt['enabled'], 2)
        self.assertEqual(header.count('MagazineCycleAdmission::ReviewedSingleFireReload11Transfer12'), 2)
        self.assertNotIn('MagazineCycleAdmission::ReviewedReload11Transfer12', header)

    def test_default_remains_byte_identical_disabled(self):
        header, receipt = registry.build(jobs())
        self.assertEqual(receipt['enabled'], 0)
        self.assertEqual(header, (MAIN/'profiles/singlefire238/DisabledRegistry.h').read_text())

    def test_exact_sp_only_review_does_not_enable_mp_variant(self):
        j = jobs(); r = next(r for r in j['descriptors'] if r['configuration']['assetName'] == 'MP443_sp')
        header, result = registry.build(j, [review(r)])
        self.assertEqual(result['enabled'], 1)
        self.assertEqual(header.count('MagazineCycleAdmission::ReviewedSingleFireReload11Transfer12'), 1)

    def test_other_family_cannot_borrow_review(self):
        j = jobs(); r = j['descriptors'][0]; approved = review(r)
        approved['family_proof'] = registry.FAMILY
        with self.assertRaisesRegex(ValueError, 'SingleFire'): registry.build(j, [approved])
        r['singlefire_magazine_proof_digest'] = ''
        with self.assertRaisesRegex(ValueError, 'SingleFire'): registry.build(j, [review(r)])

    def test_review_binds_source_config_and_static_instructions(self):
        j = jobs(); row = j['descriptors'][0]
        for key, value in [('key', 'other'), ('descriptor_digest', 'f'*64), ('instruction_proof_digest', 'f'*64),
                           ('family_proof', 'bc2.singlefire-magazine-zero-bolt.v2')]:
            with self.subTest(key=key):
                changed = review(row); changed[key] = value
                with self.assertRaises(ValueError): registry.build(j, [changed])

    def test_all_consumed_config_mutations_need_separate_review(self):
        j = jobs(); approved = review(j['descriptors'][0])
        for key in registry.ORDER:
            with self.subTest(key=key):
                changed = copy.deepcopy(j); row = changed['descriptors'][0]
                old = row['configuration']['values'][key]
                row['configuration']['values'][key] = not old if type(old) is bool else old+1
                row['descriptor_digest'] = descriptor.digest(row['configuration'])
                with self.assertRaises(ValueError): registry.build(changed, [approved])

    def test_same_digest_with_wrong_timing_or_dispatch_is_rejected(self):
        for mutation in ('timing', 'authored', 'proof', 'bolt'):
            j = jobs(); row = j['descriptors'][0]
            if mutation == 'timing': row['configuration']['timing'][4]['expected'] = 2
            elif mutation == 'authored': row['authored_symbols']['FireLogic.FireLogicType'] = 'fltAutomaticFire'
            elif mutation == 'proof': row['singlefire_magazine_proof_digest'] = 'f'*64
            else: row['configuration']['values']['boltTime'] = 1
            row['descriptor_digest'] = descriptor.digest(row['configuration'])
            with self.subTest(mutation=mutation), self.assertRaises(ValueError): registry.build(j, [review(row)])


if __name__ == '__main__': unittest.main()
