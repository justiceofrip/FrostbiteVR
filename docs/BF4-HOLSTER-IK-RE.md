# BF4 holsters and arm IK (roadmap M4, M6): findings so far

Evidence level: **static analysis only** of the decrypted research image of bf4.exe build
179547, 2026-10-19 (`tools/bf4probe/bf4_reflection.py`, `bf4_xrefs.py`). Nothing has run.

## Empty hands: the game can hide the first-person weapon itself

BC2 needed native mesh hide/show work for holsters. BF4's animation interface has a
first-person-only weapon binding with a hide signal:

| Class (reflected) | Field (AntRef, 0x14 bytes each) | Offset |
| --- | --- | --- |
| Animated1pOnlyWeaponBinding | UndeployFinished | +0x00 |
| | CameraFreeWeight | +0x14 |
| | Deploy1P | +0x28 |
| | **HideWeapon1p** | +0x3C |

The general `AnimatedWeaponBinding` (72 signals) adds `LowerGun`, `GunDown`, `Deploy`,
`Undeploy`, `QuickSwitch` and the reload/bolt/pump signals listed in BF4-MANUAL-RELOAD.md.

`AntRef` itself is `{Guid AssetGuid; Int32 ProjectId}` (0x14 bytes, reflection): a reference to
an animation asset by GUID, not a value. Driving a signal therefore goes through the game's
own lookup and setter for the character's animation instance, not a field write.

Holster plan, to verify live: while a gun is holstered in VR, hold `HideWeapon1p` for the
player's first-person weapon (a per-frame temporary override, restored exactly) and hold
the trigger at zero through the virtual pad (already possible: the reload driver does the
same). Drawing releases both. Weapon selection itself goes through the game's own switch
input (pad), so ammunition and slot state stay native. Unknowns: how an AntRef maps to a
runtime parameter slot, and where the player's 1P binding instance lives (the census
ownership walk is heading there).

## Arm IK: the character IK scene op already has hand IK inputs

Function `+0xCEC70` registers the parameters of the character's IK / level-of-detail scene
operation (`LevelOfDetailSceneOp`):

- inputs: `InEnableFootPlanting`, `InEnableLeftHandIK`, `InEnableRightHandIK`,
  `InLeftHandIKReach`, `InRightHandIKReach`, `InDistanceFromViewer`, `InDisablePoseUpdate`,
  `InDisableControllerUpdate`, `InMovementSpeed`, `InHideWeapon`;
- outputs: `OutLeftHandIKEnable`, `OutRightHandIKEnable`, `OutLeftHandIKReach`,
  `OutRightHandIKReach`, `OutEnableIK`, `OutEnableLowLodIK`, foot-planting outputs;
- tuning: `FootPlantLodDistance`, `HandsIKLodDistance`.

So BF4 solves two-hand IK itself (the support hand on the gun). For VR arms, the
preferred route is to feed the game's own hand IK targets from the controllers rather than
editing bone palettes after the fact (BC2's route); the framework's ArmIk stays the
fallback and the source of targets. Unknowns: where the hand targets come from (weapon
attachment points vs a settable target), the scene op instance for the player, and
whether `InHideWeapon` here is the same switch as `HideWeapon1p`.

## The gun at the hand (R8): renderer-side leads (static, 2026-10-19)

The 1P gun is drawn at rest relative to the camera that renders the frame (live), so it
is posed in camera space; moving it to the controller means changing that camera-space
pose. Anchors from the view's first-person scale `+0x258` (tan(fov/2) / tan(defaultFov/2),
derived by the view update `+0xB8C910`, stored at `+0xB8C991`):
- Candidates only: of 23 SSE float accesses at displacement 0x258 (a byte scan; SIB-addressed
  and integer accesses were not covered), two in the renderer region, `+0x85EC01` and
  `+0x86867B` (two variants of one pass setup), pass a float read at `+0x258` of their object
  together with a 4x4 matrix from a table at its `+0x38` (indexed by the table's bytes `+9`
  and `+0xA`, 0x20-byte stride) to `+0x928350`, which posts them to a job system (global
  `+0x2670D80`, `+0x626A80`). Dropped: the code is a chained chunk of a larger function
  whose object has a vtable of at least 75 slots (`call [rax+0x250]`) and arrays of
  64-byte entries, pointers at `+0x28/+0x30/+0x38`; it reads like an entity spawning
  something at a bone with a scale, not a view or a render pass.
- No separate first-person view (live, session 14, research `viewdump`, read-only): the
  submitted 0x1020-byte block holds three views; only view 0 is a camera (`+0x00`
  soldier-relative and `+0x40` world copies) and only it reacts to aiming down sights
  (FOV 0.785 -> 0.698, `+0x250` with it). Views `+0x4E0` and `+0x9C0` carry identity
  transforms and FOV 1.2217 (70 deg) in hip and ADS alike, with stack leftovers around
  them; the secondary view's enable byte `+0xEA0` is 0. The submitted aspect is 1.333 and
  default FOV 0.9599 (55 deg); the setup replaces the aspect. So the gun cannot be moved by
  a view of its own: its camera-space pose comes from the animation side.
- `ClientWeaponLagEntity` live (session 14, `scanq 1E6F030`: one instance, ~2 min over
  4.2 GB): qwords vtable `+0x1E6F030`; pointers at `+0x08`, `+0x10`; `0x10000300E` at
  `+0x18`; pointers at `+0x20`, `+0x28`; second vtable `+0x1E6F0E0` at `+0x30`; pointers at
  `+0x38`, `+0x40`; floats 0.07 at `+0x54` and 6.017 at `+0x58` (lag tuning, probably).
  Walking from here is a one-256-byte-peek-at-a-time animation-graph exploration.
Next: the animation side (`ClientWeaponLagEntity`'s pointers above, read-only), or a
SIB/integer-access scan of `+0x258`.

## Next steps

1. Live: locate the player's 1P weapon binding and the IK scene op instance from the
   census chain (client weapon entity -> soldier), read-only first.
2. Static: find the code that writes AntRef parameters (the runtime side of
   `AntRef`) and where `InLeftHandIKReach` / `InEnableLeftHandIK` get their values.
3. Then a temporary, exactly restored `HideWeapon1p` override behind the holster
   capability, campaign only.
