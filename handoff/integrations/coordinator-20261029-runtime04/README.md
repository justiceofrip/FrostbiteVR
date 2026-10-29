# Coordinator runtime04 — October 29, 2026

Scene06 verified the position-only correction on the actual stock game. The user
reported the whole window visible but still black. Original graphics settings
were retained. The10second trace contains591 exact-PID DXGI rows, all with display
timing, using Hardware Composed: Independent Flip; nine loss samples were zero.
This is desktop presentation evidence, not proof of game pixels or headset speed.

The collector exited0. Aggregate session enumeration then failed with
`0x80071068` (WMI GUID not recognized), preventing the after-window observation.
A later exact-owned-session query returned `0x80300002` (collector set not found);
no collector process remained. Original failures are retained, not rewritten as
successful cleanup. BC2 closed normally with exit0; all three profiles were restored
within the agreed three-minute window. No gameplay input or mod attachment occurred.

The private Present helper now queries only its generated session name and accepts
only success/presence or the SDK-defined not-found result. Other errors preserve
both streams and leave cleanup unconfirmed. Four focused checks, including an
actual read-only query, and independent review pass. Native binaries are unchanged;
prior normal-build checks remain applicable. No new game ran for this correction.

Evidence: `reports/coordinator-20261029-runtime04/{scene06-result,cleanup-query-check}.json`;
original files remain under `reports/coordinator-20261028-runtime02/scene06/`.
The initial premature ObserveWindow refusal is retained in the result; it produced
no window action or trace, and startup completed before the actual observation.

The stock black output is one shared blocker for native task/installation checks.
Do not repeat the same launch. The user has no recent ordinary stock-launch result.
Further desktop work needs a new agreed window and a specific changed comparison.
No headset is available; no OpenXR retry. Private0.1.16 remains staged/verified;
installed0.1.12 and settings are unchanged. No new knife gameplay acceptance.
