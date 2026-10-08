"""Audit bounded BC2 hand ownership telemetry, without native process access.

The default requires support and sight ownership coverage, not a fixed number of
mode switches or a direct Transfer. An explicit --coverage selects a narrower
fixture. --output may create a NEW JSON report; sources are never overwritten.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


def require(ok, message):
    if not ok:
        raise ValueError(message)


def integer(value, label, positive=False):
    require(type(value) is int and value >= (1 if positive else 0),
            f"{label}: expected {'positive' if positive else 'nonnegative'} integer")
    return value


def flag(value, label):
    require(type(value) is bool, f"{label}: expected boolean")
    return value


def object_value(value, label):
    require(isinstance(value, dict), f"{label}: expected object")
    return value


def array(value, label, cap=65536):
    require(isinstance(value, list) and len(value) <= cap, f"{label}: invalid array")
    return value


def load(path, sources):
    require(path.stat().st_size <= 64 * 1024 * 1024, f"{path.name}: exceeds audit size limit")
    raw = path.read_bytes()
    sources.append({"path": str(path.resolve()), "sha256": hashlib.sha256(raw).hexdigest()})
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"duplicate JSON key: {key}")
            result[key] = value
        return result
    def nonfinite(value):
        raise ValueError(f"nonfinite JSON constant: {value}")
    return json.loads(raw.decode("utf-8-sig"), object_pairs_hook=unique, parse_constant=nonfinite)


def owner_key(row):
    return tuple(integer(row[k], k, True) for k in ("actor", "rig_epoch", "equip_generation", "space"))


def check(native, coverage="both"):
    path = Path(native)
    if path.is_dir():
        path /= "native-trace.json"
    sources, failures = [], []
    summary = {"events": 0, "checks": 0, "support_tokens": 0, "sight_claims": 0,
               "sight_requests": 0, "sight_acknowledgements": 0, "sight_commits": 0,
               "support_to_sight": 0, "sight_to_support": 0}
    try:
        require(coverage in ("both", "support", "sight"), "unknown coverage selection")
        doc = object_value(load(path, sources), "native trace")
        require(doc.get("hooks_disabled") is True, "native telemetry is not finalized with hooks disabled")
        gameplay = object_value(doc["gameplay"], "gameplay")
        ownership = object_value(gameplay["hand_ownership"], "hand_ownership")
        require(integer(ownership["schema"], "schema") == 1, "unsupported hand ownership schema")
        capacity = integer(ownership["event_capacity"], "event_capacity", True)
        require(capacity <= 65536, "unbounded ownership event capacity")
        summary["checks"] = integer(ownership["checks"], "checks", True)
        for name in ("overlap_failures", "support_without_claim", "sight_without_claim", "lease_failures"):
            require(integer(ownership[name], name) == 0, f"all-gather invariant failed: {name}")
        total = integer(ownership["events_total"], "events_total")
        dropped = integer(ownership["events_dropped"], "events_dropped")
        events = array(ownership["events"], "events")
        require(len(events) <= capacity and total == len(events) + dropped, "ownership event accounting mismatch")
        require(dropped == 0, "ownership transition events were dropped; complete chronology unavailable")
        require(bool(events), "no ownership transition evidence")
        summary["events"] = len(events)
        token_history, closed_tokens, previous_ids = {}, set(), set()
        support_tokens, sight_claims, requests, acknowledgements, commits = {}, set(), {}, set(), set()
        last_serial, last_time = 0, 0
        previous_role = None
        for row in events:
            object_value(row, "ownership row")
            serial = integer(row["serial"], "serial", True)
            now = integer(row["now_ns"], "now_ns", True)
            require(serial > last_serial and now >= last_time, "ownership serial/time went backward or duplicated")
            last_serial, last_time = serial, now
            raw = integer(row["raw_generation"], "raw_generation")
            require(isinstance(row["reason"], str) and bool(row["reason"]), "ownership row has no reason")
            holding = flag(row["support_holding"], "support_holding")
            support_token = integer(row["support_token"], "support_token")
            phase = integer(row["sight_phase"], "sight_phase")
            require(phase <= 3, "invalid sight phase")
            request = integer(row["sight_request"], "sight_request")
            ack = integer(row["sight_ack"], "sight_ack")
            commit = flag(row["sight_commit"], "sight_commit")
            left, right = row["left"], row["right"]
            claims = {hand: object_value(value, f"{hand} claim") for hand, value in
                      (("left", left), ("right", right)) if value is not None}
            active_owner = owner_key(row) if claims or request or ack or commit else None
            physical_item = integer(row.get("physical_item", 0), "physical_item")
            weapon = integer(row.get("weapon", 0), "weapon")
            if claims:
                require(raw > 0 and physical_item > 0 and weapon > 0, "active claims lack raw/item/weapon identity")
            ids = set()
            for hand, claim in claims.items():
                token = integer(claim["id"], "claim id", True)
                kind = integer(claim["kind"], "claim kind", True)
                require(kind == 1 if hand == "right" else kind in (2, 3), "unexpected claim role for hand")
                item = integer(claim["item"], "claim item", True)
                generation = integer(claim["generation"], "claim generation", True)
                contact = integer(claim["contact"], "claim contact", True)
                contact_generation = integer(claim["contact_generation"], "contact generation", True)
                prerequisite = integer(claim["prerequisite"], "prerequisite")
                source_generation = integer(claim["input_generation"], "claim input generation", True)
                deadline = integer(claim["deadline_ns"], "claim deadline", True)
                require(item == physical_item and generation == row["equip_generation"], "claim item/equip identity mismatch")
                require(source_generation <= raw, "claim proof generation is ahead of raw input")
                require(deadline > now, "expired active claim")
                require(token not in ids and token not in closed_tokens, "duplicate or resurrected claim token")
                ids.add(token)
                identity = (hand, kind, active_owner, item, generation, contact, contact_generation, prerequisite)
                if token in token_history:
                    old_identity, old_source, old_deadline = token_history[token]
                    require(identity == old_identity, "claim identity changed without a new token")
                    require(source_generation >= old_source, "claim proof generation went backward")
                    require(source_generation > old_source or deadline <= old_deadline,
                            "duplicate proof extended claim deadline")
                token_history[token] = identity, source_generation, deadline
            closed_tokens.update(previous_ids - ids)
            previous_ids = ids
            left_kind = left["kind"] if left is not None else 0
            require(holding == (left_kind == 2), "support policy and left-hand claim disagree")
            require((phase != 0) == (left_kind == 3), "sight policy and left-hand claim disagree")
            require(bool(support_token) == holding, "support token/holding mismatch")
            require(not (holding and phase), "independent support and sight overlap")
            if right:
                require(right["prerequisite"] == 0, "GunHold unexpectedly has a prerequisite")
            if left:
                require(right is not None and left["prerequisite"] == right["id"], "left claim lacks exact GunHold dependency")
                require(left["item"] == right["item"] and left["generation"] == right["generation"], "dependent item mismatch")
                require((left["contact"], left["contact_generation"]) !=
                        (right["contact"], right["contact_generation"]), "dependent claims reuse one contact")
                role = (active_owner, physical_item, left_kind)
                if previous_role and role[:2] == previous_role[:2] and role[2] != previous_role[2]:
                    summary["support_to_sight" if role[2] == 3 else "sight_to_support"] += 1
                previous_role = role
            if holding:
                identity = (active_owner, physical_item, weapon)
                require(support_token not in support_tokens or support_tokens[support_token] == identity,
                        "support grasp token changed owner/item")
                support_tokens[support_token] = identity
            if left_kind == 3:
                sight_claims.add(left["id"])
            if request:
                require(left_kind == 3 and phase in (2, 3), "sight request without awaiting/latched ownership")
                identity = (active_owner, physical_item, weapon)
                if request in requests:
                    # Mode handoff may replace the backend weapon pointer, but never owner/item.
                    require(requests[request][:2] == identity[:2], "sight request identity changed")
                else:
                    require(phase == 2, "new sight request first appears after acknowledgement")
                    requests[request] = identity
            if ack:
                require(ack in requests and requests[ack][:2] == (active_owner, physical_item),
                        "sight acknowledgement has no exact owned request")
                acknowledgements.add(ack)
            if commit:
                require(phase == 3 and left_kind == 3 and ack in requests and ack > 0,
                        "sight commit lacks exact native acknowledgement/ownership")
                require(ack not in commits, "sight request committed more than once")
                commits.add(ack)
        require(not previous_ids, "finalized trace retains active hand claims")
        if coverage in ("both", "support"):
            require(bool(support_tokens), "support ownership coverage absent")
        if coverage in ("both", "sight"):
            require(bool(sight_claims), "sight ownership coverage absent")
        if support_tokens:
            support = object_value(gameplay["two_hand_support"], "two_hand_support")
            require(support.get("event_schema") == 1, "support transition schema unavailable")
            require(integer(support["events_dropped"], "support events_dropped") == 0,
                    "support transitions were dropped")
            support_events = array(support["events"], "support events")
            require(integer(support["events_total"], "support events_total") == len(support_events),
                    "support transition accounting mismatch")
            grabs, releases = {}, set()
            for event in support_events:
                object_value(event, "support event")
                sample = object_value(event["sample"], "support sample")
                if sample.get("engaged") is True and event.get("reset_reason") is None:
                    token = integer(sample["grasp_token"], "support grasp token", True)
                    require(token not in grabs, "support token engaged more than once")
                    grabs[token] = event
                if sample.get("released") is True or event.get("reset_reason") is not None:
                    releases.add(integer(event["previous_grasp_token"], "released support token", True))
            for token, (owner, item, weapon) in support_tokens.items():
                require(token in grabs, "owned support token lacks native support engagement evidence")
                event = grabs[token]
                require((event["actor"], event["rig_epoch"], event["space"]) == (owner[0], owner[1], owner[3])
                        and event["weapon"] == weapon, "support engagement owner/space/weapon mismatch")
                require(token in releases, "owned support token lacks release/reset evidence")
        if requests or acknowledgements or commits:
            mode_rows = array(gameplay["weapon_mode_records"], "weapon_mode_records")
            joined = {}
            for mode in mode_rows:
                object_value(mode, "mode record")
                gesture = integer(mode.get("gesture", 0), "mode gesture")
                if not gesture or gesture not in requests:
                    continue
                require(gesture not in joined, "native mode request appears more than once")
                owner, item, weapon = requests[gesture]
                require((mode["actor"], mode["owner"], mode["space"]) == (owner[0], owner[1], owner[3]),
                        "native mode owner/space mismatch")
                require(mode["from"] == weapon and mode["target"] != weapon and mode["target"] > 0,
                        "native mode source/target mismatch")
                require(mode["action"] in (33, 36), "unexpected native mode action")
                require(mode["ack_ms"] == 0 or mode["ack_ms"] >= mode["ms"], "native ack precedes request")
                if gesture in acknowledgements or gesture in commits:
                    require(mode["ack_ms"] > 0 and mode["cancelled"] is False,
                            "ownership ack/commit lacks successful native mode acknowledgement")
                joined[gesture] = mode
            require(set(joined) == set(requests), "native request identity join unavailable or incomplete")
            for row in events:
                if row["sight_ack"]:
                    require(row["weapon"] == joined[row["sight_ack"]]["target"],
                            "acknowledged backend weapon is not native target")
        summary.update(support_tokens=len(support_tokens), sight_claims=len(sight_claims),
                       sight_requests=len(requests), sight_acknowledgements=len(acknowledgements),
                       sight_commits=len(commits))
    except (OSError, UnicodeError, ValueError, KeyError, TypeError, RecursionError) as exc:
        failures.append(str(exc))
    return {"schema": "fvr.hand_ownership_audit.v1", "passed": not failures,
            "coverage": coverage, "failures": failures, "summary": summary,
            "headset_tested": False, "sources": sources,
            "scope": "all-gather counters plus retained ownership edges; no claim of a direct Transfer or unsampled pose geometry"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--coverage", choices=("both", "support", "sight"), default="both")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    result = check(args.native, args.coverage)
    text = json.dumps(result, indent=2, allow_nan=False) + "\n"
    if args.output:
        try:
            with args.output.open("x", encoding="utf-8") as stream:
                stream.write(text)
        except OSError as exc:
            parser.error(f"cannot create new output: {exc}")
    else:
        print(text, end="")
    return 0 if result["passed"] else 2


if __name__ == "__main__":
    sys.exit(main())

