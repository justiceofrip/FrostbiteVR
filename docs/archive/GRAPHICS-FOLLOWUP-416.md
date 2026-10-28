# Desktop mirror, boat reticle and culling follow-up — 2026-10-17

## Desktop presentation candidate

Native desktop composition previously remained untouched after eye capture.
The new display-only mirror retains one completed left-eye image after the
existing stereo pair finishes and native temporary state is restored. At the
owned Present boundary it copies that image into the desktop buffer **after**
the native menu surface has been captured. Known native menus pass through.
Unknown UI state and incomplete pairs retain the previous complete image; a
new owner/device/reference space starts with a neutral dark image. No extra
simulation or scene render, CPU pixel readback, GPU fence wait or flush is added.
The mirror owns one eye-sized GPU texture and never retains swapchain buffers.
Different desktop sizes use centered copy/crop with initialized borders.

This is the native left eye, before host-only body props, ammo HUD and comfort
composition. It is not a full spectator renderer. Copy bandwidth is nonzero;
unchanged headset performance and freedom from visible desktop flicker require
an actual run. Software-GPU tests cover retained content, isolation from native
eye reuse, unchanged pipeline bindings, menu/unknown/test/owner/space behavior,
and resize/crop. Do not label this a photosensitivity guarantee.

## PBLB driver grenade-launcher reticle candidate

The old reticle reader and pair producer were never connected to presentation.
The existing mounted shot observer enrolled campaign profiles but not the
separate accepted PBLB driver profile. The new PBLB-specific runtime reads the
actual configured native ray and requires the existing original-once Shoot
callback to witness that exact seat/component/mounted/effects/firing/config
instance before it grants a display sample. It rereads local seat/camera/gun
ownership and the ray before and after the callback. **The first verified shot
enables the reticle for that source lifetime.** No synthetic center/HMD ray or
ballistic-impact claim is substituted.

The ordinary observation-only producer's compile-time admission remains false;
only the separate witnessed runtime can issue `NativeSightLine`. Focus, seat,
owner, source and reference changes revoke that witness. Sources with unsupported
camera-relative targeting, inherited velocity, extra transforms or aiming
adjustment remain rejected. Source-stage diagnostics distinguish those gaps.
The immutable body-prop pair preserves one native ray across both eyes. A small
synthetic crosshair is drawn after scene-depth composition as an aiming overlay,
so terrain closer than its display plane does not hide it. There is no new IPC
layout or separate render session.

Native acceptance check: enter the known SP_02 PBLB driver seat, aim across several
yaw/pitch angles, fire once, then compare the glyph with native shot response.
Require `boat_reticle.source_binding=1`, nonzero observations and shot witnesses,
source stage 7, and both-eye glyph presentation. Exit/focus/recenter must hide or
reacquire the source without retaining the previous seat. A missing glyph with
no witness is not a rendering success. CPU and WARP cases cannot establish live
mounted-source acceptance or ballistic accuracy.

## Disappearing boulder / LOD report: read-only audit

`Bc2TrackedViews.h` already constructs one conservative envelope enclosing both
eye frusta through `EncloseStereoFrusta`, and gives each eye its own projection
plus that shared culling copy. `ApplyTrackedView` runs at the native visibility
boundary after the engine's far/LOD edits; publication also requires both native
visibility passes. `BuildCullingViewCopy` explicitly rejects envelopes beyond
the measured native half-FOV clamp instead of silently narrowing them.

The additional targeted source/disassembly review found no concrete mismatch
in those consumed camera paths. `ApplyTrackedView` writes the primary and
secondary copies after the native per-view far/LOD edits. `FinishTrackedFrame`
verifies and restores the original main-view copies and its separate `+0x30`
anchor before publishing a completed pair. This restoration check does not
establish the correctness of every downstream visibility or temporal cache.

Existing evidence in `bc2vr-recovery/overnight414/placement03` captured native
frame 9305 with camera mask 63: applied, before-draw and after-draw samples for
both eyes. Both secondary cameras retain the same culling position
`[647.14575, 119.59930, -110.02425]`, FOV and projection through both draws.
Context correction recorded 480 scopes with zero patch or restore failures.
This is evidence from that captured frame, not a reproduction of the reported
boulder disappearance.

The third and fourth RenderView blocks (`view+0xc70` and `view+0x10d0`) remain
constructor identity values with dirty bits 15 in both eyes at all three
captured stages. In the inspected native visibility routine, the calls at
`0x00bae19d` and `0x00bae1be` select the primary and secondary getters; no
third/fourth getter consumption was identified in that path. Their wider
semantics remain unclassified. These observations do not justify overwriting
those blocks as an attempted culling fix.

The child origin is a verified consumer, rather than an unused correction:
native instructions at `0x00bae218` through `0x00bae22f` obtain the parent's
separate `+0x30` anchor and pass it to child zero's setter. The preceding calls
copy the primary and secondary cameras into that child. The current per-eye
anchor correction therefore reaches this path. Recorded child and render-context
identities are distinct between eyes. Native addresses here are preferred-base
addresses in the inspected BC2 executable.

Per-object temporal occlusion identity, mesh streaming and shared LOD decisions
remain unresolved. A shared helper at `world+0x11c` is passed into native
visibility-job data, but the followed operations reset/flush a command
allocator; its identity has not been established as an occlusion cache. Do not
label that shared state a cache bug without its actual consumer contract.

The smallest useful next experiment is one stationary position at the reported
boulder, with four small head-yaw samples: neutral, left, right, neutral. Record
parent/child primary and secondary camera fields, `+0x30` anchors, LOD reference
and dirty fields, child/context identities and visibility item counts around
gather and draw. Retain at most two paired images (under 34 MB of raw pixels at
1920x1080 RGBA), and compare a flat baseline at the same object and view. This
can distinguish geometry selection from later drawing without a campaign-long
capture. There is no evidence yet that the boulder is a stock-game bug, and no
boulder fix is claimed. This checkpoint does not disable occlusion, force LODs,
expand far distances or alter the stereo camera to conceal it.

## Opening-cutscene sky report, 2026-10-17

During the 416 headset session, the user initially described the opening-cutscene
sky as doubled. The later clarification identified the flat cloud overhead in
the stock cutscene, not necessarily duplicate rendering of the complete sky.
The user started a new campaign in the same game process; game and host identities
remained those of the current 416 session. This observation is separate from the
SPAS interaction lockout and disappearing boulder; its rendering cause is unknown.

There is no established sky-specific stereo adapter or validated sky-pass
consumer in the current source. Extra projection retargeting recognizes
verified mesh/terrain callers; that does not establish how a sky pass uses a
view-relative or infinite-distance camera. Temporal resources and other camera
blocks remain unclassified. Neither explanation is a demonstrated cause.

If the clarified cloud symptom recurs, a discriminating capture is one native stereo pair
(16,588,800 raw bytes for two 1920x1080 RGBA eyes). Inspect each eye separately:
a single sky in each eye with binocular misalignment points to camera/constants;
duplication inside one eye points to native draw/history/blending; clean native
eyes require examining later presentation. Existing first-frame diagnostics
and deduplicated pass inventory cannot distinguish these. No capture was made
or rendering changed while the user continued the run.

## Death/restart distortion report, checkpoint 417 audit

In the same 416 session, the user subsequently reported stretched textures and
menu flicker after dying and reloading. The user finished and authorized ending
the mod, but the game had already exited at 13:48:39 UTC with `0xCFFFFFFF` when
the stop was attempted. The exit cause is unknown. The host shut down gracefully
and SteamVR remained open. Retained first-failure/live/host logs remain available;
the final `native-trace.json` was zero bytes, so it cannot supply final
camera/palette comparisons or native clean-detachment evidence.

**No exact camera-restoration loss was observed in this run.** The last retained
live telemetry at 763,907 ms recorded `camera_restore_failures=0` and
`gpu_failure_stage=0`. Stereo progress recorded four completed owner recoveries
with failure zero. These observations supersede the earlier audit suggestion
that an `exactRestoreLost` event explained this run. They do not prove every
frame or the final unreported interval was correct.

The source does reject further tracked rendering/rebinding after an actual
exact-camera restoration failure (`FinishTrackedFrame`, `RecoverStereoOwner`
and `RenderOwnerRecovery`). That is a conditional code path only; it is not
evidence of this session's cause. Do not reset restoration counters or weaken
owner checks in response to the visual report.

The triggering predicate remains unknown. A useful next observation must join
the first visible distortion to the current scene/actor and native eye output,
then distinguish camera/projection errors, deformed geometry/palettes and menu
surface/presentation errors. Four completed recoveries and zero sampled errors
are insufficient to select one of those causes. The knife feature's disabled
configuration likewise supplies no demonstrated causal link. This note adds
no rendering change, capture or repeated test.
