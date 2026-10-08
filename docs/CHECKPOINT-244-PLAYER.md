# Checkpoints244–245: shared mechanisms and the next player check

This is an integration checkpoint, not a release or acceptance of every weapon.
The user requested a headset check and checkpoint before further feature work.
Keep sampled pistol rails and native cycle interruption recovery in their frozen
isolated candidates until after that check.

## What changed

- The common magazine resource consumer retains a completed native operation
  across repeated tracking packets until the physical consumer can receive it.
  This fixes a reproduced lost acknowledgement, without repeating ammunition
  transfers. XM8, AEK and F2000 regression sequences cover original return,
  immediate second removal and replacement, with native responses simulated.
- Ordinary bolt startup now receives the real body draw observation. The former
  caller read a by-value diagnostic copy which was never populated, so an
  actually held M95 failed before firing in the input-only test.
- A finite resource input driver exercises the ordinary weapon-family
  dispatcher, persistent magazine/shell consumers and physical pump. It emits
  controller input; it does not manufacture native completion or hand custody.
- Shared native magazine classification can represent reviewed SingleFire
  reload/transfer states. Pistol profiles remain disabled in this composition.
- Development rules now explicitly distinguish temporary render/callback
  restoration from persistent VR magazine custody, ammo transfers, holsters and
  owed mechanism steps. There is no requirement to reset VR state each frame.

## Evidence before the244 real-game checks

SCAR241 completed original-magazine return, discard/replacement, shoulder swap,
SPAS shot/pump/shell loading, immediate support return, rifle return and final
fire. M95 physical242 completed two bolt cycles and hand returns. These were
automated actual-game runs; neither establishes headset acceptance. Each has an
incomplete callback journal which is reported separately from functional results.

M95 ordinary243 failed startup before any shot; the publication correction above
targets that failure. F2000243 completed its first original-magazine return but
lost the second removal acknowledgement on a repeated tracking packet. Its
earlier242 View cancellation was a different failure and is not retroactively
explained by the243 reproduction.

The244 tooling run passed874 tests with one skip. Both244 diagnostic variants
and the player-only build passed230 C++ suites on x86 and x64. Ordinary M95
startup, support acquisition, a shot and hand custody transfer worked in244,
but the right Mechanism claim disappeared during Unlock and the driver timed
out. Native holds continued after that loss. The final native Owner failure is
not proof of the first physical cancellation's cause; that reason was not
recorded. M95 is excluded from the next headset checklist.

The244 resource input diagnostic stopped during initialization, before weapon
actions. Its recording clock needed Defer(true) before CombinedPump(); the
existing record-window regression already requires this order. Checkpoint245
adds that diagnostic-only initialization and passed230 suites on both
architectures. The245 actual combined run and player package are tracked in
the local receipts. Their completion is separate from compilation.

Actual ordinary F2000245 completed original return, discard/chest replacement,
shoulder inventory transitions, SPAS physical/native pump, shell insertion,
immediate support return and the rifle magazine cycle after the holster swap.
All six native magazine operations completed. The overall driver still failed
its final rifle-shot check (failure21); postflight read28/180 rather than the
single-shot expectation. This is not an overall combined pass. The exact cause
of that final assertion and callback-journal completeness remain separate from
the passed mechanism observations. The game remained responsive and detached.

The245 player-only build passed230 C++ suites on both architectures. Its
automated input drivers are disabled. The fresh headset check is accepted as
a focused player test with the above final-shot caveat, not full acceptance.

## Headset check scope

1. Remove and return a partly used rifle magazine, then repeat. Discard one and
   load a chest replacement. Check displayed ammunition and immediate support.
2. Swap through both shoulders and repeat a reload, without restarting the mod.
3. Fire SPAS, move the pump back and forward, fire again, then insert a shell.
4. Check whether chest ammunition stays visible and whether the ammo counter
   agrees with firing and reloading.

M95 is deferred until its ordinary interaction passes. No pistol slide,
belt-fed reload, or full-arsenal acceptance is implied. Current
bolt cycling can retain a stock native timing tail after the physical gesture;
tracking-interruption recovery and its eventual timing improvement are separate
unmerged work. M95 holster/body enrollment and last-round reloading remain open.

## Work preserved outside this checkpoint

- Sampled magazine rails: shared curved-path policy, measured M9/MP443 poses,
  both-architecture consumer tests and extractor regressions; native admission
  remains separate.
- MP443 provisioning and SingleFire exact configuration composition: isolated,
  with no actual pistol acceptance yet.
- Native cycle recovery: scoped native Update context, tracking-loss debt and
  recovery-channel work; default disabled and not live-verified.

Root alone owns BC2 actions. Native process identities, frozen build paths and
latest actual outcomes belong in the local recovery ACTIVE-WORK.json. Do not
reuse historical PIDs or inject a fresh DLL into a previously attached process.
