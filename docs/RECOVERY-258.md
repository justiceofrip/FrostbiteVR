# Recovery258: persistent pump/bolt selection through a missing native read

ReadNativeCycleView is a nonblocking poll. Its empty result does not mean the
native service changed selected weapon. Gameplay previously reset its selection
flag from that result each frame; an unfinished physical gesture then prevented
reselection and emitted default bolt tracking. This can remove the attachment
for a frame and produce a geometry discontinuity at the next publication.

A separate identity-only tracker now retains the last accepted native mode and
exact owner across missing polls. A concrete selection change or absent selected
mode in an actual view replaces that identity. Different weapon, equip epoch or
tracked space never matches the retained selection. Native leases, readiness,
ammo counts, hand claims and fire admission still use their original checks.
Both ordinary pump and bolt selection consume this shared policy.

The regression models a held SPAS or M95 gesture with unavailable polls. It fails
with the old gameplay predicate and passes with the correction on x86 and x64,
alongside the physical pump/bolt and native recovery suites. It verifies that
the original held deadline, blocked fire and lack of readiness remain unchanged.
New bounded diagnostics count retained read gaps and distinguish subsequent
preparation skips by selection/body/eye/clock/player, including first owned input.

Source SHA-256: `51fde451a0fa3a491be9e1e865706596a305db2a7b9f32450b38c5cfb11ea92d`.
Full builds and actual repeated-cycle confirmation are pending. The257 trace
did not directly record selection availability, so identifying this code defect
does not yet establish it as the cause of input718. No headset acceptance.

Both full builds pass240/240 (x86133.28s,x6434.41s). Frozen bolt receipt:
`ordinary-bolt258-input-private/build-receipt.json`.

Actual258-01 passes both cycles, correct5/45->3/45 ammunition, real controller
exchange/grip edges and all four mechanism phases in paired renderer output.
Trace SHA-256 `7a5bd4a8bf57494dfcbebd5528d1f86dd84f946334501ebb38d2c56251a8759f`.
817 pairs, no palette fallback/reach rejection/frame expiry. Four startup player
preparation misses; none while owning gun custody. No selection read gap occurred,
so this run does not exercise the corrected branch or prove the earlier cause.
Eleven callback recorder drops; full callback coverage and headset unverified.
Clean detach, responsive game, fresh repeat pending.

Fresh258-02 also passes2/2: trace SHA-256
`10dbc04b3902f6ff7be1537c9e435b564afa663a2e07a34922070dc29a67f6ad`.
813 stereo pairs; no reach/expiry/fallback rejection; two recorder drops.
Again no selection read gap and no owned preparation skip occurred. These are
two clean repeat sequences, not proof of the intermittent257 root cause.
Combined SCAR/SPAS and normal-player builds are next.

## Combined SCAR/SPAS check

The combined composition passes240 suites per architecture (x8619.69s,
x6412.71s). Its frozen receipt is
`ordinary-resource258-interruption-private/build-receipt.json`.
Actual `resource-scar258-01` passes all runtime checks: three magazine cycles,
original return and chest replacement, shoulder round trip, SPAS shot with
support held, partial stroke and500ms tracking interruption, fresh neutral/full
pump recovery, shell7/24->8/23, immediate support return and rifle28/180 after
its final two shots. Independent receipt review verifies all six native command
identities, conservation and each branch's post-release original idle evidence.
Support returns133.3062ms after shell native completion. Trace SHA-256:
`31345a9f3b42357363fb3fe9c8ef8aa2c55b427c6be7447b69e1b39fa5536b5b`.
Eight callback recorder drops; full callback recording/headset remain unverified.
The selection correction remains unexercised by an actual read
gap in this run. The test passed using normal input/hand/native consumers, with
no weapon-specific change between the earlier SCAR/F2000 enrollment and this run.
Clean detach, game responsive and client/server postflight reads coherent.

Normal player build packaging is in progress with synthetic drivers disabled.
Campaign restart crash remains separate and unresolved.

Normal player builds now pass240/240 (x8619.30s,x6413.55s), with both synthetic
input drivers disabled and native cycle recovery enabled. See the frozen receipt
`normal258-player-checkpoint/build-receipt.json` and player checklist
`CHECKPOINT-258-PLAYER.md`. Normal packaging uses the same source hash as the
successful diagnostic compositions; the input-driver compile flags differ.
