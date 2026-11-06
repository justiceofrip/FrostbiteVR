"""Pinned actual452 Insert failure: retained supply is not a native receipt.

Set BC2_FIXED_INSERT_REPLAY to the retained live-driver directory. This replays
original observations at their original evaluation times; it never drives BC2.
"""
import copy
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "fixed_insert_actual480_geometry", ROOT / "tools/bc2_reload_stimulus_geometry.py")
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)

PINS = {
    "stimulus-02.json": "bd1fc0197c575efa6b4fd24d5ac54aadd9869e9ff0e4bdd1a7adbe99824486b9",
    "events.jsonl": "9247bd552a7df6742f69b4474cee9dcb1f051a29f667b389f12ec264e829bdbe",
    "result.json": "570715fdaffe855d1c481ec248b04290acdb23fb44cca2d17d6f8d1263a02f47",
    "observation-040.json.gz": "e22acbb6653b8295ac41874874333861dc55a6b94a04d280a05bd8f0f4a10c08",
    "observation-041.json.gz": "e159a3fcc9a73541d30e5d2267a0086b5c92cab21f297dd312b9465ed2a1e395",
    "observation-042.json.gz": "efb88c1aa56903aa19e461d821036c50fcf050b94fc0ced36fdd0dd3a9d4bfd4",
}


@unittest.skipUnless(os.environ.get("BC2_FIXED_INSERT_REPLAY"),
                     "pinned local actual452 fixed66/xr-game02 Insert observations required")
class ActualFixedInsert480Tests(unittest.TestCase):
    def setUp(self):
        root = Path(os.environ["BC2_FIXED_INSERT_REPLAY"])
        data = {}
        for name, digest in PINS.items():
            raw = (root / name).read_bytes()
            self.assertEqual(hashlib.sha256(raw).hexdigest(), digest, name)
            if name.endswith(".gz"):
                raw = gzip.decompress(raw)
            data[name] = ([json.loads(row) for row in raw.splitlines()]
                          if name.endswith(".jsonl") else json.loads(raw))
        self.stimulus = data["stimulus-02.json"]
        self.result = data["result.json"]
        self.aligned = data["observation-040.json.gz"]
        self.released = [data[f"observation-{index:03}.json.gz"] for index in (41, 42)]
        self.events = data["events.jsonl"]
        self.prior = self.result["boundaries"]
        self.observations = {e["index"]: e for e in self.events if e["type"] == "native_observation"}
        commits = [e for e in self.events if e["type"] == "command_commit"]
        self.release_written = commits[-1]["writtenNs"]
        self.alignment_written = commits[-2]["writtenNs"]

    def check(self, snapshot, *, now=None, prior=None):
        return g.check_boundary(self.stimulus, snapshot, "fixed_inserted",
            self.observations[42]["drainedNs"] if now is None else now,
            command_written_ns=self.release_written,
            prior=self.prior if prior is None else prior)

    def assert_refusal(self, snapshot, *, reason=None, **kwargs):
        with self.assertRaises(g.GeometryRefusal) as caught:
            self.check(snapshot, **kwargs)
        self.assertNotIsInstance(caught.exception, g.BoundaryPending)
        if reason is not None:
            self.assertEqual(str(caught.exception), reason)

    def test_original_alignment_is_accepted_without_mutating_evidence(self):
        original = copy.deepcopy((self.stimulus, self.aligned))
        observed = g.check_boundary(self.stimulus, self.aligned, "fixed_aligned",
            self.observations[40]["drainedNs"], command_written_ns=self.alignment_written)
        self.assertEqual(observed, self.prior["fixed_aligned"])
        self.assertEqual(observed["gate"]["claimToken"]["id"], 6)
        self.assertFalse(observed["nativeReceiptProof"])
        self.assertEqual((self.stimulus, self.aligned), original)

    def test_actual_released_round_is_neither_pending_nor_completed(self):
        self.assertEqual(self.result["commands"], 278)
        self.assertIsNone(self.result["pendingBoundary"]["lastPending"])
        self.assertFalse(self.observations[41]["currentAtReceive"])
        self.assertTrue(self.observations[42]["currentAtReceive"])
        baseline = self.stimulus["geometryEvidence"]["baseline"]
        for snapshot in self.released:
            x, t = snapshot["fixedContainer"], snapshot["fixedContainer"]["tracking"]
            self.assertEqual(t["step"], 2)
            self.assertTrue(t["carried"] and x["raw"]["admittedAtTick"])
            self.assertIsNone(t["hand"])
            self.assertIsNone(x["leftClaim"])
            self.assertIsNone(t["target"])
            self.assertIsNotNone(t["carriedSupply"])
            self.assertFalse(t["pending"])
            self.assertIsNone(x["pendingRequest"])
            self.assertIsNone(x["lastCompletion"])
            self.assertEqual(x["view"]["snapshot"]["counts"], baseline["counts"])
            self.assertEqual(t["gun"]["token"], baseline["gun"])
        original = copy.deepcopy((self.stimulus, self.released, self.prior))
        self.assertLess(self.observations[42]["drainedNs"], self.result["pendingBoundary"]["deadlineNs"])
        self.assert_refusal(self.released[-1], reason="insertion has not consumed its physical transaction")
        self.assertEqual((self.stimulus, self.released, self.prior), original)

    def test_original_deadlines_cannot_be_extended_to_turn_refusal_into_wait(self):
        snapshot = self.released[-1]
        deadline = g.source_deadline(snapshot, "fixed")
        self.assertLess(self.observations[42]["drainedNs"], deadline)
        self.assert_refusal(snapshot, now=deadline)
        self.assert_refusal(snapshot, now=self.result["pendingBoundary"]["deadlineNs"])

    def test_changed_owner_raw_input_or_alignment_cannot_authorize_completion(self):
        for fault in ("owner", "raw", "root", "lifetime", "alignment"):
            with self.subTest(fault=fault):
                snapshot, prior = copy.deepcopy((self.released[-1], self.prior))
                x = snapshot["fixedContainer"]
                if fault == "owner":
                    snapshot["owner"]["weapon"] += 1
                elif fault == "raw":
                    x["raw"]["current"] = False
                elif fault == "root":
                    x["tracking"]["gun"]["token"]["id"] += 1
                elif fault == "lifetime":
                    x["tracking"]["lifetime"] += 1
                else:
                    prior["fixed_aligned"]["gate"]["completionPrerequisite"]["planSourceNonce"] = "1"
                self.assert_refusal(snapshot, prior=prior)

    def test_consumed_shape_and_counts_without_receipt_cannot_finish(self):
        # Synthetic countercase only: do not turn the failed actual episode into
        # a completed one by inferring an operation from shape or ammo counts.
        snapshot = copy.deepcopy(self.released[-1])
        x = snapshot["fixedContainer"]
        x["tracking"].update(step=3, carried=False, carriedSupply=None)
        x["view"]["snapshot"]["counts"].update(loaded=1, reserve=6)
        self.assert_refusal(snapshot, reason="missing same-lifetime completed native transaction")


if __name__ == "__main__":
    unittest.main()
