# Launcher evidence and weapon-family research — October 1, 2026

This combines an offline review of saved source/reports with the root agent's
read-only inventory evidence. This research agent started no game input, memory
writes, native probes, or headset session.
The accepted SPAS/XM8 behavior and exact saved payload remain the baseline.

## What the latest test proves

The user accepts SPAS handling and XM8 grips, with small orientation polish
explicitly deferred. They reject launcher hand placement and support grip, and
request physically flipping its sight up to engage launcher mode. Folding it down
to return to the rifle is a proposed inverse interaction, not yet implemented or
accepted. See `GRENADE-LAUNCHER-INTERACTION.md` and
`reports/headset-grips-success-20261001-072621/acceptance.json`.

The completed `reports/native-trace-20261001-072623-921/native-trace.json` contains:

- 256 rolling hand records, all for item pointer 129298256, the XM8 in this process.
- 32 firing records for that same pointer. Sixteen phase-2 rows identify
  `XM8_sp_s`; phase-0/1 observer rows say `unclassified` because those old logging
  paths leave `weaponKind` at its default. That label does not prove an unknown
  item was equipped or fired.
- 7,173 supported/attached poses, eight grabs and eight releases, with zero
  support input-preservation failures.
- A validated 147-bone rig. Names identify `LeftHand`, `RightHand`, and
  `jntWpn_Flash`, but weapon children otherwise have generic numbered names.
- Final hooks disabled. Native animation source writes remain disabled.

The rolling hand history and capped firing history did not retain the user's
launcher segment. Those records alone cannot identify its asset, parent rifle,
sight pivot, handle, firing origin, or mode state. The subsequent read-only
inventory below establishes its exact asset name. Do not substitute an inventory
slot or process pointer for persistent asset identity.

## Exact launcher identity from current inventory

The root agent read the inventory of verified BC2 process 132784 without selecting
or firing equipment. The exact item name is **`40mmgl`**, at inventory slot 3 and
item pointer 129294672. The current selected item was slot 1, `XM8_sp_s`.
Evidence is recorded in `reports/weapon-pipeline-inventory-20261001.json`.

| Inventory slot | Exact asset name | Observed item pointer |
| --- | --- | --- |
| 0 | `SPAS12_sp` | 129294560 |
| 1 | `XM8_sp_s` | 129298256 |
| 3 | `40mmgl` | 129294672 |
| 4 | `LZ-537` | 129294784 |
| 7 | `KNV-1` | 129294896 |
| 8 | `HG-2` | 129295008 |

This establishes a separately enumerated launcher item in the current loadout. It
does not establish its parent-rifle relationship, sight/handle pivots, selected
mode behavior, launcher-specific muzzle transform, or compatibility with the
existing projectile hooks. The slot and pointer are observational context, not
a new hard-coded binding or a reason to enable features.

## Reuse already supported by the evidence

Sixteen October 1 traces with explicit SPAS/XM8 shot names share the same
147-bone `(name,parent)` topology, including the accepted headset trace. SHA-256
of compact JSON containing that ordered pair list is
`f9b38e2553c273342e7e8cac9ac72e4840276790bec8d3e5341a779cf8521949`.

Examples are `native-trace-20261001-044944-288` (SPAS),
`native-trace-20261001-050140-968` (XM8), and
`native-trace-20261001-072623-921` (latest headset test).
This supports one shared rig-validation, arm-solving, attachment-measurement and
pose-publication pipeline. It does not imply identical grip locations, animation
behavior, muzzle behavior, or launcher capability. The old SPAS-to-XM8 stale-grip
failure is direct evidence against sharing one fixed grip transform.

The validated weapon root in this rig is named `jntWpn_1`, parented to
`jntWpn_0`. `jntWpn_Flash` is a child of the root. Other weapon descendants use
`jntWpn_2` through `jntWpn_19`, including names `jntWpnwpnJnt_16` and
`jntWpnwpnJnt_14`. Names and ancestry must be resolved from each validated rig;
the observed array indices are not portable bindings. None of these generic
names establishes which geometry is a folding sight or launcher handle.

An older hand transition trace, `native-trace-20261001-054056-765`, includes 15
rows for pointer 129294672 (generations 1739–1876). This matches the pointer now
identified as `40mmgl`, so it is a useful *candidate* launcher segment. The old
rows do not carry an asset identity or establish allocation continuity across
time; the pointer match alone does not prove their historical item identity.

In that candidate segment the published right-wrist origin, transformed into the
placed weapon frame, has median `[-0.031910, -0.074189, -0.212549]` metres.
Earlier samples in the segment still reach the SPAS relation near
`[-0.029, -0.146, -0.267]`. This is consistent with the old equipment-transition
attachment contamination, but does not prove the launcher's correct grip. These
are reconstructed *published VR* positions from the pre-repair implementation,
not clean native authored handle transforms. They must not be imported as a
launcher profile. The trace has no per-item sight/handle bone samples.

## Current profile contract review

`include/fvr/interaction/WeaponProfile.h` and
`src/games/bc2/Bc2WeaponProfiles.{h,cpp}` now separate exact asset lookup from
portable aim axes and individual aim/support/muzzle verification. Only exact
`SPAS12_sp` and `XM8_sp_s` entries are enabled. Authored grip transforms remain
sampled from native animation after equipment settles, rather than being baked
from one controller posture.

This is appropriate for the measured baseline. A future launcher mode needs its
own verified attachment, aim and muzzle data. Its native equipment key and parent
rifle relationship belong in BC2. A missing sight/mode binding must remain unknown
without invalidating the rifle's independently accepted grip feature. Similar
skeletons or asset-name prefixes must never grant inherited native capabilities.

## Evidence still needed for a real flip-up interaction

1. Capture the exact equipped asset name and stable asset identifier, owner/equip
   generation, validated rig identity/topology, and native evaluated transforms
   before retargeting. Keep per-item summaries even if later VR pose publication
   fails. Record transitions as well as settled samples; a timed delay alone does
   not establish that the new animation has arrived.
2. Observe rifle → `40mmgl` → rifle with each item's identity and native selection
   state. The inventory already proves distinct item entries; determine their
   actual equipment/mode linkage. Confirm the launcher's association with the
   parent rifle and preserve native ammo/animation state. This mapping is not
   yet proven.
3. Compare each weapon child's root-relative transform across that transition.
   Correlate the changed child with the visible sight. A rotating numbered bone
   could also be a bolt, shell, stock or reload part. Require visible geometry or
   independently validated asset metadata before naming it the sight pivot.
4. Measure settled native right- and left-wrist transforms relative to the weapon
   root in both modes. These provide candidate authored trigger/support locations.
   Validate the launcher handle visually and through controlled grip/release; do
   not inherit the XM8 rifle contact point.
5. Recover the authoritative native selector or mode action and its completion
   signal. If BC2 needs a separate internal equipment slot, retain that backend
   while exposing sight manipulation to the player. Do not directly write a
   guessed equipped pointer or replace native selection/animation rules.
6. Independently verify launcher client/server firing origins and projectile
   behavior. `SupportedMuzzleShot` currently validates firing-function/config
   relationships and rejects special configurations. Adding a launcher name to
   the registry would not establish that this projectile path is compatible.
   Preserve native trajectory, ammo, spread and impact logic.

Once those bindings exist, portable interaction policy can handle hand contact,
intentional manipulation, angular thresholds/hysteresis and tracking-loss
cancellation. The adapter should acknowledge the native mode transition before
committing displayed mode, cancel on owner/equipment changes, and keep the native
reload/equip animation authoritative. Sight flip is not a thumbstick selection
feature. This research does not claim a working sight mechanism.

## Additional issue found in the completed trace

The trace records 11,695 rig fallback failures, 34,023 torso-stabilized attempts
and 22,328 published tracked poses. The difference equals the fallback count.
There were no successful partial poses. This deserves a separate regression;
it is not proof that all rejected poses were launcher frames.

Source inspection identifies a concrete path: `TrackedRig::Update` declines an
individual grip more than 1.5 metres from the current head even if its raw tracked
flag is true. `Bc2RigPublication` currently decides torso stabilization from the
raw flags before that validation. Torso writes can then change a rejected arm's
branch, causing the later exact-native fallback check to reject publication.
The retained successful samples do not contain rejected head/hand distances, so
the trace cannot prove how often this path ran or connect it to the launcher.

The repair should apply torso stabilization only when both *validated target*
hands remain available, leaving rejected/untracked branches byte-for-byte native.
It needs a regression with a raw-tracked but implausibly distant hand and the
other hand valid. Root was notified; this research does not modify runtime code.

## Pipeline implication

Capture authored native data once per item/mode, validate it in batches, group
compatible rigs for shared processing, and promote individual features only with
their evidence. This replaces repetitive hard-coded per-gun hand tuning. Unique
mechanisms such as the folding launcher sight still require one native binding
and acceptance check, after which the same interaction policy can be reused.
