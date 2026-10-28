# Campaign vehicle coverage

The shared catalog is `data/bc2/campaign-vehicle-coverage.json`. It records eleven
scenarios: fourteen retained typed human force-entry edges grouped by map, exact
asset and requested seat, plus the separately reviewed opening jeep. This is not
an exhaustive list of optional seats. Authored entry order is not a runtime slot.
The catalog does not enable adapters, and none of its scenarios is marked as a
complete current-build campaign pass.

| Map | Required seat | Existing path | Remaining distinction |
| --- | --- | --- | --- |
| SP_01 | Kurogane gunner1 | Generic native seat | Capture the actual jeepRideStart/full ride, not an unidentified gunner |
| SP_02 | Truck_Chase passenger3 | Read-only attached-soldier reader | Personal weapon callback/constraint ownership; production adapter absent |
| SP_03 | rowing_boat0 | Unclassified | Not the armed PBL |
| SP_03 | PBL_weapon0 | Exact PBL | Current seat transitions and actual sight-line reticle |
| SP_04 | AH60_Adv_Sp04 gunner1 | Generic mounted gunner | Human is not the pilot; actual seat response still needs acceptance |
| SP_04 | CAV_downhill2 and0 | Generic ground/mounted consumer | Scripted seat handoff must replace input/camera owner |
| SP_005 | M1A2_sp05 seat0 | Generic ground/mounted consumer | Join actual seat; authored component order is ambiguous |
| SP_005 | UAV_sp0 | Pending | Root rotation, flight physics, camera and AltFire are separate |
| SP_007 | AH60_Flynn gunner1 | Generic mounted gunner | Flynn is pilot0; no player-pilot enablement |
| SP_009 | Golfcart_invisible passenger2 | Read-only attached-soldier reader | Free fall retains personal weapon; never apply cart driving |

The opening jeep initially has EVRRestrictAll. The authored five-enemy-kill gate
releases it and selects seat1. `jeepRideStart` / `SAVE_4163368501` saves on entry;
the ride restricts exit/seat change while permitting aim/fire. Those restrictions
are gameplay state, not a reason to force input permission bits. A generic
seat1 camera test proves neither that checkpoint nor the complete jeep sequence.

## Existing shared emulated checks

`Bc2CampaignVehicleProfileTests` now drives the real reader, controller policy,
input planner and scoped commit together for three supported roles and six
transitions (18 combinations): menu/focus loss, original input expiry, new actor,
seat handoff, recenter, and loss of both controllers. Held move/fire must remain
inactive until a fresh neutral sample; denied native axes remain untouched.
Separate rejection cases cover soldier entries, the UAV-style route and boolean
AltFire as a primary-fire consumer. These are synthetic graph tests, not named
vehicle or headset passes. They have been prepared; root runs the focused suite.

Reuse `Bc2VehicleRuntime`, `Bc2VehicleInput`, `Bc2CampaignVehicleProfile`,
`Bc2VehicleShotObserver`, `Bc2VehicleReticleProducer` and
`Bc2VehicleMountedObserver` for their respective production changes. Do not rerun
unchanged suites merely because another authored vehicle was listed.

`tools/bc2_campaign_vehicle_coverage.py` validates the catalog, reconciles every
pinned authored human edge, and can audit the existing bounded three-snapshot
capture without hiding rejected profiles. Example (explicit new output):

```powershell
python -B tools/bc2_campaign_vehicle_coverage.py --authored-root <campaign-coverage396> --output <new-coverage.json>
python -B tools/bc2_campaign_vehicle_coverage.py --capture sp_02-truck_chase-3=<capture.json> --output <new-coverage.json>
python -B -m unittest discover -s tests -p test_bc2_campaign_vehicle_coverage.py
```

An admitted read-only profile is still not a controls pass. The current capture
does not identify the loaded blueprint, so the audit explicitly keeps exact
blueprint, controls and campaign acceptance false. A requested scenario name
cannot promote an unrelated boat or gunner capture to that scenario's coverage.
Independent `audit_vehicle_camera.py` validates both actual post-setter eye
matrices, anchor, lean/return and IPD. `audit_vehicle_fixture.py` remains a PBL
driver-specific movement check; do not use it as a universal gunner verdict.

## Next bounded native observations

The prepared recovery recipe under `performance436/vehicle-coverage-prep` pins
the existing `attached-soldier398/capture/build/CaptureVehicle.exe`, its original
build receipt, current read-only map reader dependencies and this wrapper:

```powershell
& tools/Observe-BC2CampaignVehicle.ps1 -Recipe <observation-recipe.json> -RecipeSha256 <sha256> -Case sp_005-uav_sp-0 -GameReceipt <process.json> -GameReceiptSha256 <sha256> -Output <new-G-recovery-directory>
```

Use `sp_02-truck_chase-3` or `sp_009-golfcart_invisible-2` for attached soldiers.
The supplied game receipt contains pid, creation_filetime and exact executable
path. The wrapper does not launch, focus, inject, move, fire, or close BC2. It
brackets one existing three-snapshot helper call with actual map reads and saves
the rejected/incomplete outcome. Each helper snapshot is capped at 2 seconds,
32,768 reads and 2 MiB; total helper budget is 10 seconds/100,000 reads/8 MiB.
Each map observer is capped at 5 seconds/128 reads/16 KiB. There is one attempt,
no adaptive polling, no raw image capture, and a 64 MiB storage reservation.

Reach the *actual authored seat* using the ordinary mission sequence first:

- UAV: SP_005 UAV station OnPlayerEnter → sequence
  `c829f14c-1f80-4029-b1e2-ab65ed87a513` → human trigger
  `19216103-60d2-4851-a2ce-1d2159cff840` → UAV spawn
  `df943240-2f62-4d7f-85c9-ec1ef468a06d`, requested seat0.
- Truck: SP_02 chase human trigger
  `2494f58d-2533-4760-8104-857960c77674`, requested seat3.
- Free fall: SP_009 Start FreeFall → human trigger
  `e8c5ba0f-6027-4d37-9a39-7d075263680b`, requested seat2.

The retained `newsp399-private` fixture is compiled for SP_04 and
`newsp405-sp009-private` for SP_009. They are not configurable launchers for SP_005
or SP_02, and their old wrappers write/restore the Windows game profile. Do not
run them unchanged under the current no-C-write policy or alter a level string
in a frozen binary. The current native menu fixture may load a user-selected
mission through the normal menu, but its reviewed Resume coordinates do not
select arbitrary missions/checkpoints. Starting SP_009 also does not immediately
prove free-fall entry. There is no verified automatic trigger/seat warp here.
If root needs automatic mission selection, prepare a separate scoped adaptation
of the existing verified UI dispatch, with profile isolation and loaded-map proof;
do not force raw pointers from these authored GUIDs into live objects.

For UAV, retain current entry/router/permission mask, root prediction/current/
bridge, actual physics dispatch vtable, camera parent and weapon component chain.
The existing native consumer at 0x78f660 reads action0/2 for propulsion/braking,
-action4 to physics slot0x64, action5 to0x68, -action6 to0x70, and action1 to0x6c.
This proves dispatch, not the current UAV physics meanings or camera transform.
Authored mapping has Throttle→Jump, Brake→ChangePose, Strafe→MoveFB, Yaw→Roll,
Roll→Yaw and Pitch→Pitch. AltFire is boolean action12; its Concept10 is also
mapped to Zoom14. Main missile and coax use different firing actions. A blanket
left-trigger-to-AltFire patch could also zoom or bypass a mission input gate.
Keep it disabled until the exact native seat/consumer join is witnessed.

For attached soldiers, require controlled==soldier, attached==vehicle, exact
selected seat/router/cache and occupant, native constraints matching authored
degrees, current selected equipment and its aimer sharing those constraints,
and active camera/provider ancestry. The existing reader verifies these joins;
its positive result grants no control. Follow it with the existing passive
gameplay hook observation of selected aim/fire/rig callback owners and mission
permissions before enabling a shared personal-weapon actor path. Preserve
mission movement, seat camera and aim limits; do not mark the actor on-foot.

After structural admission, use the existing ControlsVehicle lean/return recipe,
then a separate native Shoot witness. An 80 ms fire pulse cannot cover a mounted
weapon with a 600 ms authored prefire delay; the previous sustained gunner recipe
used 900 ms and observed two Shoot callbacks. Capture actual aiming response,
not just input writes. Finally check exit/scripted handoff, ordinary infantry
shoot/reload/holster/Use, tracking recovery and death/checkpoint return. These
remain distinct from matrix algebra and headset comfort.
