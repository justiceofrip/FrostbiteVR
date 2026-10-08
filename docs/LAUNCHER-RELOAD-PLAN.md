# BC2 launcher reload families and first native observation

> October 3 user decision: manual underbarrel reloads are deferred. Retain BC2
> native reload behavior while shared rifle/shotgun switching, grasp and body
> equipment are stabilized. The research below is future work, not an active
> implementation commitment or enabled capability.

Prepared 2026-10-02 for the modular Frostbite adapter. This is an offline evidence survey, not an enabled feature or a headset test. Canonical source, live processes, ammunition, and game configuration were not changed.

## Decision

Start with the exact campaign scoped XM8/XM320 pair. Its selected launcher is a one-round `rtMagazine` native weapon, not the SPAS `rtSingleBullet` path and not a physically detachable rifle magazine. Reuse the shared hand, supply, rail, feedback, and conserved-transfer infrastructure. Bind launcher opening/closing separately after measuring the actual animation and native empty-ammo cycle.

The main prerequisite is **zero-loaded ownership and cancellation**. The demonstrated rifle/SPAS abort capability requires positive loaded ammo. A fired one-round launcher normally needs reloading from zero; native ready state 2 has an automatic-empty reload path. Ending the current native reload does not yet prove it stays ended. Do not relax the positive-ammo guard or enable the XM8 rifle allowlist for `40mmgl` to skip this work.

## Evidence levels and limits

- **Live reflection / prior native captures:** exact scoped XM8 launcher configuration, shared persistent family/mesh, bidirectional mode switch, accepted sight interaction. These establish selection and sight behavior, not a manual launcher reload.
- **Archive field observations:** 23 selected DBX assets, read in memory. `survey_fields.py` accepts unique bounded tagged scalar/enum records and records offsets, bytes, and source hashes. All 22 comparable fields of `SP_rgl_XM8_GL_Firing` exactly match the saved live reflection. This is not a full DBX loader or installation override resolver.
- **Archive references / indexes:** identify other assets and animation trees. Their presence does not prove campaign availability, active overrides, compatible native bindings, or physical opening direction.

Index scans inspected 380 Package and 620 Dist/win32 archives; 3 and 5 respective bounded index reads were rejected and recorded. No extracted game assets were saved. No weapon-total estimate is inferred. The mine asset and rifle firing assets included in the metadata evidence are excluded from launcher behavior claims.

## Family matrix

All firing rows below use capacity 1, `fltSingleFire`, `rtMagazine`, and `AutoReplenishMagazine=false`, unless the row explicitly has no firing proof. Times are authored seconds, not measured native completion times. `NumberOfMagazines` is authored configuration, not a live reserve count. Every opening/insertion/closure geometry binding remains disabled until measured.

| Asset group | Actual evidence | Opening / insertion / closure status | Reuse and separate work |
| --- | --- | --- | --- |
| **Campaign scoped XM8 underbarrel, first target**: `US_rgl_XM8/SP_rgl_XM320_Scoped` | Live configuration: `SP_rgl_XM8_GL_Firing`, 2.45 s, threshold .55, 2 magazines, ReloadLogic 0. Exact family `sp_xm8_s`; current sight/mode operation accepted. | Native launcher reload motion, breech joint, open limit, round grasp/axis, spent-case behavior and closed latch are **unmeasured**. Sight joints are not breech joints. | Reuse exact family resolution and mode selection; single-round supply/rail presentation; general native conserved-transfer ledger. Must establish empty-cycle hold/abort and closure/fire interlock. |
| **Campaign GP30 candidate**: `RU_rgl_AEK971/SP_rgl_GP30` | Its DBX string table references the **same** `SP_rgl_XM8_GL_Firing` asset, but its own GP30 animation trees and AEK971 mesh. This is an archive reference, not live binding. | Distinct geometry required; no measured opening or insertion direction. Do not infer a hinge from the shared firing asset. | Strong candidate for the same native completion family after live verification; its mechanism profile remains independent. |
| **Other XM320 grenade / smoke / buckshot variants**: `US_rgl_XM320/{US_rgl,US_smk,US_shg}_XM320_Scoped` | Own DBX references select corresponding `UL_{rgl,smk,shg}_HK416_Firing`; observed firing assets each 2.45 s, .55, 2 magazines, ReloadLogic 0. XM8 mesh / XM320 animation-tree references recur. | Shared mesh is a reuse lead, not proof that all selector states, ammunition props and animations match. | Separate exact asset and ammunition-kind profiles/pools. Smoke/buckshot must never consume grenade supply merely because timing matches. |
| **M203 variants**: `MEC_rgl_M203/...` | M203 grenade/smoke/buckshot-named entries are present in archive indexes; selected archive coverage does not establish all active main/firing bindings. | **Unclassified** physical mechanism until actual animation/mesh capture. | Resolve main asset to firing asset and live family first; do not alias to XM320 from category/name. |
| **Vietnam standalone M79**: `Vietnam/NAM_gl_M79` | Own main mesh / animation / firing references; observed firing configuration 2.8 s, .68, 2 magazines, ReloadLogic 0. | Standalone asset, but pivot, latch, round insertion and closure are **unmeasured**. A break-open implementation would presently be an assumption. | Reuse one-round native receipt and supply pipeline once bound; no underbarrel/rifle mode coupling. Vietnam asset presence is not base-campaign availability. |
| **Garand rifle-grenade asset**: `Beach/M1Garand_rgl/M1Garand_rgl_Firing` | Observed 3.033 s, .75, 2 magazines, **ReloadLogic 1** (`rlReloadUnaffectedByWeaponSwitch`). | Launcher fitting/loading sequence unmeasured; do not assign it an underbarrel breech. | Separate physical mechanism and cancellation binding. The demonstrated ReloadLogic 0 abort path does not authorize this variant. |
| **RPG7 family**: `RU_at_RPG7`, `Vietnam/NAM_at_RPG7` | Observed base 5.8 s / Vietnam 5.6 s, .65, 4 magazines, ReloadLogic 0. Base main has its own projectile/mesh/animation references. | Configured reload candidates; round grasp, loading end, seating and native round-visibility transition unmeasured. | Separate rocket supply/round profile; do not reuse 40 mm geometry. Current rifle request deadline of 3.5 s is not a validated rocket deadline. |
| **Carl Gustaf family**: `MEC_at_CarlGustaf`, `SP_at_CarlGustaf_Firing` | Observed 5.8 s, .60, 4 magazines, ReloadLogic 0; own main mesh/animation/firing references. | Configured reload candidates; rear mechanism/closure semantics must be measured rather than assumed from the real weapon. | Separate rocket mechanism; native HUD/ADS scope issue remains unrelated to reload authority. |
| **M136 firing asset**: `US_at_M136/US_at_M136_Firing` | Observed **6.0 s, .65, 4 magazines**, ReloadLogic 0. This BC2 configuration exposes a reload path. | Actual in-game replacement/loading motion and disposal policy unobserved. Main active binding was not established by this sample. | Do **not** label this single-use from the real-world M136 name. Verify game inventory/empty behavior before choosing reusable tube versus whole-item replacement presentation. |
| **Single-use / discarded launchers** | No exact BC2 runtime family with proven one-shot disposal was established by this survey. | **Unbound.** | Keep a separate eventual whole-item consumption/replacement policy; never mint a replacement weapon from a projectile reservation. |

The asset matrix is intentionally not a claim that every gun requires unique code. Native transfer policy, physical mechanism profile, ammunition kind, and per-asset rig binding are four separate axes. A shared firing asset can reuse verified native behavior without sharing a hinge or hand pose.

## Existing pieces to reuse, and boundaries to preserve

`ManualReload` already defines `OpenBreech`, `InsertRound`, and `CloseBreech`, with exact request/owner acknowledgement. Adding another speculative public interface now would duplicate it. Its `bindingsVerified` contract covers the entire plan, so a timer gate is not evidence of a visibly open breech, nor is a closed pose evidence that native ammunition was credited.

Use existing `AmmoSupply` reservations, raw-contact versus guided-presentation `ReloadInsertion`, `HandInteraction` claims, and feedback delivery. A fresh round reserves exactly one unit from the current launcher pool; a spent case or removed visual object is non-spendable. Opening, closing or dropping an object must not create ammunition. Capture feedback and native-receipt feedback stay distinct.

Reuse native request identity/generations, typed startup result, per-callback exact context restoration, callback retirement and the conserved transfer ledger. The current ledger is named/typed for `SeatMagazine`; factor a common observed refill receipt only after the launcher observation proves its path. Do not disguise a grenade as a detachable magazine or assume the rifle's 3.5-second deadline fits a 5.8-second rocket.

Preserve sight interaction: the same persistent physical family is shared by rifle and launcher, while their active native weapon pointers and reserve pools differ. A mechanism claim must not steal an existing sight/support/ammo claim. Mode/equip/actor/space changes revoke the reload capability. In particular, `jntWpn_9` and `jntWpn_11` are supported sight movements, **not** a breech binding; `jntWpn_Flash` remained at the rifle muzzle in the saved captures, so translated launcher muzzle correction remains unsupported.

Closed-breech firing needs an explicit proven interlock. Decide the semantic acknowledgement ordering only after observing whether native transfer precedes, coincides with, or follows actual closure. A staged physical round may be retained without ammunition credit; a rail capture alone never acknowledges `InsertRound` as native completion.

## First XM8 launcher native observation: smallest useful run

**No hold, abort helper, timer override, synthetic receipt, ammunition write, or new launcher enablement.** Root owns the live game and execution. This plan has not run.

1. Start from ordinary campaign gameplay on the left monitor, exact scoped XM8 launcher selected, native aim toward a safe distant impact area. Read-only preflight must prove the full launcher asset path, `sp_xm8_s` family, current mode, all three firing identities, capacity 1, loaded 1 and finite reserve at least 1. Record native state/config/listeners, both rifle/launcher pool identities and current first-person mesh/skeleton. Do not select by generic `40mmgl` or a slot number.
2. Attach only the existing ordinary reload-flow/rig observation path for at most 20 seconds. Keep physical reload, magazine reload, sight flip, holsters and every hold/request fixture disabled. Preserve native animation; capture raw original matrices before VR retargeting. Record at least 1 second before the shot and the entire 2.45-second authored reload plus 2 seconds after return to idle; use an event-centred capture window rather than exhausting initial-frame capacity before the action.
3. After hooks are warm, perform **one ordinary native shot**, then release Fire and observe. The key question is whether the zero-loaded branch begins native reload automatically. Do not prefire before attaching, because that can hide the exact empty transition. If it remains empty and idle, a single ordinary Reload pulse may be a separate labelled phase; do not silently mix that into the automatic-empty result.
4. Collect all Update / Commit / Step / TimedStep / Transfer / Restore records with source timestamp, exact identity/generation, branch, caller, current/next state, timers, context input bits, loaded/reserve and capacity. Correlate native begin/end callbacks, animation state and unmodified weapon/round joint matrices. Capture reflected `ProjectileBoneName`, `HideProjectileAfterFireTime`, hidden bones and actual selected mesh/skin groups; field names or a `jntWpn_*` label alone are not a rig binding.
5. Require an accounted native shot followed by a conserved single-round refill across the authoritative server and both client copies, ordinary idle completion, no second unrequested shot, and unchanged rifle-pool counts. Preserve actual counterexamples if zero-loaded reload does not behave as predicted. Cleanly detach all hooks, complete the existing stability interval, then read-only postflight the final owner and all three ammo copies.

**Existing launch support and limitation:** the ordinary XM8 rifle observer `native-trace-20261002-032717-503` used Controllers + MotionAim + PoseObserve, no hand retargeting/two-hand/sight/reload-control flags, 15 seconds, and a static async 240-pair receiver. Its `--controls-support-reload --support-primary-direction 0` receiver produces Reload at 6000..6200 ms, a visual-only .65 trigger curl at 4200..5500 ms, and support gestures; it does not produce a shot without the optional two-shot preparation schedule. Therefore it is suitable for a pre-empty explicit-Reload observation, **not** the required one-shot/automatic-empty experiment. Do not use `--support-prepare-shots` or the existing three-shot probe on a one-round explosive launcher. Either use ordinary foreground native input while the observer is configured to preserve it, or add a small explicitly named one-shot receiver schedule after checking Gameplay's source arbitration. No ready-to-run command is presented as though that missing schedule exists.

## Evidence needed before a manual launcher gate

- Observe the selected launcher's true zero-loaded entry and native completion, including all five statically known transfer call sites; a sampled state alone cannot exclude an intermediate native transfer.
- Prove a bounded zero-loaded hold and stop path that cannot immediately restart from state 2, cannot fire a round as a cancellation side effect, and restores exact native context. Resolve interruption, mode switch, reserve exhaustion, loss of focus/ownership, death and checkpoint reload explicitly. Positive-loaded rifle abort acceptance is not this proof.
- Identify the original animated mechanism/round subtree and physical aperture. Derive grasp, rail, open/closed limits and prop visibility from those measurements. Keep anatomical raw contact separate from guided hand/round previews to avoid self-latching.
- Validate one `0/reserve -> 1/(reserve-1)` request, no transfer before physical completion, a separate observed native receipt, and no automatic follow-up transfer. Then run the actual consumer fixture; headset feel comes after this native boundary and ownership test.

## Source map

Canonical evidence and code, all read-only in this task:

- `<local-workspace>\reports\reload-config-all-weapons-20261001.json` â€” saved live SPAS, scoped XM8 rifle and scoped launcher reflection; the filename is not evidence for every weapon.
- `<local-workspace>\reports\launcher-{bindings-live,switch-map,reflection,state-fields,mesh-links,asset-links}-20261001.json` â€” exact family/selection/mesh relationships.
- `<local-workspace>\docs\LAUNCHER-PILOT-20261001.md`, `LAUNCHER-BINDINGS-20261001.md`, `WEAPON-PROFILE-PIPELINE.md` â€” supported mode/sight limits and muzzle exception.
- `<local-workspace>\reports\reload-native-code-20261001.json`, `<local-workspace>\docs\BC2-PUMP-RELOAD-20261001.md` â€” native state/transfer/caller proof; state 2 empty-ammo entry.
- `<local-workspace>\reports\native-trace-20261002-040421-018` and `040613-711` â€” positive-loaded rifle cancel and native refill acceptance, not launcher acceptance.
- `<local-workspace>\src\games\bc2\Bc2ReloadAbort.h`, `Bc2MagazineReloadCycle.{h,cpp}`, `Bc2MagazineReload.h`, `Bc2ReloadFlowRuntime.{h,cpp}` â€” current exact guards and reusable lifecycle.
- `<local-workspace>\include\fvr\interaction\ManualReload.h` â€” existing semantic operation interface.

This folder contains only source code and metadata reports: archive index observations, selected asset reference strings, bounded field observations, cross-checks, and hashes. No canonical patch or new launcher native capability is included.
