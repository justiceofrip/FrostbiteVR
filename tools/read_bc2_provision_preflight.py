"""Read saved BPF1 preflight evidence only; no process, input or request writes.

Missing or invalid joined evidence is UNKNOWN, never a set of zero counters.
Terminal receipt decoding is descriptive and does not authorize a native action.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct


_spec = importlib.util.spec_from_file_location(
    "_provision_control_reader", Path(__file__).with_name("read_bc2_provision_control.py"))
_control = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_control)

OUTCOMES = ("Accepted", "SnapshotMismatch", "RequestMismatch", "BoundaryRejected",
            "RevalidationRejected", "PolicyPhase", "SeatSequence", "OwnerChanged",
            "ServerSequence", "NestedDepth")
BOUNDARIES = ("None", "ThreadConflict", "Owner", "FreshOwner", "Thread", "ServerAge",
              "Actor", "Caller", "NativeDepth", "EntryDepth", "Code", "Seat", "Idle")
SERVER_EVENTS = ("Advanced", "Invalid", "Conflict", "NotAdvanced", "WrongFiring", "OwnerRejected")
PROGRESSION = ("Advanced", "Repeated", "Regressed", "Unavailable")
STATUS_NAMES = ("Disabled", "Ready", "Observing", "Preflight", "Excluded", "AwaitingReplication",
                "Equipped", "AwaitingRestore", "Restored", "Refused", "Expired", "Quarantined", "Stopped")
PHASE_NAMES = ("Preflight", "Armed", "AwaitingReplication", "Equipped", "RestoreArmed",
               "AwaitingRestore", "Restored", "Indeterminate", "Cancelled")
OPERATIONS = ("observe", "equip", "restore")
TERMINAL_STATUSES = (1, 6, 8, 9, 10, 11, 12)
COUNTER_LIMIT = 65535
PREFLIGHT_SIZE = 1496
DETAILS_OFFSET = 1216
PREFLIGHT_REQUIRED = 8
REFUSAL_MARKER = "fvr.provision-command-refusal.v1"
REFUSAL_REASONS = {"WaitForFreshSource": "TimedOut", "ArmRequest": "Rejected", "CanPublishRequest": "Rejected"}


def _require(okay, message):
    if not okay:
        raise ValueError(message)


def decode_terminal_receipt(blob, expected_identity, job, nonce, operation):
    """Validate identity/request binding and decode any saved terminal receipt.

    Unlike the successful Observe/Equip validator, this accepts Refused, Expired,
    Quarantined and Stopped with incomplete preflight and preserved failure data.
    A terminal status alone is not a validated provisioning success.
    """
    _control._header(blob, 1272, 0x31524342)
    status = struct.unpack_from("<I", blob, 12)[0]
    _require(status in TERMINAL_STATUSES, "receipt is not terminal")
    process = _control._identity(blob[16:1096])
    _require(process == expected_identity, "receipt process identity differs")
    got_nonce, got_job, preflight, retirements, dispatch = struct.unpack_from("<5Q", blob, 1096)
    _require(nonce and job and (got_nonce, got_job) == (nonce, job), "receipt nonce/job differs")
    _require(operation in OPERATIONS, "unsupported operation")
    expected_success = {1: "observe", 6: "equip", 8: "restore"}
    _require(status not in expected_success or operation == expected_success[status],
             "terminal status/operation mismatch")
    observed, dispatched = struct.unpack_from("<2q", blob, 1136)
    previous = struct.unpack_from("<4I3Q", blob, 1152)
    current = struct.unpack_from("<4I3Q", blob, 1192)
    (phase, calls, returned, thread, boundary, server, converged,
     excluded, retired, reserved) = struct.unpack_from("<10I", blob, 1232)
    _require(phase < len(PHASE_NAMES) and boundary < len(BOUNDARIES), "unsupported receipt phase/boundary")
    _require(all(value in (0, 1) for value in (server, converged, excluded, retired)) and not reserved,
             "receipt boolean/reserved fields")
    _require(observed >= 0 and dispatched >= 0, "negative receipt timestamp")
    return dict(status=status, status_name=STATUS_NAMES[status], identity=process,
                nonce=nonce, job=job, operation=operation, preflight=preflight,
                preflight_required=PREFLIGHT_REQUIRED, preflight_met=preflight >= PREFLIGHT_REQUIRED,
                retirements=retirements, native_calls=calls, dispatch_sequence=dispatch,
                dispatch_ns=dispatched, observed_ns=observed, previous=list(previous), current=list(current),
                returned_item=returned, thread=thread, phase=phase, phase_name=PHASE_NAMES[phase],
                boundary=boundary, boundary_name=BOUNDARIES[boundary], input_excluded=bool(excluded),
                old_item_retired=bool(retired), server_item_observed=bool(server),
                client_server_converged=bool(converged))


def _quartet(control, request, receipt, admission):
    for name, blob in (("control", control), ("request", request), ("receipt", receipt), ("admission", admission)):
        _require(blob is not None, f"missing {name}")
    joined = _control.decode(control, request, receipt, admission)
    operation = struct.unpack_from("<I", request, 12)[0]
    _require(operation < len(OPERATIONS), "unsupported request operation")
    _require(struct.unpack_from("<I", request, 1260)[0] == 0, "reserved request field")
    counts = struct.unpack_from("<10I", admission, 1160)
    mask = struct.unpack_from("<I", admission, 1156)[0]
    _require(all(count <= 31 for count in counts), "admission counter outside saturation bound")
    _require(mask == sum(1 << index for index, count in enumerate(counts) if count == 31),
             "admission saturation mask mismatch")
    _require(joined["scope"] or not any(counts), "exhausted admission scope contains samples")
    identity = _control._identity(request[16:1096])
    terminal = decode_terminal_receipt(receipt, identity, joined["job"], joined["nonce"], OPERATIONS[operation])
    return joined, terminal


def _details(preflight, admission, joined):
    _require(preflight is not None, "missing preflight sidecar")
    _control._header(preflight, PREFLIGHT_SIZE, 0x31465042)
    _require(struct.unpack_from("<I", preflight, 12)[0] == 0, "reserved preflight header")
    _require(preflight[16:1216] == admission, "preflight embedded admission differs from saved quartet")
    details = preflight[DETAILS_OFFSET:]
    mask, server_at_submit, server_latest = struct.unpack_from("<3Q", details)
    maximum_depth, reserved = struct.unpack_from("<2I", details, 88)
    _require(not reserved and mask < (1 << 54), "unsupported preflight detail flags")
    expected_mask = 0

    def counters(offset, names, first_bit):
        nonlocal expected_mask
        values = struct.unpack_from(f"<{len(names)}I", details, offset)
        _require(all(value <= COUNTER_LIMIT for value in values), "preflight counter outside saturation bound")
        result = {}
        for index, (name, count) in enumerate(zip(names, values)):
            saturated = count == COUNTER_LIMIT
            if saturated:
                expected_mask |= 1 << (first_bit + index)
            result[name] = dict(count=count, saturated=saturated, lower_bound=saturated)
        return result

    def progression(offset, first_bit):
        first, last = struct.unpack_from("<2Q", details, offset)
        return dict(first_sequence=first, last_sequence=last,
                    counts=counters(offset + 16, PROGRESSION, first_bit))

    result = dict(
        schema="fvr.provision-preflight.v1", state="KNOWN", nonce=joined["nonce"], job=joined["job"],
        scope=joined["scope"], status=joined["status"], status_name=STATUS_NAMES[joined["status"]],
        accepted_ns=joined["accepted_ns"], issued_ns=joined["issued_ns"],
        deadline_ns=joined["deadline_ns"], frozen_ns=joined["frozen_ns"], saturated_mask=mask,
        server_sequence_at_submit=server_at_submit, server_sequence_latest=server_latest,
        maximum_native_depth=maximum_depth,
        outcomes=counters(96, OUTCOMES, 0), boundary=counters(136, BOUNDARIES, 10),
        revalidated_boundary=counters(188, BOUNDARIES, 23), native_depth=counters(240, ("0", "1", "2", ">=3"), 36),
        server_events=counters(256, SERVER_EVENTS, 40), seats=progression(24, 46),
        zero_depth_seats=progression(56, 50),
        limits=["65535 is a lower bound, not an exact count; counts cover this arm only.",
                "Receipt preflight is cumulative and need not equal this arm's Accepted count.",
                "Sequence progression uses sampled server sequences; gaps are not event counts.",
                "Boundary counters cover checks actually run; zero does not prove a check succeeded.",
                "This diagnostic does not establish native success or authorize input or retry."])
    _require(mask == expected_mask, "preflight saturation mask mismatch")
    return result


def decode(control, request, receipt, admission, preflight):
    """Strictly join five saved blobs; invalid or incomplete evidence raises ValueError."""
    joined, _ = _quartet(control, request, receipt, admission)
    return _details(preflight, admission, joined)


def inspect(control=None, request=None, receipt=None, admission=None, preflight=None):
    """Return KNOWN/UNKNOWN with no invented counters; preserve a valid quartet's terminal receipt.

    Keys: state, reason, terminal, preflight. On a sidecar error, terminal may
    remain known but preflight is None. On a quartet error both are None.
    """
    result = dict(state="UNKNOWN", reason=None, terminal=None, preflight=None)
    try:
        joined, result["terminal"] = _quartet(control, request, receipt, admission)
        result["preflight"] = _details(preflight, admission, joined)
    except (ValueError, struct.error) as error:
        result["reason"] = str(error)
        return result
    result["state"] = "KNOWN"
    return result


def _command_refusal(command_log, expected_identity, job, nonce, operation):
    """Accept one current v1 marker; absent/foreign/malformed markers explain no subbranch."""
    if not isinstance(command_log, str):
        return None
    lines = [line for line in command_log.splitlines() if "fvr.provision-command-refusal" in line]
    if len(lines) != 1:
        return None
    try:
        prefix, *tokens = lines[0].split()
        pairs = [token.split("=") for token in tokens]
        _require(prefix == REFUSAL_MARKER and all(len(pair) == 2 for pair in pairs), "marker shape")
        fields = dict(pairs)
        unsigned = ("pid", "job", "nonce", "operation", "sample_pid", "creation", "session", "source_sequence")
        signed = ("source_observed_ns", "issued_ns", "deadline_ns", "now_ns")
        _require(len(fields) == len(pairs) and set(fields) == set(unsigned + signed + ("stage", "reason", "image")),
                 "marker fields")
        _require(fields["stage"] in REFUSAL_REASONS and REFUSAL_REASONS[fields["stage"]] == fields["reason"], "marker branch")
        for name in unsigned + signed:
            _require(re.fullmatch(r"[0-9]+" if name in unsigned else r"-?[0-9]+", fields[name]), "marker number")
            fields[name] = int(fields[name])
            _require(0 <= fields[name] < 2**64 if name in unsigned else -2**63 <= fields[name] < 2**63,
                     "marker number range")
        _require((fields["pid"], fields["job"], fields["nonce"], fields["operation"]) ==
                 (expected_identity["pid"], job, nonce, OPERATIONS.index(operation)), "marker request differs")
        _require((fields["sample_pid"], fields["creation"], fields["session"], fields["image"]) ==
                 (expected_identity["pid"], expected_identity["creation"], expected_identity["session"],
                  expected_identity["executable_sha256"]), "marker process differs")
        _require(re.fullmatch(r"[0-9a-f]{64}", fields["image"]), "marker image")
        return fields
    except (ValueError, KeyError, TypeError):
        return None


def classify_command_outcome(command_exit, *, command_pinned, expected_identity, job, nonce, operation,
                             command_log=None, timed_out=False, control=None, request=None, receipt=None,
                             admission=None, preflight=None):
    """Classify a saved invocation; never retry, write, or validate native success.

    command_pinned must be True only after the caller verifies the executable
    against its accepted binding. Only that pinned command's exit 4 guarantees
    no publication. exact_terminal still requires the caller's successful-receipt
    validator before claiming success. A marker cannot override an exit/timeout.
    """
    result = dict(classification="outcome_unknown", status="outcome_unknown_no_retry", stage="unknown_subbranch",
                  reason=None, request_submitted=None, native_assignment_requested=None,
                  diagnostic=None, terminal=None, prior_terminal=None, prior_preflight=None)
    evidence = inspect(control, request, receipt, admission, preflight)
    terminal = evidence["terminal"]
    if (terminal and terminal["identity"] == expected_identity and terminal["job"] == job
            and terminal["nonce"] < nonce):
        result.update(prior_terminal=terminal, prior_preflight=evidence["preflight"])
    if timed_out or command_exit == 7:
        result["reason"] = "process_timeout" if timed_out else "terminal_wait_timeout"
    elif command_pinned is not True:
        result["reason"] = "command_not_pinned"
    elif command_exit == 4:
        diagnostic = _command_refusal(command_log, expected_identity, job, nonce, operation)
        result.update(classification="known_not_published", status="not_submitted_no_retry",
                      reason="command_prepublication_refusal", request_submitted=False,
                      native_assignment_requested=False, diagnostic=diagnostic,
                      stage=diagnostic["stage"] if diagnostic else "unknown_subbranch")
    elif command_exit in (0, 6):
        exact = (terminal and terminal["identity"] == expected_identity and terminal["job"] == job
                 and terminal["nonce"] == nonce and terminal["operation"] == operation)
        if exact and ((command_exit == 0) == (terminal["status"] in (1, 6, 8))):
            result.update(classification="exact_terminal", status="terminal_capture", reason=None,
                          request_submitted=True, native_assignment_requested=operation != "observe", terminal=terminal)
        else:
            result["reason"] = "terminal_binding_or_exit_mismatch" if terminal else evidence["reason"]
    else:
        result["reason"] = "unclassified_command_exit"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    names = ("control", "request", "receipt", "admission", "preflight")
    sizes = (1344, 1264, 1272, 1200, PREFLIGHT_SIZE)
    for name in names:
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    blobs, errors = [], []
    for name, size in zip(names, sizes):
        try:
            with getattr(args, name).open("rb") as stream:
                blobs.append(stream.read(size + 1))
        except OSError as error:
            blobs.append(None)
            errors.append(f"{name}: {error}")
    result = inspect(*blobs)
    if errors:
        result["read_errors"] = errors
    print(json.dumps(result, indent=2))
    return 0 if result["state"] == "KNOWN" else 2


if __name__ == "__main__":
    raise SystemExit(main())
