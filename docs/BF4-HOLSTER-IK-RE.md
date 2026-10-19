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

## Next steps

1. Live: locate the player's 1P weapon binding and the IK scene op instance from the
   census chain (client weapon entity -> soldier), read-only first.
2. Static: find the code that writes AntRef parameters (the runtime side of
   `AntRef`) and where `InLeftHandIKReach` / `InEnableLeftHandIK` get their values.
3. Then a temporary, exactly restored `HideWeapon1p` override behind the holster
   capability, campaign only.
