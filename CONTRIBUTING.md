# Forking and contributing

Fork the repository and start with the offline builds in [BUILDING.md](BUILDING.md).
Read [STATUS.md](docs/STATUS.md), [ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[AGENTS.md](AGENTS.md) before changing native integration.

| Area | Location | Responsibility |
| --- | --- | --- |
| Shared interactions | `include/fvr/interaction`, `src/interaction` | Hand claims, grips, ammo supply, guided insertion, body inventory and mechanism policy |
| Math and frame ownership | `include/fvr`, `src/math`, `src/runtime`, `src/ipc` | Tracking transforms, stereo, lifetime and frame transport |
| Frostbite discovery | `src/engines/frostbite` | Discovery/validation patterns; layouts still require per-game proof |
| BC2 adapter | `src/games/bc2` | Native camera/render, input, rigs, selection, reload and restoration |
| Graphics and XR | `src/graphics`, `src/xr`, `src/platform/windows` | D3D11 transfer, presentation, OpenXR and Windows integration |
| Asset/weapon pipeline | `tools/bc2_*`, `profiles` | Exact configuration identities, components, authored contacts and coverage reports |
| Regression tests | `tests` | Shared-policy, adapter-fixture and offline tool checks |

## Work that helps most

- Extend repeated/interrupted reload and holster/pickup coverage through the real
  persistent consumers, rather than adding another weapon-name state machine.
- Join new AR/SMG data by exact configuration and shared components. A display name
  does not prove identical attachments, geometry or native behavior.
- Integrate existing feed/action groundwork for pump, bolt, charging-handle and
  belt-fed mechanisms after verifying the corresponding native operations.
- Investigate physical zoomed scopes, optic-dot visibility, full-body IK and
  campaign/vehicle coverage with measured references and reproducible evidence.
- For future Frostbite games, begin with an isolated adapter and a minimal native
  rendering/input milestone. Follow [the porting roadmap](docs/FROSTBITE-PORTING-ROADMAP.md).

## Validation expectations

Describe the trigger, expected behavior, actual behavior, affected shared layer
and tests. Separate deterministic tests, actual-game automated checks and human
headset acceptance. Keep failed evidence and limitations explicit. Do not infer
native success from a mock receipt, a static profile or final ammunition alone.

New native bindings need executable/signature, ABI, ownership and restoration
evidence. Shared Frostbite branding does not establish compatible offsets or rig
indices. Do not advance simulation for a second eye.

Keep changes scoped and run the relevant regressions plus both full architecture
builds. Pull requests should explain which behavior changes and which coverage
remains pending. Use synthetic fixtures for CI; never upload installed game files,
generated meshes/textures, saves, local configuration, credentials or unreviewed
recordings/traces. Raw diagnostic reports may contain local paths and game images.

MIT and dependency copyright notices must be retained. Contributions do not need
a multiplayer or server dependency for the single-player adapter.
