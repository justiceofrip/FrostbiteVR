import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("audit", Path(__file__).resolve().parents[1] / "tools/audit_reload_request_probe.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture():
    def row(event, seconds, load=6, reserve=21, request=0):
        now = int(seconds * 1e9)
        return {"event": event, "now_ns": now, "source_ns": now - 1, "deadline_ns": now + 100_000_000,
                "sequence": request or 91, "cycle": 91, "request": request, "server_invocation": 777 if event == 4 else 0,
                "lease": {"held": True, "loaded": load, "reserve": reserve},
                "native": {"branches": [{"loaded": load, "reserve": reserve, "transfers": 1, "last_transfer": 777 if b == 2 else 700+b} for b in range(3)]}}
    rows = [row(1, 6), row(2, 7), row(0, 21.5), row(3, 23, request=123), row(4, 23.4, 7, 20, 123),
            row(7, 23.8, 7, 20, 123), row(5, 25, 7, 20, 123), row(0, 27, 8, 19, 123)]
    return {"hooks_disabled": True, "gameplay": {
        "reload_flow": {"drained": True, "start_ns": 1_000_000_000,
                        "diagnostic_hold": {"patch_failures": 0, "restore_failures": 0, "applied": [100, 100, 100], "restored": [100, 100, 100]},
                        "request_cycle": {"undelivered_acknowledgements": 0}},
        "reload_request_probe": {"enabled": True, "scripted_reservation": True, "physical_bridge_integrated": False,
                                 "started": True, "submitted": True, "acknowledged": True, "reheld_350ms": True,
                                 "stopped": True, "reason": 0, "dropped": 0, "journal": rows}}}


class AuditTests(unittest.TestCase):
    def test_complete(self):
        self.assertTrue(module.audit(fixture())["passed"])

    def test_never_accept_old_round_fixture(self):
        self.assertFalse(module.audit({"hooks_disabled": True, "gameplay": {"reload_flow": {"drained": True}}})["passed"])

    def test_retirement_and_post_drain_reserve(self):
        trace = fixture()
        probe = trace["gameplay"]["reload_request_probe"]
        identity = {"owner": [1, 2, 3, 4, 5, 6, 7], "server": [8, 9, 10], "firing": [11, 12, 13]}
        native = probe["journal"][5]["native"]
        native.update(owner=identity["owner"], server=identity["server"])
        for branch, firing in zip(native["branches"], identity["firing"]):
            branch["firing"] = firing
        probe["retirement"] = {
            "enabled": True, "verified": True, "reason": 0, "stopped_ns": 25_000_000_000, "expected_identity": identity,
            "receipt": {"identity": identity, "cycle": 91, "event": 17, "observed_ns": 25_010_000_000,
                        "checked_ns": 25_011_000_000, "deadline_ns": 25_210_000_000, "verified": True},
            "reserve": {"identity": identity, "sequence": 77, "observed_ns": 25_012_000_000,
                        "checked_ns": 25_013_000_000, "deadline_ns": 25_112_000_000,
                        "loaded": 7, "count": 20, "capacity": 8, "verified": True}}
        self.assertTrue(module.audit(trace)["passed"])
        for variant in range(8):
            bad = copy.deepcopy(trace)
            recovery = bad["gameplay"]["reload_request_probe"]["retirement"]
            if variant == 0:
                recovery["verified"] = False
            elif variant == 1:
                recovery["receipt"]["cycle"] += 1
            elif variant == 2:
                recovery["receipt"]["event"] = 0
            elif variant == 3:
                recovery["receipt"]["deadline_ns"] = recovery["receipt"]["checked_ns"]
            elif variant == 4:
                recovery["reserve"]["observed_ns"] = recovery["receipt"]["checked_ns"] - 1
            elif variant == 5:
                recovery["reserve"]["identity"] = {}
            elif variant == 6:
                recovery["reserve"]["count"] += 1
            else:
                recovery["reserve"]["checked_ns"] = recovery["receipt"]["deadline_ns"]
            self.assertFalse(module.audit(bad)["passed"], variant)

    def test_reject_missing_or_wrong_authority_and_restore(self):
        for variant in range(7):
            trace = copy.deepcopy(fixture())
            probe = trace["gameplay"]["reload_request_probe"]
            if variant == 0:
                probe["journal"][4]["server_invocation"] += 1
            elif variant == 1:
                probe["journal"][5]["native"]["branches"][2]["reserve"] += 1
            elif variant == 2:
                trace["gameplay"]["reload_flow"]["diagnostic_hold"]["restored"][1] -= 1
            elif variant == 3:
                probe["journal"][2]["lease"]["held"] = False
            elif variant == 4:
                probe["journal"][3]["now_ns"] = 19_000_000_000
            elif variant == 5:
                probe["journal"][5]["now_ns"] = 23_500_000_000
            else:
                probe["journal"][-1]["native"]["branches"][0]["loaded"] = 7
            self.assertFalse(module.audit(trace)["passed"], variant)


if __name__ == "__main__":
    unittest.main()
