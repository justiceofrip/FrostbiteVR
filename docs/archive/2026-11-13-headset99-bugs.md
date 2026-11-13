# Private 0.1.99 headset feedback — November 13, 2026

User session `20261113-134542-247-8333f896` is retained in the local launcher runtime sessions. Its source closure matches installed0.1.99. Public release remains on hold.

| Reported failure | Status |
| --- | --- |
| LMG replacement box on the belt does not grab. | Open; shared feed-supply acquisition correction in progress. |
| Released old LMG box floats in the air. | Open; current presentation deliberately retains its last world position. |
| Chest knife cannot be picked up. | Open; shared empty-hand and chest approach behavior under repair. |
| Empty-hand grip/trigger displays the M8. | Open; Fire-only suspension clears hidden-firearm presentation without an equipment transition. |

The LMG telemetry stays at DetachContainer: it records no removal endpoint/completion and never reaches AttachContainer. Source inspection found that discarding the old box required matching an authored destination position and rotation. The shared correction recognizes measured outward clearance, while retaining dwell, release and ownership checks. The recording lacks hand poses, so the exact failed movement remains unknown. Belt-pickup admission is unchanged in this batch; it was blocked upstream of that step.

The empty-hand fix retains already-proved hidden-firearm presentation during sole-Fire input. The chest correction accepts a deliberate squeeze shortly before reaching the chest, with continuous current input; a skipped native gather cancels that approach. Trigger-neutral acquisition remains required. Normal telemetry does not record chest/holster claims, so these source fixes do not prove the precise knife failure in the user's session.

The native final report is empty; game/host are absent and graphics recovery reports restored, but full hook-retirement diagnostics are unavailable. Preserve this user recording and prior failed evidence. Current user saves and settings must not be restored to old automated-session backups merely to prepare an offline build.
