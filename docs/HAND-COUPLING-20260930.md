# September 30 evening: off-hand coupling repair

The first hand headset test reported that moving the gun hand dragged the other
hand. A deterministic native campaign probe reproduced a specific cause: rotating
the right aim while holding both grip positions fixed rotates BOTH native shoulder
origins. The left wrist's independent target stayed put, but native shoulder motion
pushed it past arm reach. The solver then clamped the left wrist toward the gun.

## Repair and framework boundary

Shared TrackedRig now calibrates shoulder origins and elbow pole directions in the
upright body frame, together with the existing independent wrist calibration.
ArmIk accepts optional shoulder origins and keeps lengths from the current native
evaluated animation, carrying finger/twist descendants through the solved segment
transforms. BC2 supplies its own evaluated rig and locomotion/roomscale-compensated
body base. Original native animation buffers remain untouched. No native Refractor
layout or bone index was transferred. Body anchors reset on the same actor/equip/
skeleton/reference-space transitions as wrist calibration.

This intentionally removes gun-driven shoulder/pole motion from tracked arms.
Full torso anatomy, physical two-hand grip policy, stance fit and reload/equip feel
still need work. Losing either tracked grip still falls both arms back to native
animation; that separate limitation is not claimed fixed. The LOD issue remains
deferred at the user's request.

## Reproduction and validation

Both grip positions fixed; right aim/grip yaw sweeps to +2.1, then -2.1 radians.
Units below use the current one-world-unit-per-metre calibration.

| Metric | Before | After |
| --- | ---: | ---: |
| Left native shoulder excursion | 38.72 cm | 38.03 cm |
| Left requested wrist excursion | 8.43 mm | 0.114 mm |
| Left solved wrist excursion | 24.57 cm | 0.197 mm |
| Maximum left reach error | 24.07 cm | 0 |
| Maximum solved-vs-requested left wrist error | about 24 cm | 0.137 mm |

Baseline: native-trace-20261001-020043-420 / native-ipc-20261001-020043.
Repaired: native-trace-20261001-021129-253 / native-ipc-20261001-021128.
Both delivered 240/240 stereo pairs. Repaired evidence contains 75 samples,
1622 placed poses, 3244 modified palette copies, one calibration, no tracking,
packing, source-change, GPU or camera-restoration errors. The game remained
responsive through shutdown and 15 seconds afterward. No headset was used.

The reusable tools/check_hand_independence.py checks stationary grip inputs,
sufficient native shoulder movement, wrist error below 1 mm, calibration and
publication errors. Its report is reports/hand-independence-comparison-20261001.json.
Deterministic tests rotate the native rig over 49 aim angles: old shoulder origins
reproduce coupling, while body anchors preserve both wrist targets, native segment
lengths and finger-relative transforms. Builds pass 31 x86 and 30 x64 suites.
Actual executable discovery rejects 52 mutations, including the grenade enum.

Final combined hands/aim/roomscale run: native-trace-20261001-022411-521 /
native-ipc-20261001-022411. 240/240 pairs, 1515 placed poses, 3030 modified copies,
zero unexpected tracking/source/packing failures. The scripted tracking dropout
produced 81 expected unavailable samples and then recovered. A 40 cm roomscale
step consumed 28.14 cm of actual native movement. No crash or exception observed.
Exact metrics: reports/hand-repair-summary-20261001.json.

## Death investigation: still unresolved

A bounded -DeathProbe diagnostic uses verified EntryInputActionEnum action 38,
EiaThrowGrenade, at preferred metadata VA 0x1c27fe8. It passes the existing native
boolean input path and ownership/action-eligibility checks. It is explicitly
rejected for continuous sessions. Synthetic right squeeze requests it only when
this diagnostic is enabled; normal XR controller mappings do not throw grenades.
No health, actor lifetime or game settings were patched.

Single blasts with/without hand overrides and a two-blast sequence were observed
in campaign, each with 240 stereo pairs, clean hook shutdown and zero captured
exceptions. None killed the player at this dock, so none exercises death/respawn.
The old null-EIP crash after barrel death remains unresolved; do not report these
runs as a death fix. Reports: native-trace-20261001-021638-086, -021852-335,
-022201-507, plus matching exception-watch-stream reports. The grenades used in
these diagnostics are spent in the current campaign state.

## Desktop behavior

At the user's request BC2 is on the LEFT monitor, exact rectangle
[-1920,0,0,1080], borderless client 1920x1080. Placement and all later diagnostics
preserved the foreground app. The game_window.py helper now supports --left-monitor
without activation; mouse/keyboard operations require explicit --activate.
Do not use that activation option while the user is using the right monitor.
BC2 is left running; do not minimize it or start SteamVR without current need.


## October 1 video follow-up: arm distortion during turning

User reports the shoulder does not follow snap turning in a supplied BC2 capture.
At approximately17.5–21s the support hand stays on the weapon, while the visible
arm/sleeve stretches into a thin loop. This is distinct from support grip release.
The single captured view does not identify raw stick input or split real head/
body yaw from virtual snap yaw, so the specific transform/skinning cause is open.
No IK behavior was changed from the clip alone. Regressions should isolate pure
virtual snap invariance, physical torso yaw, and camera/rig publication coherence
before adjusting accepted wrist, grip or firing alignment. Exact video hash and
observations: ../reports/native-xr-session-20261001-153359-592/video-shoulder-observation.json.
