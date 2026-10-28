# Recovery252: shared pre-hold snapshot progression

Actual251's first shotgun shot was cancelled when a native snapshot moved the
second predicted client from post-shot wait into the pump boundary. Ammunition,
owner, snapshot bytes and native lifetime were unchanged. The existing shared
validator already accepts this transition for the selected boundary; only the
service's M95 restriction prevented SPAS admission.

The service now applies that same validated transition to both supported
nonempty operating classes. It retains each class's exact held state/timer,
the real shot debt and all three independently observed native holds. A restore
does not complete the mechanism or invent a missing Commit. Duplicate restores,
changed counts, foreign owners, open Updates and transitions after interception
starts remain rejected. Empty-cycle admission is unchanged.

The exact251 restore is preserved as a regression fixture with the source trace
hash. The timer comes from the original unchanged snapshot bytes, because the
JSON decimal output rounded it. A service regression failed before the fix and
now passes for both client branches; negative interception tests cover pump
and bolt. Seven focused suites and all240 full suites pass on both architectures
(x86 tests126.98s, x64 tests36.44s). Frozen source SHA256:
`61bca6bbf27b9e33e0d0e8174568bcbf9d8c5618e9371a61f762f7f8045f5e89`.
Actual252 completed the same input-only combined sequence with F2000/SPAS and
SCAR/SPAS in two fresh campaigns. Each passed all runtime completion checks:
original-mag return, discard/chest replacement, shoulder swap, partial pump with
500ms tracking loss, neutral recovery/full stroke, shell insertion, immediate
support, return to the rifle, a third magazine return, and firing. Each recorded
six conserved native magazine operations and a shell transfer7/24->8/23.
Shell support returned135.13ms (F2000) and128.26ms (SCAR) after native completion.

The independent receipt review found at least two fresh post-release native
idle Updates in each of the three copies before recovery acknowledgement.
Recording contention dropped4/2 rows respectively (F2000 recorded486 of487
guarded Updates; SCAR492 of492). All recorded guards restored correctly, but
complete callback coverage is not claimed. Both games detached cleanly and
remained responsive. Headset acceptance is still pending.

Local evidence under `pipeline-runtime-20261005/root-monitor`:

- `resource-f2000252-01`, trace
  `5193f9e8bbbedf3f25394a3277620ca068a525ab3c41206294d625a7556fdd9e`.
- `resource-scar252-01`, trace
  `801c4c3b8a31be42326540069ecd6a433a0789e1d00f4fcdcbda8f6c4970edd3`.

Each directory contains runtime-status, completion, native trace, before/after
reads, receipt-review, controller and scene evidence. Ordinary M95 testing is
next; no new weapon geometry was needed for either combined run.

This includes251's explicit mechanism-only cleanup and durable recovery
diagnostics. No new weapon geometry/profile, headset acceptance or scene-crash
fix is claimed. Published GitHub checkpoint remains245.
