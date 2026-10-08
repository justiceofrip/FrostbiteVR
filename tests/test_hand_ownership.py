"""Synthetic contract regressions for offline hand ownership evidence."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("hand_ownership", Path(__file__).resolve().parents[1] / "tools" / "check_hand_ownership.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def claim(token, kind, sequence, now, prerequisite=0):
    return {"id": token, "kind": kind, "item": 900, "generation": 4,
            "contact": kind, "contact_generation": 4, "prerequisite": prerequisite,
            "input_generation": sequence, "deadline_ns": now + 100000000}


def row(serial, left_kind=0, support_token=0, phase=0, request=0, ack=0, commit=False, weapon=100):
    now = serial * 1000000
    left_id = 2 if left_kind == 2 and support_token == 10 else 4 if left_kind == 2 else 3
    return {"serial": serial, "now_ns": now, "raw_generation": serial,
            "actor": 42, "rig_epoch": 3, "equip_generation": 4, "space": 9,
            "weapon": weapon, "physical_item": 900, "reason": "transition",
            "right": claim(1, 1, serial, now),
            "left": claim(left_id, left_kind, serial, now, 1) if left_kind else None,
            "support_holding": left_kind == 2, "support_token": support_token,
            "sight_phase": phase, "sight_request": request, "sight_ack": ack,
            "sight_commit": commit}


def fixture():
    events = [row(1, 2, 10), row(2), row(3, 3, phase=1),
              row(4, 3, phase=2, request=7),
              row(5, 3, phase=3, ack=7, commit=True, weapon=200),
              row(6, weapon=200), row(7, 2, 11, weapon=200), row(8, weapon=200)]
    events[-1].update(left=None, right=None, raw_generation=0, reason="reset")
    support_events = []
    for token, weapon in ((10, 100), (11, 200)):
        support_events.append({"actor": 42, "rig_epoch": 3, "space": 9, "weapon": weapon,
            "previous_grasp_token": 0, "reset_reason": None,
            "sample": {"engaged": True, "released": False, "holding": True, "grasp_token": token}})
        # Forced reset intentionally retains the prior sample's engaged/holding
        # values. reset_reason, not those copied flags, defines the release edge.
        support_events.append({"actor": 42, "rig_epoch": 3, "space": 9, "weapon": weapon,
            "previous_grasp_token": token, "reset_reason": "session_stop" if token == 11 else None,
            "sample": {"engaged": token == 11, "released": token == 10,
                       "holding": token == 11, "grasp_token": token if token == 11 else 0}})
    return {"hooks_disabled": True, "gameplay": {
        "hand_ownership": {"schema": 1, "event_capacity": 512, "checks": 200,
            "overlap_failures": 0, "support_without_claim": 0, "sight_without_claim": 0,
            "lease_failures": 0, "events_total": len(events), "events_dropped": 0, "events": events},
        "two_hand_support": {"event_schema": 1, "events_total": len(support_events),
            "events_dropped": 0, "events": support_events},
        "weapon_mode_records": [{"gesture": 7, "ms": 4, "ack_ms": 5, "actor": 42,
            "weak": 8, "owner": 3, "space": 9, "from": 100, "target": 200,
            "action": 33, "cancelled": False}]}}


class HandOwnershipTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "native-trace.json"
        self.doc = fixture()

    def run_audit(self, coverage="both"):
        self.path.write_text(json.dumps(self.doc), encoding="utf-8")
        return audit.check(self.path, coverage)

    def reject(self, text=None):
        result = self.run_audit()
        self.assertFalse(result["passed"], result)
        if text:
            self.assertIn(text, result["failures"][0])

    def test_roles_native_join_and_semantic_item_survive_backend_switch(self):
        result = self.run_audit()
        self.assertTrue(result["passed"], result)
        self.assertEqual(result["summary"]["support_tokens"], 2)
        self.assertEqual(result["summary"]["sight_claims"], 1)
        self.assertEqual(result["summary"]["sight_commits"], 1)
        self.assertEqual(result["summary"]["support_to_sight"], 1)
        self.assertEqual(result["summary"]["sight_to_support"], 1)
        self.assertFalse(result["headset_tested"])

    def test_counter_failures_dropped_edges_and_accounting_rejected(self):
        for key in ("checks", "overlap_failures", "support_without_claim", "sight_without_claim", "lease_failures"):
            self.doc = fixture()
            self.doc["gameplay"]["hand_ownership"][key] = 0 if key == "checks" else 1
            with self.subTest(key=key): self.reject()
        self.doc = fixture(); o = self.doc["gameplay"]["hand_ownership"]
        o["events_dropped"] = 1; o["events_total"] += 1
        self.reject("dropped")
        self.doc = fixture(); self.doc["gameplay"]["hand_ownership"]["events_total"] += 1
        self.reject("accounting")

    def test_no_overlap_and_no_independent_policy_without_claim(self):
        events = self.doc["gameplay"]["hand_ownership"]["events"]
        events[0]["sight_phase"] = 1
        self.reject("sight policy")
        self.doc = fixture(); self.doc["gameplay"]["hand_ownership"]["events"][0]["left"] = None
        self.reject("support policy")

    def test_dependency_contact_and_equip_generation(self):
        for field, value in (("prerequisite", 99), ("generation", 99), ("item", 901), ("contact", 1)):
            self.doc = fixture()
            self.doc["gameplay"]["hand_ownership"]["events"][0]["left"][field] = value
            with self.subTest(field=field): self.reject()

    def test_expiry_and_future_or_retimed_proofs_rejected(self):
        events = self.doc["gameplay"]["hand_ownership"]["events"]
        events[0]["left"]["deadline_ns"] = events[0]["now_ns"]
        self.reject("expired")
        self.doc = fixture(); self.doc["gameplay"]["hand_ownership"]["events"][0]["left"]["input_generation"] = 2
        self.reject("ahead")
        self.doc = fixture(); events = self.doc["gameplay"]["hand_ownership"]["events"]
        events[1]["right"]["input_generation"] = 1
        self.reject("duplicate proof extended")

    def test_clock_serial_and_immutable_owner(self):
        self.doc["gameplay"]["hand_ownership"]["events"][1]["serial"] = 1
        self.reject("serial/time")
        self.doc = fixture(); self.doc["gameplay"]["hand_ownership"]["events"][1]["now_ns"] = 1
        self.reject("serial/time")
        self.doc = fixture(); self.doc["gameplay"]["hand_ownership"]["events"][1]["space"] = 10
        self.reject("identity changed")

    def test_claim_cannot_resurrect_after_release(self):
        self.doc["gameplay"]["hand_ownership"]["events"][6]["left"]["id"] = 2
        self.reject("resurrected")

    def test_commit_requires_exact_request_ack_and_native_target(self):
        events = self.doc["gameplay"]["hand_ownership"]["events"]
        events[4]["sight_ack"] = 8
        self.reject("exact owned request")
        self.doc = fixture(); self.doc["gameplay"]["weapon_mode_records"][0]["ack_ms"] = 0
        self.reject("successful native")
        self.doc = fixture(); self.doc["gameplay"]["weapon_mode_records"][0]["target"] = 201
        self.reject("not native target")
        self.doc = fixture(); self.doc["gameplay"]["weapon_mode_records"][0]["space"] = 10
        self.reject("owner/space")
        self.doc = fixture(); del self.doc["gameplay"]["weapon_mode_records"][0]["gesture"]
        self.reject("identity join")

    def test_support_correlates_exact_native_token_owner_and_release(self):
        self.doc["gameplay"]["two_hand_support"]["events"][0]["sample"]["grasp_token"] = 12
        self.reject("engagement evidence")
        self.doc = fixture(); self.doc["gameplay"]["two_hand_support"]["events"][0]["actor"] = 43
        self.reject("owner/space/weapon")
        self.doc = fixture(); self.doc["gameplay"]["two_hand_support"]["events"][1]["sample"]["released"] = False
        self.reject("release/reset")

    def test_both_roles_required_but_no_mode_cycle_or_direct_transfer_required(self):
        o = self.doc["gameplay"]["hand_ownership"]
        o["events"] = o["events"][:3] + [o["events"][-1]]
        o["events_total"] = len(o["events"])
        self.doc["gameplay"]["weapon_mode_records"] = []
        self.assertTrue(self.run_audit()["passed"])
        o["events"] = [o["events"][0], o["events"][-1]]; o["events_total"] = 2
        self.reject("sight ownership coverage absent")
        self.assertTrue(self.run_audit("support")["passed"])

    def test_reset_row_may_have_no_raw_or_owner_identity(self):
        final = self.doc["gameplay"]["hand_ownership"]["events"][-1]
        for key in ("actor", "rig_epoch", "equip_generation", "space", "weapon", "physical_item"):
            final[key] = 0
        self.assertTrue(self.run_audit()["passed"])

    def test_finalized_trace_cannot_leave_claims_active(self):
        o = self.doc["gameplay"]["hand_ownership"]
        o["events"].pop(); o["events_total"] -= 1
        self.reject("retains active")

    def test_malformed_types_and_partial_json_fail_closed(self):
        self.doc["gameplay"]["hand_ownership"]["events"][0]["support_holding"] = 1
        self.reject("boolean")
        self.path.write_text('{"gameplay":')
        self.assertFalse(audit.check(self.path)["passed"])
        self.path.write_text('{"gameplay":{},"gameplay":{}}')
        self.assertIn("duplicate", audit.check(self.path)["failures"][0])

    def test_cli_does_not_modify_source_and_refuses_overwrite(self):
        self.run_audit()
        before = self.path.read_bytes()
        with contextlib.redirect_stdout(io.StringIO()) as stream:
            self.assertEqual(audit.main(["--native", str(self.path)]), 0)
        self.assertTrue(json.loads(stream.getvalue())["passed"])
        self.assertEqual(before, self.path.read_bytes())
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            audit.main(["--native", str(self.path), "--output", str(self.path)])
        self.assertEqual(before, self.path.read_bytes())


if __name__ == "__main__":
    unittest.main()
