import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import audit_empty_fire as audit
import report_empty_step as step


def fixture():
    owner = [65536, 65537, 65538, 65539, 1, 2, 3]
    rows = []
    for branch in range(3):
        state = dict(firing=70000+branch*256, current=2, next=2, loaded=0, reserve=22, flags_a8=162, timer=0)
        rows.append(dict(stage=9, now_ns=100, input_sequence=1, owner_revision=1,
                         observed_ns=90, deadline_ns=110, branch=branch, profile=0, phase=0,
                         owner=owner, requested=True, applied=True, restored=True, owner_retained=True,
                         family_known=True, family=0, before=state, after=copy.deepcopy(state),
                         context=dict(delta=.01, reload_multiplier=1, input_flags=0, flags_24_28=[1,0,0,0,0])))
    journal = dict(schema=1, diagnostic_only=True, drained=True, stages=list(step.STAGES),
                   capacity=128, observed=3, overwritten=0, lock_drops=0, publisher_lock_drops=0,
                   counts=[0]*9+[3,0], rows=rows)
    return dict(hooks_disabled=True, gameplay=dict(
        empty_fire_probe=dict(phase=3, failure=0, starting_loaded=8, last_loaded=0, consumed=8,
                              verified_zero_dwell_ns=3_000_000_000, selected_owner=owner,
                              auto_reload_count_increase=False, native_state_writes=False, reload_input=False),
        reload_flow=dict(drained=True, empty_magazine_control=dict(requested=[3]*3, applied=[3]*3, restored=[3]*3,
                                                    patch_failures=0, restore_failures=0, receipt_failures=0),
                         empty_step_diagnostic=journal)))


class AuditTests(unittest.TestCase):
    def test_good(self):
        self.assertEqual(audit.audit(fixture())["status"], "bounded_monitor_verified")

    def test_negative_evidence(self):
        changes = {
            "timeout_zero_shots": lambda d: d["gameplay"]["empty_fire_probe"].update(phase=4, failure=1, consumed=0),
            "shotbound_four_shots": lambda d: d["gameplay"]["empty_fire_probe"].update(phase=4, failure=6, consumed=4),
            "count_increase": lambda d: d["gameplay"]["empty_fire_probe"].update(auto_reload_count_increase=True),
            "short_dwell": lambda d: d["gameplay"]["empty_fire_probe"].update(verified_zero_dwell_ns=2_999_999_999),
            "write": lambda d: d["gameplay"]["empty_fire_probe"].update(native_state_writes=True),
            "reload": lambda d: d["gameplay"]["empty_fire_probe"].update(reload_input=True),
            "hooks": lambda d: d.update(hooks_disabled=False),
            "restore_failure": lambda d: d["gameplay"]["reload_flow"]["empty_magazine_control"].update(restore_failures=1),
            "counter_mismatch": lambda d: d["gameplay"]["reload_flow"]["empty_magazine_control"].update(restored=[3,2,3]),
            "not_drained": lambda d: d["gameplay"]["reload_flow"].update(drained=False),
            "negative_receipts": lambda d: d["gameplay"]["reload_flow"]["empty_magazine_control"].update(receipt_failures=-1),
        }
        for name, change in changes.items():
            with self.subTest(name=name):
                d = fixture(); change(d)
                self.assertEqual(audit.audit(d)["status"], "inconclusive")

    def test_negative_native_rows(self):
        for name in ("missing_branch", "wrong_owner", "unknown_family", "no_rows", "no_reserve",
                     "restore_anomaly", "automatic_entry", "missing_after", "unowned", "invalid", "native_increase", "expired"):
            with self.subTest(name=name):
                d = fixture(); j = d["gameplay"]["reload_flow"]["empty_step_diagnostic"]; r = j["rows"][0]
                if name == "missing_branch": j["rows"].pop()
                elif name == "wrong_owner": r["owner"] = [65536,65537,65538,65540,1,2,3]
                elif name == "unknown_family": r["family_known"] = False
                elif name == "no_rows": j["rows"] = []
                elif name == "no_reserve": r["before"]["reserve"] = r["after"]["reserve"] = 0
                elif name == "restore_anomaly": r["restored"] = False
                elif name == "automatic_entry": r["after"]["next"] = 10
                elif name == "missing_after": r["after"] = None
                elif name == "unowned": r["owner_retained"] = False
                elif name == "invalid": r["branch"] = "0"
                elif name == "native_increase": r["after"]["loaded"] = 8
                elif name == "expired": r["deadline_ns"] = 99
                self.assertEqual(audit.audit(d)["status"], "inconclusive")

    def test_full_depletion_is_required(self):
        d = fixture(); d["gameplay"]["empty_fire_probe"]["consumed"] = 7
        self.assertIn("full_depletion_not_verified", audit.audit(d)["reasons"])

    def test_hitch_policy_is_explicit_and_does_not_renew_evidence(self):
        d = fixture()
        rows = d["gameplay"]["reload_flow"]["empty_step_diagnostic"]["rows"]
        for delta in (.0596221, step.delta_limit("bounded100")):
            for r in rows:
                r["context"]["delta"] = delta
            self.assertEqual(audit.audit(d)["status"], "inconclusive")
            self.assertEqual(audit.audit(d, "bounded100")["status"], "bounded_monitor_verified")
        rows[0]["context"]["delta"] = .10001
        self.assertEqual(audit.audit(d, "bounded100")["status"], "inconclusive")
        rows[0]["context"]["delta"] = .06
        rows[0]["deadline_ns"] = rows[0]["now_ns"]
        self.assertEqual(audit.audit(d, "bounded100")["status"], "inconclusive")
        with self.assertRaises(step.ReportError):
            audit.audit(d, "unbounded")

    def test_normal_receipt_rejection_is_informational(self):
        d = fixture(); d["gameplay"]["reload_flow"]["empty_magazine_control"]["receipt_failures"] = 22
        self.assertEqual(audit.audit(d)["status"], "bounded_monitor_verified")
        self.assertEqual(audit.audit(d)["receipt_rejections"], 22)
        d["gameplay"]["reload_flow"]["empty_step_diagnostic"]["rows"].pop()
        self.assertEqual(audit.audit(d)["status"], "inconclusive")


if __name__ == "__main__":
    unittest.main()
