# Boat driver aim and firing monitor checkpoint — October 2

The exact campaign PBLB driver/front-GMG profile now has monitor evidence for corrected forward-facing stereo, native head-aim response, and a visible native firing response in both GPU eyes. Headset comfort, aim feel, and campaign-wide vehicle coverage remain unverified. This is a limited driver/front-GMG result, not support for arbitrary vehicle seats.

The native nearest-entry ancestry identifies the front GMG as belonging to selected driver slot 0; the separate dummy seat is not substituted as firing owner. Head aim uses the verified Roll 6/Pitch 5 native routes, Fire 8, current owner/generation, permissions, focus, and original tracking deadlines. The selected renderer-facing StaticCamera supplies the camera basis; the authored component frame has opposite right/forward axes and previously made neutral stereo face backwards. Native vehicle/weapon simulation remains authoritative. These features retain the explicit Disabled/AimOnly/AimAndFire configuration boundary.

## Corrected aim and camera

Trace `native-trace-20261002-050727-323`, receiver `boat-head-aim-20261002-050727-115`, delivered 240 pairs with no timeout, camera restoration failure, GPU failure, or boat aim rejection. It recorded 1,568 aim commits, no firing commits, and neutral throttle/steering throughout. The numerical audit checks 16 complete paired camera samples: initial native/adjusted forward dot 0.9999648, exact planned/applied matrices, head-composition maximum error 0.00002287, and approximately 63.986 mm eye separation. The bounded ±10° yaw and +5° pitch phases moved the native joints with reducing target error and subsequent neutral recovery; these are finite-duration response measurements, not exact settled-angle guarantees.

The later firing capture independently shows a stable forward boat/world baseline and return in both eyes. The corrected camera and native aim still need a human headset test, including recenter and disconnect/reconnect while seated.

## Actual fire pulse

Run `boat-fire-20261002-055504-813` used native trace `native-trace-20261002-055513-947`. The receiver delivered four Fire input samples from 2,000 through 2,062 ms of its bounded [2,000,2,080) ms pulse. The selected-owner native input path recorded 19 permitted fire commits, 1,575 total aim commits, no aim/route rejection, no throttle/steer command, and no gather readback failure. Fire commits follow successful native input-plan apply/commit; the separate gather readback diagnostics cover throttle/steer rather than an independent Fire-byte receipt.

The read-only observer completed 4,366 samples against the same selected entity, entry, mounted weapon, firing config, and embedded firing object. It recorded a single interval of state 9 (69 samples), surrounded by ready state 2. The first state 9 observation shares clock tick 18,628,125 with the first source pulse; the weapon returned to ready by tick 18,628,531. The native phase timer declined from 0.395 to0.0034 seconds. Loaded1/reserve−1 remained unchanged; this infinite-reserve case does not provide a finite-ammunition decrement or exact shot count.

State9 is the automatic shot/repeat path, not an error or reload state. In the exact installed executable, native entry helper 0x6e1fa0 selects next 9 directly for FireLogicType 2. State-step dispatch maps 9 to 0x6e6654, which invokes the same native shot-listener helper 0x6dd960 used by state 6 and applies the automatic interval/repeat behavior. Its timed release path selects ready 2 when firing is released. Thus the observer's legacy `native_shot_step_observed: false`, which recognizes only states 5/6, is not a failure verdict for this weapon. Automatic entry can bypass those states entirely.

Root inspected both saved GPU-eye montages: pairs 0/55 show the baseline; pair 60 eye 0 begins a star-shaped flash; pairs 61/62 show a bright front-GMG muzzle flare in both eyes; pair 65 shows smoke/clearing; pairs 77/182/239 retain the stable forward boat/world without the flash. The combined native and visual evidence supports actual mounted-gun firing response. Projectile entity creation, impact, damage, and exact shot count were not independently verified.

All 240 stereo pairs arrived, with one recovered asynchronous timeout and no GPU-busy result. Native hooks detached cleanly; the game remained responsive during the recorded 15-second stability observation with no new crash. The diagnostic cleanup also reports eight completed CPU images released after disk serialization and graphics quiescence: 66,355,200 bytes. That cleanup is separate from the native checkpoint-loading black screen observed in a fresh process before any mod attachment.

## Evidence limits and next check

The observer polls at a requested 5 ms but its GetTickCount64 timestamps advance here in 15/16 ms steps with duplicates. Shared timestamps show bounded correlation, not exact order within a clock tick. A native update can complete transient states between observations. Original firing-state/code spans were verified against live bytes during this run; additional entry/listener/interval disassembly was checked offline against the identical installed executable and is labeled separately in the audit.

The immediate remaining vehicle check is human headset use of this exact driver seat: forward reference, head-aim direction/limits, trigger response, left-stick driving, recenter, seat entry/exit, and tracking reconnect. Other campaign vehicles, alternate seats, projectile impacts, and full campaign completion need their own evidence. No multiplayer or BF3/BF4 compatibility claim follows from this checkpoint.

Offline audit artifacts are staged in `bc2vr-recovery/boat-fire-audit-055504`: `audit.json`, `native-state9-source.json`, reproducible `Audit.py`, and their manifest. The audit includes raw observer/native/receiver hashes plus both montage hashes and all reviewed raw GPU-pair hashes. Corrected-aim numerical evidence is `bc2vr-recovery/boat-corrected-audit-050727.json`.
