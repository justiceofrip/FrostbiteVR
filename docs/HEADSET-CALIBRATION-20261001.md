# Headset recovery and body alignment — October 1

The 05:40 headset candidate was rejected for body alignment. The user confirmed
stereo was working, but the head felt behind the body, the shoulders remained
coupled, and removing/replacing the headset or reconnecting Steam Link broke body
tracking. The SPAS firing animation also mismatched the hand. XM8 scope and
thumbstick switching worked, but physical body inventory is explicitly deferred.
Death-crash investigation and LOD/render polish remain deferred.

## Implemented repair

OpenXR now distinguishes tracked head poses from merely valid stale poses.
Sustained focus/tracking loss (750 ms) triggers one automatic upright recenter
when tracked focus returns. A 1500 ms frame-delivery gap covers runtimes that
stop frames instead of reporting loss. Eyes, hands and the body receive the same
new reference generation; old presentations and pending work are invalidated.
Brief loss does not recenter. Manual recenter is **hold both thumbstick clicks
for one second**, after releasing the sticks on recovery. A completely destroyed
runtime session still ends the host and requires another launch.

BC2 input pauses no longer create a new rig-owner generation. Only an actual
actor change does. The verified native soldier camera supplies eye/stance height;
the collision actor supplies horizontal position. Rendering and hand placement
use this same base, with consumed roomscale movement subtracted once. Wrist
positions now come directly from tracked grip positions relative to the head
reference, rather than arbitrary controller positions at calibration.

Shoulder landmarks come from BC2's validated inverse bind data. A shared helper
recovers their anatomical axes, so native weapon aiming cannot shift either
shoulder anchor. The common torso ancestor is stabilized in private palettes
when both hands are available; single-hand loss retains the missing arm's native
branch. Gun attachment to the tracked wrist is cached through recoil/pump,
while authored child animation is retained. A late-returning gun hand cannot
reuse a prior actor's torso or weapon attachment.

Native SPAS reload exposed another cause of animation fallback: BC2 hides the
named leaf jntWpn_7 using a 1e-4 diagonal basis in all three native palettes.
The BC2 reader recognizes only that observed finite pattern on a leaf directly
under the validated weapon root. Its native bytes are preserved exactly;
unrecognized malformed transforms remain rejected. Shared subtree retargeting
can preserve explicit leaves, and arm IK validates only its actual arm branches.
No new native hook, executable offset, animation-source write or game file edit
was introduced.

## Native and offline evidence

All checks below use the running campaign with synthetic tracked input, not a
headset. Source/binary/evidence checkpoint:
reports/headset-recovery-20261001/checkpoint.json.
Machine-readable results: reports/headset-recovery-20261001.json.

- Final reconnect host: native-xr-recovery-20261001-062716, native trace
  native-trace-20261001-062716-764. 397 fresh pairs, zero timeouts; one automatic
  and one manual recenter, three coherent reference generations, one stable
  actor generation. Maximum wrist residual 0.161 mm. Two pending pairs were
  intentionally discarded at reference transitions. Zero native-state errors.
- Final SPAS reload/fire: native-trace-20261001-062440-468 and
  native-ipc-20261001-062440. 240 pairs, zero timeouts, two matching client/server
  shot events, 369 tracked poses preserving hidden geometry, zero rig rejections
  or source/packing/fallback errors. Gun-to-wrist offset drift 0.107 mm.
- Final roomscale: native-trace-20261001-062636-490 and
  native-ipc-20261001-062636. 240 pairs, zero timeouts; 28.3 cm of actual collision
  movement consumed from the scripted step, zero rig rejections, maximum wrist
  residual 0.137 mm. Independent right-hand loss also preserves native fallback.
- Earlier aim sweep in this repair sequence:
  native-trace-20261001-061646-674. Native shoulders moved 30–38 cm while wrist
  residual stayed below 0.144 mm, with one calibration and zero rig rejections.
  All 240 pairs arrived; one startup request timed out. This predates the final
  hidden-leaf and partial-hand initialization changes.
- x86 34/34 and x64 33/33 suites pass, including targeted hidden-leaf, absolute
  wrist placement, recoil attachment and partial-hand initialization regressions.
  Executable discovery rejects 80 mutations.
- Four actual-host test-runtime scenarios pass: focus loss, valid-but-untracked
  head, brief dropout and a stalled frame stream. Each also exercises manual
  recenter. Original GPU presentation/controller lifecycle tests pass.
- Final native checks detach their hooks and exception observers, leave BC2
  responding, and record no exceptions or crash report. A test-harness attempt
  to start a second observer before the first detached was aborted; no game
  crash occurred. The harness now waits for detachment before completing.

The initial shared-origin attempt had an 8.5 cm wrist reach error; bind anatomy
removed it. Initial SPAS runs had hundreds of rig rejections during hidden-leaf
reload; the final corrected run has zero. Those rejected diagnostics remain
available and are not treated as accepted builds.

## Headset acceptance still required

The corrected build has not been viewed in the headset. Confirm camera/body
placement, free-hand shoulder appearance, removal/replacement or Steam Link
reconnect, manual recenter, then SPAS grip during firing/reload. Grip feel and
projectile-impact behavior remain unverified. No physical weapon grabbing,
further thumbstick feature work, death diagnostics or LOD polish was added.

Use the real-runtime launcher Start-BC2VRSession.ps1 -TwoHandGrip for the next
user-ready test; keep it continuous until the user closes it. Never use the
explicit test-only XR DLL for a headset session. Keep BC2 on the left monitor,
and never activate it or send global desktop input during background work.
