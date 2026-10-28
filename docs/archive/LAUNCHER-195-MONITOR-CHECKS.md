# Launcher checks 194–195 — October 7, 2026

The scoped XM8 launcher passed bounded campaign checks with `MagazineReload`,
`SightFlip`, and `PoseObserve` enabled. These checks used scripted controllers and
the actual native renderer. They did not use a headset or body inventory.

| Check | Result |
| --- | --- |
| Rifle → launcher → rifle | Two physical sight requests and two native acknowledgements; no cancellation |
| Launcher → rifle → launcher | Two physical sight requests and two native acknowledgements; no cancellation |
| Launcher support grip | Attached and independent free-hand samples in both runs; maximum attached error 0.331 mm |
| Fixed sight grasp | Continuous opening/closing; maximum resolved palm error 0.227 mm; no preview fallback |
| Native launcher firing and stock refill | Ammunition 1/7 → 1/6, with native firing callbacks and a full chamber afterward |
| Rifle reload interference | No magazine transaction, detached-magazine grab, or unintended equipment cycle in these runs |
| Stereo delivery | 240 pairs per bounded run; no asynchronous timeout or native camera/GPU restoration failure |

The launcher firing fixture requests reload while initially full, then supplies
two tracked trigger pulses and a later untracked negative pulse. One round was
consumed and stock refill was observed. This does not claim two shots fired,
projectile impact validation, or translated grenade muzzle support.

## Changes that make these checks repeatable

The old synthetic sight reach used a wrist origin. Current interaction uses the
bind-derived thumb/index mechanism point; the old reach missed it by 111 mm.
`SightGestureFixtureGeometry.h` now inverts separately captured primary and
launcher wrist mappings plus that mechanism point. Both hands receive the same
10 cm translation toward the body, preserving weapon-local contact while keeping
the scripted closing motion within arm reach. Receiver versions are 8 and 9.
These constants belong to this static diagnostic, not runtime tracking policy.

The preview audit now checks hinge rotation against the captured zero-angle
grasp basis. The native animation can change after an acknowledged weapon-mode
switch while the physical grasp remains fixed. A native subitem handover requires
the same physical gun, owner and space, exact supported configuration and named
rig evidence, plus the matching acknowledged gesture and native selection action.
An initial zero-angle record must belong to the exact gesture episode; logging
can legitimately skip more than one input generation. Numeric limits are unchanged.

The original overextended fixture remains a negative result: 21.9 mm of arm-reach
clamping still fails the palm audit. It was not relabeled as a passing native run.

The actual magazine consumer also has regression coverage for switching native
subitems during removal, replacement holding and submitted insertion. It must
cancel the old transaction, release its claim, and wait for the exact old cycle
to retire before ordinary launcher fire/reload actions continue.

## Validation and remaining scope

Both complete x86/x64 builds pass all 186 suites. The receiver geometry regression
is included in `SightFlipPacketTests`; ten self-contained Python audit tests pass.
The game DLL is unchanged from render-lifetime193:
`48e6c3c42bd565dec20b0a4e6cd0da3619a11b1aed393a69e9b288741476216d`.

Local evidence is in `bc2vr-recovery/pipeline-runtime-20261005/launcher195-evidence/`
and `root-monitor/{sight195-02,sight195-secondary-01,launcher-fire195-01}`.
The previous render193 manual-magazine fix remains the headset candidate.
Headset feel, dynamic aiming, body-inventory combinations and projectile impacts
remain unverified by this batch. The AEK/GP30 horizontal sight remains disabled;
these XM8 checks do not establish its native activation. Underbarrel manual reload
remains deferred, with stock reload retained.
