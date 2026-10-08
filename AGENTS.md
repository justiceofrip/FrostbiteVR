# Frostbite VR development rules

Read README.md, docs/ARCHITECTURE.md, docs/2142-TRANSFER.md and docs/HANDOFF.md
before modifying this workspace.

- Keep VR policy/math, engine integration, graphics, XR runtime, and game rules
  separate. Do not add Refractor native offsets/types/rig indices to this port.
- Verify native signatures, ABI, object relationships and ownership before hooks.
  New executable hashes alone neither authorize writes nor prove incompatibility.
- No simulation replay for a second eye. Restore native state on every outcome.
- Preserve native animation; use actor generation and exact-write restoration.
- Every enabled native feature needs specific evidence; unsupported features
  remain disabled with an explicit status.
- Do not modify the working BF2142 project or its cloud/launch configuration.
- Do not include game binaries/assets or private credentials in source.
- Run relevant deterministic regressions, then Build.ps1 for x86 and x64.
  Headset/runtime claims require actual game and headset checks.
- Update the transfer ledger and handoff with implementation and validation gaps.