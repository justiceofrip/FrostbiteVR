# Local295: pistol magazine uses the shared reload pipeline

The exact MP443_sp now uses the existing magazine resource service, sampled
reload geometry, hand arbitration, visibility and body ammunition rendering.
An actual-game controller run drew it, removed/returned its original full
magazine (17/85 -> 0/85 -> 17/85), then discarded it and loaded a chest
replacement (17/85 -> 0/85 -> 17/68). All four operations were independently
confirmed by both client branches and the server, within their original
deadlines. All 240 stereo pairs were consumed, 40 pixel-checked; the interaction
reported no presentation fallback or conflicting hand claim. Hooks drained and
BC2 remained alive. No operating-system input or headset was used.

This extends the pipeline to the separately reviewed SingleFire, zero-bolt
magazine family. The exact asset descriptor and existing instruction proof
authorize MP443_sp only; it does not infer support for every pistol. The
authored sampled magazine path is reused. Right-hand static grip remains live
calibrated because its authored spline is not an accepted static binding.
Slide/chamber interaction, empty-magazine firing, pistol hip-holster routing
and headset hand placement remain unverified or unfinished.

The private magazine-only controller driver skips unrelated SPAS steps. It
cannot change ammo or acknowledge a consumer and is disabled in normal builds.
All 243 regression suites pass on x86 and x64; seven focused suites passed on
each architecture. Generic magazine test fixtures now use each exact capacity
and sampled path, instead of assuming a straight 30-round rifle magazine.
Installed-asset regeneration reproduced the 46-record private body cache
exactly (16,473,040 bytes); installed game geometry must not be distributed.

Prior294 proves native VR-menu checkpoint recovery with a removed rifle
magazine. Prior290 proves eight SPAS pumps through empty, shell insertion and
F2000 magazine return; prior293 proves five M95 bolts through empty, retaining
native magazine-refill fallback. These are separate tests, not a full campaign
or new headset acceptance. Eighteen compiled magazine prerequisites do not mean
eighteen accepted weapons. Sustained campaign/death recovery, belt-fed loading,
broader exact enrollment and remaining visual/interaction acceptance are open.

Evidence: `pistol295-native01/singlefire-magazine-audit.json`.
Source: `e03fbc41dde389e960942f5773cafa24ee322bab16914d1d681d3f2bd038df1d`.

Trace: `57f505fe79074d00324e0e418ca53f5c897820ff2d6dcb8a8ea12db26346fd16`.
