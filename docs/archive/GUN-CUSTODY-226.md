# Atomic gun custody candidate

`HandInteraction::TransferGunCustody(currentSafety, transfer)` now supports exchanging gun custody between hands as one serialized arbiter operation. Existing Acquire/Transfer/Renew behavior and the shared PhysicalWeaponCycle implementation are unchanged. This is a shared-layer candidate; no game adapter, rig, profile or installed file was changed.

The request carries the exact current GunHold token, an optional exact current other-hand WeaponSupport/Mechanism token, a required destination GunHold request and its original evidence packet, and an optional destination WeaponSupport/Mechanism request with its own evidence packet. Each contact is validated against recorded history, current focus/tracking/release state, release/invalidation barriers and its original deadline. The two original packets may differ. Both per-hand intents are consumed on a failed attempted exchange, preventing held-intent retries after contention resolves.

`result.transaction` uses the existing HandInteractionResult shape. Its claim is the new GunHold; `result.companion` is the optional new support/mechanism claim. Both superseded tokens produce Transferred release receipts. New companion requests set prerequisiteClaim to zero because the transaction allocates the new parent token only after complete validation. The new companion's returned token depends on that new parent. Both target contacts must be distinct and belong to the same exact owner/item. Other claim kinds cannot participate or be displaced.

Once validation completes, the arbiter replaces both claims without invoking dependency invalidation between removal and reparenting. A failed request keeps both valid current claims intact; authoritative lifecycle invalidation still applies. Parent release/expiry invalidates its new dependent. When the old gun hand becomes empty, an invalidation barrier prevents geometry from before that explicit custody release being reused to acquire the mechanism.

## Physical sequence and adapter requirements

The intended right-hand bolt sequence is:

1. While RightGun and LeftSupport are still valid, consume an explicit handoff intent and fresh left gun-custody contact to atomically obtain LeftGun and RightEmpty. This requires the adapter to request custody before its ordinary Update processes the real right grip release. The transaction cannot resurrect claims already dropped by release, tracking loss, expiry or a selection change.
2. Observe the actual right release/neutral interval while retaining and renewing LeftGun. The right hand is genuinely unclaimed during travel. A later fresh mechanism contact and acquisition intent can use existing AcquireFrom to obtain RightMechanism dependent on LeftGun. The mechanism consumer still owns neutral/press/stroke sequencing.
3. After cycling, release RightMechanism while LeftGun persists. On fresh right gun contact/regrip, atomically exchange LeftGun + RightEmpty for RightGun + LeftSupport. An immediate two-claim exchange is also supported when a caller has valid contact and gesture authorization for both targets; the transaction itself does not invent any grip edge.

The unit replay tests this full empty-hand sequence, plus direct pair exchange in both handedness orientations. It does not reinterpret a held right grip as a physical bolt regrasp.

Current BC2 ObserveHandOwnership still reconstructs right-gun ownership, and current rig placement follows the right wrist. Runtime right-hand bolt operation therefore still needs an explicit adapter custody state, left-hand gun placement while the right hand is free, contact-derived wrist/controller alignment and parameterized mechanism/gun hand assignment in WeaponCycle/PhysicalWeaponCycle. Existing left-mechanism/right-gun runtime tests do not establish these ergonomics. Pump agent owns current physical-cycle work; this payload does not edit those headers.

## Validation

MSVC x86 and x64 builds pass with C++20 `/W4 /WX /permissive-`. Each runs the complete canonical HandInteractionTests and nine new HandGunCustodyTests groups: both orientations and return, actual release/travel/regrip, separate immutable historical packets/deadlines, failed requests/consumed intents, stale tokens/unrelated occupants, lifecycle loss, release/tracking barriers, evicted/forged evidence, and 512 consecutive exchanges. All tests pass.

Run `Build.ps1 -Architecture x86` and `-Architecture x64` from this candidate. Include its `include` directory before the canonical repo include. The only existing file replacement is `include/fvr/interaction/HandInteraction.h`; baseline SHA-256 is `e379d3340f3a81e24303debdd04e0121e2bbd212f1fedbf9d215e010d416f9c7`. Add `tests/HandGunCustodyTests.cpp` to the repo's test registration during integration. No new runtime default or native admission is part of this candidate.
