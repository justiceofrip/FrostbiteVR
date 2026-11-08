"""Portable producer-generation regressions; synthetic evidence, no native claims."""
import copy
import json
from types import SimpleNamespace
import unittest

import test_bc2_draw_readiness_driver as readiness

adapter, policy, fixture = readiness.adapter, readiness.policy, readiness.fixture
NOW, MS = fixture.NOW, fixture.MS


class BindingGenerationTests(unittest.TestCase):
    def sample(self, millis, nonce, *, cohort=4, generation=7, unknown=False):
        self.now = NOW + millis * MS
        snapshot = fixture.snapshot(self.now, nonce, unknown)
        snapshot["bodyInventory"]["cohort"] = cohort
        if generation is not None:
            snapshot["bodyInventory"]["nativeBindingGeneration"] = generation
        return snapshot

    def driver(self, prime=None):
        self.now = NOW
        budget = adapter.AdmissionBudget(policy, NOW-policy.NS, NOW+29*policy.NS)
        budget.prime_attempt(NOW-20*MS)
        observed = None
        if prime is not None:
            result = budget.admit_prime(prime, prime["observed_ns"],
                fixture.context(prime, used=1, phase="prime"), fixture.admit)
            self.assertEqual(result.status, policy.Status.READY, result.reason)
            observed = json.loads(result.binding)
        cls = adapter.make_driver(readiness.Harness, readiness.binding,
                                  budget=budget, clock=lambda: self.now)
        args = SimpleNamespace(stow_asset=fixture.SOURCE[0], stow_config=fixture.SOURCE[1],
            target_asset=fixture.TARGET[0], target_config=fixture.TARGET[1], observed_binding=observed)
        return cls(args), budget

    def started(self, generation=7):
        driver, budget = self.driver()
        driver.queue = [self.sample(0, 1, generation=generation)]
        initial, _ = driver.current()
        driver.baseline, driver.phase = initial, "stow_reach"
        driver.command(initial, driver.right_pose, 0)
        return driver, budget

    def assert_refused_without_rebind(self, driver, budget, snapshot):
        before = copy.deepcopy((driver.slot_binding, driver.args.observed_binding))
        history = budget.session.mapping_identity, budget.session.last_display_cohort
        sent, accepted = len(driver.sent), len(driver.accepted)
        original = copy.deepcopy(snapshot)
        driver.queue = [snapshot]
        with self.assertRaises(policy.Refusal):
            driver.current()
        self.assertEqual(budget.decisions[-1]["status"], "Refuse")
        self.assertEqual((driver.slot_binding, driver.args.observed_binding), before)
        self.assertEqual((budget.session.mapping_identity, budget.session.last_display_cohort), history)
        self.assertEqual((len(driver.sent), len(driver.accepted)), (sent, accepted))
        self.assertEqual(snapshot, original)
        self.assertIsNone(driver._readiness_ready)
        self.assertIsNone(driver.publication_source)
        self.assertFalse(any(row["type"] == "neutral_source_display_rebound"
                             for row in driver.artifacts.rows))

    def test_fresh_neutral_renewal_rebinds_before_inherited_publication(self):
        driver, budget = self.started()
        prior = copy.deepcopy(driver.slot_binding)
        snapshot = self.sample(20, 2, cohort=5)
        original = copy.deepcopy(snapshot)
        driver.queue = [snapshot]
        current, _ = driver.current()
        expected = dict(prior, cohort=5)
        self.assertIs(current, snapshot)
        self.assertEqual(driver.slot_binding, expected)
        self.assertEqual(driver.args.observed_binding, expected)
        self.assertEqual(budget.decisions[-1]["status"], "Ready")
        self.assertEqual(len(driver.sent), 1)
        rebound = [row for row in driver.artifacts.rows if row["type"] == "neutral_source_display_rebound"]
        self.assertEqual(rebound, [dict(type="neutral_source_display_rebound", beforeCohort=4,
            afterCohort=5, nativeBindingGeneration=7, nonce=2, commands=0)])
        driver.command(current, driver.right_pose, 0)
        self.assertEqual(len(driver.sent), 2)
        self.assertEqual(snapshot, original)
        with self.assertRaisesRegex(policy.Refusal, "latest fresh Ready"):
            driver.command(current, driver.right_pose, 0)
        self.assertEqual(len(driver.sent), 2)

    def test_legacy_or_newly_introduced_generation_cannot_renew(self):
        for previous, current in ((None, None), (None, 7), (7, None)):
            with self.subTest(previous=previous, current=current):
                driver, budget = self.started(previous)
                self.assert_refused_without_rebind(driver, budget,
                    self.sample(20, 2, cohort=5, generation=current))

    def test_renewal_preserves_binding_owner_claim_neutrality_and_freshness_guards(self):
        mutations = {
            "changed_generation": lambda s, d: s["bodyInventory"].update(nativeBindingGeneration=8),
            "null_generation": lambda s, d: s["bodyInventory"].update(nativeBindingGeneration=None),
            "zero_generation": lambda s, d: s["bodyInventory"].update(nativeBindingGeneration=0),
            "negative_generation": lambda s, d: s["bodyInventory"].update(nativeBindingGeneration=-1),
            "boolean_generation": lambda s, d: s["bodyInventory"].update(nativeBindingGeneration=True),
            "owner": lambda s, d: s["bodyInventory"]["physicalOwner"].update(equipGeneration=3),
            "revision": lambda s, d: s["bodyInventory"].update(revision=4),
            "claim": lambda s, d: s["bodyHolster"]["rightClaim"]["token"].update(id=3),
            "request": lambda s, d: s["bodyHolster"].update(request=3),
            "configuration": lambda s, d: s["bodyInventory"]["slots"][1]["configuration"].update(soleConfiguredArray=99),
            "regression": lambda s, d: s["bodyInventory"].update(cohort=3),
            "native_pressed": lambda s, d: s["input"]["hands"][1].update(squeeze=1),
            "driver_pressed": lambda s, d: setattr(d, "held_right", True),
            "disabled": lambda s, d: s["bodyHolster"].update(allowAutomaticGunHold=False),
            "expired_claim": lambda s, d: s["bodyHolster"]["rightClaim"].update(deadlineNs=s["observed_ns"]),
            "expired_mapping": lambda s, d: s["bodyInventory"].update(deadlineNs=s["observed_ns"]),
        }
        for name, mutate in mutations.items():
            with self.subTest(boundary=name):
                driver, budget = self.started()
                snapshot = self.sample(20, 2, cohort=5)
                mutate(snapshot, driver)
                self.assert_refused_without_rebind(driver, budget, snapshot)

    def test_unknown_renewal_has_no_binding_authority_until_fresh_ready(self):
        driver, budget = self.started()
        before = copy.deepcopy((driver.slot_binding, driver.args.observed_binding))
        unknown = self.sample(20, 2, cohort=5, unknown=True)
        driver.queue = [unknown]
        self.assertIsNone(driver.receive_snapshot())
        self.assertEqual(budget.decisions[-1]["status"], "ObserveOnly")
        self.assertEqual((driver.slot_binding, driver.args.observed_binding), before)
        self.assertIsNone(driver.publication_source)
        self.assertFalse(any(row["type"] == "neutral_source_display_rebound"
                             for row in driver.artifacts.rows))
        with self.assertRaises(policy.Refusal):
            driver.command(unknown, driver.right_pose, 0)
        self.assertEqual(len(driver.sent), 1)
        ready = self.sample(40, 3, cohort=5)
        driver.queue = [ready]
        current, _ = driver.current()
        self.assertIs(current, ready)
        self.assertEqual(driver.slot_binding["cohort"], 5)
        self.assertEqual(budget.decisions[-1]["status"], "Ready")
        self.assertEqual(len(driver.sent), 1)

    def test_unrecorded_prime_binding_cannot_supply_generation_history(self):
        prior = fixture.admit(self.sample(-20, 1), NOW-20*MS)
        for cohort in (4, 5):
            with self.subTest(cohort=cohort):
                driver, budget = self.driver()
                driver.args.observed_binding = copy.deepcopy(prior)
                self.assertFalse(budget.session.mapping_identity)
                snapshot = self.sample(0, 2, cohort=cohort)
                if cohort == 4:
                    driver.queue = [snapshot]
                    driver.current()
                    self.assertEqual(driver.slot_binding, prior)
                    self.assertEqual(budget.decisions[-1]["status"], "Ready")
                else:
                    self.assert_refused_without_rebind(driver, budget, snapshot)
                    self.assertIsNone(driver.slot_binding)
                self.assertEqual(driver.sent, [])

    def test_recorded_prime_generation_permits_fresh_baseline_renewal(self):
        prime = self.sample(-20, 1)
        driver, budget = self.driver(prime)
        self.assertEqual(json.loads(budget.session.mapping_identity)["nativeBindingGeneration"], 7)
        snapshot = self.sample(0, 2, cohort=5)
        driver.queue = [snapshot]
        current, _ = driver.current()
        self.assertIs(current, snapshot)
        self.assertEqual(driver.slot_binding["cohort"], 5)
        self.assertEqual(driver.args.observed_binding, driver.slot_binding)
        self.assertEqual(budget.decisions[-1]["status"], "Ready")
        self.assertEqual(driver.sent, [])


if __name__ == "__main__":
    unittest.main()
