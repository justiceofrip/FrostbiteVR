# Checkpoint 416: headset regression blocks release

**Hold public release and local promotion.** Latest runtime 416/source
`8a294b78` headset feedback: the SPAS initially worked, then holstering and
vehicle entry stopped working. The user authorized ending the session. BC2 had
already exited at 13:48:39 UTC with code `0xCFFFFFFF`; the cause is unknown.
The host shut down gracefully. Retained telemetry and the first-failure record
show a cycle Restore rejection, but final `native-trace.json` is zero bytes, so
the exact rejected callback cannot be reconstructed. See
[failure evidence](headset-failure-20261017.json) and the
[417 investigation](../checkpoint417/README.md).
This failed headset result is distinct from the earlier positive 414 run and
the successful automated menu sequence below.

**Final result:** source `8a294b78`, receipt `b0a2e1bc`, probe `6ab46b2a` passed
286/286 suites on each architecture and the actual `campaign-menu-visible01`
launch/menu/campaign/pause/resume sequence. Trace and receiver exited 0, with
37 world pairs and 13 resumed pairs. Root reviewed both eyes at initial frame
6236 (visible campaign during fade-in) and resumed frame 6896 (fully lit): boat,
gunner, river, dock and cliffs were visible without a loading/menu overlay.
The exact visual reviews are retained separately from numeric pixel checks.
This is not headset acceptance, all-weapon coverage or full-campaign completion.
The subsequent headset run failed; the automated result does not accept its
weapon-handling behavior.

The final private launcher passed ValidateOnly with 104 pins and 226,715 bytes,
without copying binaries. Its promotion gate requires both the combined run and
the exact before-pause/resumed image reviews in both eyes. Earlier candidates
and runtime 414 rollback are preserved; no installed promotion is recorded here.

## Earlier generations and retained evidence

This source checkpoint follows the 2026-10-17 human run of **runtime 414 / source
415**. Initial source `6abac213` passed 285/285 C++ suites on both architectures
and actual BC2 combined reload/pump/holster/rifle and shell-audio diagnostics.
The boat source correction `f5bc3f78` passed 285/285 on x86 and x64, followed by
285/285 on both architectures and WARP acceptance for the final reticle graphics
fix. Actual boat testing now verifies the native ray and visible pixels in both
eyes. Startup cleanup is now integrated in normal source `d2d398ec`, whose
x86/x64 builds passed **286/286** suites each. The full normal-runtime main menu
was visually confirmed three times, but that generation crashed on Resume before
a world pair, with terrain purecall evidence at `0x00A1C212` to `0x00401090`.
The preceding cache-fix source `4dfea3cd` passed 26 focused groups and **286/286 full
suites on both architectures**. Its real transition run transported 37 world
pairs and 14 resumed pairs, with clean hooks and no watched exception. The
retained initial world image is entirely black, so transport success does not
establish a visible campaign. **The overall artifact also remains failed on a
native camera diagnostic; this is not full transition acceptance.**
No 416 headset acceptance is claimed. The installed launcher remains
**0.1.2 / profile 414**.

The previous human 414 feedback was positive on holsters, SPAS pump feel and pump audio
("PERFECT"). Open reports were full-loop shell reload audio, desktop flicker,
missing boat reticle, controller menu opening, absent LMG manual reload and
disappearing scenery. The 412 lockout is older history.

## Implementation and evidence

The front-end route verifies menu/renderer ownership without a campaign world.
Native menu input uses the existing UI callback; world transitions retain normal
view/callback/restoration requirements. Menu-only shutdown cannot stand in for
campaign stereo. The private launcher requests menu startup, with explicit
campaign routing for development. Right-stick weapon scrolling is removed;
holsters, pickup and turning retain their existing roles. The GUI's one-shot
initial activation is tied to a user launch action, not background polling.
Focused launcher checks and private 0.1.3 packaging passed. The preceding private
cache-build menu stage also passed ValidateOnly and 92 pinned-file checks; it
contains 199,478 bytes of helpers/manifests without copying shared binaries,
preserving existing stages and audio provenance.
Its promotion gate requires fresh visible-menu and real campaign-transition
evidence matching the latest probe. Opt-in user launch uses passive window
placement followed by the pinned one-time activation helper with exact
PID/creation/HWND checks. Existing sessions and validation return before that
step; default CLI launch does not activate BC2. That earlier generation remained
unpromoted because its native diagnostic failed. Final-source evidence is above.

The desktop mirror retains a completed native left eye after restoration and
presents it after native UI capture. It neither writes XR sources nor adds a
simulation/world draw; host-only props/HUD are excluded. WARP isolation, retained
frame, pipeline-state and resize/crop checks passed, as did the body-prop WARP
suite. The replacement diagnostic recorded 240 completed-left commits, 3,593
copies, two neutral presentations and zero failures. Counters do not establish
freedom from visible flicker or performance loss. Independent source review
found no actionable mirror defect; this is not new visual or performance evidence.

Shell audio Observe/Replace audits passed every check in actual BC2 on initial
source `6abac213`. Replace
suppressed the matching native insertion/finish starts and completed one cue for
one committed shell; pump playback remained 2/2 in each run. Both sequences also
passed magazine handling, physical pumping, shell insertion, shoulder holstering
and rifle firing, with clean hook/driver cleanup and a responsive game. The
diagnostic changes input generation only, using normal consumers and the same
initial source closure. These retained results are not a native rerun of the
reticle-corrected source. They prove callback binding/playback completion, not
acoustic quality, headset comfort or normal-player acceptance. Replacement is
explicitly enabled in the prepared private 416 profile; installed 414 is unchanged.
Audio is extracted from the owner's local files and never redistributed.

## Visual checks and retained failures

The first normal front-end run passed transport and pointer dispatch: 60
surfaces, 321 input-ready samples, 298 moves, zero world calls and clean hooks.
Its saved image was black, so it was not accepted as visible UI. The later
`campaign-transition-late-pixels01` receiver sampled 40 candidates after surface
60; all RGB values were zero. It received 682 surfaces, withheld Continue and
produced no world pairs. Hooks cleaned up and the fresh game closed. Vanilla
background baselines were also black while another application held foreground.
The subsequent `campaign-transition-reticle02` check used authorized initial
focus: activation succeeded and all temporary input-thread attachments detached.
The stock desktop was already black before injection. With normal `f5bc3f78`
runtime it received 821 images and 40 black late candidates; Continue was withheld
and world pairs stayed zero. The game closed normally. Foreground availability
is no longer the blocker.

The later stock `menu-escape-baseline03` used one guarded Escape and reached
title art, without DLL injection or Continue. The private
`startup-movie-native01` then advanced the disabled EA and Dolby sequence with
two native cleanup calls, no OS input and no movie-file changes. Its hook was
restored and drained, and the game closed normally. Only title art was visible.
`startup-movie-native02` then reached the full main menu after one activation:
MAIN, SINGLEPLAYER, OPTIONS and RESUME GAME were visually reviewed. It made the
same two verified native cleanup calls, recorded 211 positive-time UI updates,
restored/drained cleanly, sent no key/mouse input or Continue, and closed normally.
Those private results preceded the production integration. Earlier black-image
failures remain retained and were not relabeled as successful runs.

Normal source `d2d398ec` integrates the reviewed startup lifecycle. The combined
`campaign-transition-v3-01` attempt stopped before injection because its wrapper
requested 90 seconds from a native trace limited to 60; no runtime conclusion
is drawn from that fixture failure. The corrected `campaign-transition-v3-02`
and `campaign-transition-v3-crash01` reused the same frozen runtime. Both produced
full main-menu images reviewed against separate image witnesses. One native
pointer Down/Up pair on Resume reached Gameplay state, without a Continue DLL
or OS key/mouse input, but delivered zero complete world pairs and then failed.

The second run retained bounded exception-watch JSON, not a dump. The breakpoint
exception is `0x80000003`, with the terrain purecall path at `0x00A1C212` to
`0x00401090`. Native rebuild and per-eye draw/visibility milestones exist, but
the final native report is empty. This does not prove a mod caller or clean hook
shutdown. Both runs remain failed; no playable transition or promotion is
claimed.

`campaign-transition-v3-crash02` reproduced the failure and added bounded
object/cache evidence. The failing entry at cache index 2 points to a RenderView
with abstract/destructor vtable `0x01476BE0` and reference count zero. The terrain
cache held 20 entries against 14 registered views, with its dirty byte set to 1.
The inspected native draw at `0x00A1C1A0` does not check that byte before calling
the borrowed view's virtual slot `+0x18`. This supports an inference of a retired
borrowed view remaining in the terrain cache; it does not yet establish a
complete lifetime explanation or a working fix.

The same run's 100/100 read-only graphics samples, over 26.3 seconds, retained one
renderer/device/swapchain identity and matching cached device/context identities.
Device recreation was not observed during that interval. The normal menu was
again visually confirmed, but the transition still failed. Exception evidence
remains JSON-only, with no dump and no clean-hook claim for that failed generation.
Earlier failed evidence is preserved.

The current correction is implemented in `NativeProbe.cpp` and
`Bc2StereoRecovery.h`, with extensions to the existing recovery tests. It compares
the verified current registered-view list with borrowed terrain-cache pointers
without dereferencing stale cached views, and bounds native refresh to the
reviewed owner/boundary. The 26 focused stereo graph/cache/current-shutdown
generation groups pass, followed by 286/286 full suites on both architectures.
Unchanged audio, weapon, hand and graphics-geometry code does not prove equivalent
runtime behavior when render lifetime handling changes.

The subsequent `campaign-menu-cache01` uses exact receipt `0719eb36` and source
`4dfea3cd`. The native cache refreshed from 20 entries to 14 registered views.
The receiver passed its real Resume, pause-open/pause-close and resumed-world
transport predicates: 37 world pairs, 14 resumed pairs, zero receiver errors, one pointer
Down/Up pair and two acknowledged visibility requests. Native hooks and startup
restoration/drain cleaned up; no exception was observed by the JSON watcher.
However, trace/probe exits are 2/12. Camera-copy culling indexes 4/6 report
maximum distance 0.00195312; the cause and complete failure predicates are under
investigation. Root's decoded initial `world-color1` frame 6126 is entirely
black (`world-left-review.png`). This may be early loading or sentinel-camera
state, but that cause is not proved. No later resumed-world image was validated
by this receiver, so the counts establish transport only. A bounded later-pair
capture and nonblack/visual review are required. The overall validation remains
failed and is not promoted.

The PBLB reticle candidate requires a native Shoot witness and configured gun ray,
freezes one ray for both eyes and revokes it on source/seat/focus/reference changes.
Actual `boat-reticle01` reported source binding verified, but stage 4 rejected
the authored camera flag and inherited speed (`inherit_speed=1`), admitting no
observations and no glyph. A
separate fixture limitation produced 116 of 240 requested pairs in eight seconds
and the receiver did not drain; the native trace exited zero with hooks drained.
The retained read-only bracket verified camera bytes `[0,1,0]`, a null camera
owner, zero inherited velocity and no additional transform; all 15 native code
proof spans were reverified. The corrected reader follows the native null-owner
branch and adds the coherently captured inherited velocity for coefficients zero
or one. Other camera branches, unknown coefficients, missing/changing fields and
nonfinite velocity still reject. It reports the unspread initial launch sight
line, not a gravity/collision/impact prediction. `boat-reticle02` aborted before
native injection because the reduced fixture conflicted with the receiver's
240-pair guard. It is retained as a fixture failure, not a native game failure.

With the corrected private 64-pair receiver, `boat-reticle03` recorded 389 native
ray observations, one shot witness, zero rejected sources and source stage 7.
All 64 pairs completed, native/receiver exits were zero, and 44 fresh matching
both-eye glyph packet pairs were published. The old receiver required scene
depth and composed zero glyphs; the host's retained depth state could similarly
suppress the reticle after foot-to-boat transition. The overall artifact remains
failed, including a postflight guard comparing mutable component fields. No
historical failure was relabeled as a pass.

The separate overlay fix adds an explicit reticle-only missing-depth policy
while retaining world/body depth guards. Numeric analysis of all 44 retained
pairs (88 eyes) found zero covered pixel centers with the original thin glyph.
The final six-file graphics change widens its half-stroke from 0.10 to 0.25.
Production WARP replay of those pairs and fractional pixel-position checks
passed before the final actual-game run.

`boat-reticle04` passed on the exact final normal receipt/probe: 389 native ray
observations, one shot witness, zero source rejects, 64 pairs and 44 fresh
overlay-only compositions. All six retained paired captures showed 8–10
changed white pixels in each eye, matching the projected reticle bars with no
white changes outside them. There were no stale/missing props or composition
failures. Stable-owner pre/postflight checks passed, native and receiver exits
were zero, hooks drained, and the game subsequently closed normally. This is
actual-game paired-pixel acceptance, not headset appearance or impact-ballistics
acceptance. The exact native first-shot witness is still required. No historical
failed run was relabeled.
See [native branch evidence](../../docs/archive/VEHICLE-RETICLE-BRANCHES-416.md).

## Tested source and handoff

The [public validation summary](validation.json) contains sanitized results;
full private evidence stays under `overnight416`. Initial frozen normal builds
and the retained audio/mirror/menu diagnostics used 525 dependency files, SHA-256
`6abac213efc4070e3e07fb6181074bde123eaf1556320a7fc096203a80923993`.
Its reviewed-header digest is
`1511a789f0a019c01bbdf916a3e30f3b397b8dc0f43e845c552ddfbbc7a12fd7`.
Audio diagnostics did not alter that frozen normal probe. The preceding reticle
source closure is
`f5bc3f788d4c21b39dca9f14c28b0901d9e6c2a2a4bb05ac829885e64d8ffa88`,
with reviewed-header digest
`4bef135b14f105763a04f47108b864b5aea94e162b9d1cc376a20080e29346f5`.
Its runtime dependency delta is exactly `Bc2VehicleReticleSource.cpp/.h` and
`Bc2Gameplay.cpp` serialization of the `camera_owner` diagnostic; associated
regression tests were extended separately. Pump/reload/hand/audio policy and
capability rows are unchanged. The corrected native frozen build receipt is
`2eef72e0f3df1d2b58a85394edc60493c293ef9ad4ed0eb6ff72227a01806b06`.
Its x86/x64 builds passed 285 suites each; `boat-reticle03` uses this generation.

The final graphics build preserves the same native closure while changing six
separately pinned files: `D3D11BodyPropCompositor.cpp/.h`, `OpenXrHost.cpp`,
`NativeIpcProbe.cpp`, `VehicleReticleAsset.h` and
`BodyPropCompositorWarpTests.cpp`. Its frozen receipt is
`2712fb12ffa29d38394626cb50c90c8ed36336e9ca29b40d58ddafcafc9df06e`,
with normal native probe
`24565f4dd078ef47e15948da1366dc5d05de4eb3bb89d7a3a313ce0a009bdca3`.
Both architectures again passed 285 suites, and the x64 focused WARP test passed.
`boat-reticle04` uses this exact build. The earlier audio diagnostics retain
their initial `6abac213` provenance; they are not relabeled as a new-source run.
The intermediate five-file overlay receipt `3567caa1` remains separate.

The preceding fully built startup-integrated closure is
`d2d398ecf570b77991ec1d0a0327312cddf61cbc710168c9e5c95e59969d2497`
(529 dependencies), with reviewed header
`8fb3cd18a42da5d035980c7d78df29e6336d6a156dcd8d5041aaf4f3c96127da`.
The frozen normal receipt is
`325f0081d090b95510164d06039be0ab3c30a6623e07ff125b5c8e8cb237425a`,
with probe `af123ee20bddd0689897de67cbded68d585b5e5b9b840831427ce3a773d4d538`.
Both normal architectures passed 286 suites. Against the preceding native
closure, only `Bc2MenuRuntime.cpp` changes and `Bc2MenuStartup.cpp/.h` plus
`Bc2MenuStartupRuntime.cpp/.h` are added. Capability rows, pump/reload/hand/audio
policy and the six-file graphics implementation are unchanged. CMake/test
wiring and the delayed single-image `NativeMenuProbe.cpp` diagnostic are pinned
separately. The latter is outside the native runtime dependency closure.
Exact compatibility checks preserve the initial audio and boat04 generations;
neither earlier live test is claimed as a `d2d398ec` rerun.

The preceding 529-file cache-fix closure is
`4dfea3cd48e6970e4374a87bb0949a06e29132fe8e20104c1fdefbad9530bb5a`,
with reviewed header
`7b143d00fddfe6c84518bae9c653f16bb79fb3be6357a8ebaaae22e7bdfbc1c7`.
Only `NativeProbe.cpp` and `Bc2StereoRecovery.h` differ in the native dependency
set; `Bc2StereoRecoveryTests.cpp` changes separately. Header capability rows,
source roots/recipe/wiring and other compiled files are unchanged. Exact source
compatibility checks passed, including rejection of an unrelated reload-code
change. Frozen receipt
`0719eb3602ad663d0526f435e3be04650e0413781971f99027dce998bf51845a`
pins probe `1679cf9a7f96c7f64f2fc92366b7e0b35b23aaaf957004b98b19df0635f82d5d`
after 286/286 full suites per architecture. The old source/header and all previous
stages remain preserved. The new `headset-launch416-menu-cache-private` stage
passed ValidateOnly; its separate promotion gate requires overall success from
this exact generation. Its transport-only success is not relabeled as
an overall pass or as a rerun of earlier audio/boat diagnostics.

The final 529-file closure is
`8a294b7881e995663ab1398a6d6680811778d2935057c0ac0957a9b8ef8896b2`,
with reviewed header
`477efb148e709ec4b54ef987d63131444bc995ca196097ddd71eb48d9071c225`.
Frozen receipt
`b0a2e1bc2de935b227d9cc3eee208cdf5dd61a3c55dac9dabedaf5aed040bbf0`
pins probe `6ab46b2aa3428990fa04d927f3119ee65d9e35cd34e7cd3eb5185e131630a444`.
The reviewed native delta changes `StereoCulling.cpp/.h` and `NativeProbe.cpp`
only for floating-point containment evidence/comparison and reporting, with
tests and source provenance updated separately. Live `EncloseStereoFrusta`
construction, weapon/hand/audio policy and capability rows are unchanged.

`campaign-menu-visible01` adds bounded initial/resumed eye readback to the private
receiver. Its second initial candidate and first resumed candidate qualified;
only four world-eye images were saved (33,177,600 bytes total). The receiver
requires nonblack content in both eyes after at least eight transported pairs
per phase, but that alone does not establish scene visibility. Separate pinned
`root-menu-visual-review.json` and `root-world-visual-review.json` record the
actual menu and four-eye reviews described above. Final validation passed with
trace/receiver exits 0 and the game closed normally. Earlier black-frame,
diagnostic-failure and crash artifacts remain failed and preserved.

`Build-Checkpoint.ps1` selects `profiles/checkpoint416/source-with-header.json`.
Keep 415's compatible shared runtime-test object, separate bolt-specific compile,
parallel CTest, static CRT and discovery/CI fixes. Initial, native-corrected and
overlay builds have separate receipts; none reuse 415 acceptance. No remote CI
result is claimed.

See [current state](../../docs/CURRENT.md),
[shell audio](../../docs/archive/SHELL-RELOAD-AUDIO-416.md) and
[graphics investigation](../../docs/archive/GRAPHICS-FOLLOWUP-416.md). The culling audit
found coherent paired cameras and a verified child-anchor consumer, but no
boulder-causing mismatch. Third/fourth-view semantics and downstream cache/LOD
ownership remain unclassified; no scenery fix or forced LOD is included.

The bounded visible menu/campaign/pause/resume check is accepted, but the latest
headset regression blocks release and promotion. Investigate that failure before
publishing any pre-alpha. Controller reopening and a full campaign remain
separate acceptance work. A future combined headset check should
cover desktop stability, shell audio with unchanged pump audio and the boat
glyph. LMG reloads, broader coverage and a full campaign remain unfinished or
unaccepted. Frozen 414 stays the player rollback; preserve exact 413 audio and
earlier rollback references. Generated work stays on G: within existing limits.
Lossless compression of 71 retained JSON records recovered 2.232 GiB; hashes,
paths and budgets are unchanged, with no files deleted. Additional compiler/raw
compression reduces allocation without weakening retained evidence.
This handoff contains no game assets, recordings, raw traces or local profiles.
