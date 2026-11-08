"""Decode saved Control-refusal v1 evidence; no process, input or request writes."""
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
