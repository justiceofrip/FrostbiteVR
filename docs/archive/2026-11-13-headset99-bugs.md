# Private 0.1.99 headset feedback — November 13, 2026

User session `20261113-134542-247-8333f896` is retained in the local launcher runtime sessions. Its source closure matches installed0.1.99. Public release remains on hold.

| Reported failure | Status |
| --- | --- |
| LMG replacement box on the belt does not grab. | Private101 adds continuous grip approach, departure from the belt after actual pickup, and same-object regrasp. Full actual-game/headset reload remains open. |
| Released old LMG box floats in the air. | Private100 makes explicitly discarded empty originals fall then hide after accepted detach. Actual101/01 shows a suspended original box before accepted detach; its release state is under investigation. |
| Chest knife cannot be picked up. | Private100 adds bounded grip approach with skipped-gather cancellation. Exact99 cause and new headset acceptance remain open. |
| Empty-hand grip/trigger displays the M8. | Private100 retains proven hidden-firearm presentation during sole-Fire suspension. Grip-only causality remains unproved. |

The user's99 LMG telemetry stays at DetachContainer: it records no removal endpoint/completion and never reaches AttachContainer. Source inspection found that discarding the old box required matching an authored destination position and rotation. Private100 recognizes measured outward clearance, while retaining dwell, release and ownership checks. The recording lacks hand poses, so the exact failed movement remains unknown. Private101 also changes belt pickup: an actual neutral-outside/squeeze/held-arrival sequence admits the same acquired object, retains it through verified owner migration and permits regrasp without a second debit or duplicate object.

Private101 passed the complete normal builds and bulk family regressions, including192 natural/regrasp/skipped-render cycles across24 profiles. Actual101/01 reached the same campaign LMG at0/687 and opened the lid. Its removal observation and pickup command hit their existing deadlines; cleanup released both hands. A final stereo image shows the suspended original box without an accepted detach. This is incomplete evidence, not a successful reload or headset acceptance; the failure artifacts are retained under `reports/coordinator-20261108-feedback721/normal101/xr-game01`.

The empty-hand fix retains already-proved hidden-firearm presentation during sole-Fire input. The chest correction accepts a deliberate squeeze shortly before reaching the chest, with continuous current input; a skipped native gather cancels that approach. Trigger-neutral acquisition remains required. Normal telemetry does not record chest/holster claims, so these source fixes do not prove the precise knife failure in the user's session.

The native final report is empty; game/host are absent and graphics recovery reports restored, but full hook-retirement diagnostics are unavailable. Preserve this user recording and prior failed evidence. Current user saves and settings must not be restored to old automated-session backups merely to prepare an offline build.
