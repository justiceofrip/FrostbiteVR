# Checkpoint353: paired depth and held magazine admission

This work integrates350's depth compositor and fixes two independently
reproduced interaction failures. The combined353 actual-game test passes. Human
acceptance of the controller alignment, shotgun assembly and depth appearance
remains pending. Plain-gray body-prop materials remain unfinished.

Both full normal builds pass all245 C++ suites. Normal353 binaries and matching
symbols are frozen separately from the private input-driver build. The matched
player stage uses IPC6 on both sides; do not mix its host with an earlier probe.
Normal350 remains available as a separate fallback. The local source archive and
staged-file manifest pin every included file; no game assets enter the source ZIP.

## Native gameplay and shared interaction changes

In351, a deliberate shoulder grab coincided with an expired paired visibility
receipt. Rehiding the empty weapon consumed the grab edge, so continuing to hold
that shoulder never drew its weapon.352 retains only that physical contact for
up to600ms while fresh visibility evidence arrives. It retains no old native
receipt or selection authority. Release, moving away, trigger input, tracking
loss, selection changes or timeout cancel it. A connected deterministic test
reproduces the original failure and covers nine cancellation cases.

The corrected352 game run then exposed a separate magazine admission failure.
Returning the original full magazine succeeded. The next removal request was
constructed from input at303069170407300ns with deadline303069270407300ns, but
the native admission point processed it at303069283895600ns. The service
correctly rejected the expired request without dispatch or ammunition changes.
The hand adapter incorrectly cancelled the still-held grab permanently.

353 allows a new removal request from fresh input only after the exact prior
terminal result proves rejection without native dispatch or a receipt. The same
hand claim, gun claim, owner, equipment, counts and held gesture must remain
current, within600ms of the initial attempt. The old request and its deadlines
are unchanged. An uncertain or possibly executed operation cannot be retried.
This shared adapter regression passes across all18 enrolled magazine profiles,
including seven cancellation/corruption cases for each. The test fixture now
uses each profile's authored insertion path, including curved pistol paths.
These are deterministic configuration checks, not18 human weapon acceptances.

`bc2vr-recovery/depth353-native01` completes three magazine cycles, rifle support,
both shoulder swaps, a shot and manual SPAS pump, one inserted shell, immediate
support return, and subsequent rifle fire. Six magazine native commands have
independent server and both-client completion evidence. Final SCAR counts are
29/180; support returns130.122ms after shell completion. All240 eye pairs arrive;
magazine presentation fallbacks and pump-source rejects are zero. Four trace
record-lock drops mean this is not complete callback coverage.

## Depth transport and rendering

IPC version6 attaches184-byte depth metadata to the exact delivered color pair.
Four legacy shared resources contain world and first-person depth for both eyes.
Descriptors, adapter, resource generation, dimensions, sample count and the
complete pair ticket are checked. The existing producer and consumer color-copy
fences cover the added depth copies. After consumption, composition uses local
host copies; a discarded pair retires its shared resources.

BC2 copies its full-size multisampled depth before the second clear of each
tracked eye and again after that eye's draw. The collector verifies the same
resource and clear sequence. It adds no native draw, dispatch or target binding.
Owner/device changes allocate a new color generation so a new scene may restart
its frame serial. Unrecognized depth layouts provide no depth attachment.

The host intersects per-sample world and first-person visibility when composing
body props. Both eye outputs must succeed before originals change. After depth
support is observed, a missing attachment omits that pair's body props instead
of drawing them through the scene. Native color and the separate ammo HUD remain
available. Native materials are not recovered by this change.

Component evidence includes real two-device1/2/4/8-sample transfers, stale-pair,
discard and native-owner restart checks;96 software D24/D32 cases; four color
formats; and rasterized body props hidden separately by world and hand depth.
The connected353 run copies239 depth pairs, composes169 pairs, and reports zero
composition, missing-geometry, invalid-prop or render failures. One warmup pair
has no depth and two body presentations expire. Straight-ahead captures contain
no visible chest geometry and are not visual acceptance evidence.

`depth353-body02` is a separate neutral-controller, look-down test. It delivers32
color pairs,31 valid depth/composed pairs, and no copy/composition failures.
The saved comparison visibly shows a chest magazine in both eyes with the nearer
arm occluding its edge. Visible geometry appears in the early held-weapon frame;
the ordinary startup stow subsequently removes the selected-ammo presentation.
This is bounded live rendering evidence, not a sustained headset test or proof
for every map, menu, vehicle or graphics setting.

The look-down receiver rejects gameplay-driver options: those tests require a
neutral head pose. Its presentation logs include exact per-eye view/projection
and prop-world matrices. The first352 look-down attempt was rejected by that
neutral-pose gameplay guard; it was not a reload result. The first353 body-only
attempt omitted required physical-magazine session mode and returned before
native initialization. Its corrected run is the body02 evidence above.

## Remaining limits

- Plain-gray material textures still need a verified local asset pipeline.
- SCAR controller orientation and fixed SPAS stock/ring/fore-end need human
  visual acceptance; native palette/cycle evidence does not establish comfort.
- The intermittent startup/restart graphics-stack crash remains unresolved.
- Pistol chambering, full belt-fed manipulation, player knife integration and
  broader campaign/weapon coverage remain in the existing roadmap.
- MP-common bundle mounting and REST/debug claims were independently audited
  in [the fork audit](FORK-LOADING-AUDIT-340.md); they are not enabled here.

No new headset test or GitHub publication occurred. Original game settings and
package manifest were preserved. Agent-launched diagnostic games were closed
after detachment; SteamVR and other games were not closed.
