"""Synthetic observer rejection checks; never native/game readiness evidence."""
import copy
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import pistol_readiness_probe as probe


class ObserverChecks(unittest.TestCase):
    def setUp(self):
        self.identity = dict(asset_name=probe.ASSET, asset_path=probe.ASSET_PATH,
            client_owner=dict(selected_weapon=100, selected_slot=1), server_item=200)
        self.server = dict(self.identity, state=dict(loaded=17, reserve=68),
            identity_coherent=True, atomic_native_snapshot=False, authority_proven=False)
        self.weapon = dict(asset_name=probe.ASSET, asset_path=probe.ASSET_PATH,
            fire_logic=dict(FireLogicType=dict(value=0), ReloadType=dict(value=1),
                ReloadTime=3.75, ReloadThreshold=.5, ReloadDelay=0, PostReloadSequenceTime=0,
                BoltAction=dict(BoltActionTime=0, BoltActionDelay=0,
                    HoldBoltActionUntilFireRelease=False, HoldBoltActionUntilZoomRelease=False)),
            ammo_config=dict(MagazineCapacity=17, AutoReplenishMagazine=False))
        self.addresses = {0x3c: 1000, 0x40: 2000}
        self.inspect = SimpleNamespace(weapon=lambda *_: self.weapon,
            state=lambda _, branch: dict(address=self.addresses[branch]))

    def sample(self, after=None):
        with patch.object(probe, "capture", return_value=self.server), patch.object(
                probe, "chain", return_value=self.identity if after is None else after):
            return probe.selected_sample(self.inspect, {})

    def test_valid_shape_never_invents_chamber_or_permission(self):
        result = self.sample()
        self.assertEqual(result["chamber_knowledge"], "Unknown")
        self.assertIsNone(result["slide_closed"])
        self.assertIsNone(result["fire_permission"])
        self.assertFalse(result["server"]["authority_proven"])

    def test_wrong_exact_asset_rejected(self):
        for key, wrong in (("asset_name", "MP443"), ("asset_path", "Objects/Other")):
            with self.subTest(key=key):
                before = self.server[key]
                self.server[key] = wrong
                with self.assertRaisesRegex(ValueError, "exact MP443_sp"):
                    self.sample()
                self.server[key] = before

    def test_unreviewed_family_timing_bolt_and_ammo_rejected(self):
        original = copy.deepcopy(self.weapon)
        mutations = (
            lambda w: w["fire_logic"]["FireLogicType"].update(value=1),
            lambda w: w["fire_logic"].update(ReloadTime=3.0),
            lambda w: w["fire_logic"]["BoltAction"].update(BoltActionTime=.1),
            lambda w: w["ammo_config"].update(MagazineCapacity=18),
            lambda w: w["ammo_config"].update(AutoReplenishMagazine=True))
        for mutation in mutations:
            self.weapon = copy.deepcopy(original)
            mutation(self.weapon)
            with self.assertRaises(ValueError):
                self.sample()

    def test_alias_and_identity_change_rejected(self):
        self.addresses[0x40] = self.addresses[0x3c]
        with self.assertRaisesRegex(ValueError, "alias"):
            self.sample()
        self.addresses[0x40] = 2000
        after = dict(self.identity, server_item=201)
        with self.assertRaises(ValueError) as caught:
            self.sample(after)
        self.assertEqual(caught.exception.identity_read_failure["changed_fields"], ["server_item"])

    def test_bounded_observer_keeps_first_failure_and_closes_handle(self):
        process = Mock()
        first = dict(identity=self.identity, observed_ns=10)
        changed = dict(identity=dict(self.identity, server_item=201), observed_ns=20)
        args = SimpleNamespace(exe=Path("unused.exe"), pid=1, stage="empty-after", samples=3, interval_ms=20)
        with patch.object(probe, "image_for"), patch.object(probe, "Process", return_value=process), \
             patch.object(probe, "Inspector"), patch.object(probe, "discover"), \
             patch.object(probe, "selected_sample", side_effect=[first, changed, first]) as sampled, \
             patch.object(probe.time, "sleep"):
            report = probe.observe(args)
        self.assertEqual(len(report["samples"]), 1)
        self.assertEqual(len(report["rejected"]), 1)
        self.assertEqual(sampled.call_count, 2)
        self.assertFalse(report["stage_is_event_receipt"])
        self.assertEqual(report["rejected"][0]["identity_read_failure"]["changed_fields"], ["server_item"])
        process.close.assert_called_once()

    def test_cli_bounds_and_no_overwrite_precede_process_access(self):
        script = str(Path(probe.__file__))
        existing = str(Path(__file__).parent / "preparation02.json")
        commands = (
            ["prepare", "--game", "does-not-exist", "--output", existing],
            ["observe", "--exe", "does-not-exist", "--pid", "1", "--stage", "empty-after",
             "--samples", "17", "--output", str(Path(__file__).parent / "must-not-be-created.json")])
        for arguments in commands:
            result = subprocess.run([sys.executable, "-B", script, *arguments], text=True, capture_output=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn("error:", result.stderr)
        self.assertFalse((Path(__file__).parent / "must-not-be-created.json").exists())

    def test_offline_preparation_has_scoped_metadata_and_no_chamber_claim(self):
        data = json.loads((Path(__file__).parent / "preparation02.json").read_text())
        self.assertEqual(data["executable_sha256"], probe.EXE_SHA)
        self.assertEqual({x["name"] for x in data["reflected_metadata"]}, set(probe.TYPE_NAMES))
        self.assertEqual(data["authored_readiness_states"][0]["fields"], {"Relaxed": False, "Alert": False})
        self.assertTrue(data["additional_fire_logic"]["WaitForTriggerRelease"])
        self.assertEqual(data["chamber_knowledge"], "Unknown")
        self.assertFalse(data["game_process_opened"] or data["native_admission_changed"] or data["slide_ready_admitted"])


if __name__ == "__main__":
    unittest.main()
