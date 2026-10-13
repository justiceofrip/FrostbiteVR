# Checkpoint342: shell admission and rendering investigation

## Shared ownership correction

The combined340 actual-game run completed its rifle magazine operation and
shotgun pump, but lost the shell before a native reload could start. The old
Boolean start interface treated a temporarily unavailable native callback/cohort
and an invalid operation as the same rejection. The exact failed gate was not
recorded in340.

341 separates Accepted, Deferred and Rejected without bypassing native identity
or callback checks.342 removes341's arbitrary250ms pre-admission discard timer.
A held shell can remain in the same hand while fresh input, source and ownership
continue to validate. No native operation, reload pulse, ammo credit or renewed
native lease is granted by waiting. Each fresh input may retry admission with a
new cycle identifier; repeated packets cannot retry. Release, lost tracking,
invalid source and changed identity still cancel. Accepted native operations
retain their existing independent deadlines and completion checks.

The focused actual-consumer suite passes, including76 deferred starts
over1.5s with the same claim and unchanged ammunition, followed by acceptance.
The release/rejection cases also cover changed native firing identity, lost focus
and expired input. Nine existing vehicle/equipment recovery variants also pass.
The exact shell adapter remains SPAS; this change does not claim enrollment or
headset acceptance for other tube-fed guns.

## Rendering evidence and limits

The gray magazine has ordinary asset dimensions. Host props currently lack the
game's material textures and first-person depth occlusion. Shrinking the magazine
would conceal the symptom, not correct that rendering contract.

340/341 paired native color/depth captures contain world depth without the gun
or hand silhouettes. Simply importing that depth cannot fix hand occlusion. The
captured body magazine was outside the viewport, so those images do not establish
visible magazine-depth acceptance.341 recorded no eligible DrawIndexed depth
candidates; that is missing pass coverage, not proof of no first-person depth.

342's private diagnostic additionally observes Draw, DrawIndexedInstanced and
DrawInstanced, records filtered-call reasons, and can read four-byte D32 as well
as D24S8 resources. It adds no draws and changes no bound target or depth state.
The copies remain bounded to one stereo pair. These hooks and captures are absent
from the normal player configuration. No depth IPC/compositor change is enabled.

## Previous completed evidence

341 full normal x86/x64 builds passed244 suites each. Its separate actual-game
combined run passed three magazine cycles, shoulder exchange, a shot/manual pump,
shell insertion, support return and subsequent rifle fire. Independent server and
both-client records agree on ammunition. Support returned132.152ms after shell
completion.240 stereo pairs were consumed,40 image pairs saved; cleanup passed.
No native start deferral occurred in that run; delayed admission is established
by the deterministic regression, not falsely attributed to the live pass.

341 recorded no magazine presentation fallbacks,118 moving-pump paired palettes
and418 resting paired palettes. Those counters prove execution, not human visual
acceptance. The intermittent graphics-stack crash and remaining campaign feature
gaps are unresolved.

## Completed342 validation

Both full normal builds pass244 suites (x86:141.54s; x64:39.23s). Initial link
failures occurred while generated test symbols filled the build volume. Failed
logs and four symbol files were preserved; lossless in-place compression recovered
space, and the remaining links plus full suites then passed. No source tests were
weakened and no game/user files were deleted.

`bc2vr-recovery/depth342-native01` passes the combined native sequence and
independent ammo/support audits. It records three magazine cycles, one manual
pump, one inserted shell, shoulder exchange and subsequent rifle fire. Final
SCAR counts are29/180 in all three native branches. Shell support return is
152.827ms.240 stereo pairs and40 saved image pairs complete; hooks detach, native
transactions drain and BC2 survives. Neither live run341 nor342 happened to defer
shell admission; the long-deferral case remains deterministic-test evidence.

The expanded graphics capture observes520 indexed and251 indexed-instanced draws
per eye, plus33/25 non-indexed draws.568 draws use format44 and207 use format53.
566 draws are excluded by the current1920x1080 single-sample filter. Two eligible
draws use depth comparison ALWAYS. These counts identify a capture limitation;
they do not establish a first-person depth/projection association. Body-prop
occlusion remains unresolved.

Normal binaries are frozen in `normal342-player-checkpoint`; raw x86 build output
contains the separate diagnostic. Player stage: `headset342-20261011`. Source
archive: `checkpoint342-local`. These private paths are under `bc2vr-recovery`.
A fresh BC2 process is required before the player test. No new headset acceptance
or broad campaign/weapon-roster acceptance is claimed.

## Fork research

See [the verified loading/debug audit](FORK-LOADING-AUDIT-340.md). Some address,
asset-location and settings findings are useful. The proposed package mounting
recipe crashed our saved SP03 campaign in two different configurations before
VR injection. The original manifest was restored byte-for-byte. This does not
disprove a fresh SP02 route, but it cannot be promoted as a working campaign
weapon-loader here. No REST service or speculative console was enabled.
