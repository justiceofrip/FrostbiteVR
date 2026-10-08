# Actual private-palette reload consumer

This candidate connects the measured shell/left-hand composition to
`Bc2RigPublication::RetargetWeapon` and its existing private GetA/GetB/Pack path.
All source is isolated; no game input, native hook or runtime setting was changed
by this work. Gameplay must explicitly publish the new mode before it can run.

## Caller contract

`Tracking.reload` is a `ReloadTracking` with `enabled`, the complete native
`ReloadStateOwner`, the original current `HandInteractionSample`, and an
optional immutable `shared_ptr<const ReloadPreview>`. Native equip generation
and the physical hand equip generation are deliberately independent. Actor,
actor generation and tracking space must agree exactly; each identity retains
its own equip generation.

`ReloadPreview` owns the actual immutable `SelectedMeshesSnapshot`, current
AmmoObject/GunHold claims, and one explicit phase:

- **Carried:** genuine idle `Bc2AmmoReserveLease`, no native cycle. The renderer
  derives the shell from this publication's raw calibrated wrist and measured
  row58 grasp. It grants no insertion authority.
- **Guided:** genuine current `ReloadRoundLease`, matching held cycle, and
  `Bc2ReloadTargets` produced by the interaction using original raw geometry.
- **Pending:** retains the same real-cycle requirements. If native release is
  underway and all-three-held evidence disappears, the preview drops; it does
  not claim a fictitious held state during completion.

Claims may retain an earlier real contact sequence, provided they are the
current arbiter tokens and retain original unexpired deadlines. Guided geometry
keeps its original input sequence, observed time and deadline. No sequence or
clock is restamped. The physical weapon key uses physical hand equip generation;
mesh/reserve/native round ownership uses native equip generation.

`rigPublication::ReadReloadContact(fullNativeOwner)` returns raw calibrated left
wrist and actual placed weapon worlds in canonical metres, the original input
evidence, exact rig identity/fingerprint, and native shell visibility. It is
published with the same immutable shot/rig pose before support attachment or
reload IK. Use HandHistory to recover its original input for insertion. It is
available with `reload.enabled` even before a preview or native cycle exists.

## Palette and invalidation

The consumer solves only the left arm to the measured shell-grasp wrist, adds
the actual native shell bone and captured finger pose, and preserves existing
weapon/right-hand writes. It composes one final palette against the original
native source. An unreachable wrist cancels the overlay rather than separating
the shell and hand. Native animation bytes are never edited.

A separate atomic reload guard updates even when the pose mutex is occupied.
Pack falls back to the accepted base palette on release, focus/tracking loss,
owner/equip/space/cycle/phase change, token replacement, metadata expiry or the
original target deadline. It also checks the current shot-input generation;
current and previous packed copies retain the same immutable source. Existing
GetA/GetB count, owner and full original-source-byte equality checks still apply.

The exact SPAS profile/mesh/147-bone rig and authored one-entry rigid skin are
bound by the completed 77-frame stereo packed-byte evidence. This authorizes
only the specific owned-bone operation. It does not authorize reticle clipping,
arbitrary draw suppression or arbitrary configured mesh names. Fresh actual
SelectedMeshes1p metadata remains mandatory. No per-frame GPU readback or live
vertex-shader bytecode identity is required for owned palette substitution.

Native-hidden shell leaves still reject the overlay. The report now exposes
`native_visible_contacts`, `native_hidden_contacts`,
`native_hidden_shell_rejections`, per-phase pose counts, rejection reasons and
base fallback count. No uncollapse behavior is introduced.

## Validation and integration

Focused suites: 12 preview cases, 7 composition cases and the 5 existing
presentation cases pass on x86 and x64. The actual native x86 publication source
compiles; only pre-existing local-name shadow warnings remain. Tests include
divergent physical/native equip generations, historical geometry preservation,
claim/native/metadata expiry, renewed-current-versus-expired-old preview,
zero-cycle carried semantics, explicit hidden-shell rejection, units, source
isolation and right-hand preservation.

Add `Bc2ReloadPalette.cpp` and `Bc2ReloadPreview.cpp` to BC2Camera and register
their focused tests. The `Bc2AmmoReserve.h` dependency belongs to the separately
reviewed ammo-supply candidate. Four existing files change: the publication
header/source and presentation header/source. `integration.patch` is generated
against exact staged hashes in `candidate-report.json`.

Full parent builds and the integrated native/VR interaction remain pending.
No headset acceptance or playable manual reload is claimed by these tests.
