# Current state: checkpoint 429 menu startup / launcher 0.1.10

## Tester startup correction — October 20

The supplied 0.1.9 EA/VirtualDesktopXR log establishes two local software bugs:
the host disabled pointing for the tester's 1024×768 raster, and the launcher
stopped a healthy main-menu-only session after 35 seconds because its readiness
check required world-render input telemetry. The supplied archive does not
contain the later failed retries. See [the diagnosis and implementation](MENU-STARTUP-HOTFIX429.md).

The candidate uses the original verified native pixel-to-APT mapper, supports
full-raster pointing at different aspects, and publishes a separate expiring
menu connection handshake. Explicit restart guidance replaces misleading
retry status; it does not force-close games or delete installation data.
The previous 0.1.9 knife and 0.1.8 selected-drive corrections remain included.

Native source is frozen as
`bc035d08a511df6d0d57cbf7bb78689a8d7842977cbc6bee953ac5940621c133`.
Both full normal builds passed 293/293 C++ suites each. The runtime receipt is
`6d6184ebad27f567f40d3051d8b79825e1e04dbf35e316f97980be82dca9b093`.
The 45-second actual-game menu check passed with world input zero, fresh menu
readiness beyond 35 seconds, correct off-center native mapping and clean hook
drain. The owned game was closed normally afterward. Package validation passed
21 checks covering all 87 payload files, fresh install, 0.1.9 update, installed
validation and rollback. Private evidence is in `bc2vr-recovery/startup429`.

The local launcher now uses packaged 0.1.10, with 0.1.9 retained for rollback.
Settings are byte-for-byte unchanged and installed validation reports the
existing asset cache ready. Installation started no game or headset session.
Keep **Use local checkpoint** unchecked to run the installed package; the
optional older local profile remains preserved.

The new actual OpenXR-menu presenter regression passed across four raster sizes,
off-center input, panel/cursor/beam layers, trigger and focus recovery. Native
mapper/getter instruction emulation passed 72 cases. Changing this workstation's
window to 1024×768 did not change the internal 1920×1080 renderer, so that resize
is explicitly inconclusive for actual-game 4:3 acceptance. The bounded live check
used the actual 1920×1080 renderer; it sent hover only, without clicks or OS input.
Actual Virtual Desktop headset acceptance and death/reload crash recovery remain
unresolved.

## Knife hotfix published — October 19, 16:57 UTC

[BC2VR 0.1.9](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.9)
publishes the tested r14 runtime, installer and updater manifest. All three
uploaded assets match their tested sizes and SHA-256 digests. Source and the
bounded native-hit checkpoint are at `72166d082e8114f05bf60132544aec73233deac4`.
The release retains the death/reload-crash, full-campaign and human-headset
acceptance limits. This is a narrow knife hotfix, not campaign acceptance.

## Verified opening interaction — October 19

The main r14 candidate has now delivered an actual physical knife hit in the
opening mission. In `navigation42803`, emulated tracked-controller stroke input
449 reached the ordinary native path, acquired target `895084672`, and produced
one native damage callback with cause 1. The verified native health field changed
from 12.5 to 12.375. The later distinct stroke, input 602, completed a clean miss
and rearmed; the final pending count was zero. Native and receiver helpers exited
zero, and retained hook/reference/view cleanup passed. This is native damage
evidence beyond the earlier client animations and targetless attacks.

The independent audit joins the delivered inputs, fresh menu/tracking authority,
repeated typed owner reads, native acquisition/attack and damage timestamps to
the same game06 lifetime. No trigger-button attack was synthesized. It also
measures the native blade axis within 0.517 degrees of body forward. The retained
post-attack image was independently reviewed and visibly reads “Two down. About
a thousand to go.” while the player faces wooden stairs with the knife held.
That is an observed narrative response after the hit. The runtime target has not
been independently joined to an authored guard GUID; two separate kill events,
objective completion and a scripted camera turn are not established.

Private evidence remains under `overnight426/knife/navigation42803`, with
`navigation42803-independent-hit-audit.json` beside it. The audit SHA-256 is
`ab2991d3d0ea31e5a55a0ff70ab3da846d6c8227ac1d76dd94823d013ceb430c`.
Its native report, input journal, before/after identities and cleanup receipt
remain pinned; raw game evidence is not distributed with source/packages.

This was a bounded game test with emulated controller input, not a human headset
test. Whole-campaign playability, actual headset handling, death/reload recovery
and long-session stability remain unverified. The older 0.1.8 is unchanged. The local
0.1.9 candidate is installed and its installed-runtime Validate passed with assets
ready. Settings remained byte-for-byte unchanged, and 0.1.7 is retained for
rollback. Installation/validation started no game or headset session. The exact
installer bundle passed 20 smoke checks covering all 87 payload files. The
root-owned game06 was closed normally with exit zero after testing. The optional
old local-checkpoint profile remains in settings; leave **Use local checkpoint**
unchecked to run packaged 0.1.9. No further session was launched.

## Built and frozen main candidate

Both r14 full architectures passed **293/293 C++ suites**, following focused
selected-knife ownership/geometry and receiver-fixture regressions. The shared
`build/x86` and `build/x64` trees again use this main repository rather than the
isolated 0.1.8 hotfix worktree. Normal feature flags and input protocol 4 were
verified from retained configuration and compiled target definitions.

The frozen runtime is `overnight426/normal-runtime-r14`. Its build receipt is
`7443155a14404ecf99cf592df7bc36bd7d32ebdcbb70435a3e3293be05780960`;
the full source snapshot is
`44f637ccdabb8991665610fd60e4d26b60a0f92e8792a811ec2ae326153aa1fa`.
Native source remains unchanged from r13:
`17f0b0647fa09f063d7becb2acff27ea0bc69b50b0f511a9c4dc3fe0edd3ddf7`.
Frozen r13 and published r4/0.1.8 binaries remain separate.

R14 changes only the private receiver's prepared-approach recipe and its tests:
it rotates the measured wrist before movement, holds that orientation through
the approach, and schedules the first physical jab after 500 ms of released
movement. The option requires motion and measured rotation, excludes turn/resume
combinations, and retains the menu, tracking, lifetime and delivery gates.
It does not add a production knife mechanism or assign native damage.

The production candidate includes the earlier exact ordinary acquisition caller
correction (`0x6E1FCA`) and consistent normalization of established transient
owner context bits. Inventory selection and typed lifetime checks remain exact.
Those changes previously passed repeated native miss/rearm checks; the new hit
supplies the missing bounded native damage evidence. Earlier failed runs remain
failed and retained. See [selected-knife evidence](SELECTED-KNIFE-NATIVE-426.md)
and [the checkpoint428 record](history/2026-10-19-checkpoint428.md).

## Public startup hotfix

[BC2VR 0.1.8](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.8) was
published on October 19 at 15:18 UTC from `hotfix/0.1.8-startup`, implementation
commit `475dc87f13a94546c0c6f639efedc51ad393a86c`, over published 0.1.7 gameplay.
It fixes the G:-only native audio-cache check that rejected the EA tester's
valid X: cache, retains exact content/callback validation, and surfaces bounded
native startup failures. It does not contain the main branch's knife work.
The public campaign warning therefore remains applicable to that release.

Both hotfix architectures passed 293/293 suites; focused launcher/setup checks
passed 135, and exact package validation passed 44 checks plus 51 launcher checks.
The unchanged asset recipe/cache was validated with the staged runtime. All three
uploaded assets match their tested sizes and SHA-256 digests. EA/VDXR headset
rendering is still unverified; runtime initialization is not rendering acceptance.
The separate hotfix source branch is published for fork review.

The earlier [0.1.7 recovery/resource hotfix](LAUNCHER-HOTFIX017.md) and
[0.1.6 installer/runtime hotfix](LAUNCHER-HOTFIX016.md) retain their own package,
settings, update/rollback and cold-cache evidence. Versions 0.1.5–0.1.7 used
normal-r4 native gameplay, source
`9196965b6f424cbfa33e98650fc230de273fc444de24c720218dbfa5f703b05b`,
with 291 tests per architecture. Its preserved profile is
`profiles/checkpoint426`; those results do not establish the new candidate's
gameplay. Build instructions remain in [BUILDING.md](../BUILDING.md).

## Other retained functionality and limits

The shared Nambu100 grip/support pipeline accepts its authored absent optional
persistence record while preserving exact weapon/data/configuration/mesh/rig
and lifetime checks. An earlier actual-game run verified partial-magazine return,
replacement from reserve and a subsequent shot, with native copies agreeing
and counts changing from 30/114 to 29/84. Detached-magazine pixels and headset
placement remain unverified. See [opening coverage](OPENING-WEAPONS-426.md).

Nambu's earlier pre-spread axis matched the authored barrel, but its stock shot
origin was displaced from the displayed muzzle. The translated-muzzle descriptor
and downstream argument observer remain Candidate; final native acceptance and
impact accuracy are pending. Matching other weapons' configuration fields does
not enroll them. See [aim evidence](NAMBU-AIM-426.md).

Manual recenter remains both thumbstick clicks held for one second. The vertical
look/reference-yaw correction and protocol 4 tracking-basis distinction have
combined support/recenter regression coverage; headset acceptance remains open.
The checkpoint425 opening-cloud correction was accepted by the user in a headset
and is retained in [the 425 handoff](../handoff/checkpoint425/README.md).

The reported death/reload access violation is unresolved. Failed session
`20261019-114521-913-2e4b9243` has an empty final report and an invalid instruction
target `0xC` in the retained game crash XML. Actor loss precedes the crash, but the
producer/corruption cause and mod involvement remain unknown. A prior no-VR fault
does not exonerate the mod. Bounded `checkpoint42701` evidence shows a scene
replacement, later stereo rendering and clean cleanup despite an incomplete
wrapper poll. `navigation42705` shows actor loss before its scheduled jabs and
later checkpoint return, but does not verify post-death controller/stereo recovery.
Neither result establishes a death/reload-crash fix.

Manual LMG reloads, wider weapon/vehicle coverage, spent-shell ejection, some
magnified scopes, full-body IK and multiplayer remain unfinished or unverified.
The next acceptance work is ordinary progression beyond the observed opening
response, real headset handling and bounded death/reload testing.

## Distribution and storage

The launcher retains manifest-checked updates/rollback and manual sanitized
feedback; nothing uploads automatically. Python is bundled. First asset
preparation fetches a pinned upstream FFmpeg decoder and builds private assets
from the player's own installation; validated warm caches require no download.
No game executable/assets, saves, raw traces or credentials belong in a package.

The G: rule is local development policy. Public installers/caches use the
player's selected valid local drive, including C:. All work here stays on G:
under [the storage budget](STORAGE-BUDGET.md). R14 reused the two existing build
trees; headroom was recovered by lossless, hash-verified compression of idle
generated files. Source, runtime symbols and evidence were preserved. Older
checkpoint results retain their scope in [history](history/2026-10-18-checkpoint425.md).
