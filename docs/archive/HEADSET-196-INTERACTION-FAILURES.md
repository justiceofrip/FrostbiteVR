# October 7: failed headset193 run and interaction196 corrections

The user reported broken rifle reloading, a holstered weapon appearing without
a shoulder grab, and working grenade-launcher interaction. The run is a failed
rifle/holster acceptance test. The mod ended with permission at
2026-10-07T23:35:31.6696077Z; BC2 PID42188 and SteamVR were left open. The probe is
disabled but remains resident, so another injected session requires fresh BC2.

Evidence: `reports/native-trace-20261007-232814-026/native-trace.json` and
`pipeline-runtime-20261005/headset196-investigation/evidence.json` under the
recovery root. The trace has two magazine starts, two cancellations, no
replacement acquisitions, no submissions and no completed magazine reloads.
Sight interaction committed both mode changes with no cancellation.

## What failed

- Magazine cycle1 reached RemovedHeld, then cancelled with PoseJump at input12248.
  Native identity, current ammo source and all three held firing states were valid.
  The shared policy still applied the extraction movement limit to a freely
  carried magazine. The full/zero-reserve original-magazine path had the same rule.
- Cycle2 cancelled in Pulling with PullAbandoned at input13477. Its native hold
  and ownership were also valid. The old trace cannot distinguish a grip release,
  lateral departure or reverse pull. This remains unresolved, not a confirmed
  repeat of the free-carry failure.
- Committed Empty at input8947 became Recovering on input_apply_rejected9292,
  then ShowPending9294 and Held9295 without a Draw request. Input recovery had
  discarded the intent to remain holstered. There were no Next/PreviousWeapon
  commands; the trace does not establish which finger gesture the user made.

## Changes and regression coverage

Extraction position/rotation continuity now applies only before physical removal.
Freely carrying a removed magazine keeps the existing hand claim and native cycle.
Both the ordinary reload consumer and full/zero-reserve original-magazine consumer
use that rule. Original-magazine return and replacement insertion still require
their existing capture, travel, continuity, ownership and native receipt checks.
No weapon-specific offsets or ammo authority changed.

InputApplyRejected now retains only already-committed empty intent. Recovery still
clears old presentation/claims and requires exact current identity, new native
suppression and a new paired visibility receipt. It does not create a Draw.

Bounded cancellation diagnostics now distinguish release, translation step,
rotation step, off-axis pull and reverse pull, retaining measured values and limits.
They do not change the pre-removal rejection policy.

The unchanged carry policy fails the new free-movement regression. The unchanged
holster policy fails the recovery regression on both architectures. Integrated
targeted tests pass on x86/x64, including actual consumer removal, large carry
translation/rotation, discard, pouch acquisition, insertion and native-API-mocked
completion for XM8 and AEK. Full build results are recorded in the investigation
evidence once completed. These are not headset or all-weapon acceptance.

## Why the previous monitor pass was insufficient

The native fixture constrains controller motion to 1cm and 0.05rad per update and
follows a straight extraction/insert path. It therefore did not challenge the
7cm extraction step limit during free carry. A native completion on that path
proved the transfer plumbing for that path, not ordinary human hand movement.

Next work: include free carry and pre-removal cancellation details in the native
monitor exercise, then resolve the remaining PullAbandoned path. Do not promote
this candidate to another combined headset test solely on unit or build success.
