# Fixed-container physical adapter

Source implementation for the four generated exact configurations: MP412,
M1 Garand base/Assault, and Type 5. Uses the completed `weapons46-special`
geometry and cache records. No new asset investigation, build, game session,
native-success claim, or headset acceptance was performed by this worker.

## Components and integration

- `Bc2PhysicalFixedContainer.{h,cpp}` owns bounded persistent per-weapon
  physical policies and source-object custody. Inject native runtime read,
  shared-ledger callback, request submit/outcome, and gate callbacks. The
  `nextRequest` callback consumes Gameplay's existing shared `handIntent`.
  No second ammunition ledger is created. All policy updates that touch the
  ledger execute inside `WithAmmoResourceLedger`; submission happens after it.
- Each `Tick` receives exact current equipment/configuration/mesh/fingerprint,
  native and hand owners, current input, hand item, body pose/input generation,
  and the renderer's original `Bc2FixedContainerRawContact`. Configure its
  chest contact with `SetBodyContact` alongside other body providers.
- Capture an actual prior left claim immediately before the central hand
  safety update. A real `Released` result can populate `releaseEvidence` with
  `{beforeClaim, releasedEvent, originalSafetyInput}`. A cached token alone
  cannot complete a release. Renderer input timestamps are never refreshed.
- `BuildFixedContainerRawContact` validates the exact live rig fingerprint,
  part names/indices and native owner. It carries the original grip edge and
  pouch contact along with the original input. Call before guiding the wrist.
- Tick returns `tracking`, `blocksFire`, actual hand ownership, and optional
  `bodyAmmo`/`bodyVisual`. Publish the third body family as
  `BodyAmmoTracking{visual, {}, {}, fixed}`. Feed the tracking into the same
  private rig snapshot used to obtain raw contacts on the next render.
- `BuildFixedContainerPresentation` returns ordinary part writes, authored
  left wrist/hand shape, exact native supply bones to conceal, and compound
  cached supply placements. Root composes the writes and hand shape into its
  ordinary immutable rig palette and collapses only `hiddenSupplyBones`.
- `BuildFixedContainerRenderSource(tracking, rig, placed, units, now)` creates
  the immutable source for `BodyPropSources.fixed` (sixth field). It includes
  retained loaded rounds plus the carried two-part loader. The actual body
  pair path appends complete assemblies, rejects overflow beyond eight props,
  preserves original clocks/poses, and intersects both eyes by exact identity.
  Ammo wire ordinals 1/2 are loaded parts; 3/4 are carried parts. Pouch stays 0.
- `ExpandBodyAmmoRenderSource` and `BodyAmmoHostInstances` expand the pouch's
  root into both authored parts. Direct renderer also requires the complete
  cache assembly before it draws. Existing magazine/shell branches remain.

## Behavior and invariants

MP412 open/eject/close use the generated hinge contact targets. Every coupled
part follows the same recognizer delta from its own authored start; the eject
flick returns to the start rather than staying at the outward peak. Open,
eject and close never remove live ammunition. Its speedloader metal is drawn
only while carried, while existing loaded rounds retain their own geometry.

Garand/Type 5 use the explicitly authored left grasp and complete clip12 plus
ammunition13 assembly. All inserts request one bulk transfer bounded by actual
remaining capacity and reserve (six/eight), with an exact prepared command and
original source deadline. Only the service's exact receipt resolves consumption.
Rejected channel submissions cancel only the same still-queued command. Unknown
native work remains pending. Native fire is interlocked before gestures begin;
initial closed enrollment requires an actual exact-rig raw contact.

Tracking loss drops hand authority and guiding targets while preserving the
mechanical state and pending operation. Same-resource context migration retains
physical debt; no current counts or old pose can stand in for a native receipt.

## Verification

`tests/Bc2PhysicalFixedContainerTests.cpp` covers exact selectors and compound
source metadata, native gate refusal, full MP412 opening/flick-return/closing
without ammo mutation, and all three clip configurations queuing one shared
bulk command without a fabricated receipt. Root owns compilation/execution and
the normal x86/x64 checks. Treat these as unrun until root records results.
