# Checkpoint 412 fork handoff

This is the accumulated development source since GitHub checkpoint 390, plus
the presentation/launcher follow-up to the latest normal410 headset run. It is
not a public player release or a claim that the whole campaign is accepted.

## Current result

The user reported substantially better pumping and reliable manual shell
loading in normal410. Checkpoint412 lowers the shared chest-ammo anchor, places
the native loaded/reserve counter beside the gun hand, keeps the verified stock
loading-shell prop hidden after its native transaction finishes, and reduces
repeated launch storage scans. No new headset run was requested or performed.

Both full architecture builds pass **283 C++ suites each**. Additional checks:

| Check | Result and scope |
| --- | --- |
| Ammo HUD | D3D11 WARP pixel readback and mocked OpenXR lifecycle, stereo layer, pose and freshness pass; no headset |
| Installed body cache | 46 ammo/equipment parts reconstructed; all six matching magazine profiles pass below-face clearance, including XM8, AEK, suppressed/unsuppressed SCAR, F2000 and MP443; CPU geometry only |
| Storage accounting | 34 checks pass |
| Single-launch storage handoff | 19 checks pass, including a real hidden child process |
| Desktop launcher | 38 checks pass; no game/VR process started |
| Source package | 35 Python checks pass |
| Personal launch preflight | Frozen binary architecture/hash and 24 dependency pins pass; no launch |

The machine-local desktop launcher is updated to **0.1.2** and points to frozen
normal412 binaries. The previous app version and exact prior settings are
retained. This local deployment is distinct from a downloadable native runtime;
the GitHub checkpoint contains source only. The 12,957-byte launcher-only package
does not include the VR native runtime or game assets.

## Build and compare

Follow [BUILDING.md](../../BUILDING.md#checkpoint412-player-composition). Reuse
the x86/x64 trees, run them sequentially, and retain G-only scratch/storage
admission. Optional knife/grenade/contextual-tool code is compiled in the normal
composition, but those runtime opt-ins remain OFF in the current personal profile.

The exact source-bound native-operation receipt is
`profiles/checkpoint412/source-with-header.json`, SHA256 closure
`5d0542ffed1acdd2688df8b05534f9b465b4780fb233f52fb45aa07c31a1370d`.
Native offsets/ABI are unchanged from410: config1220 bytes, input3/frame7.
The [compact validation record](validation.json) also records the built outputs.

`checkpoint/410-headset-rollback` preserves the retained normal410 source at
`57e04e90e8d9f1d8f9448556db549725c888fa9a`. Its original binaries remain local.
Use that branch to inspect the smaller412 change; compare with `0e5ad46` for all
work since the last public checkpoint. Older `handoff/checkpoint245/pending`
payloads remain historical and must not be applied over current source.

## Where to look

| Area | Entry points |
| --- | --- |
| Current shell presentation | `Bc2ReloadPreview*`, `Bc2PhysicalReload.cpp`, `Bc2RigPublication.cpp`; [evidence](../../docs/SHELL-PRESENTATION-412.md) |
| Shared chest placement | `include/fvr/interaction/BodyAnchors.h`, `tests/Bc2BodyAmmoCacheTests.cpp` |
| Gun-side counter | `include/fvr/graphics/AmmoCounterPlacement.h`, `src/xr/OpenXrAmmoHud*`, `AmmoCounterHudWarpTests` |
| Launch overhead and bounds | `tools/StorageBudget.ps1`, `Start-BC2VRSession.ps1`, `Start-NativeTrace.ps1`, `launcher`; [launcher](../../docs/DESKTOP-LAUNCHER.md), [storage](../../docs/STORAGE-BUDGET.md) |
| Pump, shoulder and tracking recovery since390 | `Bc2PhysicalPump*`, `Bc2BodyHolster*`, `Bc2PumpPosePlan.h`, `Bc2PumpCompanionParts.h`; [408](../../docs/CAMPAIGN-DIAGNOSTICS-408.md) |
| Stereo damage/death since390 | `Bc2StereoColorScale.h`, `NativeProbe.cpp`; [410](../../docs/CAMPAIGN-DIAGNOSTICS-410.md), [411](../../docs/CAMPAIGN-DIAGNOSTICS-411.md) |
| Knife/tools and vehicle research | `Bc2ChestKnife*`, `Bc2ContextualTool*`, `Bc2VehicleShot*`; [409](../../docs/CAMPAIGN-DIAGNOSTICS-409.md) |

## Limits and next work

Spent-shell ejection is **not implemented**. Offline investigation found the
dedicated casing mesh and effect, but no verified SPAS ejection-port transform.
A local once-per-shot transient is feasible; it needs measured placement and
an explicit rigid-mesh source, not a guessed native event. Immediate post-shot
pump eligibility is deliberately unchanged, as the user allowed deferring it.

New chest/HUD/shell visual changes have automated coverage, not human visual
acceptance. Storage inventory improved27.08s to7.50s and now runs once per launch;
that is not an end-to-end cold game/headset startup measurement. Current body
placement still needs future subjective comfort assessment.

Prior bounded native results remain distinct:410 damage presentation,411 death
and player recovery,409 knife close, and selected gun/pump/bolt/vehicle checks.
They do not establish every weapon, belt-fed reloads, scope coverage, all campaign
missions, multiplayer, or sustained Steam Link stability. Raw captures, game
assets, saves, recordings and private configuration are intentionally excluded.
