# Current state: checkpoint426 with published launcher 0.1.6 hotfix

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
