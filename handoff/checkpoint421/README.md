# Checkpoint 421: recover after a rejected render observation

**Saved locally; release blocked.** Four focused suites and all 288 C++ suites
on each architecture passed. The bounded restart04 actual-game check passed a
checkpoint restart, resumed rendering and pause/resume in the same process.
Opening02 reproduced a large cloud disparity in retained native eye images;
its long black interval remains unexplained. Actual-death, headset and
full-campaign acceptance remain pending.
The frozen build receipt records the offline state at freeze time; later live
results remain separately identified.

Source: `2caa8ab3ef2fedfd6255b46d6849f823a87ed6aa408efedb64caf6f16b0a9c15`.
Frozen build receipt:
`81c573d1f670ce51c3e0de6c2c8af484ef57b6c0ff3df5ca2fb44100b6711072`.

## Change

A rejected first-person projection observation previously incremented a lifetime
counter that blocked every later stereo correction. The new helper keeps the
cumulative diagnostic count while limiting rejection to the admitted frame.
An atomic epoch/rejection token prevents delayed callbacks from poisoning a
newer frame or clearing a newer rejection. The hook captures that token on entry
and rejects stale context collection after the original callback returns.

Exact native restoration failures remain terminal. The change does not relax
ownership checks, reuse old camera evidence, reset equipment state or change
sky/cloud rendering. It is a demonstrated source-level flaw, not a proven
explanation of the checkpoint416 death/restart distortion. That failed final
trace is empty, and inspected completed416 checks recorded no such rejection.

See [implementation and graphics limits](../../docs/RENDER-OBSERVATION-RECOVERY-421.md)
and [validation.json](validation.json) for exact scope and receipts. Checkpoint420
cycle ordering and selected-knife foundations remain inherited work; this narrow
render correction does not establish their headset acceptance.

## Offline validation

| Check | Result |
| --- | --- |
| ProjectionOverride, ExactWriteBatch, Bc2StereoRecovery, RenderOwnerRecovery | 4/4 passed |
| Full normal x86 build | 288/288 suites passed |
| Full normal x64 build | 288/288 suites passed |
| Independent epoch/rejection accounting source review | No outstanding finding in the bounded change |
| Actual checkpoint restart, restart04 | Passed bounded protocol; sampled both-eye scenery and replacement pause menu reviewed |
| Opening scene, opening02 | Entire mission restarted; large cloud disparity measured; visual acceptance blocked |
| Actual death/restart | Pending |
| Headset and full campaign | Not accepted |

Regressions cover a bad observation followed by a valid next frame, rejection
that persists within the bad frame, delayed old-token callbacks, and an exact
restore conflict that remains terminal without overwriting new native bytes.
They do not prove native context-cache lifetimes or identify the opening cloud
consumer.

## Retained evidence and next gate

The restart02 fixture used this frozen421 build and reached the native restart
confirmation, but did not confirm it. Its 240 published/consumed pairs and clean
context scopes are ordinary rendering evidence, not restart acceptance. The
retained camera sample is a forest scene after the intro with symmetric FOVs;
it cannot accept the reported opening-cloud effect.

Restart04 used the same frozen421 build and completed one confirmed native
checkpoint restart. The replacement scene rebuilt 2.563 seconds after the actual
confirmation click; the replacement actor was observed 140 ms after rebuilding.
The receiver consumed 240 pairs, including 194 whose original input publication
followed rebuilding. Native production recorded 241 published pairs, one
discard, 482 captured eyes and zero camera/context restoration, GPU or rig
packing failures. One owner recovery completed. All three diagnostic processes
exited normally; native hooks, lists, callbacks and ordered surviving views were
restored, with request references 8 -> 8.

Root reviewed paired images 0/48/72/96/192 and the replacement pause-menu image.
Pair48 was black in both eyes during the transition, 93 ms after rebuilding;
backgrounds were visible in both eyes at 72/96/192, without gross world stretching
in those samples. The pause menu was legible. This used static synthetic poses
and symmetric FOVs. Hands, knife appearance/damage, headset comfort, actual death
and opening-cloud rendering are not accepted. No weapon-context rejection
occurred, so this run does not exercise421's new rejection recovery or establish
the cause of416's reported failure.

Restart04 runner result SHA-256:
`519fb1a51e0bca3c6280ef5bc5e6ff783855537435a783813454af7d5fbd9fda`.
Independent transition audit SHA-256:
`22eeb64697b7c295f253f90ad8b0bfaba541984a7b3a816cbe698caa65f6fc67`.
The audit's earlier console hash was calculated before Windows newline encoding;
the hash above verifies the retained file bytes. Evidence content was unchanged.

Opening02 completed a confirmed native entire-mission restart and consumed 240
pairs with one completed owner recovery, zero camera/context restoration or GPU
failures, and normal exits from all three diagnostic processes. The original
runner result remains **failed**: its shutdown assertion incorrectly treated
`owned_pending_references` as a post-release residue. That field records the six
verified owned children pending before release. A separate source-backed audit
accepted the existing native detach contract: exact surviving-view order, hooks,
lists and callbacks restored, and request references 15 immediately before
release -> 8 afterward, matching the baseline of 8. This qualification does not
rewrite the failed runner result or accept the visuals.

Both eyes were black in reviewed opening pairs 24/48/72/96/144/168. Later images
at pairs 216/239 showed the opening scene. The prolonged black interval remains
unexplained. In these two late native eye pairs, matching upper-cloud features
had about 245-280 pixels of horizontal disparity (correlation 0.950-0.997),
compared with about 27-80 pixels for nearby boat occupants and 0-8 pixels for the
distant hill. This confirms the image-level discrepancy before headset display;
it does not establish the responsible renderer, whether authored finite-distance
clouds account for it, or a correction. Initial forest camera diagnostics do not
describe those late affected frames. The observer-only422 investigation targets
that missing correlation. No actual death, headset or long campaign was tested.

Opening02 original runner SHA-256:
`7f88c8529d357bc555d4f9ee351dd8d923cd644c6a882349121f4e8a585ce3a3`.
Qualified detach audit SHA-256:
`7b7150f891b2eda80edfe1a7dabb53a0141c12ebe380afbfc4adfd5d618f0f56`.
Cloud correlation report SHA-256:
`aff5fd780f89e74448ef8deee7ae6d878470473411b74bfdeb0f88c214efbb61`.

Private checkpoint directory:
`G:\Unity\VRMMO\bc2vr-recovery\overnight421\normal-build`.
It contains four runtime/diagnostic binaries, exact build metadata and a small
changed-text snapshot. The420 parent receipt remains immutable. No complete
build trees or game assets are included in this handoff. No desktop promotion
or GitHub publication is performed by this checkpoint note.
