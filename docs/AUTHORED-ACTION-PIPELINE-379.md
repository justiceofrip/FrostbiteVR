# Shared authored action pipeline — 379

The reusable action batch resolves named replacements from each exact weapon
configuration's authored animation tree. It uses the same source resolver as
magazine reload extraction, then applies the existing part-motion, hand-contact
and linear-rail measurements. Adding another weapon does not require copying a
controller state machine or borrowing the XM8/SPAS coordinates.

`tools/bc2_authored_action_geometry.py` accepts existing reference bindings and
mesh bindings. For example, with locally prepared evidence files:

```powershell
python tools/bc2_authored_action_geometry.py `
  --game 'G:\New folder (2)\Battlefield Bad Company 2' `
  --bindings reference-bindings.json --mesh-bindings mesh-bindings.json `
  --role 1p_boltaction --role 1p_reload --role 1p_reload_in `
  --role 1p_reload_out --role 1p_deploy --output action-candidates.json
```

Optional repeated `--asset` arguments limit the selected native asset names.
The output must be new, so completed evidence is not silently overwritten.
Installed game assets are read locally; the tool writes derived measurements
and provenance, not extracted animations or meshes.

## What remains shared

- Typed tree, animation GUID and skeleton checks resolve action roles from the
  authored `Name`; array position or a similarly named loose file is insufficient.
- Archive indexes and selected resource hashes preserve the source identities.
  Identical cross-archive clips can be reused; conflicting contents reject.
- Each exact weapon/configuration/state/mesh identity retains its own result.
  Numeric work is cached only for equal mesh, LOD, animation, reference pose and
  skeleton contents. Sharing a rig does not share native weapon identity.
- Original parent relationships distinguish a rotating part from movement of
  its ancestor. Cross-part weighted geometry remains a coupled-assembly gap.
- Existing sampled left-wrist/finger contacts remain candidates. Linear-rail
  bounds come from the complete decoded control intervals. The two kinds of
  evidence are labelled separately.
- Missing replacement roles, context failures and action failures are recorded
  separately. A missing BoltAction on an automatic rifle is not proof of a
  broken file or a manual chambering implementation.

The graph reader batches its document reads because a common archive is a
compressed stream. Reading each small DBX separately would decompress that
stream repeatedly. A stopped initial survey exposed this cost; the corrected
batch preserves the same validation and groups each tree's dependencies.

## Validation

The installed-data batch covers 169 configuration/mesh entries, 73 native asset
names (including aliases) and 39 weapon models. It produces 427 action records
from 93 distinct content jobs in 416.84 seconds: 166 reload, 169 deploy, 42
BoltAction, 25 reload-in and 25 reload-out records. The 139 linear candidates
reduce to 26 distinct mesh/clip/part combinations. These are measurements, not
139 enabled mechanics or 73 accepted guns.

All 845 requested profile/role combinations are accounted for: 427 records,
415 absent roles and three failures. The failures are PP2000 reload variants
whose decoded orientation violates the existing unit-quaternion check. The
check remains intact and no substitute pose is admitted. Public derived
coverage is in `profiles/action379/coverage.json`; the detailed local report is
`bc2vr-recovery/action379-batched/actions.json`.

All 228 relevant Python tests pass, including cross-configuration cache reuse,
local GUID references, wrong graph/skeleton identities, duplicate roles and
partial batches. Both normal builds pass all 246 C++ suites (x86 24.83 s,
x64 14.45 s). The refactored reload reader reproduces all five checkpoint378
output files byte for byte: 26 mechanism records in five content jobs, plus
the same magazine, reference, mesh and coverage results.

The native player remains checkpoint375; these tools do not enable additional
weapons or change the headset build. A complete native reload/pump/bolt action
still requires its verified engine binding and integration evidence. Human
controller alignment and campaign acceptance remain open. See
[current coverage](WEAPON-COVERAGE-CURRENT.md) and
[player375 evidence](SHARED-PUMP-GEOMETRY-375.md).
