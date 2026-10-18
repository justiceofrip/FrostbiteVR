# Render observation recovery, checkpoint 421 candidate

Code review found that one rejected first-person projection observation could
prevent every later stereo pair from being published. `ProjectionContextHook`
recorded invalid matrices, context reads and overflow in the cumulative
`weaponContextFailures` counter. `ScopedContextEye::Apply` then treated any
nonzero historical count as a current failure. Starting a new frame reset the
captured context count but never cleared that rejection condition.

The candidate separates the cumulative diagnostic count from the current
frame's observation rejection. All five existing failure sites still reject
the current frame. Only the already admitted next-frame boundary clears that
local rejection; it must collect fresh context evidence before correction.
The public diagnostic field retains its cumulative meaning. A callback captures
an observation token on entry. The token and rejection bit share one atomic
state, so a delayed rejection cannot poison a newer frame or erase its failure.
After the original projection callback returns, a stale token prevents context
collection. This accounts for delayed rejection without claiming that every
native context-cache object lifetime has been independently proved.

An exact native restoration failure remains independent and terminal. This
change does not clear restoration counters, renew old input, retain old camera
or context pointers, relax owner checks, or alter sky/cloud rendering. Native
simulation and the exact-write cleanup remain unchanged.

The `ProjectionOverride` regression exercises an invalid native projection,
rejection of the current frame even after another valid observation, and a
fresh valid frame that applies and restores an actual `ExactWriteBatch`.
A separate conflicting native write survives failed restoration and keeps
later frames blocked. Delayed old-token rejections are tested both before and
after a current-frame rejection, preserving the current failure and cumulative
count. Independent source review found no outstanding issue in this bounded
epoch/rejection accounting change; full native cache lifetime remains separate.

All four focused suites passed: `ProjectionOverride`, `ExactWriteBatch`,
`Bc2StereoRecovery` and `RenderOwnerRecovery`. Both full normal builds passed
**288/288 C++ suites on x86 and 288/288 on x64**. Source `2caa8ab3` is frozen in
build receipt `81c573d1`. Exact pins and offline validation receipts are in the
[421 handoff](../handoff/checkpoint421/README.md). Actual checkpoint restart,
opening-scene visuals and headset acceptance remain **pending the root audit**;
offline tests do not establish those results.

This is a demonstrated source-level recovery flaw, not an established cause
of the reported checkpoint416 death/restart distortion. Its failed final trace
is empty; the completed416 checks inspected during this investigation record
zero weapon-context failures. The actual transition observer separately records
current native main/world/request and ordered view registrations without
dereferencing cached main or vector elements. Stable external reads are not
an atomic native-lifetime proof and cannot exclude between-poll replacement.

## Sky/cloud camera coverage and evidence limits

The stereo path derives both eye cameras from the current native primary view
and uses one conservative stereo envelope for the secondary culling camera.
The verified separate `+0x30` view anchor reaches the child-renderer path.
Native ordering remains all visibility gathers, all Prepare calls, then each
eye's Draw. Left-eye scopes correct the last gather's context values at both
Prepare and Draw and restore exact bytes afterward. First-person projection
and the identified mesh/terrain depth-bias callers have separate corrections.
No sky/cloud-specific camera or fog consumer has been identified. These known
paths do not prove that every screen-space, view-relative or temporal effect
uses the corrected values.

Read-only review of frozen421 `restart02`, native frame 20985, found stable
primary cameras with approximately 64 mm eye separation. Secondary position,
FOV and projection remained equal between eyes. The right secondary camera's
dirty bit 4 cleared and its cached `+0x210..+0x21c` values changed; this is not
evidence of an incorrect culling projection. Third/fourth camera blocks remained
constructor identity values with dirty flags 15 at all three sampled stages.
The run recorded 480 context-correction scopes with no patch or restoration
failure. These are **forest frames after the intro**, not the original cloud.
Both eyes used the same symmetric FOV, so this sample does not exercise
asymmetric projection correction. No checkpoint restart occurred in restart02;
the driver stopped before confirming the correctly displayed restart modal.

Current camera/context diagnostics retain only the first tracked frame. The
optional pass inventory captures only full 1920x1080 non-instanced draws,
deduplicates repeated shader/target combinations and samples VS/PS constant
buffer slots 0 and 1. It omits smaller render targets and later constant changes
under repeated draw state. `restart02/pass-buffer-evidence.json` is empty.
These records cannot identify the opening cloud's draw or prove that its history,
blending or view-relative camera is correct.

The first useful observation is one affected native stereo pair, tied to its
native frame, tracking/FOV, timestamp and world/request/current-main identities.
Inspect each eye separately: duplication inside one eye calls for draw/history
investigation; a single cloud in each eye with binocular mismatch calls for
camera/constants investigation; clean native eyes move the inquiry to later
presentation. If the pair establishes a native defect, a bounded one-frame
ordered-draw observer should include all viewport sizes and draw types, shader
and target identities, relevant constant-buffer bytes, blend/depth state and
sampled-resource identities, associated with that exact eye/frame/owner. This
is an evidence plan, not implemented instrumentation or a sky fix. Do not copy
unclassified camera blocks, disable cloud effects or reorder native jobs based
only on the current report.
