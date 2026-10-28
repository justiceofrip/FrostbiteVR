# Manual reload cancellation on a frame hitch — 211

The background combined inventory/reload diagnostic found a reproducible reason
for an apparently random reload interruption. In `inventory-presentation210-01`,
the shoulder sequence and original-magazine return completed. During the second
magazine's travel from the chest, native record 4074 supplied a delta of
0.0596221 seconds, neutral input, multiplier 1 and retained actor identity. The
magazine policy rejected it because its delta limit was 0.05 seconds. The consumer
then lost KeepAlive, cancelled and released the ordinary native reload. Its later
30/183 count therefore cannot count as successful manual insertion. No second
seat or manual completion existed. The strict combined audit remains rejected.

Checkpoint 211 shares a finite, positive, at-most-100 ms simulation-delta check
between the ordinary detachable-magazine and shell-insertion native cycles and
their exact delta override. This changes the accepted elapsed simulation time;
it does not make an old context, hand, owner or controller observation fresh.
The original control deadlines, 50 ms context-read age, three-branch agreement,
input neutrality, native configuration and exact restoration checks still apply.
The narrower one-shot diagnostic, empty-fire and abort admission checks retain
their existing bounds. No native offsets, ammo writes or weapon profiles are added.

Focused tests exercise the recorded 59.6 ms frame and the 100 ms boundary in
both ordinary mechanisms. The first post-hitch publication cannot claim all three
branches held; fresh completed observations must arrive before the positive lease
returns. Expired controller input still cancels. Exact delta restoration tests
check the original function runs once, unrelated context words survive and the
original float bits return; nonfinite, nonpositive and over-limit deltas are
rejected. All three focused suites and all **190 suites on both x86 and x64**
pass. Software-D3D ammo-HUD pixel readback also passes. The frozen private stage
is `headset211-20261008`; Build-Checkpoint selects operation profile 211 and
retains the checkpoint 202 geometry calibration.

The first fresh 211 campaign launch reached a boat. Its on-foot preflight refused
to start the rifle diagnostic. One background window-directed E press had no
effect; a guarded focus attempt detected pointer movement and sent no input.
The user then left the boat. These setup attempts are not reload-test results.
Automatic Continue is verified as a campaign start, not a guarantee of a specific
seat, weapon or player state.

Full-magazine discard/accounting, AEK native acceptance, chest-prop continuity
and headset HUD/support-grip acceptance remain open. The shared fix does not
establish all-weapon coverage or a player release. No GitHub update is included.

## Corrected native result

`inventory-presentation211-01` completed all 17 combined stages with persistent
consumers: shoulder swaps, return of the original partly loaded magazine, and
replacement from the chest. It recorded one real replacement seat/completion,
one original return, zero cancellations and no pending operation at shutdown.
All 240 stereo pairs arrived with zero transport timeouts. Counts changed from
22/191 to 30/183, conserving 213 rounds, with an exact native server transfer
and coherent late client/server readers. Mapped executable bytes matched the
tested DLL; native hooks and view state were restored.

The first postflight crossed an ownership read boundary and was rejected with
no accepted samples. The original audit remains inconclusive. A separately
recorded successful read-only follow-up, with the same process creation identity,
passed the unchanged strict reload/transport checks. The explicit follow-up
audit verifies the original evidence hashes and preserves both the rejected
read and its exit code. This is a bounded combined sequence with a settled
postflight, not an assertion that every read in the original harness passed.

No over-50 ms reload delta occurred in the corrected native run. The reproduced
hitch and limit behavior are covered deterministically; native long-frame
acceptance remains unobserved. BC2 is left open after this finite monitor run;
a fresh game process is required before attaching another mod session.
