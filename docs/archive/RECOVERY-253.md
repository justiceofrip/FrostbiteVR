# Recovery253: ordinary repeated bolt-cycle diagnostic

Checkpoint252's F2000/SPAS and SCAR/SPAS combined results remain preserved.
The separate ordinary-input M95 trial completed its first full bolt cycle and
returned gun/support custody. It fired the second shot and transferred the gun
to the left hand, but timed out waiting for the second mechanism grip. Native
holds continued until that timeout; the final Owner cancellation occurred later
and is not evidence that ownership loss caused the missed grip.

Both shots consumed exactly two rounds (5/45 to 3/45), with unchanged native
owner identity. The mod detached and BC2 remained responsive. This is a failed
two-cycle acceptance run, not a bolt-ready result. Evidence is local at
`pipeline-runtime-20261005/root-monitor/m95-ordinary252-01`; trace SHA256:
`006978f175bd71344799ac35a692ed988d4001c39e39bbaea7fc0e1a6d6ab436`.

The existing ordinary player regression stopped after the second custody
exchange. It now continues through neutral input and the delayed second grip,
using measured closed-handle geometry. That offline test passes without a
production fix, so it has not reproduced the actual failure.

The253 candidate adds bounded physical-transition observations to the existing
input-only bolt driver: mechanism phase/failure, custody, current/original input
sequence, raw-contact validity/distance, and whether the mechanism claim exists.
It changes no native authority, motion thresholds, ammunition or gesture rules.
All240 suites passed on both architectures (x86 126.73s, x64 34.92s).
The actual253 run encountered a different earlier failure: clientB restored
its just-predicted 4-round post-shot state to the original 5-round idle snapshot,
then replayed 5->4 before the server independently fired 5->4. The service
cancelled at the Restore before reaching the physical bolt gesture. The first
failure journal preserves invocation1048. This failed run neither reproduces
nor resolves252's second-grip timeout.

Local evidence: `pipeline-runtime-20261005/root-monitor/m95-ordinary253-01`,
trace `7aa21a15c626003c380a23155cb5e54a0c787deae71bcdbcb5786ce82f9fc895`.

Checkpoint reload/death crashing remains a separate unresolved investigation.
No new headset acceptance or GitHub publication is claimed.
