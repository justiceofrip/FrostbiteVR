# Hand transitions and physical torso — September 30, 2026

This increment has native campaign and offline verification; no headset retest.
Death-crash investigation and LOD polish are deferred at the user's request.

## Changes

Each hand now loses and reacquires tracking independently. The missing arm uses
native animation while the other remains tracked. Actions also recover per hand:
a held trigger cannot fire on tracking recovery until released. Weapon switches
preserve anatomical calibration; actor, skeleton and reference-space changes reset it.

The right stick selects weapons with a vertical flick. Each flick sends one signed
native selection command. BC2 uses per-slot candidate tables, and a command can
retain the current weapon; this is not a universal inventory clamp or wrap policy.
Native vehicle exit
uses its verified ChangeVehicle action, independently from on-foot interaction.

TrackedBodyFrame removes native horizontal reload-root sway using the verified
character position while preserving native stance height. PhysicalTorsoFrame uses
physical head translation to anchor shoulders even when the collision body trails
roomscale movement. Head orientation does not rotate the torso. Native simulation
and animation sources remain untouched; private pose copies supply render output.

## Recorded verification

All three final runs delivered 240/240 eye pairs, clean hook shutdown and 15-second
post-cleanup game survival, without GPU, source, packing or restoration failures.

| Check | Native report | Evidence |
| --- | --- | --- |
| Independent tracking and action recovery | native-trace-20261001-024026-006 | 1563 poses, 402 partial; 15 fire samples with left hand absent; 120 held-trigger recovery samples suppressed |
| Weapon cycle and reload | native-trace-20261001-030650-167 | A to B to A, exactly one next/previous command; 14.33 cm native root sway with at most 1 micrometre horizontal target drift relative to actor |
| Physical torso and roomscale | native-trace-20261001-033048-474 | 1497 poses, 78 partial; 28.27 cm collision-body consumption of 40 cm step; maximum wrist residual 0.136 mm, zero reach clamp; shoulders within 1.5 micrometres of physical torso anchors |

Per-hand calibration counts remained [1,1]. Earlier roomscale candidates exposed
shoulders lagging the physical torso and are superseded by 033048-474.

Reproduce the recorded-evidence checks without touching the game:

```powershell
python tools/check_hand_transitions.py --recovery reports/native-trace-20261001-024026-006 --switch reports/native-trace-20261001-030650-167 --roomscale reports/native-trace-20261001-033048-474
```

Summary: reports/hand-transitions-20261001.json. Both build suites pass (31 x86,
30 x64); actual executable discovery rejects 59 mutated binding candidates.
Physical crouch changes hand/shoulder targets, not the native vertical collider.
Two-hand support and final headset feel remain open. The later translated muzzle
pilot is documented in FIRING-ORIGIN-20260930.md.
