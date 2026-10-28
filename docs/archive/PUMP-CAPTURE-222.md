# Passive SPAS calibration capture — 222

This port adds the reviewed original-rig capture from the older isolated ordinary
weapon-cycle candidate to the current source. It changes neither the existing
350 ms native diagnostic hold nor ordinary gameplay. It introduces no new native
address, signature, mutation, input or registration.

The existing explicit 15-second `PumpHoldProbe` now records original SPAS
`jntWpn_4` and left wrist transforms relative to `jntWpn_1`, in metres. Every
original rig read is bracketed by independent native owner/configuration and
all-three firing observations. The getter checks no callback is active and that
the callback revision remains unchanged across its read. It never enters the
native callback exclusion or increments callback depth/revision. Any contention,
read failure or changing lifetime rejects that observation.

Records include exact native owner/weapon/config pointers, firing copies and
states/counts/timers, rig fingerprint, source deadlines, selected configuration
observation time/deadline, input sequence/deadline and both native brackets.
`input_observed_ns=0` explicitly records unavailable original receive timing.
`stable_state_bracket` does not mean an atomic snapshot or a submitted mesh.
The captured palette is the original evaluated animation, before tracked arm,
weapon, sight, reload or magazine overlays. Sampling is bounded to 1024 rows and
at most one row each 15 ms.

Integration: add `src/games/bc2/Bc2PumpCapture.cpp` to BC2Camera and
`Bc2PumpCapture` to its test list. The ordinary NativeProbe already enables the
bounded configured-mesh reader for the 15-second Hands diagnostic. No new CLI
flag or binary layout is needed.

The integration owner can run the existing diagnostic with a fresh exact SPAS
process, at least three loaded rounds and a harmless aim direction:

```powershell
.\Test-NativeStream.ps1 -PumpHoldProbe -Seconds 15 -Pairs 240 -StaticPose -Async
```

Review `rig_publication.pump_part_capture.samples` alongside the existing strict
pump-hold auditor. Required calibration evidence is repeated stable held
7/previous6/next8 fore-end/wrist observations, followed by the original resumed
8→1→2/tail geometry. Correlate measured extrema with those states to establish
which pose is closed and the signed rear direction. A moving, missing or mixed
bracket is incomplete evidence. Native authored wrist placement is a contact
candidate, not proof of a tracked physical grasp or headset comfort.

Four capture test groups pass on x86 and x64. Both actual
Bc2ReloadFlowRuntime.cpp and Bc2RigPublication.cpp translation units compile on
both architectures in isolated overlays. The first compile attempt mixed two
header roots and correctly failed with duplicate definitions; the fixed overlay
compile preserves one current header identity. Full builds and actual native
capture are integration-owner work. No game process was opened by this task.

Transfer/handoff: native state brackets and original animation geometry can
calibrate a future manual mechanism, but cannot activate it. Sustained cycle
service, ordinary player arbitration and paired hand/part publication remain a
separate candidate under development.
