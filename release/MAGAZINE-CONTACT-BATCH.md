# Authored magazine contact batch

The batch reuses the same paired reload-grasp generator as the accepted AEK experiment. New candidates are offline data, not enabled weapons.

The inspected roster contains 32 exact native asset names and 76 configuration definitions. It produces 27 paired state profiles across 13 names and seven base weapon meshes. Five additional base models have candidates: **9A-91, AKS-74U, M416, UZI and XM8 Compact**. MP/SP names and optics variants are counted separately in the table; they are not additional guns.

| Exact native name | Configs | Capacity / authored reload type | Paired profiles | Hand + magazine / entry | Remaining gap |
|---|---:|---|---:|---|---|
| `9A91` | 3 | 20 / rtMagazine | 3 | authored pair + closed part / geometry estimate | native/runtime review pending |
| `AEK971` | 3 | 30 / rtMagazine | 3 | authored pair + closed part / geometry estimate | native/runtime review pending |
| `AEK971_sp` | 1 | 30 / rtMagazine | 1 | authored pair + closed part / geometry estimate | native/runtime review pending |
| `AEK971_sp_k` | 1 | 30 / rtMagazine | 1 | authored pair + closed part / geometry estimate | native/runtime review pending |
| `AKS74u` | 3 | 30 / rtMagazine | 3 | authored pair + closed part / geometry estimate | native/runtime review pending; main weapon grip is nonstatic/unadmitted |
| `AKS74u_sp` | 1 | 30 / rtMagazine | 1 | authored pair + closed part / geometry estimate | native/runtime review pending; main weapon grip is nonstatic/unadmitted |
| `AKS74u_sp_k` | 1 | 30 / rtMagazine | 1 | authored pair + closed part / geometry estimate | native/runtime review pending; main weapon grip is nonstatic/unadmitted |
| `AN94` | 3 | 30 / rtMagazine | 0 | unresolved | no unique rigid magazine role |
| `AN94_sp_k` | 1 | 30 / rtMagazine | 0 | unresolved | no unique rigid magazine role |
| `AN94_sp_s` | 1 | 30 / rtMagazine | 0 | unresolved | no unique rigid magazine role |
| `AUG` | 3 | 30 / rtMagazine | 0 | unresolved | No stable authored magazine contact interval |
| `F2000` | 3 | 30 / rtMagazine | 0 | unresolved | no unique rigid magazine role |
| `F2000_sp` | 1 | 30 / rtMagazine | 0 | unresolved | no unique rigid magazine role |
| `F2000_sp_k` | 1 | 30 / rtMagazine | 0 | unresolved | no unique rigid magazine role |
| `G3` | 6 | 20 / rtMagazine | 0 | unresolved | mesh inventory missing |
| `Garand` | 2 | 8 / rtMagazine | 0 | unresolved | Deferred en-bloc mechanism candidate; native rtMagazine alone does not establish box-magazine geometry or underbarrel support |
| `M16` | 3 | 30 / rtMagazine | 0 | unresolved | no unique rigid magazine role |
| `M16k` | 3 | 30 / rtMagazine | 0 | unresolved | no unique rigid magazine role |
| `M1A1 Thompson` | 2 | 30 / rtMagazine | 0 | unresolved | mesh inventory missing |
| `M416` | 3 | 30 / rtMagazine | 3 | authored pair + closed part / geometry estimate | native/runtime review pending; main weapon grip is nonstatic/unadmitted |
| `Mk14EBR` | 6 | 10 / rtMagazine | 0 | unresolved | No stable authored magazine contact interval |
| `PP2000` | 3 | 40 / rtMagazine | 0 | unresolved | no unique rigid magazine role |
| `SCAR` | 3 | 30 / rtMagazine | 0 | unresolved | No stable authored magazine contact interval |
| `SCAR_sp` | 1 | 30 / rtMagazine | 0 | unresolved | No stable authored magazine contact interval |
| `SCAR_sp_s` | 1 | 30 / rtMagazine | 0 | unresolved | No stable authored magazine contact interval |
| `UMP` | 3 | 25 / rtMagazine | 0 | unresolved | no unique rigid magazine role |
| `UMPk` | 3 | 25 / rtMagazine | 0 | unresolved | no unique rigid magazine role |
| `UZI` | 3 | 32 / rtMagazine | 3 | authored pair + closed part / geometry estimate | native/runtime review pending; main weapon grip is nonstatic/unadmitted |
| `XM8` | 3 | 30 / rtMagazine | 3 | authored pair + closed part / geometry estimate | native/runtime review pending |
| `XM8C` | 3 | 30 / rtMagazine | 3 | authored pair + closed part / geometry estimate | native/runtime review pending |
| `XM8_sp` | 1 | 30 / rtMagazine | 1 | authored pair + closed part / geometry estimate | native/runtime review pending |
| `XM8_sp_s` | 1 | 30 / rtMagazine | 1 | authored pair + closed part / geometry estimate | native/runtime review pending |

The exact configuration paths, GUIDs and content hashes are in [CONTACT-COVERAGE.json](CONTACT-COVERAGE.json). No game vertices, animation curves, local machine paths or generated runtime headers are included there.

## What the positions mean

- **Extracted:** exact typed config → animation/skeleton/mesh references; constant closed magazine frame; the wrist-to-magazine relation and all 15 finger transforms from one reload frame, selected within a stable 200 ms authored interval.
- **Estimated:** insertion end/axis comes from the rigid magazine shape and its proximity to the weapon body. The linear magnetic rail and its travel/tolerances are shared VR design values, not the original curved animation path.
- **Independent:** chest/belt supply placement is shared body-inventory policy. No gun animation determines the player’s chest slot. Main weapon grip/support is also separate: M416 has a reload-contact candidate but nonstatic HandsIkPose data, so this batch does not make its main gun grip usable.
- **Missing where reported:** a unique rigid magazine part, stable authored contact, or exact mesh inventory. Some records have decoded spline HandsIkPose controls; a separate reload-reference schema preserves those references without admitting them as static weapon grips.

Native `rtMagazine` describes a pooled reload, not necessarily a removable box. Garand stays deferred as an en-bloc mechanism. This task grants no underbarrel, chambering, muzzle, holster, or multiplayer capability.

## Repeat a selected batch

Use the existing installed inventory, common-metadata export, and hand-pose batch. Select exact names explicitly:

```powershell
python tools/bc2_magazine_contact_batch.py --game "<BC2 installation>" `
  --common-metadata private/common-metadata.json --inventory private/installed-inventory.json `
  --hand-poses private/hand-poses.json --asset 9A91 --asset M416 --asset XM8C `
  --output private/contact-batch
```

Output consists of private reload-reference bindings, measured/estimated candidate geometry, exact mesh joins and a compact coverage report. It does not edit the game, create a runtime header automatically, or change accepted profiles. Optional later header generation still requires an explicit asset allowlist and independent native registration.

Only the exact `mp_common/level-00.fbrb` archive receives the existing bounded 640 MiB streaming allowance. Other archives keep the 512 MiB limit, selected reads stay at 32 MiB, and the ephemeral batch cache is bounded at 256 MiB. No extracted resource bytes are written to disk.

The new five-profile private header passes shared rigid-transform and rail-closure checks on x86 and x64. Seventy-six configuration definitions and 27 profile rows do not imply campaign readiness. Candidate contact previews retain visible residual intersections, and their current native hand-skin identity is unverified.
