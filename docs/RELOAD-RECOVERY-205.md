# Persistent reload recovery diagnostic — 205

The controller-motion diagnostic previously assumed lifetime counters started
at zero. Starting a second original-magazine-return script on an already used
consumer reproduced a false failure: the driver read the previous completion
as a completion of its new attempt, before issuing any reload gesture.

Each diagnostic episode now captures an immutable counter baseline from an
attached, idle consumer. It measures its own work against that baseline while
checking that lifetime counters never decrease. Reports preserve both
`counter_baseline` and `counter_latest`; the existing acquired, submitted,
completed and original-return fields describe the episode. A busy or retiring
consumer cannot establish a new baseline. Neither the driver nor a new episode
resets the real consumer, hand arbiter, ammunition, native-cycle IDs or receipts.

This is a diagnostic change, not a new player reload behavior or a new native
capability. The operation header is regenerated for the reviewed diagnostic
source changes in `profiles/checkpoint205`; the native hook ABI, ammunition
authority, admission rules and restoration code are unchanged. Historical 204
source and native acceptance evidence remain separate.

## Offline coverage

The extended loop runs the real controller action policy, shared hand arbiter,
tracked rig, magazine consumer and chest-supply policy. The fixture supplies
**mock native observations, acknowledgements and renderer copy receipts**.
It does not emulate the entire game, OpenXR, the network or headset optics.

- Thirty interruption/recovery cases: pulling the original magazine, carrying
  it, an empty well, holding a replacement, guiding it, and waiting for its native
  acknowledgement; each interrupted by loss of the left controller, right
  controller, head tracking, focus, or fresh input.
- Each case drains the old operation, returns an original magazine, then loads
  a chest replacement through the same consumer. The mock cancelled pending
  transfer does not apply ammo; the subsequent acknowledged replacement does.
- Three successive original-magazine returns followed by a replacement, plus
  four successive replacements with explicit mock shot-count changes between
  episodes. Lifetime operation/request IDs increase and ammo is conserved
  after accounting for those simulated shots.
- New scripts reject busy consumers and rolled-back counters. Old completion
  receipts and old renderer counts cannot complete a new episode.

Both full architecture builds pass all 186 C++ suites (x86: 72.95 seconds;
x64: 28.47 seconds). No new game or headset acceptance is implied by these
tests. The user's separate unmodified-BC2 launch-switch tests have exclusive use
of the live game during this work.

The initial x86 link encountered a locked old `BC2NativeProbe.dll`. A separate
`build/x86-checkpoint205` build avoids closing or modifying that session.
`build/x86` contains the earlier mapped DLL and must not be mistaken for this
checkpoint. Both build scripts now accept an optional `-BuildDirectory`.
The protected source closure is
`66d97c8f558230daa2a63f43ed30b0028f79e41d732f9db9a00fa0f7191d4105`.

## Next native check

Keep the existing 204 live combined sequence as the baseline. A new explicit
bounded input script should interrupt tracking during a reload, observe real
native retirement, then perform normal shoulder changes and a fresh chest
reload. Do not reset a consumer or manufacture an equipment change to make the
script advance. Normal holster requests are intentionally blocked while a reload
owns equipment; a live test must respect that policy. The existing 203 CPU
cross-profile tests remain separate from native profile-transition acceptance.

No interruption scenario has been newly wired into the live launcher in 205.
The new repeated-episode support and offline coverage are prerequisites for
that integration, not a claim that it is already running in BC2.
