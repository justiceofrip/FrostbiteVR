# Frostbite VR development rules

Read AGENTS.md and docs/CURRENT.md before modifying this workspace. Also read
docs/ARCHITECTURE.md when changing native integration. The logs in docs/history/
are append-only history, not required reading; search them by checkpoint number.

- Keep VR policy/math, engine integration, graphics, XR runtime, and game rules
  separate. Do not add Refractor native offsets/types/rig indices to this port.
- Verify native signatures, ABI, object relationships and ownership before hooks.
  New executable hashes alone neither authorize writes nor prove incompatibility.
- No simulation replay for a second eye. Restore temporary camera, palette and
  callback-context overrides on every outcome using exact-write restoration.
- Persistent VR equipment state is not a temporary override: retain magazine
  custody, ammunition transactions, holster ownership and owed pump/bolt steps
  across frames until their actual completion or explicit lifetime transition.
  Never reset these merely to return control to stock animation or on a repeated
  controller packet. Completed native transfers must not be rolled back.
- Preserve unrelated native animation. Requested manual mechanics may suppress
  or replace conflicting stock reload/cycle animation through a verified adapter;
  stock animation is not a required owner of a VR interaction. Keep actor,
  prediction and object-lifetime checks separate from temporary write cleanup.
- Every enabled native feature needs specific evidence; unsupported features
  remain disabled with an explicit status.
- Do not modify the working BF2142 project or its cloud/launch configuration.
- User storage policy: C: is the small OS drive. Keep BC2 VR installs, updates,
  generated files, logs and temporary/compiler scratch on G:. Set process-local
  TEMP/TMP/TMPDIR before work; children must inherit them. Never require free
  space on C: for work that writes only to G:, and never change Windows-wide
  environment settings or move other applications' files to accomplish this.
  Existing Windows/runtime files may be read; do not duplicate runtimes onto C:.
- Do not include game binaries/assets or private credentials in source.
- Run relevant deterministic regressions, then Build.ps1 for x86 and x64.
  Headset/runtime claims require actual game and headset checks.
- Overwrite docs/CURRENT.md to reflect the new state, including implementation
  and validation gaps. Append a short dated entry to docs/history/ only for
  decisions worth keeping.
- Storage is bounded: call tools/StorageBudget.ps1 before builds, captures or
  sessions, including private recovery wrappers. Do not bypass a failed budget.
  Use the existing x86/x64 build trees; archive source patches and small result
  summaries instead of copying whole build directories for every checkpoint.
- Keep raw captures opt-in and restricted to the event being investigated.
  Budget their actual frame count, dimensions and bytes before launching. The
  default limit is 512 MiB per capture run and 40 GiB of generated disk usage;
  preserve at least 15 GiB free on output and temporary-file volumes. See
  docs/STORAGE-BUDGET.md. Review retention before increasing these limits.
- Preserve current runtime symbols, source, useful checkpoint receipts and user
  recordings. Rebuildable intermediate/test-symbol files need not be archived.
  Inspect contents and current references before pruning: old directory names
  can still contain current build output.
