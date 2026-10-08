# Exclusive hand-interaction ownership

October 1, 2026. BC2 support grip and launcher sight now use the shared hand
arbiter in [Bc2Gameplay](../src/games/bc2/Bc2Gameplay.cpp). Both architectures
pass **52 x86 / 51 x64 suites**. Native ownership, hand-role and sight-preview checks pass with a recovered
delivery timeout retained in the combined run. This change has no new headset
acceptance and does not enable manual reload or empty hands. See the
[BC2 native validation record](HAND-OWNERSHIP-BC2-20261001.md) for exact evidence.

[HandInteraction.h](../include/fvr/interaction/HandInteraction.h) owns at most one
semantic claim per hand. [HandPose](../include/fvr/interaction/HandPose.h) remains
a visual pose generator: selecting a finger pose does not acquire an interaction
or authorize an item, reload operation, sight change or native input.

## Contract and API

`HandInteraction` is header-only, fixed-size, allocation-free and noncopyable.
It has no native pointers, engine types, rendering, global input or controller
touch requirement. One persistent instance belongs to a serialized actor
interaction domain; tokens are instance-local, not a network authentication
mechanism. Call `Reset` instead of reconstructing it to retain token uniqueness.

| Operation | Result |
| --- | --- |
| `Update(current)` | Apply authoritative lifecycle/focus/tracking/release and freshness changes; return up to two exact released tokens with reasons |
| `Acquire(current, request)` | Acquire a free hand using proof from that same current sample |
| `Transfer(current, oldToken, request)` | Validate the complete replacement before removing the exact old claim |
| `Renew(current, token, proof)` | Refresh the same claim/contact from current input; never change ownership or kind |
| `AcquireFrom(current, original, request)` | Acquire using current safety and an exact recorded original evidence sample |
| `TransferFrom(current, original, oldToken, request)` | Atomically replace the old claim using independently validated current safety and original evidence |
| `RenewFrom(current, original, token, proof)` | Renew from recorded original evidence without moving its sequence backward or extending a duplicate lease |
| `Release(current, token)` | Release the exact full token once; stale or forged identity cannot release its replacement |
| `Reset()` | Release local claims, clear evidence history and require a new input packet; preserve claim/intent high-water marks |
| `Current(hand)` | Inspect the last validated state; this is not a clock source or automatic expiry check |

The adapter calls `Update` on current authoritative input/clock before using
claims, even when no gesture occurred. Stale queued token operations return
without processing an old lifecycle snapshot, so an obsolete release cannot
invalidate newer ownership. Direct `Update` remains responsible for cancelling
on actual clock discontinuity or invalid authoritative input.

All owner fields are mandatory: actor identity, actor generation, equip generation
and reference-space generation. Item and contact keys each contain an opaque
identity and generation. Contact keys identify exact interaction regions; their
generation changes when that binding is replaced. The adapter supplies verified
eligibility. This policy does not infer geometry or validate a native binding.

A full token includes the monotonically increasing claim ID, owner, hand, kind,
item, contact and prerequisite claim. Equality is exact. Reset, tracking loss,
equip changes and successful transfer never reuse IDs; exhaustion rejects
acquisition. Intent serials increase per destination hand for the arbiter's
lifetime. A new intent attempted while current focus/tracking is unavailable is
consumed, so it cannot acquire later merely by retrying with new geometry.
Stale queued samples cannot consume a newer input stream's intent state.

Kinds are GunHold, WeaponSupport, Sight, Mechanism, AmmoObject and BodyInventory.
None and unknown values reject acquisition. There are no implicit priorities or
ownership steals.

## Sharing and atomic transfer

Items are exclusive across hands unless sharing is explicit. WeaponSupport
requires the other hand's exact active GunHold claim ID for the same item.
Sight and Mechanism may own an otherwise unclaimed item or explicitly depend
on its other-hand GunHold. Shared claims use distinct contacts. AmmoObject,
BodyInventory and GunHold cannot masquerade as dependent support.

A failed support-to-sight transfer preserves the original support token while
its lifecycle and lease remain valid. Success returns the old token with reason
Transferred and a new token in one result. Replaying the old release has no
effect. Cross-hand transfer requires a free destination. Simultaneous two-hand
acquisition/swapping is not implemented.

Loss/release/expiry of a GunHold also releases its dependent claim with reason
DependencyLost. An independent ammo-object hand is unaffected by loss of the
other hand's unrelated gun. These cancellations do not send native rollback
operations or extend expired leases.

## Current safety and recorded original evidence

A sample contains its observation time, sequence, original deadline and current
processing time. Default maximum input lifetime is 150 ms. BC2 converts actual
QPC values to monotonic nanoseconds and retains the input lane's deadline.
The first observed sample metadata is immutable across duplicate gathers;
OpenXR predicted time is not substituted for the QPC deadline clock.

Rendering can publish contact from packet N-1 while native gather reads N.
The `From` APIs process N's safety first, then require the supplied original
sample to match the arbiter's bounded 32-entry history. Owner, sequence,
observation time, deadline, focus, tracking and release fields must match.
The original sample's processing-time `nowNs` is not identity. Missing, evicted,
mutated, future or expired evidence rejects the operation. Normal APIs remain
strictly tied to the current sample.

Historical contact proof must match the original sequence and deadline exactly,
remain eligible and still be unexpired at the current clock. Renewals cannot
move a claim's evidence sequence backward. Reusing the same packet cannot
increase its deadline. Geometry is never relabeled as the current packet.

`current.released[hand]` reports a real current release. It cancels/rejects
ownership even when an older contact shows a press. A new current-authorized
intent may use geometry from the last genuinely neutral packet: equality with
that release boundary is allowed only when the recorded source itself reports
release. Evidence before that boundary rejects.

Tracking/focus loss, explicit token release, reset and dependency loss install
a stricter invalidation boundary. Evidence at that sequence also rejects, even
if it was initially recorded as neutral before a duplicate poll reported loss.
This closes same-packet loss/recovery replay. SightFlipPackets retains its
separate press/release pairing rules; allowing neutral geometry does not invent
a sight press.

Duplicate polls may cancel but cannot restore tracking, refresh timestamps or
arm interactions. Gesture policies still own neutral arming and grip thresholds.
Ordinary acquisition requires actual policy intent. The two narrow lease
continuations below retain an already established interaction, under separate
checks; neither turns arbitrary held squeeze into a new grab.

## BC2 integration and lease continuations

BC2's right GunHold represents its already equipped native weapon, not physical
drawing or empty hands. Current native ownership and tracking can restore this
reservation. The left hand exclusively owns WeaponSupport or Sight. BC2 maps
the actor/rig epoch, physical item, inventory relationship and space into claim
identity. The verified rifle/launcher family retains its semantic item across
an expected mode handoff; the sight packet policy and exact native request/ack
checks still reject unexpected equipment changes.

Support and sight contact now come from one immutable published weapon frame.
The adapter retains original input evidence, including generation and deadline.
It evaluates copies of SupportGrip/SightFlip before accepting new ownership.
Only a successful sight claim cancels support. A denied new grab is cancelled
without fabricated neutral, private preview or native dispatch. Support's
existing steering and grasp token remain separate from the ownership token.

[SightOwnershipRecovery](../include/fvr/interaction/SightOwnershipRecovery.h)
only reports eligibility after the exact previous Sight lease expired, or after
DependencyLost caused solely by that sight's exact GunHold prerequisite expiring.
Same owner/item, valid current focus/both hands, no release/cancel and finite
held squeeze are required; other lifecycle causes reject.

The adapter must additionally obtain `committedMode` from a SightFlip trial for
the exact outstanding native request and target. Only then can it acquire a
new strict-current reservation/token/deadline without old-mode geometry. It
does not resend the mode request or revive the old claim. If reservation fails,
the adapter cancels the original pending policy, preserving its request ID for
cleanup, and clears the acknowledgement. Cancelling the already committed trial
would lose that pending identity.

[SupportOwnershipRecovery](../include/fvr/interaction/SupportOwnershipRecovery.h)
only permits attempting a new claim after the exact prior WeaponSupport lease
expired. The proposed SupportGrip must still hold the same existing grasp token,
without a new engage/release or cancellation. The hand must be free, its current
GunHold valid, and the owner/item/contact unchanged. Any old parent release must
be that exact prerequisite's lease expiry, reconciled with the current gun.

Support continuation requires a strictly newer, fresh original publication with
matching proof and valid tracking. It finishes through `AcquireFrom`, so recorded
history and all invalidation barriers remain authoritative. DependencyLost is
explicitly excluded; a native sight acknowledgement is not support geometry.
It never transfers a competing claim, changes the SupportGrip token, alters
pose math or extends an old deadline.

## Validation and scope

The original portable-only checkpoint passed 50 x86 / 49 x64 suites and had no
runtime integration. That historical limit no longer describes this candidate.
The current integrated candidate passes **52 x86 / 51 x64 suites**, including
both recovery suites. Monitor/native validation is underway; use the separate
[BC2 hand-ownership record](HAND-OWNERSHIP-BC2-20261001.md) for final results.

[HandInteractionTests](../tests/HandInteractionTests.cpp) covers contention,
atomic failed/successful transfers, exact/stale release, sharing, owner/equip/
space/focus changes, consumed intents, lease expiry, historical evidence,
neutral boundaries and duplicate loss/recovery.
[SightOwnershipRecoveryTests](../tests/SightOwnershipRecoveryTests.cpp) uses
actual arbiter release records, verifies exact lease causes, rejects wrong/duplicate
acks and checks original-pending cancellation after denied trial commit.
[SupportOwnershipRecoveryTests](../tests/SupportOwnershipRecoveryTests.cpp)
covers lease-only continuation with newer geometry and preservation of the
existing support grasp.

This integration does not grant new headset acceptance for appearance or feel.
It does not enable physical manual reload, ammunition handling, empty hands or
body holsters. ManualReload, FeedMechanism and BodyInventory remain separately
gated consumers. A hand claim neither creates ammunition nor hides a gun,
suppresses firing by itself, commits inventory or confirms native reload.

Preserve accepted SPAS/XM8 grips, free-left animation, recenter and sight
behavior. Native ownership diagnostics and headset checks have distinct roles:
deterministic/lifecycle success alone does not establish visual comfort.
