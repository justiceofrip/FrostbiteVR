# Bounded weapon visibility diagnostic — 2026-10-01

Candidate only. No native attachment, game input, restart or headset test was performed by this agent. The physical reload checkpoint is unchanged.

`-WeaponVisibilityProbe` is explicit/default off, uses flag `0x40000000`, and only accepts a 15-second tracked static asynchronous stream. It rejects every other diagnostic, physical reload, body inventory, continuous sessions and pass-evidence mode. The current weapon must already be SPAS12_sp or XM8_sp_s; it never selects a weapon. Test each weapon in a separate owner-controlled run.

Root launch after a fresh game process and review:

```powershell
.\Test-NativeStream.ps1 -WeaponVisibilityProbe -Seconds 15 -Pairs 240 -StaticPose -Async
```

The existing native IPC receiver supplies fixed neutral tracked hands and paces 240 ordinary eye transfers over 14 seconds. It saves the existing eye pixels every 24 pairs and at the final pair; this adds no new graphics capture path. Gameplay uses the shared hand owner and its original IPC observed/deadline timestamps, independently retaining native equipment and physical hand equipment generations. Configured mesh snapshots retain their original observation deadlines.

Phases are 0 warmup, 1 ordinary baseline, 2 hidden, 3 restored ordinary, 4 done, 5 failed. Each phase requires two distinct actual paired Pack receipts and at least two seconds before advancing. A timestamp alone never proves hide/show. All nonweapon palette entries, including arms, must exactly match the ordinary private palette; hidden weighted entries must have zero consumed xyz and unchanged SIMD padding. A changed owner, stale input, altered original timestamp, missing configured mesh snapshot, unsupported weapon or 11-second sequence timeout clears the render intent. The completion phase disables visibility override. Cleanup disables before hook shutdown and again after callbacks drain.

The test suppresses Fire, AlternateFire/ADS, Reload, grenade and cycling commands through the existing verified input override. It reports actual native cache readback counts in `gameplay.weapon_visibility_action_suppression`; failures must be zero. No ammunition or native animation source is written.

Evidence is separated:

- `gameplay.weapon_visibility_probe`: immutable original input epochs, owner generations, phases, accepted paired receipts and unchanged ordinary entry counts.
- `gameplay.rig_publication.weapon_visibility`: actual verified private Pack counters; source changes and packing failures remain existing independent rejection counters.
- `weapon_visibility_eyes`: bounded scalar rows for actual native frame/eye, sampled phase before and after the original Draw, and separately retained receipt identity/deadline. Mixed-phase draws are omitted. These rows do not assert a RenderRequest-to-rig submission relationship.
- Receiver `pairs.jsonl` and `pair-*-eye-*.rgba`: actual eye pixels joined to scalar rows by native frame. Inspect visible→hidden→restored pairs for both eyes and unchanged hands. A paired Pack receipt alone is not visual acceptance.

Failure reasons: 1 clock, 2 sequence timeout, 3 stale/untracked input, 4 input rollback/restamp, 5 unsupported weapon, 6 configured snapshot missing/stale, 7 changed full owner, 8 owner/state cancellation, 9 session cleanup before completion.

`empty_hands_acknowledged`, `gpu_visibility_verified`, and `headset_verified` remain false. Muzzle flashes, casing particles, scope/HUD overlays and right-hand open-palm policy remain separate from mesh coverage. Native acceptance and the body-inventory gate are root-owned next steps.

Validation: five focused policy groups on x86/x64; 38 offline launcher/parser checks; x86 Gameplay, NativeProbe and NativeTrace translation units and x64 NativeIpcProbe compile. New BC2Camera source: `src/games/bc2/Bc2WeaponVisibilityProbe.cpp`. Test executable: `Bc2WeaponVisibilityProbeTests`, linked to BC2Camera. Root must run full required builds after integration.
