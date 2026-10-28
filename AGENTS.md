# FrostbiteVR agent rules

This file is the single source for these rules. Other docs link here instead of
restating them.

## What to read (and where to stop)

- Always: [docs/CURRENT.md](docs/CURRENT.md).
- Changing native integration: also [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
  (contracts and invariants only).
- Everything else is on demand. `docs/history/`, `docs/archive/`, `handoff/`,
  `third_party/` and profile/coverage JSON are hidden from default search by
  `.ignore`. Read them by path when a task names a checkpoint or run (R55, 426),
  or search with `rg --no-ignore`. Never read a history file whole.
- Never read these whole. Grep for the symbol, then read a range:
  `src/games/bc2/NativeProbe.cpp`, `Bc2Gameplay.cpp`, `Bc2ReloadFlowRuntime.cpp`,
  `Bc2RigPublication.cpp`, `src/platform/windows/NativeIpcProbe.cpp` (each
  120-380 KB), `CMakeLists.txt` (95 KB), and any `tests/*Tests.cpp` over 50 KB.

## Architecture and native safety

- Keep VR policy/math, engine integration, graphics, XR runtime and game rules
  separate. Do not add Refractor native offsets/types/rig indices to this port.
- Verify native signatures, ABI, object relationships and ownership before hooks.
  A new executable hash neither authorizes writes nor proves incompatibility.
- No simulation replay for a second eye. Restore temporary camera, palette and
  callback-context overrides on every outcome with exact-write restoration.
- Persistent VR equipment state is not a temporary override: keep magazine
  custody, ammunition transactions, holster ownership and owed pump/bolt steps
  across frames until they actually complete or hit an explicit lifetime
  transition. Never reset them to hand control back to stock animation or on a
  repeated controller packet. Never roll back a completed native transfer.
- Preserve unrelated native animation. Manual mechanics may suppress or replace
  conflicting stock reload/cycle animation through a verified adapter. Keep
  actor, prediction and object-lifetime checks separate from write cleanup.
- Every enabled native feature needs specific evidence. Unsupported features
  stay disabled with an explicit status.

## Evidence

Every claim names its level: deterministic test, actual-game check, or headset
acceptance (definitions in CURRENT.md). A mock receipt, static profile,
capability count, final ammo count or passing unit test does not prove native
success. Keep failed episodes; a later pass does not rewrite an earlier failure.

## Storage (local development policy)

- C: is the small OS drive. Keep installs, updates, generated files, logs and
  temp/compiler scratch on G:. Set process-local TEMP/TMP/TMPDIR so children
  inherit them. Never change Windows-wide settings, move other applications'
  files, duplicate runtimes onto C:, or require free space on C: for G:-only work.
- This is workstation policy, not a product rule. Shipped installers and runtime
  caches follow the player's chosen drive, including C:. Never hardcode G:.
- Call `tools/StorageBudget.ps1` before every build, capture or session,
  including private wrappers. Never bypass a refusal. Limits and retention:
  [docs/STORAGE-BUDGET.md](docs/STORAGE-BUDGET.md).
- Raw captures are opt-in and limited to the event under investigation. Budget
  frames x size x eyes x streams before launching.
- Reuse the existing x86/x64 build trees. Keep source patches and small result
  summaries, not copies of build directories. Before pruning, inspect contents
  and current references: old directory names can hold current build output.
  Keep runtime symbols, source, checkpoint receipts and user recordings.

## Repository hygiene

- Never commit game binaries/assets, saves, local config or credentials.
- Do not modify the BF2142 project or its cloud/launch configuration.
- Run the relevant deterministic tests, then `Build.ps1` for x86 and x64. Do not
  rerun passed suites for docs-only changes or unchanged binaries. Headset and
  runtime claims need an actual game and headset check.
- Edit text files with the editor tools, not Windows PowerShell 5.1
  `Get-Content`/`Set-Content`: that round trip corrupts UTF-8 (an em dash
  becomes `â€”`). A script that writes text uses
  `[IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))`.

## Keeping docs small (this is what keeps sessions usable)

- `docs/CURRENT.md` is state, not a log. Edit the affected bullet in place,
  delete what is no longer true, and keep it under about 250 lines. When a run
  finishes, CURRENT gets one line (result, run folder, what it does not cover).
  Hashes, receipt paths, counts and audit details stay in the run's receipt or
  `handoff/` README, not in CURRENT.
- `docs/ARCHITECTURE.md` holds only durable contracts and invariants. A
  candidate's design note (R57, ...) goes in a short dated file in
  `docs/history/`, not in ARCHITECTURE.
- A decision worth keeping gets one short dated file in `docs/history/` (under
  40 lines). Do not also copy it into CURRENT.
- Notes from one investigation go in `docs/archive/`, named with the checkpoint
  or date. Do not add new top-level files to `docs/` for them.
- Never snapshot CURRENT.md into history and point CURRENT at the snapshot.
  Delete stale lines instead; git keeps the old text.
- When a doc you touch states something CURRENT contradicts, fix or delete that
  line in the same change.
