# Private knife timing and prediction evidence

The normal player build remains333. This investigation does not enable a player
knife or alter firearm reloads, grips or carry rules.

The installed knife firing asset specifies `Shot.SpawnDelay = 0.5`. A read-only
live reflection capture independently places that field at primary-function
offset0xd0 (Shot0xa0 + SpawnDelay0x30). The verified native Step state5 loads it
into the phase timer before state6 delivers the attack. The state6 recovery
subtracts that delay from the authored60/52-second cycle.

The private adapter changes only the phase-timer result of those exact owned
transitions. It requires the current actor/inventory, selected KNV-1 on both
sides, exact firing object/configuration, an active motion intent, the original
Update-to-Step caller and its same-thread context. Shared configuration,
ammunition, hit range and original input remain unchanged. The removed windup
is added to recovery, preserving the authored attack interval.

## What the failed check taught us

335 removes the entire windup. Eight isolated original-x86 scheduling cases
pass at30/60/90Hz and100ms deltas. Effects listeners and recoil randomness are
explicit mocks; that emulator does not simulate prediction or native ownership.
The live335 run delivers two server attacks in4–10ms but **zero client effects
callbacks**. All hooks restore and the rifle returns, but the independent audit
marks the run incomplete. Never promote the runner's cleanup success into
knife acceptance.

## Revised native result

336 retains50ms of windup and moves450ms into recovery. Two simulated strokes
produce exactly two owned client and two owned server Shoot calls. Their
measured intent-to-composition delays are16.62/30.59ms on the client and
35.01/48.90ms on the server. Both server recovery transitions retain the full
1.153846-second attack interval. The native simulation consumes part of the
windup within its tick, so measured wall time is not exactly50ms.

Independent matrix checks still verify both directed attacks, unchanged source
matrices and the four exact stack-owned outputs. All three firing copies have
the observed sequence2,5,6,2,5,6,2. The audit includes before/after Update states;
a short state5 can occur inside an Update rather than appear in its final state.

Read-only export/restore hooks capture239 same-pointer, byte-for-byte handoffs
from the secondary client firing copy to the visible predictive copy, including
the shortened windup and extended cooldown for both attacks.479 restore outputs
match their source state/timer. The journals drop3 ordinary firing rows and3
snapshot rows under contention; these are observed sequences, not a lossless
trace. Server-to-client packet identity is **not** established by these matches.

The original rifle is restored with unchanged ammunition. Hooks and verified
native code restore; BC2 remains alive/responding. No desktop input is sent.

Evidence is private under `bc2vr-recovery`:

- `knife335-timing-inspection`: authored/live reflection and original-x86 emulator.
- `knife335-timing01/partial-audit.json`: rejected zero-windup result.
- `knife336-prediction01/timing-audit.json`: revised numerical/lifetime audit.
- Corresponding source/build receipts and helper scripts preserve reproducibility.

336 native trace SHA256:
`ded366fa244f8c3e49cf35c87b7213918703f2ed8fe15d4b3fd28f20a2f49c9a`.

## Remaining work

50ms is a diagnostic choice, not a production guarantee across every hitch and
prediction order. Do not simply copy these hooks into the normal renderer:
Step/Update are already owned by the production reload dispatcher. Integrate
through that dispatcher with an explicit melee transaction and snapshot
provenance, including a defined policy when the simulation delta consumes the
whole retained windup. The diagnostic intent ordinal is not a native shot token.

Player chest custody, tracked mesh/hand placement, cleanup on scene/menu changes
and downstream hit/damage remain open. Knife330's placement proof, this timing
proof and shared318 motion recognition are separate prerequisites. None grants
chest-knife player acceptance or fixes the unrelated intermittent graphics crash.
