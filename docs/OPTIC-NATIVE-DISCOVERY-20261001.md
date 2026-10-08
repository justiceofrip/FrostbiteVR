# Native optic observation bindings — October 1

The next optics capture can target a specific native lens-filter function and
separate zoom configuration from its blur settings. This is static discovery,
not a physical-scope implementation. No live process operations, hooks, inputs,
asset changes, or renderer changes were made. Working XM8/ACOG optics, accepted
support grips, launcher mechanics and the removed manual ADS binding are unchanged.

This extends [the optics roadmap](OPTICS-ADS-ROADMAP.md) and
[the saved capture inventory](OPTIC-INVENTORY-20261001.md), rather than replacing
their missing-evidence gates. User reports identify candidate sniper/Carl Gustav
behaviors; no exact affected asset/variant has been classified here.

## New evidence

[The machine-readable report](../reports/optic-native-bindings-20261001.json)
records exact metadata descriptors, registrations, source hashes and test results.
Preferred VAs use image base `0x400000`; code returns RVAs. Executable SHA-256:
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.

| Native data | Verified metadata | What it enables next |
| --- | --- | --- |
| SoldierAimingSimulationData | Size `0x7c`; ZoomLevels `+0x4c` contains ZoomLevelData; ZoomType `+0x48` | Capture the exact item's authored zoom levels without inferring a scope from its display name |
| ZoomLevelData | Size `0x74`; FieldOfView `+0x48`; transition/FOV timings; dispersion/recoil/movement multipliers | Keep optical projection and native gameplay effects separate; FOV units and active-level state still need consumer proof |
| Foreground blur fields in ZoomLevelData | ForegroundBlurFilter `+0x34` is the four-byte **BlurFilter enum**, not a pointer; deviation `+0x38`, view distance `+0x3c` | A specific lead for the ADS blur symptom, not its confirmed cause |
| BlurFilter | None=0; Gaussian3/5/7/9/15/31Pixels=1..6 | Capture typed values; do not globally disable blur or reinterpret the enum as an object |
| ScopeFilterData | Size `0x30`; ScissorRegion `+0x20` | A native filter aperture/scissor lead; coordinate convention still unknown |
| SniperLensScopeFilterData | Derives ScopeFilterData, size `0xd0`; BlurScale `+0xc0`, BlurCenter `+0x60`; chromatic/radial/tint fields | Identify a concrete native lens-filter asset and consumer; it does not prove where a magnified scene or reticle is rendered |
| Existing WeaponStateData capture | ZoomedScopeFilter `+0xac`, NonZoomedScopeFilter `+0xbc`, MeshZoom1p `+0x94`, ZoomMeshTransitionFactor `+0xb0` | Join the actual active weapon state's filter and zoom mesh to the observed pass |

ZoomTypeEnum is separately verified as HoldButtonToZoom=0 and
PressButtonToSwitchZoomLevel=1. Future automatic ADS must respect the item's
native mode rather than repeatedly holding or clicking every optic the same way.
Metadata does not establish a native acknowledgement or cancellation state.

The existing source resolves the current wrapper through the soldier's validated
inventory; wrapper `+4` owns SoldierWeaponData and wrapper `+0x2c` owns an aiming
simulation whose first pointer equals data `+0x9c`. Reuse that identity proof.
Do not reuse historical pointers or assume the ZoomLevels container has the same
layout/stride as the WeaponStateData value array: verify its native accessors.

## Concrete rendering boundary

At `0xbb2b60`, native rendering calls `0xb8d6b0` after checking the filter's
reflection type against SniperLensScopeFilterData and a ScopeFilterEnable flag.
The function is `__thiscall(renderer, filter)` with one stack argument, and
`ret 4` at `0xb8d8b7`. Its first five bytes span incomplete instruction boundaries;
any future hook must use the normal decoder/trampoline, not copy an arbitrary
five-byte prefix. Its one direct call site returns at `0xbb2b65`.

The function reads the argument's BlurScale, BlurCenter and inherited scissor,
then binds renderer-owned resource wrappers and invokes native rendering. The
wrapper values at renderer `+0xd8`, `+0xdc`, `+0xc8`, and other virtual interfaces
are **not yet identified D3D textures, targets or SRVs**. Do not dereference them
as graphics interfaces or claim they contain the desired magnified view.
The caller's settings `+0xbf` agrees with the ScopeFilterEnable descriptor;
turning that flag off globally is not an authorized optic solution.

[Discovery implementation](../src/games/bc2/Bc2OpticDiscovery.cpp) validates unique
function patterns, complete inspected renderer/caller bodies, ABI and argument
consumers, reflection registration/fields, the ZoomLevelData array element and
scope inheritance. Its return value deliberately marks native ADS state, scene
target and reticle as unverified. No runtime caller or hook was added. Before
using a candidate, compare the loaded function/metadata bytes, establish the
native thread/view identity and join the actual filter argument to the exact
active weapon state. A matching executable hash alone is insufficient.

## Next bounded capture

1. Use one freshly identified affected campaign item/mode, plus unchanged XM8 as
   a negative control. Record build, actor/weak generation, item/equip generation,
   attachment/state identity, input generation, native frame, view and eye.
   Preserve ACOGs. No asset-name substring enables a rendering feature.
2. Observe native hip, entering ADS, settled ADS and leaving ADS separately.
   Original Zoom input is intent only. Obtain active-level/transition evidence
   from its native consumer before calling any sample an ADS acknowledgement.
   Current controller override clears physical Zoom by design: use a separately
   authorized native baseline/diagnostic, not a restored left-trigger binding.
3. Snapshot authored zoom fields, current state/filter identities, MeshZoom1p,
   weapon RenderFov/ZoomRenderFov, original camera projection and presentation
   state. Verify enum/scalar types and container accessors before typed reads.
4. In a diagnostic-only original-call wrapper around the verified lens-filter
   boundary, record entry/exit with a thread-local scope ID. The original native
   function runs exactly once. Join draws observed inside it to this scope, and
   retain a bounded before/after window to discover the reticle/HUD outside it.
   Calls absent in a particular optic are a useful result, not proof of no optic.
5. Extend the existing draw inventory only for this bounded capture: remove the
   1920x1080-only filter; keep actual viewport/scissor, all RTV/DSV identities and
   descriptions, SRV/texture bindings, shaders, constants, blend/depth/stencil,
   samplers and explicit draw/sample/drop counts. Do not deduplicate away later
   HUD draws sharing a shader/target tuple. Current saved captures cannot supply
   these missing fields retroactively.
6. Capture a few phase-tagged original/final images and constant/resource
   snapshots through private staging with deferred completion. Do not map live
   resources, stall rendering for readback, replay simulation, or disable global
   HUD/FX. Bound each phase to a small frame window and fixed sample/storage cap;
   export overflow and incomplete work so a partial result cannot look complete.

The resulting evidence should answer whether native ADS magnifies the main view
or supplies a reusable scene target, whether the lens pass causes the observed
blur, where reticle/sight HUD are composed, and which native state selects them.
Filter identity alone cannot answer those questions. The physical-scope renderer
must retain normal HMD/world FOV, selective per-eye aperture/eye relief and native
fire behavior as specified in the roadmap. Additional scene rendering, if needed,
requires its own view/visibility/cache ownership proof.

## Validation and limits

Standalone discovery tests pass on x86 and x64 against the installed executable.
Eleven targeted offline mutations are rejected: render entry, filter argument
load, return ABI, call edge, type operand, FOV/filter field offsets, zoom array
element, type size and scope inheritance. Malformed and wrong-architecture input
also reject. Test entry accepts the executable path as an optional argument;
without it only malformed-input checks run. Root owns CMake registration.

No native renderer callback, GPU optic capture, sniper/Carl Gustav selection,
headset check, magnified target extraction, reticle replacement or visual-effect
suppression occurred. The deferred general LOD report remains separate from the
specific ADS-triggered blur/detail observation.
