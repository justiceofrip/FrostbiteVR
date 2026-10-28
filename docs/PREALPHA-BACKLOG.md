# BC2 VR pre-alpha work list

Updated October 25, 2026. This is a current planning index, not a new release or
a claim that every historical issue still reproduces. Public/installed 0.1.12
and unpublished candidate 436 R45 build06 have different validation scopes. See
[CURRENT.md](CURRENT.md) for exact build identity and evidence.

## Preserve the working baseline

External headset feedback confirms opening knife kills and progression to the
scientist, loading-screen text, manual magazine loading, and no VR dropout in
that particular run. Earlier feedback accepted shoulder holstering and much of
the SPAS pump behavior. These are useful acceptance points, not all-weapons or
full-campaign certification. Do not reopen the opening knife as an unfixed
feature without new evidence. The October 25 feedback supplies new polish
evidence: jabs can require repeated attempts and stock attack audio plays the
whole animation. Investigate recognition, native hit timing and local audio
separately while retaining the accepted opening progression.

The October 25 overnight queue adds a selectable campaign weapon bench with
reload/holster emulation, ammo-counter flicker, knife response/audio, an explicit
campaign-seat matrix, and paired destruction-draw evidence. The latest accepted
private performance measurement remains R38: 88.183674 fresh stereo pairs/s at
2784x3024 per eye with full measured depth; a valid
same-runtime comparison and physical-headset performance remain outstanding.
The sections below preserve older measurements as history, not current-build
performance estimates. Root records newer validation in CURRENT.md.

Tester feedback reports an XM8 separating from its hands. Frozen R41
repairs three shared publication defects: partial gun/hand commit, a skipped
final tracking guard for generic bindings, and a Pack scratch-lock conflict
that could reject the matching hands after a weapon-root commit. Both normal
architectures passed 305 suites. Frozen R43 combines the R42 diagnostics with
the complete immutable input handoff and passed all 305 suites on both
architectures. R44 then repaired a reproduced shared rig-reader rejection of a
native hidden weapon leaf; R45 repairs the subsequent draw acknowledgement across
neutral input rearm. R45 build06 passed all 305 suites on both architectures.
Its October 25 actual-game SP01 emulated run passed both shoulder draws with
two commits, no cancellation or unintended stow, and 240 received stereo pairs
without receiver timeouts. This is a mechanical Nambu/Type5 draw-handoff result,
not new reload coverage or proof of the tester's exact trigger. Exact per-pose
root-to-Pack correspondence, interruption recovery and headset acceptance remain
open; the visual analyzer still reports insufficient event correspondence.
These are shared-path corrections, not XM8 calibration or an all-weapons claim.
Current weapon-family enrollment limits below still apply.

R14 passed an actual-game, emulated-tracking CPU capture at 2784x3024 per eye
and 299 suites on each architecture. It retains the reproduced consumer-copy
failure fix and preserves native resolution through tested transitions. R15's
desktop-copy ordering change passed regression validation. R16 passed 300 suites
on each architecture and an actual-game GPU capture (341 consumed pairs, two
request timeouts, no host-rejected pairs). Neither these
checks nor the older synthetic scheduling result measures physical-headset FPS.
R19 passed 302 suites on each architecture. Its same-game queue/service
crossover measured 21.87 fresh pairs/s for the reduced-queue candidate versus
18.89/17.48 for the surrounding baselines, at unchanged resolution and quality.
The observed 15.73–25.12% difference is limited by fixed order and 7.51% baseline
drift; CPU remained high in that run. This is historical evidence, not the
current candidate's rate or a general performance guarantee. The staged R19
headset launcher is also historical; current candidate identity and remaining
acceptance are recorded above and in CURRENT.md. The desktop launcher remains
0.1.12. Passing automated checks should not be repeated without a new reason.

## Work possible without a headset

| Priority | Work and present status | Useful automated or actual-game emulated check | Remaining acceptance limit |
| --- | --- | --- | --- |
| 1 | Performance: latest private R38 measurement is 88.18 fresh stereo pairs/s at 2784x3024 per eye, with full measured depth and zero host error counters | Preserve the frozen result and investigate loading/startup failures before a valid matched comparison. Do not repeat accepted captures/builds without a new question | No accepted percentage gain or physical-headset 90 FPS result. The matched old baseline omitted depth; the corrected same-runtime control crashed before measurement. VDXR/SteamVR pacing, streaming and perceived latency remain headset checks |
| 2 | Opening jeep: candidate separates campaign-seat cameras from boat-specific aim evidence; actual jeep acceptance is pending | Reach the real scripted jeep seat, verify both eyes continue publishing, then test aim, fire and recenter. Investigate X entry separately against the mission's actual permissions | The existing boat fixture is not proof of opening-jeep support; ride comfort remains a headset check |
| 3 | Death/checkpoint and interruption recovery: earlier successes do not establish a crash fix | Exercise death, checkpoint load, menu entry, vehicle transition and tracking loss during a pump/reload; require both-eye recovery and working shoot, grip, holster and Use afterward | No complete campaign or extended headset run has been accepted |
| 4 | Cutscene facing: tester reports looking away from scripted action | Inspect camera ownership and yaw transitions; replay known head/stick offsets into and out of cinematics, preserving free head look and correct gameplay return | Comfortable framing and transitions require a short visual check |
| 5 | Weapon-family coverage: shared systems exist, but manual LMG loading and broader mechanism/configuration enrollment remain incomplete | Extend the catalog/provisioning test setup; run common ammo-conservation, empty-fire, reinsertion, pump/bolt, pickup, holster and interrupted-action contracts against reviewed available configurations | Data registrations are not working-gun counts. Hand placement, physical feel and distinctive mechanisms still need representative headset acceptance |
| 6 | Stereo artifacts: left-eye building report, general LOD/culling and floating ACOG dot remain separate investigations | Capture a few paired-eye frames at controlled poses; identify exact view/draw ownership; test depth/occlusion and transitions without broad asset suppression | The left-eye-only report lacks a reproducer; ordinary desktop recordings may miss it |
| 7 | Setup, diagnostics and release readiness | Extend real-script tests for selected drives, cold/warm caches, update/rollback, runtime loss and render sizes. Preserve useful failure reasons even when final logs are incomplete | Mocked paths do not verify another executable, GPU driver, Wi-Fi link or VDXR installation; the reported rejected Steam build still needs inspection |

These are scoped investigations, not instructions to repeat every old test.
Use deterministic tests for identified failure boundaries, bounded real-game
checks for native integration, then a combined headset acceptance pass. Store
compact timing summaries by default; capture pixels only for a specific visual
question, within the existing storage budget.

## Polish and incomplete equipment features

- Controller-to-gun alignment, support contacts and magazine/shell placement:
  audit measured geometry and replay transformations before headset fine tuning.
  Recent accepted placement must not be replaced by guesses based on old videos.
- Pump/reload presentation: retain the working physical cycle and pump audio;
  review remaining animation, single-shell audio and spent-shell ejection evidence.
  Faster immediate pumping was explicitly deferred for a later timing/balance decision.
- Body equipment and ammo HUD: check anchors through crouch, recenter, snap turn,
  holstering and vehicle exit. Visibility/comfort still needs human confirmation.
- Launcher sights: preserve working toggle behavior; distinct sight hinges,
  including the AEK's horizontal motion, need configuration-specific validation.
  AEK/GP30 geometry exists, but its current native mode and sight admissions are
  explicitly disabled; it is not merely an accepted feature needing cosmetic polish.
- Reload coverage: belt-fed LMG steps, broader bolt-action support and pistol
  slide/chamber steps remain work, rather than assumed benefits of magazine support.
  Working Nambu100 does not establish Type5/Garand, Thompson or Nambu pistol
  coverage; those exact variants retain separate enrollment gaps.
  Manual underbarrel reloads and physical grenades can remain deferred as requested.
- Magnified sniper/launcher scopes need separate work from ordinary 3D optics
  and the floating-dot defect; do not apply a blanket ACOG/ADS change.
- VR settings and a measured settings guide remain player-facing improvements.
  The installer/updater/feedback mechanism already exists.

## Later scope

Full local body/legs, the flatscreen remote-IK viewer, multiplayer/crossplay and
ports to later Frostbite games remain separate projects. A third native carried
weapon slot also needs its own inventory evidence; a pistol hip anchor alone
does not establish that extra slot. These should not displace the campaign hard
stops and performance work above.

## Evidence references

- [Performance](PERFORMANCE-436.md), [native resolution](NATIVE-RESOLUTION-436.md)
- [Jeep failure and actual-mission test plan](JEEP-TURRET-436.md)
- [Weapon evidence and its historical limits](WEAPON-COVERAGE-CURRENT.md)
- [Test-range catalog and missing native steps](TEST-RANGE-PLAN430.md)
- [Reticle investigation](RETICLE-DRAW-FOLLOWUP-20261003.md), [optics roadmap](OPTICS-ADS-ROADMAP.md)
- [Executable admission](EXECUTABLE-ADMISSION-436.md), [body/IK roadmap](FULL-BODY-IK-ROADMAP.md)
