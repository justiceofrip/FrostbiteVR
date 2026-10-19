# Current state: campaign blocked; checkpoint427 investigation / launcher 0.1.8

## Startup hotfix published — October 19, 15:18 UTC

[BC2VR 0.1.8](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.8)
is the public installer/runtime hotfix. It was built separately on
`hotfix/0.1.8-startup`, implementation commit
`475dc87f13a94546c0c6f639efedc51ad393a86c`, over published 0.1.7 gameplay.
It removes the G:-only native audio-cache check that rejected the EA tester's
valid X: cache, preserves content validation, and improves bounded native error
reporting. It does not include this branch's knife/navigation investigation.

Both full architecture builds passed 293/293 suites. Focused launcher/setup
checks passed 135; exact installer/update validation passed 44 checks plus 51
existing launcher checks. The unchanged asset recipe/cache passed validation
with the newly staged runtime. All three uploaded release assets match their
tested SHA-256 digests and sizes. Actual EA/VDXR headset rendering remains
unverified. The knife, opening progression and death/reload crashes remain
unresolved; no playable-campaign claim is made.

The separate source branch is published for fork review. The main development
branch retains the unbuilt r14 diagnostic described below. Both shared build
trees currently point at the isolated hotfix worktree; restore their main-source
configuration and obtain new paired receipts before using them for r14. Frozen
r13 and published r4/0.1.8 binaries remain separate. Game05 was closed normally
before installer validation; its old actor, weapon and navigation identities
must not be reused. The user's installed launcher was not changed; 0.1.8 is
available through its updater. All new output and scratch stayed on G:.

## Release blocker — October 19

The opening mission knife still fails to damage enemies and blocks campaign
progress. The user reproduced that failure and a game crash on the published
0.1.7 package. Versions 0.1.5–0.1.7 contain the same normal-r4 native gameplay
build; the later launcher fixes did not fix the knife. Publishing an unverified
mandatory opening interaction was a release decision error. The public README
and 0.1.7 release notice now prominently mark the campaign blocked.

No new gameplay release or playable-campaign claim is warranted until actual
native knife damage, opening progression and death/reload behavior are verified.
The existing repeated-jab rendering test supplies simulated native outcomes;
neither it nor observed client attack animations prove server damage callbacks.

The failed session `20261019-114521-913-2e4b9243` has an empty final native report
and an access-violation probe result. Its preserved game crash XML records an
invalid instruction target `0xC`; stack addresses match the earlier AMD command
dispatcher, but the current producer/corruption cause and mod involvement remain
unknown. Actor loss precedes the crash by about six seconds without a recorded
scene replacement. Do not equate this correlation with a proven lifetime bug.

The private actual-game `fallback42701` run found the server receiving the stab
and invoking acquisition, while the adapter rejected its caller and subsequently
rejected attack as not acquired. The old filter expected a different native
notifier. Installed-executable inspection proves ordinary FireLogic0 Step calls
the acquisition listener through return address `0x6E1FCA`. The unpublished r8
candidate corrects that exact filter and fingerprints both the helper and Step
call site. Raw caller/self diagnostics now include rejected callbacks. Actual
acquisition, physical jab, enemy damage and opening progression are separate
acceptance requirements; static proof and unit checks do not establish them.

The paired r8 build passed 291 tests on each architecture. In actual game05,
`fallback42702` confirmed raw caller `0x6E1FCA`, admitted acquisition/attack and
one completed targetless attack (pending zero), with clean trace/receiver exits.
However `motion42701` caught another blocker without a headset: the first
physical jab reached the server but acquisition failed its owner predicate;
attack was then not acquired and the second jab remained blocked (pending
unknown). No target damage was observed. Do not call r8 a working knife fix or
publish it. Sparse first-failure owner diagnostics are the next investigation;
no native outcomes have been fabricated.

The paired r9 diagnostic build passed 292 tests on each architecture. Actual
`motion42702` admitted acquisition and copied both blade-search arguments, then
rejected attack solely because the bracketed client soldier flags changed from
3 to 19 (bit 0x10). Player, inventory and effects identities were unchanged; the
existing normalized owner comparison also matched. The next candidate applies
the existing 0x18 context-bit normalization consistently to callback brackets;
initial publication, inventory selector bit 0 and all other identities remain
exact. The paired r10 candidate passed 293 tests on each architecture. In the
actual game, `fallback42703` completed one targetless native attack without
ownership failures. `motion42703` then completed two separate physical jabs,
each with both copied blade-search arguments and returned acquisition/attack,
zero failures, zero unknown outcomes and no pending action. This verifies the
previously failing miss/rearm flow; target damage and opening progression are
still unverified, so the candidate is not a released knife fix.

The r11 paired build also passed 293 tests per architecture. Its actual
`forward42701` run moved the emulated player 3.67 m while crouched and completed
two physical jabs. Independent same-run input/render measurements put the blade
within 0.201 degrees of body forward and confirm the native search uses that
direction. Neither movement nor the slow wrist preparation generated an attack.
Both jabs missed; `forward42702` also completed two targetless attacks cleanly.
These runs verify movement, geometry and rearming, not enemy damage. The short
sessions inherit the previous native aiming direction on reattach; a bounded
ordinary snap-turn prefix is being added to navigate toward the opening guard
without desktop input or repeated swings from an unchanged empty location.

The r12 paired build passed 293 tests per architecture. Ordinary snap turns
and movement worked in the real game, but both navigation runs cancelled
before a jab when consecutive native menu publications were separated by
466 ms and 199 ms. Stereo delivery paused concurrently; the latter failure
preceded capture and wrist movement. Neither run supplies knife-damage evidence.
Both native adapters retired cleanly, with receiver failures preserved separately.
The ten retained probe workers had exited; no stale-worker cause was observed.
The production 100 ms input-authority deadline remains unchanged. The r13 paired
build passed 293 tests per architecture and adds an opt-in, receiver-only
movement pause. Actual `navigation42703` verified one 640 ms pause, including
28 fresh neutral publications spanning 513 ms, before ordinary movement resumed.
A second timeout correctly cancelled the recipe before any wrist rotation or
jab. Its initial camera heading was already 73.69 degrees different from the
prior run before scripted movement began; the cause between sessions is unknown.
Further navigation needs a fresh typed heading/position read, rather than assuming
the prior session's camera direction. No enemy damage or opening progression is
claimed. Cleanup is now recorded independently of receiver success, using the
full native reference/view-restoration checks; failed tests remain failed.

Actual `navigation42705` recovered the hand claim after its single menu pause,
but lost the player actor at 7.125 seconds, before either scheduled jab. Both
eyes show the guard immediately above the player and blood at movement release;
the subsequent desktop capture shows a sideways collapse. A new scene and actor
returned at 15.687 seconds, followed by the opening checkpoint/objective. This
strongly supports one gameplay death and automatic checkpoint return, although
no health-zero or explicit death event was recorded. No jab reached the old
actor after its retirement. The receiver ended before the replacement, so this
does not verify post-death stereo or controller recovery. Cleanup passed.

`navigation42706` then completed a safe 13.7 m crouched transit using the
replacement actor, one targetless native attack, and clean native/receiver exits.
Its measured knife direction was re-derived for the new weapon lifetime. Neither
the earlier owner loss nor successful transit proves opening-guard damage.

The r14 receiver-only prepared-approach diagnostic is implemented but **unbuilt
and untested**. It prepares the measured wrist orientation before movement,
holds it through the approach, then schedules the first physical jab after
500 ms of released movement instead of the previous 2.5-second wait. The opt-in
requires motion and measured rotation, rejects turn/resume combinations, and
retains the existing tracking, menu and publication gates. Changes are limited
to `TriggerInputSmoke.h`, `NativeIpcProbe.cpp` and `SceneRecoveryProbeTests.cpp`;
the added C++ cases have not been compiled or run. This is diagnostic scheduling,
not a production gameplay change or evidence of knife damage.

The latest built main native closure remains r13, source
`17f0b0647fa09f063d7becb2acff27ea0bc69b50b0f511a9c4dc3fe0edd3ddf7`,
with the existing 293 passing tests on each architecture. Game testing is paused
while the isolated public 0.1.8 package is validated. No r14 runtime, headset,
enemy-damage or campaign-progression acceptance is claimed.

The confirmed install-drive/startup-reporting hotfix is being isolated from this
unfinished gameplay work for release 0.1.8. The campaign warning stays in place;
the setup fix must not be presented as verified knife or campaign completion.

The r9 `checkpoint42701` diagnostic performed exactly one Last Checkpoint reload.
The wrapper stopped its actor polling on a transient null-player read and is
preserved as incomplete. Supplemental recorded evidence independently shows
one native scene recovery, a replacement player, subsequent native Update,
eight retained stereo pairs after replacement, both-eye world rendering and
exact cleanup. Its fresh 60-second exception watcher captured no exceptions and
detached with the game alive. The private polling bug is corrected; no duplicate
game run was used to overwrite the failed wrapper result. Actual death and
longer-session stability remain unverified.

The r7 diagnostic finished its native cleanup/report but its outer helper timed
out before receiving the thread-exit receipt. Its evidence is preserved and that
root-owned game was closed normally with exit zero before a fresh game05. Finite
diagnostics now allow 30 seconds beyond the requested input interval for startup,
retirement and report writes; input duration and public host lifetime are unchanged.
The private receiver can perform a bounded ordinary-stick approach before the
existing trigger/two-jab recipe, cancelling on tracking/menu/lifetime changes and
recording every requested input and publication result. It never edits position
or health, and publication alone does not prove movement or damage.

The launcher also no longer labels a nonzero native exit as clean merely
because the game has already exited. Its focused regression reproduced the old
classification and passed 13 checks, including normal game closure.

New EA tester feedback confirms 0.1.7 local asset preparation succeeds and VR
starts briefly before native `probe_exit:20`. This is the generic caught native
exception path, distinct from the earlier access violation; its exact cause
was found in the tester's newly supplied `runtime.zip`: both attempts report
`Mechanism audio callback binding or private cache unverified`. Their executable
is the same supported SHA-256, and the audio files match the exact required hashes
and formats. Both native audio readers incorrectly required G:, rejecting the
tester's X: cache. The unpublished shared path helper admits absolute local paths
on the user's chosen drive while retaining all content and callback checks.
VirtualDesktopXR instance/system initialization succeeded in these logs; no
rendered gameplay acceptance follows. Published 0.1.7 fails to show or bundle
the native error object. Unpublished startup/feedback changes now surface a
bounded error and sanitized failure summary, retaining log fallback for absent,
partial or oversized reports. They passed 25 new diagnostic, 26 startup and 45
feedback checks. The actual packaged installer/update/rollback compatibility
matrix also passed 26 checks with mocked process/XR boundaries; its portable test
is now in `tests/Test-LauncherUserSetups.ps1`. Other users' hardware compatibility
and actual headset gameplay remain unverified.

## Launcher 0.1.7 published hotfix

[BC2VR 0.1.7](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.7) was published
as a pre-release on 2026-10-19 at 11:39:58 UTC after exact-package and cold-preparation
validation. All three uploaded asset sizes and SHA-256 digests match the tested
packages. The local G: launcher is updated with every settings value preserved,
a verified warm asset cache and 0.1.6 retained for rollback; installed validation
passed without starting the game or headset. Its 122-input packaging manifest is
`f3b14c96efc45cb334f6ea31b6a48a691b93be67ca8b6181e86c831717c17911`.
It retains the exact normal-r4 checkpoint426 native VR binaries and executable gate.

Failed package identity or file verification now leaves feedback, update checks,
repair and verified rollback available, using the official recovery repository.
Runtime entry remains blocked. Setup/update can repair a damaged existing version
only after the complete replacement passes verification. A bounded journal
preserves the original through interrupted replacement; settings and a distinct
verified previous version are retained. Healthy same-version packages are not
silently replaced. This does not diagnose which identity field caused the external
user's failure; the new report records expected and observed values.

Local asset preparation now records its stage and bounded failure details for
feedback. Public cache preparation verifies exact required mesh, animation,
configuration and audio resources while allowing unrelated archive-index changes.
Geometry/catalog checks remain exact, and authoring keeps its default index checks.
This is asset-content compatibility, not new native executable enrollment. The
external asset failure's cause remains unknown; no EA/store or VDXR headset
acceptance follows from these changes.

Focused checks passed: package recovery 37, settings/UI 70, desktop launcher 51,
startup 26, feedback 45, asset diagnostics/storage 9, package staging 5 and resource
identity/geometry/audio 66. Exact 0.1.7 package install/update/repair validation and
rollback passed 44 checks, plus 51 desktop launcher checks. Fresh preparation with
the exact staged runtime and packaged Python passed cache integrity and completion
diagnostics: all six outputs, including the 48-part body cache, are byte-identical
to the verified 0.1.6 outputs (16,730,338 bytes total). The existing decoder was
rehashed; no download, game launch or headset session occurred. No new native or
headset test is claimed. See [the 0.1.7 record](LAUNCHER-HOTFIX017.md).

For a damaged installation or a blocked older updater, use the new Setup and
choose the existing launcher folder containing `active.json`. Settings are
preserved. All gameplay limitations below remain.

## Published checkpoint426 baseline

[BC2VR 0.1.5](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.5) was published
as a pre-release on 2026-10-18 at 15:52:22 UTC, with an installer, launcher/updater
and manual feedback reporting. All three server asset digests match the exact
local package. This framework checkout is the source handoff; publication does
not remove the native gameplay and stability limits below.

## Launcher 0.1.6 hotfix

[BC2VR 0.1.6](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.6) was published
as a pre-release on 2026-10-18 at 21:57:49 UTC. All three uploaded asset digests
match the tested package. The local G: launcher is updated to 0.1.6 with every
settings value preserved and 0.1.5 retained for rollback. Installed validation
passed with the newly verified warm cache; no game or headset session was started.

The 0.1.6 launcher package keeps the exact normal-r4 native binaries below.
It adds a visible game-folder picker and recovery for malformed launcher settings,
allows installation on a chosen local fixed drive with space checks based on the
operation, and uses the selected OpenXR runtime instead of forcing SteamVR.
An unverified executable is reported with its SHA-256 before any VR hook attaches;
this does not establish compatibility or incompatibility for every EA/store build.

Startup now checks the active Direct3D 11 renderer and uses its stable dimensions
instead of assuming 1920×1080. It allows up to 90 seconds for the main menu while
exiting immediately once stable, and waits for native VR input telemetry before
reporting the session as running. Failures include bounded local diagnostics.
No game settings are changed automatically. The exact package passed 28 package
checks and 51 launcher checks. Focused checks passed: startup 20, session 8,
feedback 44, portable runtime 42, settings/UI 68, storage 18, installer settings
10, Python cache 5, dependencies 34 and menu reader 8. The packaged Worker also
rejected an unverified executable early with its fingerprint, without changing
BC2, XR-host or vrserver processes. Cold preparation using packaged Python and
the rehashed local decoder regenerated 48 parts plus audio (16,730,338 bytes);
runtime cache integrity passed. No game or headset session was started by these
checks. See [exact hotfix evidence](LAUNCHER-HOTFIX016.md).

For an existing 0.1.5 installation blocked by its old free-space check, download
the new Setup, run Install.cmd and choose the existing launcher folder; settings
are preserved. Real-headset startup acceptance for 0.1.6 remains open. None of
these changes resolves the knife, Nambu impact-alignment or death/reload limits.

## Frozen source and validation

Normal-r4 native source is
`9196965b6f424cbfa33e98650fc230de273fc444de24c720218dbfa5f703b05b`.
Both r4 full architectures passed 291/291 C++ suites. Earlier r3 remains retained
as a blocked candidate; its native knife runs prepared no attack. The exact r4 native capability
header and 544-file dependency receipt are preserved under
`profiles/checkpoint426`; no new native verification is inferred from the hash.
The paired host/adapter use input protocol 4. Source build instructions and the
normal feature composition are in [BUILDING.md](../BUILDING.md).

The preceding 425 opening cloud correction was accepted by the user in a
headset. Its bounded native control/active comparison and exact restore evidence
remain in [the 425 handoff](../handoff/checkpoint425/README.md). The public launcher
enables this correction and clears observer/experiment/delay variables only in
the fresh game's environment. Portable-runtime regression passed 40 checks.

## Opening weapons, aim and recenter

Exact installed/live Nambu100 identity includes an absent optional persistence
record. The shared grip/support pipeline handles that case while retaining
weapon/data/configuration/mesh/rig and lifetime checks. Authored support and
magazine geometry are measured. An actual-game run verified magazine removal,
returning the original partial magazine, replacing from reserve and firing;
the three native firing copies converged and counts changed 30/114 to 29/84.
This does not establish detached-magazine pixels or headset placement.
See [opening coverage](OPENING-WEAPONS-426.md).

The original Nambu pre-spread composition axis matched the authored barrel
within 0.000024 degrees, but its stock origin was about 86.2 cm from the displayed
muzzle, including 10.5 cm perpendicular separation. Exact candidate muzzle
enrollment and a read-only downstream argument observer are implemented.
The observer distinguishes supplied launch arguments from native server return,
virtual receiver acceptance and impacts. The descriptor remains Candidate.
Final native downstream acceptance is pending; do not report impact accuracy.
A private provision observer could not qualify the currently selected melee
subtype, so it made zero assignments and left no hook installed. The reader was
not broadened to manufacture a firearm test. Earlier Nambu reload evidence remains
accepted for its original source and operation scope.
The camera-flag guard accepts the exact authored XM8/SPAS configurations; AEK
and SCAR passing those flags does not grant a translated-muzzle capability.
See [aim evidence](NAMBU-AIM-426.md).

Selected Knife_Equipable routes semantic fire to its exact action 37 and has a
bounded motion/acquisition adapter with exactly-once admission and native-owned
damage/search behavior. The first motion run rejected the old zero-direction
gate; exact installed and repeated live data show InitialDirection=(0,0,1),
InitialPosition=(0,0,0). Native acquisition constructs a forward basis from that
vector. The corrected gate and malformed-value regressions are in normal-r3.
The corrected motion02 run has zero ShotOffsets failures but still prepares no attack.
The r3 motion02 and fallback01 runs were blocked by an unissued automatic-stow
preference (one entry, zero requests). R4 retires only that preference after a
fresh coherent native inventory read joins the exact selected knife configuration
and typed mesh. It requires Held with no pending selection/new command; issued
work and native attack gates remain intact. Focused regressions passed after
a test-only timestamp correction. The r4 motion03 run now retires the preference
(pending=false, unsupported=1) and prepares one attack. Both client firing copies
progress through 5/6, 6/1 and idle in about 1.2 seconds. There is no admitted server
Acquire/Attack observation; unknown=1, pending=3 and failure10 has no timestamp.
Those counters cannot establish damage, completion, repeat-jab recovery or first
mission progression. The evidence audit remains failed for its requested level.
All hooks stopped cleanly, references returned 8 to 8, and survivor order, camera,
clock and list state were restored. The game then closed normally.
See [selected-knife evidence](SELECTED-KNIFE-NATIVE-426.md).

Manual recenter remains both thumbstick clicks held for one second. The 425 run
recorded three shorter attempts and two automatic recenters, which does not
prove a broken binding. A separate vertical-look recenter defect was found and
corrected: gameplay must not add old head yaw when the host retains reference
yaw. Protocol 4 distinguishes reference changes from a replaced tracking basis.
Combined support/recenter regressions pass; headset acceptance remains open.

## Crash and lifetime limits

The user reported an access violation after two deaths/reloads. The 425 host
closed gracefully after the game exited. The new scene guard blocks only a
twice-verified different current world/request; same-request root changes remain
diagnostic-only until recovery exists. This is not a proven fix for the crash.
A fresh no-VR Continue run also faulted in an invalid AMD queued callback; that
evidence does not identify the headset crash's cause or exonerate the mod.

One earlier 426 normal candidate completed an Entire Mission restart with exact
current-view cleanup and request references 8→8. This is not a death/reload-crash
acceptance. Earlier neutral fixture failures and the failed first knife motion
run remain retained. No whole-campaign, reconnect or long-session stability
claim follows from bounded native checks.

## Distribution and remaining work

The original 0.1.5 release passed 25 package checks and 49 launcher checks, covering fresh
installation, updates, rollback, local feedback and corrupt-package rejection.
The user's launcher was updated to 0.1.5 with settings and previous version 0.1.2
preserved. Installed-runtime validation passed for the exact r4 source and warm
local asset cache, without starting the game. Private generated assets were
verified and retained locally; none were published. These checks do not establish
headset or campaign acceptance.

The portable launcher starts through the VR main menu, supports manifest-checked
updates/rollback and prepares manual GitHub feedback with bounded sanitized
text diagnostics. Nothing uploads automatically. Python is bundled; first asset
preparation fetches a pinned about 77 MB FFmpeg decoder directly from upstream and
caches it on the chosen installation drive. Validation and warm asset caches do not
download it. No game executable, assets, saves, raw traces or private credentials
belong in either public repository/package.

Actual cold asset preparation from the staged r3 package passed using bundled
Python and the verified upstream decoder: 48 body parts for 17 configurations,
plus audio, totaling 16,730,338 generated bytes. Runtime cache integrity passed;
the body cache hash starts b0825b56. These assets remain private and are rebuilt
from each user's own installation. This is retained r3 packaging evidence, not
an invented r4 rerun. The final r4 stage revalidated all 73 non-native runtime
files, all 61 recipe inputs and the existing generated cache/audio bytes as
identical, so the preparation result is reused only for that unchanged scope.
The changed 0.1.6 recipe received a separate actual cold preparation and cache
integrity check; its exact receipt and hashes are in the hotfix evidence above.

Manual LMG reloads, wider weapon/vehicle coverage, spent-shell ejection, some
magnified scopes, full-body IK and multiplayer remain unfinished or unverified.
The latest 426 candidate has no human headset acceptance. Preserve 425 source,
symbols and crash evidence and 414's earlier accepted handling/audio reference.
Historical 416–425 investigations are retained in their handoff records and
[history](history/2026-10-18-checkpoint425.md); their results describe their own
source generations. Generated data and temporary work stay on G: under the
existing storage budget.
