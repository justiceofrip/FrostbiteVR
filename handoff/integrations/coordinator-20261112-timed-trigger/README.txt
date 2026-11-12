Shared simulator-timed trigger — November12 2026

Normal83/06 reached fresh sight contact and exact underbarrel selection using the
integrated neutral-motion backend. Invalid observation018 leaves interval coverage
incomplete; fresh019 proves selected target. The preparation input fired once on
all three native copies,1/7->0/7, but press-to-release publication took100.1499ms.
Its refused result is preserved. The game closed normally and restored settings.

The common shot helper now uses existing `trigger r pull 50` on the exact verified
xr-sim binary. Fixed preparation and post-reload firing use the same helper.
Unknown runtimes retain numeric DOWN/UP. One-attempt/source/native gates remain.
DOWN admission/ACK retains100ms; natural-neutral observation uses the original
separate250ms release-observation phase anchored to settled DOWN, capped by the
unchanged driver deadline. Native Fire events still require the original100ms
window. Configured50ms is not physical-duration proof; receipts say so explicitly.
Exact neutral controls and ACK+2 frame evidence settle natural timer expiry.
Numeric0 releases a value but cannot cancel its timer: uncertain timer custody
keeps legacy pending=true and prevents later input, including older readers.

965:18 new tests plus8 deadline checks passed; broader regression105pass/1skip
and one stale refusal-message assertion. ROOT corrected only that message
expectation;14 readiness checks pass with all zero-write/deadline checks intact.
969 adds default20Hz and original phase-deadline regressions; see its frozen receipt.

ROOT968 host03 directly observed timed high/natural low, numeric release/drain and
restored state sampling. All scoped checks pass; sampled high-to-low47ms is not
exact physical duration. Diagnostic state hz0 differs from production20Hz.
OVERALL host03 FAILED later on a non-pulse state sharing read; forced host cleanup
and the full failed trace remain. No repeat of the passed pulse scope is needed.
Host01 prior multi-line ACK and host02 transient state-read failures were fixed
with retained replay and bounded read handling; their failures remain preserved.

Receipts: WT/reports/coordinator-20261112-feedback965,966,967,968,969.
ROOT source/push: reports/coordinator-20261108-feedback721/timed-trigger/.
Installed0.1.83/native59dfee1 is unchanged; no native build/install/public release.
Complete representative reload/fire/switch in BC2 remains the next acceptance.
