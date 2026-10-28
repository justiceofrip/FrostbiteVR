# Campaign turret camera rejection — 436

## Report and evidence boundary

The user reported that the first mission's jeep turret went black in the headset
after the script placed the player in the seat. Pressing the interaction button
had not entered the seat earlier. A separate destroyed-building artifact was
visible only in the left eye and stayed in one direction. These are three
reports, not one demonstrated cause.

The inspected installed session is
`G:\Unity\VRMMO\BC2VR-Launcher\data\runtime\sessions\20261021-114942-519-ab0ad2ad`.
Its `session.json` identifies release **0.1.12 / source checkpoint 434**,
source SHA-256
`2e693ca5bbe9e8e333e582675fc43dd74a001b241b896db42b8f3c672274c738`,
1920 x 1080 eye images, started 2026-10-21 11:49:42.887 UTC and ended
12:00:03.584 UTC. This session predates the scheduling/rig-read changes.
`native-trace.json` is empty; there is no final vehicle route or camera report.
The native helper exit code is zero. No new game session or capture was started
for this investigation and no logs were duplicated.

## Observed failure timeline

The surviving one-second `session-telemetry.jsonl` rows show a sustained halt in
image publication while the game renderer and native input sampling continued:

| UTC | Native world calls | Published pairs | Consumed pairs | Input samples |
| --- | ---: | ---: | ---: | ---: |
| 11:59:26.573 | 92,731 | 15,742 | 15,738 | 49,138 |
| 11:59:28.582 | 93,035 | 15,800 | 15,796 | 49,318 |
| 11:59:29.587 | 93,234 | 15,805 | 15,801 | 49,407 |
| 11:59:30.592 | 93,434 | 15,805 | 15,801 | 49,496 |
| 12:00:02.760 | 99,719 | 15,805 | 15,801 | 52,324 |

GPU failure stage, camera restoration failures and stereo-frame admission blocks
remain zero. These counters do not cover every view rejection, so zero does not
mean the stereo transaction completed.

The bounded `host.stdout.log` frame diagnostics show:

- Frame 51,723 / wall tick 450763390: fresh pair, world layer, valid tracking.
- Frame 51,774 / tick 450763953: no new pair; the retained image reaches
  255.564 ms and expires. Reasons 1536 are `no_new_pair | retained_age`.
- Frame 51,775 / tick 450763968: no retained pair and no world layer. Reasons
  16896 are `no_new_pair | no_retained_pair`.
- Later recorded frames remain focused, head/view/tracking-valid and outside
  menus, with no world layer. Input publication generally succeeds.

`xr-events.jsonl` has no focus/session-state loss near this transition. The last
earlier focus recovery was at wall tick 450225500, more than eight minutes before
the publication halt. Later reference changes at 450788843 and 450790031 did not
restore images. This supports native image starvation, rather than a headset
disconnect or the VR menu owning the display.

The scene journal only retained its initial epoch samples; it has no vehicle
owner snapshot at the failure time. The exact jeep profile admission is therefore
not proven by this session's available logs.

## Concrete code defect

The production launcher enables `--boat-head-aim` globally. In the vehicle Gather
path, `Bc2Gameplay.cpp` deliberately obtains PBL head-aim evidence only when the
seat is a verified PBL driver and **not** a `cachedCampaignVehicle`. Every Gather
clears `boatAimReady` first. A generic verified campaign driver/gunner therefore
publishes a supported, live seat-camera sample while correctly publishing no
PBL aim proof.

Before this correction, `AdjustViewBase` checked the global `boatHeadAim` flag
for every supported vehicle. It then demanded `boatAimReady` and called the
PBL-specific reader even for a generic campaign seat. Such a seat always failed
camera preparation. `NativeProbe::ApplyTrackedView` consequently cancelled the
extra eye, so no new complete pair reached the host. After the retained pair's
250 ms lifetime elapsed, the headset had no world layer.

This is a deterministic code-path defect and fits the observed publication
halt. The available logs cannot establish that this was the only cause of the
reported jeep failure.

## Narrow correction

`Bc2VehicleCameraPolicy.h` provides the exact camera-path selection used by the
runtime. The Gather thread publishes a camera mode with the seat sample under
`vehicleViewMutex`:

- `NativeSeat`: a verified campaign seat, or a PBL seat with head aiming disabled,
  uses the existing native-seat camera anchor.
- `PblHeadAim`: the verified PBL path additionally requires current PBL aim proof
  for the exact actor, actor generation, vehicle, seat and reference space. The
  runtime still rereads the verified PBL camera/weapon relationship before use.
- `Unavailable`: preserves the existing unsupported-seat policy. It does not
  create input authority or a verified vehicle profile.

Both supported modes retain the existing live-input deadline, current actor,
vehicle/seat, space, focus and head-tracking checks. A known supported seat with
missing/stale evidence still waits. Mode transitions reset the seat anchor;
temporary input gaps retain only identity, not an expired lease. No vehicle
input mapping, fire permission, interaction action or native camera validation
was relaxed.

The existing `Bc2VehicleRuntimeTests` gains one regression group covering generic
seat rendering without PBL proof, native-camera centering, PBL/campaign switches,
same-seat policy changes, invalid/stale owner/input, missing/wrong PBL proof and
temporary input loss. It exercises the actual selector used by `AdjustViewBase`.
Existing PBL and campaign native-profile tests remain applicable. Root owns the
combined build; this agent has not built or launched the candidate. No claim of
headset or actual jeep acceptance is made yet.

## Still unresolved

- The on-foot interaction-button failure has no surviving contextual-action
  snapshot. It could involve the mission script, range, native permissions or
  routing. Do not change the action binding or infer an entry permission from
  this camera fix.
- The left-eye destroyed-building artifact is separate. No image or retained
  native view-context evidence identifies its draw path in this session. The
  camera-mode fix makes no claim about culling, fracture geometry or that artifact.
- Add compact camera rejection/vehicle-mode counters to continuously flushed
  telemetry in a separate scoped change if further failures need diagnosis;
  this session's empty final native report concealed these distinctions.

## Authored SP01 entry gate and checkpoint

Read-only inspection on 2026-10-21 used the existing extracted mission graph:
`G:\Unity\VRMMO\bc2vr-recovery\headset391-investigation\campaign-coverage396\sp_01-selected.json`,
SHA-256 `a9fd060ca7ce0115e6eb8b1593a75d1f5579402f2c2cd8c6343f1edf46f368c1`.
Its `Levels/SP_01.dbx` digest is
`66869f99bf8409294bbe01f9df5b27dc7d18a54d8e3af488781a361300e51b47`.
These are authored definitions and connections, **not observed live gate state**:

- PLAYER jeep spawn `0a5bcfb2-4671-4776-a2b0-c3801cb3fdca`, named
  `KuroganeJeep 2  PLAYER`, starts behind enabled restriction
  `0d94fc23-e6b3-4c19-8773-d818a459ee17` (`EVRRestrictAll`).
- `count enemy kills at "steal jeeep"`
  (`c7f37057-3674-478c-8e01-702eac89ae26`) has trigger amount five and
  receives five authored soldiers' `OnKill` events. Its `OnTrigger` disables
  that restriction. `OnDisabled` enables
  `fceef4f4-e9c0-42b8-8578-9138faf10164`, `Position player in correct seat
  when enter`, with `EVRSeatSpecific` and seat index one.
- The separately named `Deny player to entry this jeep` entities target the
  FRIENDS jeep, not the PLAYER jeep; those names alone do not diagnose X.
- PLAYER jeep `OnPlayerEnter` calls Save on
  `aa6d9748-2149-4a2d-a6ed-e9f040be8aa0`, named `jeepRideStart`, with
  save filename `SAVE_4163368501`. Both `OnSaveDone` and `OnLoadDone` signal
  `on player enter jeep`. That sequence activates the ride input restriction,
  a streaming-aware fade to black, and entry animations. The ride permits
  yaw/pitch/roll/fire but blocks ChangeVehicle and ChangeEntry.

Consequently, entry before mission authorization and exit during the ride can
legitimately fail. The reported X failure has no corresponding live gate or
target-owner receipt, so this does not establish its cause or justify bypassing
native restrictions. A short authored entry fade also differs from the observed
33-second loss of new stereo pairs.

### Save availability remains unknown

A bounded read-only check of `C:\Users\outca\Documents\BFBC2` found the current
`GameSettings.bin` (168,868 bytes, SHA-256
`775166d2f605518b1c30d63b834fcf68a064d9d71357e10e1b3f79d2b3c819de`)
and `GameSettings.ini` (3,586 bytes, SHA-256
`d42bdf0c92988fc8cde520947dcc9fb9a6790593989b60fc8fa95625ad5452ec`).
Neither contains either checkpoint label in ASCII, UTF-16LE or UTF-16BE.
That negative string search is **not a decoded save inventory** and cannot
establish checkpoint absence. No new save parser was built and no saves or
settings were changed.

The retained reported session above has neither label in its JSON/JSONL/log
files. `bc2vr-recovery/knife430/game03/continue.jsonl` proves a native saved-body
Continue request, but records no checkpoint label. Its subsequent knife/guard
progression is not jeep-checkpoint evidence. The existing Continue helper reads
the native last-save entry and level, but its final receipt does not identify
the save filename. Actual availability of `jeepRideStart` therefore remains
**unknown**; do not represent the authored SaveEntity as an installed save.

On October 22, game18's live read-only menu inventory independently observed
two equal snapshots containing one valid `Levels/SP_01` entry, selected by
native Continue. Candidate adjacent fields did not have valid bounded string
shapes. This identifies the level only; it neither identifies `jeepRideStart`
nor proves its saved body is available. The observer sent no commands and
changed no game memory or save files. Evidence:
`performance436/onfoot-r11b/menu-save-inventory.json`.

## Minimum actual-jeep emulated check

### Existing campaign-gunner coverage, October 22

The prior fixture's boat-oriented naming is not evidence of a PBL seat. In
game20/onfoot05, the read-only seat witness and native report join the same
vehicle `872250400` and entry `872260672`: role 2, no driver consumer, one weapon
consumer, gunner action routes 6/5/8. With the normal global boat-head-aim option
enabled, there are zero PBL aim samples/views but 103 ordinary seat-camera views.
All 16 retained camera rows over 8.125 seconds have both-eye application.
An ordinary X pulse exits to the same infantry actor.

The exact game20 menu-manager/menu/entry addresses also match its Continue
helper, and that menu entry is labelled `Levels/SP_01`. This proves the selected
menu request, not an independently identified restored world or saved checkpoint.
No retained blueprint name or vehicle-model identity proves Kurogane or
`jeepRideStart`. The route fingerprint describes input mappings, not the model.
Evidence: `performance436/r12/existing-campaign-seat-evidence.json`.

R12 game21/onfoot06 repeats the generic campaign-gunner success without a
debugger: 119 ordinary seat-camera views, zero PBL aim views/samples, 16 retained
camera rows across 7.968 seconds with both eyes applied and planned/applied
matrices equal, zero route rejects or unsupported samples, and successful X
exit. The selected-mesh observer (not the static capability catalog) subsequently
identifies `Nambu100` at
`Objects/Weapons/Handheld/Beach/Nambu100/Nambu100`, with the same selected weapon
owner in 122 observations. These observations support current opening-campaign
coverage but still do not name the vehicle or prove the complete jeep ride.

The corrected `NativeSeat` path is therefore exercised in the actual game;
exact scripted-jeep identity, entry permission gating and full ride acceptance
remain the narrower outstanding checks below.

1. If the existing native Continue selection is confirmed to be the jeep
   checkpoint, reuse `bc2vr-recovery/faststart209-continue-candidate` to load it.
   Otherwise reach the same native scripted seat normally. The existing
   `knife430/RunKnife434.ps1` approach only reaches the first guards; it does not
   automate the scientist/jeep section. Do not set mission gates or spawn a
   substitute vehicle and call that opening-mission acceptance.
2. Once the real actor/vehicle/entry and campaign gunner profile are verified,
   reuse `NativeIpcProbe --controls-vehicle`: 240 pairs over 6.5 seconds, an
   initial 0.6 m/20-degree tracking offset, then 10 cm lean and return. Keep the
   normal `--boat-head-aim` option enabled to exercise the corrected selection.
   On a verified gunner-only profile its left-stick driver pulses have no driver
   mapping. Use `tools/audit_vehicle_camera.py` for the camera matrices;
   `audit_vehicle_fixture.py` additionally requires PBL driver throttle/steer
   evidence and is not an applicable jeep-gunner acceptance test.
3. Require fresh stereo publication beyond the 250 ms retained-image lifetime,
   both-eye native post-setter readback (`applied_mask=3`), correct lean/return,
   no exact camera restoration failure, and a clean detach with a nonempty final
   report. General seat-camera views should increase while PBL aim-camera views
   stay zero. This establishes an emulated real-game check, not headset comfort
   or complete ride acceptance.
4. Investigate X separately after native entry is authorized. The existing
   `--controls-use` pulse and `context_interact` journal distinguish a controller
   request, semantic Use, verified action-27/action-16 alias, cache commit, and
   actual owner transition. `GatherHook` currently requires on-foot movement/fire
   permission mask `0x103` before admitting actions including Use. A scripted
   phase allowing entry while denying those other actions could suppress VR X;
   this is an unverified gating possibility requiring the actual permissions and
   context record, not a demonstrated binding defect.

## Additional destroyed-building image — October 22

Read-only preparation identified two discriminating checks for a later affected
pair. `ScopedContextEye` corrects verified camera/context bindings, but the
projection hook deliberately skips derived adjustments during native Draw and
covers only known mesh/terrain callers. A separate fracture/decal projection
consumer is a coverage possibility, not an identified cause. Separately, the
prepared native draw data is passed through unchanged; no fracture-specific
instance-transform/history ownership proof exists yet. Comparing the wall's
ordered draw/instance payloads and shader camera constants between the two eyes
would distinguish these possibilities. Both eyes already use the same
conservative culling envelope, so this evidence does not justify a blanket LOD
or culling change. Any capture should target one positively affected pair and
include exact frame/ticket/world/request/view identities and instanced/indirect
draws. Existing first-frame non-instanced capture coverage is insufficient.

The user supplied another player's footage still at
`C:/Users/outca/AppData/Local/Temp/codex-clipboard-f877796e-5f8b-4cf5-a903-0c0c5e558364.png`.
It shows dark disconnected wall/brick-shaped pieces forming a partial upright
surface in the middle of a snowy street. The user associates the artifact with
destruction. The tester did not specify whether it appears in one eye or both.
The exact map, build, triggering destruction event, camera motion and original
video timing are unknown. The still is visual evidence; it does not establish
the actual draw's material, native entity, transform or occlusion state.

The user reconfirmed their own artifact was left-eye-only. The mod's desktop
mirror calls `CommitLeft` with eye 0 in `D3D11FrameBridge::PublishRestored`;
recording that game window can therefore capture the left eye. A headset or
runtime capture can select a different eye. The other player's capture path
is unknown, so their eye remains unspecified rather than inferred as right.

Investigate destructible/fracture-instance transforms, visibility lists and
per-eye camera constants as separate hypotheses. Capture the same native scene
before/after destruction and at two head orientations, with both eyes identified
and original draw ownership retained. Do not diagnose ordinary LOD alone or
disable destruction/unknown geometry as a blanket fix. The earlier user report
was explicitly left-eye-only; a shared root cause remains unproven. This image
does not change the current priority: performance, opening jeep, then
death/checkpoint recovery before the next headset request.

## Death fixture admission result — October 22

Game22/death02 used the frozen R12 normal runtime, with the separately recorded
`--player-grenades` opt-in. It exited the observed campaign seat through ordinary
X input but stopped before its shoulder squeeze or grenade. The fixture matched
one fresh hidden-weapon pair to canonical profile 31 (Nambu100), then rejected
the expired render lease before the next pair arrived. No draw, grenade, death
or restart was tested. This is a fixture freshness limitation, not a demonstrated
holster failure. Guard release and normal owned-game close passed.

The correction must distinguish a historical identity match from current action
authority: two independently fresh, consistent observations may straddle expiry
of the first lease, but expired evidence cannot authorize reaching or gripping.
The original deadline, exact actor/weapon identity, and one-attempt limit remain.

R13 game24/death03 subsequently stopped earlier, during startup after Continue
returned and restored its callback. The game exited before world evidence,
shoulder draw or grenade. `trace.stdout.log` records `probe_exit=3221225477`
(0xC0000005), while native-trace and stereo-progress files are empty. No matching
Application 1000/1001 event was found in the immediate read. The parent records
no normal close or explicit guard release because the game process was already
gone; do not report normal detach. This is a startup access-violation observation,
not a death/restart failure or proof of its cause. The same frozen runtime had
just completed game23 normally. Preserve this evidence and collect a bounded
exception context on a diagnostic attempt before attributing the fault.

The watched R13 retry (game25/death04/death-r1301) reached world rendering,
captured zero exceptions, detached the watcher and native adapter normally,
released the resolution guard and closed the owned game normally. This does
not explain the intermittent startup access violation and is excluded from
performance acceptance. Its ordinary X seat exit completed, but no shoulder
draw or grenade was sent: the controller's nested storage scan consumed about
6.9 seconds of its already-running action deadline, leaving 31.648 seconds
against a 34.5-second prerequisite. Move this admission before host startup
while retaining the absolute session/action bounds; do not call this a gameplay
failure or death/restart pass. Eight raw startup frames were losslessly
compressed after collection (248,037,376 bytes reclaimed, hashes preserved).

## R14 pause observation and health-baseline stop — October 22

Game27/pause01 sent one controller pause gesture and captured the released
2784x3024 menu quad after 33 projection frames. Eight fresh pairs and normal
cleanup/guard release/owned close passed. The retained preview was subsequently
reviewed: OBJECTIVES, OPTIONS, RESTART, RESUME GAME and EXIT GAME are visible.
No click or restart was attempted. Use this exact human-reviewed reference plus
a fresh pause epoch and current quad geometry for the next one-click probe;
do not label reference coordinates as live OCR.

Game28/death05 used the prepared storage admission before the action window,
retaining existing absolute bounds and the original fixture/witness binaries.
It stopped with `Independent positive health baseline unavailable; no grenade
command`. No grenade, health-zero transition, death fade or restart was tested.
Native cleanup, resolution-guard release and normal owned-game close passed.
Inspect the observer's evidence before changing it or running another attempt.

The retained health report then identified the exact precondition failure:
`Native proof mismatch: server_damage`, zero samples, exit 1 after about 94 ms.
The reader never reached actor/health resolution. It required all 2,021 original
bytes at 0x6c4c00 to match live memory, but the selected-knife feature deliberately
hooks that function. Its enabled/installed telemetry and source establish this
conflict; the exact modified live prefix was not captured. All original bytes
still match the pinned disk proof. The read-only health getter is independently
proved by live constructor/vtable/getter spans (`fld dword ptr[ecx+8]; ret` on
the embedded interface), typed client/server owners and lifetime checks.

A private reader revision may classify only server_damage as static evidence,
still require its exact disk proof, and preserve every relevant live health
read/ownership check. It must report the live damage consumer as unverified and
never treat a zero field alone as proof of actual death. Keep the working knife
enabled. This diagnosis does not resolve the reported death/restart defect.
