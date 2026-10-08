# Explicit two-hand support - October 1, 2026

Status: implemented as opt-in BC2 campaign support, with native grip/release checks
on XM8_sp_s and SPAS12_sp and combined supported firing on XM8. This increment has
not been tested in a headset. Death-crash and LOD work remain deferred.

## Behavior

Run Start-BC2VRSession.ps1 -TwoHandGrip (or the bounded Start-BC2VRTest.ps1 equivalent).
It includes MuzzleFire, Hands, BodyFollow, MotionAim and Controllers. Existing
launch modes retain their behavior.

The left grip engages support near the rendered gun's authored support-hand
position. The current two-hand direction is captured on engagement, avoiding an
aim snap. Subsequent relative hand movement steers both the right grip orientation
and firing aim together. Releasing returns raw one-hand aim immediately. Neither
hand's position, the off-hand orientation, the head, nor any action value is changed
by this policy. The free hand therefore stays independent.

Squeeze must first be neutral (at most 0.35), then reach 0.7 to grab within 16 cm.
A held grip breaks beyond 28 cm, on lost tracking/focus, invalid ownership, an
equipped item/reference-space/scale transition, or cancellation. Recovery requires
neutral input before grabbing again. Duplicate packets cannot create a new grab.
BC2 cancels on semantic reload, use and weapon-selection actions. Native automatic
reload and unusual animation transitions still need broader acceptance.

## Framework and BC2 boundary

SupportGrip in the shared interaction module contains only input, ownership,
contact and quaternion policy. It has no native addresses, bone indices, engine
types or timing APIs. Minimum-rotation alignment preserves gun-hand roll; crossed
hands beyond the supported angular range release instead of selecting an arbitrary
180-degree rotation axis.

Bc2RigPublication derives contact from the named/validated native left wrist and
evaluated weapon root. The native wrist's local weapon transform is placed on the
tracked weapon and compared with the independently calibrated left target. The
adapter publishes the calibrated-minus-raw wrist separation in reference-space
metres alongside the existing immutable muzzle pose. Original deadlines and
actor/weak/owner-generation/weapon/space checks govern reads.

Bc2Gameplay applies support to a private copy of raw XR input before native aim and
rig publication. Cached raw input remains unchanged. The existing native setters
receive the assisted aim; the existing private palette path renders the assisted
gun. MuzzleFire uses the same tracked gun with the already verified per-shot
client/server position latch. Native animation, matrix inputs and projectile
configuration stay untouched. No simulation is replayed.

Native flag 0x100000 requires muzzle firing and hand publication. Unsupported
firearm assets cannot engage support. Right-hand gun ownership is currently fixed;
handedness switching, fingers, physical inventory and physical reload are not part
of this increment.

## Evidence

- Shared/native builds: 34 x86 and 33 x64 suites pass. Support regressions cover
  neutral rearm, grab/release hysteresis, duplicate packets, tracking loss,
  malformed contact, owner/equip/space/scale/gap resets, cancellation, reference
  yaw equivariance, calibrated offsets and exact off-hand/position preservation.
- Executable discovery: 80 signature mutations rejected. No new native hook or
  offset was required for support.
- Bounded XM8: native-trace-20261001-052503-579 /
  native-ipc-20261001-052502. Three grabs/releases, 709 held samples, about 10.16
  degrees of correction, no input/source/packing/restoration failures. All 240
  pairs arrived; one asynchronous request timed out. No shots in this sequence.
- Full XR host: native-trace-20261001-052909-276 /
  native-xr-support-20261001-052908. Actual BC2XrHost with explicit
  FvrNativeSupportTestXr.dll, never SteamVR: 495 fresh pairs, 1007 presented frames
  including 512 reuses, 151 blank frames including startup, zero request timeouts.
  Four grabs/releases, 1077 assisted aim samples, 298 supported fire-input samples.
  Maximum recorded native-vs-assisted aim residual was zero.
- The full-host run made 11 client and 22 server muzzle writes, 17 after the
  bounded shot log filled; zero origin fallback/write/source errors. Four recorded
  client/server event groups shared the exact latched muzzle. These callback
  counts are not a physical bullet count. Tracking loss suppressed attempted fire.
- SPAS-12: native-trace-20261001-053235-915 /
  native-ipc-20261001-053235. One native positive primary-selection pulse selected
  the shotgun (weapon_kind 1). Three grabs/releases, 721 held samples, about 11.43
  degrees of correction, 240 pairs, zero timeouts or native aim residual. No firing
  in this support sequence; SPAS muzzle firing was verified separately in the
  preceding muzzle checkpoint.
- All three exception watchers recorded zero exceptions and detached while BC2
  remained alive. Native hooks retired cleanly and the 15-second post-test
  survival checks passed. BC2 stayed on the left monitor, without desktop focus or
  global keyboard/mouse injection.
- Original GPU/OpenXR regression fixture still passes: 100 frames, 13 fresh, 53
  reused, 34 intentionally blank, 2 deliberately rejected, zero validation errors;
  lifecycle, predicted input, scale and reference-space fallbacks pass.

The last source change after the full-host run adds only weapon-kind telemetry to
gameplay and a bounded receiver selection option. The final binary then passed the
SPAS native run. Exact payload hashes are retained in each native manifest.

Replay evidence validation with tools/check_support_grip.py --host <host-report>,
or --native <native-report> --receiver <receiver-report> [--weapon-kind 1].
Test-NativeXrMuzzle.ps1 -Python <python-path> -TwoHandGrip runs the combined explicit
fixture after its read-only campaign loadout/ammunition check. Test-NativeStream
-ControlsSupportGrip selects the bounded sequence; -SupportPrimaryDirection 1
adds the verified selection pulse from the inspected XM8 loadout.

Summary: reports/two-hand-support-20261001.json.
Checkpoint: reports/two-hand-support-20261001/checkpoint.json.
Previous muzzle and accepted rendering/controller checkpoints remain intact.

Headset comfort, grip reach/radii across users, recoil feel, rapid motion and
downstream projectile impacts still require acceptance. These native and test-only
runtime results do not claim a new headset pass.
