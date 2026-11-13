"""Decode saved provisioning v1 evidence; no process, input or request writes."""
import argparse
import json
from pathlib import Path
import struct


def _require(okay, message):
    if not okay:
        raise ValueError(message)


def _header(data, size, magic):
    _require(len(data) == size, f"expected {size} bytes, got {len(data)}")
    _require(struct.unpack_from("<3I", data) == (magic, 1, size), "unsupported wire header")


def _identity(blob):
    _require(len(blob) == 1080, "process identity wire size")
    pid, reserved, creation, session = struct.unpack_from("<IIQQ", blob)
    units = struct.unpack("<512H", blob[56:])
    _require(0 in units, "unterminated process path")
    end = units.index(0)
    _require(pid and not reserved and creation and session == creation and end
             and not any(units[end:]), "process identity padding/session")
    return dict(pid=pid, creation=creation, session=session,
                executable=blob[56:56 + end * 2].decode("utf-16-le"),
                executable_sha256=blob[24:56].hex())


def decode_receipt(blob, expected_identity, job, nonce, operation):
    """Validate a saved successful Observe/Equip receipt against its exact request.

    ReloadStateOwner omits target weapon data and the returned server item, so
    equal previous/current tuples are valid. Production verifies both target
    data and server item before recording client/server convergence. This saved
    receipt does not authorize a retry or establish later gameplay readiness.
    """
    _header(blob, 1272, 0x31524342)
    status = struct.unpack_from("<I", blob, 12)[0]
    process = _identity(blob[16:1096])
    _require(process == expected_identity, "receipt process identity differs")
    got_nonce, got_job, preflight, retirements, dispatch = struct.unpack_from("<5Q", blob, 1096)
    observed, dispatched = struct.unpack_from("<2q", blob, 1136)
    previous = struct.unpack_from("<4I3Q", blob, 1152)
    current = struct.unpack_from("<4I3Q", blob, 1192)
    (phase, calls, returned, thread, boundary, server, converged,
     excluded, retired, reserved) = struct.unpack_from("<10I", blob, 1232)
    _require(nonce and job and (got_nonce, got_job) == (nonce, job) and preflight >= 8
             and observed > 0 and not boundary and not excluded and not reserved,
             "receipt nonce/job/preflight/boundary/exclusion differs")
    if operation == "observe":
        _require(status == 1 and phase == 0 and calls == 0 and retirements == 0 and retired == 0,
                 "Observe must be Ready/Preflight without any assignment or retirement")
    else:
        _require(operation == "equip" and status == 6 and phase == 3 and calls == 1 and retirements == 1
                 and retired == server == converged == 1 and returned and thread and dispatch
                 and dispatched > 0 and all(current),
                 "Equip lacks one assignment, retirement or client/server convergence")
    return dict(status=status, identity=process, nonce=nonce, job=job, preflight=preflight,
                retirements=retirements, native_calls=calls, dispatch_sequence=dispatch,
                dispatch_ns=dispatched, observed_ns=observed, previous=list(previous),
                current=list(current), returned_item=returned, thread=thread, phase=phase,
                input_excluded=bool(excluded), old_item_retired=bool(retired),
                server_item_observed=bool(server), client_server_converged=bool(converged))


def decode(control, request, receipt, admission):
    """Require the saved terminal quartet to agree; never use a later receipt."""
    _header(control, 1344, 0x31464342)
    _require(struct.unpack_from("<I", control, 12)[0] == 0, "reserved control header")
    _header(request, 1264, 0x31504342)
    _header(receipt, 1272, 0x31524342)
    _header(admission, 1200, 0x31414342)
    _require(control[16:1216] == admission, "embedded admission differs from saved admission")
    _require(request[16:1112] == receipt[16:1112] == admission[16:1112],
             "full process identity or nonce/job mismatch")
    _require(request[1120:1136] == admission[1120:1136], "original issue/deadline mismatch")
    _require(request[12:16] == admission[12:16], "operation mismatch")
    nonce, job = struct.unpack_from("<2Q", admission, 1096)
    accepted, issued, deadline, frozen_ns = struct.unpack_from("<4q", admission, 1112)
    status, scope, frozen, admission_saturation = struct.unpack_from("<4I", admission, 1144)
    _require(nonce and job and issued <= accepted < deadline, "invalid request scope")
    _require(frozen == 1 and status in (1, 6, 8, 9, 10, 11, 12), "saved admission is not terminal")
    _require(status == struct.unpack_from("<I", receipt, 12)[0], "terminal status mismatch")
    observed = struct.unpack_from("<q", receipt, 1136)[0]
    _require(frozen_ns == observed or (status == 12 and frozen_ns == 0), "terminal timestamp mismatch")
    _require(admission_saturation < (1 << 10), "unknown admission saturation bits")
    counts = struct.unpack_from("<2I", control, 1216)
    saturation, reserved = struct.unpack_from("<2I", control, 1224)
    _require(not reserved and saturation < 4, "unsupported detail flags")
    _require(all(count <= 31 for count in counts), "control counter outside saturation bound")
    _require(saturation == sum((1 << index) for index, count in enumerate(counts) if count == 31),
             "control saturation mask mismatch")
    _require(scope or not any(counts), "exhausted scope contains control samples")
    causes = {}
    for index, name in enumerate(("MutexUnavailable", "ActiveCallbacks")):
        raw = control[1232 + index * 56:1288 + index * 56]
        boundary, sequence, thread, caller, seat, firing, depth, reason, callbacks, update, gather, pad = struct.unpack("<qQ10I", raw)
        sample = None
        if counts[index]:
            _require(reason == index + 1 and not pad, "sample reason/reserved mismatch")
            _require(accepted <= boundary < deadline and sequence and thread and depth, "sample outside admitted boundary")
            if index == 0:
                _require(not (callbacks or update or gather), "unavailable mutex exposes unsupported callback counts")
            else:
                _require(callbacks and callbacks == update + gather, "active callback depth mismatch")
            sample = {
                "boundary_ns": boundary, "seat_sequence": sequence, "thread": thread,
                "caller": caller, "seat": seat, "native_firing_depth": firing, "seat_depth": depth,
                "callback_counts_known": index == 1,
                "callbacks": callbacks if index else None,
                "update_depth": update if index else None,
                "gather_depth": gather if index else None,
            }
        else:
            _require(raw == bytes(56), "uncounted sample is nonzero")
        causes[name] = {"count": counts[index], "saturated": counts[index] == 31, "first": sample}
    return {
        "schema": "fvr.provision-control-refusal.v1", "nonce": nonce, "job": job,
        "scope": scope, "status": status, "accepted_ns": accepted, "issued_ns": issued,
        "deadline_ns": deadline, "frozen_ns": frozen_ns, "causes": causes,
        "limits": ["Boundary time precedes source lookup and Control acquisition; no new clock sample.",
                   "MutexUnavailable exposes no holder identity or callback counts; no absent-callback inference.",
                   "ActiveCallbacks owns the recursive mutex: depths belong to this sample's seat thread.",
                   "Two first samples and saturating counts are not a full callback trace or native success proof."],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("control", "request", "receipt", "admission"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    try:
        result = decode(*(getattr(args, name).read_bytes() for name in ("control", "request", "receipt", "admission")))
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
