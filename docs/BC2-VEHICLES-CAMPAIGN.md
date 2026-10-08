## October 3: vehicle aiming reticle follow-up

The user requests the missing boat reticle and the same capability for other
armed vehicles. Boat camera, stick control and firing have positive user feedback;
the aiming reticle is unfinished. Root owns this work while the three agents
continue their current reload/optic assignments.

Use the actual mounted weapon's aim or verified native sight line. Head aim is a
target sent through native input: turret response can lag or reach its mechanical
limit, so a dot fixed at HMD center would misrepresent aim. The current PBL driver
snapshot supplies exact selected seat/weapon, hull/camera and joint angles, but
does not establish a muzzle ray, native impact target or reticle render producer.
Projectile drop/impact prediction is separate from an aiming-direction indicator.

The shared presentation must be stable in both eyes, follow native aim while
driving, and carry current seat/weapon/space and original source/pair identity.
Hide on foot, in unarmed/unsupported seats, in menus, or when the current source
is lost. Seat changes, exit, recenter and reconnect must retire old data. Preserve
accepted boat steering/camera/fire. Extend through measured per-seat bindings to
other armed vehicles; PBL control axes are not a universal vehicle profile.

Acceptance: actual gun response and yaw/pitch limits versus reticle, both-eye
alignment, driving/recenter/reconnect, enter/exit/seat switching and no infantry
crosshair. Current empty-XM8/shoulder test binaries remain unchanged. No vehicle
reticle implementation or native/headset result is claimed by this requirement.
Private source/hash audit: `bc2vr-recovery/vehicle-reticle-120602-requirements.json`.

Historical vehicle checkpoints follow.

# BC2 campaign vehicle controls — October 1, 2026

The next vehicle increment is ordinary thumbstick and button control through the
existing native input-gather boundary. No physical cockpit, steering-wheel
interaction, vehicle IK or vehicle simulation replacement is required for this
candidate. Campaign vehicle controls are not enabled by this work.

## Implemented candidate

`src/games/bc2/Bc2VehicleInput.h/.cpp` adds
`BuildVehicleInputPlan(profile, seatSnapshot, command, constCache)`.
It returns bounded expected-before/after byte edits without writing a buffer,
calling the game, sending input or enabling hooks. `Bc2VehicleInputTests.cpp`
checks native permission preservation, fixed gunner and driver roles, exit-only
bit ownership, exact identity, unknown/malformed profiles and nonfinite input.

A seat profile explicitly binds five semantic axes: throttle, steering, look yaw,
look pitch and fire. It carries an observed route fingerprint and a classified
Driver, Gunner or DriverGunner role. Unclassified seats fail closed. Exact actor,
actor generation, controlled entity, seat generation, entry, router and input
cache must match the command. Replacing a seat or controlled entity therefore
cannot retain an old vehicle command.

The planner supports native float actions only where their individual gather
permission bit is set. It preserves every unsupported action and all low-eleven
permission bits. An optional verified exit binding owns only ChangeVehicle16;
other boolean actions remain native. Fire is unsigned [0,1]; movement/look axes
are signed [-1,1]. Weapon cycling, brake/handbrake, seat switching, alternative
fire and aircraft control are outside this first planner. Those can be added as
separate explicit bindings when a campaign seat needs them.

The native adapter still needs to own original input deadlines, focus, menu and
neutral recovery, verify current seat before commit, check every expected-before
word, and commit after original gather. This helper is intentionally not an
operator that can force permission bits or mutate a process.

## Static native evidence

Inspected installed file:
`D:\Games\Battlefield Bad Company 2\BFBC2Game.exe`.
SHA-256 `3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
Preferred image base `0x00400000`. Addresses below are preferred virtual addresses,
not live pointers or permission to reuse this layout on another executable.

Existing `DiscoverInputBinding` in `Bc2Profile.cpp` already validates the cache
setters, local-player update/gather linkage, ABI and a subset of entry-action
metadata. Static inspection for this task recovered additional vehicle metadata.
A production vehicle binding must validate those additional enum names/values and
its seat mapping too; an executable hash alone is insufficient.

| Native boundary | Observed evidence |
| --- | --- |
| `0x0062EEE0`, input gather | Loops all49 Entry actions. Resolves each action through the current router hash. Missing mapping becomes ConceptUndefined76. Queries native availability and records float permissions in cache+0x98 low11 bits. Disabled float actions become zero. Dispatches native float/bool setters. |
| `0x0061C7E0`, float setter | Clamps, uses scalar action mask0x7FF and signed mask0x6F3; writes cache+8+4*action. Unsigned actions are absolute-valued. Do not linear-disassemble embedded jump-skipped data as instructions. |
| `0x00612A00`, bool setter | Writes native action bits at cache+0x98/+0x9C; existing discovery verifies offsets and return8 ABI. |
| `0x0062ED80`, router constructor | Stores vtable0x0141C8E8; builds its hash from this entry's configured action mappings. Router+0x14 is bucket storage and+0x18 bucket count; linked nodes contain action key+0, mapped Concept+4, next+8. This is per-entry data. |
| `0x0081AC70`, player input update | Chooses attached entry using player+0xC60/+0xC64, or controlled entity and+0xC6C. Resolves the entry and its router, passes player+0xC74 cache to router vtable slot3. Later native processing consumes the committed input. |
| `0x007E6003`/`0x007E6059` | Entry construction calls the router constructor and stores the result at entry+0x198. The two paths differ; do not assume every scripted seat has the ordinary configured map. |
| `0x01C27C58` | EntryInputActionEnum metadata table,24-byte records; names and integer values recovered below. |
| `0x01C2F140` | Concept metadata begins at ConceptMoveFB0. Concept identifiers are separate from Entry action identifiers. |

Relevant Entry actions are:

| Action | ID | Stored value |
| --- | ---: | --- |
| Throttle / Strafe | 0 / 1 | Signed float |
| Brake / HandBrake | 2 / 3 | Unsigned float |
| Yaw / Pitch / Roll | 4 / 5 / 6 | Signed float |
| SwitchPrimaryWeapon | 7 | Signed float; excluded from minimal vehicle planner |
| Fire | 8 | Unsigned float |
| CameraPitch / CameraYaw | 9 / 10 | Signed float |
| FireCountermeasure / AltFire | 11 / 12 | Boolean |
| Zoom | 14 | Boolean |
| ChangeVehicle / ChangeEntry | 16 / 17 | Boolean |
| SelectEntry0 through7 | 18 through25 | Boolean |
| Interact / Reload / ToggleCamera | 27 / 29 / 30 | Boolean |

ConceptMoveFB0, MoveLR1, Yaw6, Pitch7, Roll8, Fire9, AltFire10,
ChangeVehicle36, Brake37, HandBrake38, NextPosition42 and CameraPitch51/
CameraYaw52 are distinct metadata values. These names do not establish the
actual Entry-to-Concept mapping of a specific vehicle. In particular, steering
must not automatically be mapped to EntryYaw4, nor turret aiming to CameraYaw10.
The per-seat map and native consumer must decide.

## Current runtime boundary and preservation

`Bc2Gameplay::Resolve` already distinguishes attached and controlled entities,
checks the selected entry/router/cache and rereads local ownership. The non-foot
branch currently clears infantry support/sight/IK and roomscale, stages only the
verified native exit action, and returns before infantry aiming or controller
locomotion. This is why basic vehicle stick/fire support is absent today.

Its existing `(allowed & 0x11) == 0x11` vehicle eligibility test assumes both
Throttle0 and Yaw4. That condition must not become a generic gunner-seat gate:
a fixed turret can expose only aiming and fire. The new planner uses each mapped
axis's actual permission bit instead. Native menu/cutscene eligibility remains
an independent required check; all axes being mapped is not proof of playing.

Keep the native vehicle/camera simulation authoritative. Continue suspending
on-foot collision follow, hand/weapon IK, support attachment and rifle shot
translation while seated. Headset stereo can use the native seat camera, but
seat transitions, camera limits, head tracking, recenter and scripted-camera
handoff require their own native/headset validation. Do not route turret sticks
through the infantry snap-turn or right-stick weapon-cycle policy.

## First practical capture and integration

1. From the current campaign, use an accessible boat or mounted gun seat with
   the normal native interaction. Root owns any game input. Capture on-foot,
   entry, seated neutral and exit states, with exact local actor/weak owner,
   controlled entity/type, seat index, entry/router/cache identity, generation,
   input permissions, active native camera type/owner, and menu state.
2. Bound the router hash walk to49 valid action keys and a small verified bucket
   limit. Reject cycles, duplicate keys, out-of-range Concepts, unreadable nodes,
   identity changes and changed snapshots. Retain configured Entry-to-Concept
   mappings and native consumer evidence. Capture independent short ordinary
   forward/back, steering, look yaw/pitch and fire inputs. Do not combine motion
   and shooting during the first identification step.
3. Build the first exact seat profile. A gunner needs look yaw/pitch+fire and,
   when native rules permit it, exit. A driver additionally needs throttle and
   steering. Use left stick for vehicle-relative motion and right stick for
   continuous look/turret input; right trigger fires. On-foot Interact27 remains
   the existing entry request; seated Use requests ChangeVehicle16. No head-yaw
   rotation of the driver's throttle/steering axes. Tune axis sign/rate only from
   observed native response, keeping native turret limits and damping.
4. Add original-deadline, neutral-armed controller sampling for this seat; feed
   it to the new planner at the existing gather boundary. Revalidate current
   ownership and source words, commit the command for the native tick, and let
   the next original gather rebuild input on disconnect/stale tracking. Avoid
   global keyboard/mouse injection or foreground changes.
5. Validate aim/fire separately from camera stereo, then entry/exit, paused menus,
   focus loss, controller disconnect, seat change, death and checkpoint reload.
   Save native restrictions and unchanged infantry regression evidence.

Campaign coverage should be recorded by encountered checkpoint and actual seat:
scripted boat/passenger gun, fixed gun, drivable ground vehicle and any airborne
seat encountered. A helicopter's presence does not prove a player-pilot section;
an on-rails gunner is a different profile from a flight controller. Aircraft
pitch/roll/yaw/collective and mission-specific interactions remain unclassified
until captured. Do not claim all campaign missions covered from one boat test.

The next runnable candidate is one verified gunner/driver seat through the current
input cache, with the rest failing closed. Broader vehicle data can then use the
same planner and batched seat profiles. This is independent of the ongoing native
menu work and preserves the accepted infantry hand/weapon checkpoint.
