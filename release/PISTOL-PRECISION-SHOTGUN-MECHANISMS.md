# Pistol, precision-rifle and shotgun authored data

This batch inspected **73 exact configuration definitions, 30 native names and
variants**. It produced 46 mechanism records for 11 decoded body models. These
counts are not playable weapons. No native feature or new runtime header was
enabled. Full metadata and explicit failures are in
[PISTOL-PRECISION-SHOTGUN-COVERAGE.json](PISTOL-PRECISION-SHOTGUN-COVERAGE.json).

| Body model/family | Concrete extracted result | Remaining gate |
|---|---|---|
| M9, including M9-3 | Independent magazine `jntWpn_12`; rigid slide `jntWpn_3`, about 72 mm travel; complete diagnostic magazine/hand pose | Strict 60 Hz grasp check fails; no separate slide-stop actuation proved |
| MP443 | Independent magazine `jntWpn_12`; rigid slide `jntWpn_3`, about 72 mm travel | No stable complete magazine contact under current criteria; slide-stop semantics unresolved |
| M1911 | Exact configuration/reference gap retained | Beach mesh has no exact geometry binding in this inventory |
| MP412 | Native bulk-reload metadata retained separately from cylinder mechanism | Unsupported animation wrapper; no removable-box or cylinder admission |
| M95 | Static magazine plus complete paired wrist/fingers and insertion candidate | Native bolt family, insertion-axis review and headset validation |
| SV98, SVU, VSS | Magazine distinguished from another moving geometric candidate using unique complete paired contact | Native binding, axis/skin review and runtime checks |
| M24 | Bolt and magazine part motion decoded | Neither ambiguous part passes strict paired contact |
| GOL | Magazine motion and diagnostic hand contact decoded | Strict near-seat withdrawal witness fails |
| Type 88 (`QBU88`) | Magazine and 142 mm slider motion decoded | Strict paired magazine contact fails |
| Saiga (`S20K`) | Magazine motion and 105 mm slider candidate decoded | No strict paired contact resolves the two geometric candidates |
| USAS12 | Moving drum and diagnostic paired hand contact decoded | Strict detachable-magazine role unresolved |
| 870, NS2000, T194 | Exact single-round/bolt-action metadata retained | Unsupported animation wrapper; no invented pump/round poses |
| SPAS12 | Existing accepted runtime behavior is unchanged | Generic `1P_Reload` resource is absent; resolve the authored staged tube-reload nodes rather than fabricate this clip |

The generated magazine candidates cover 17 exact configuration rows, seven native
names and **four body models: M95, SV98, SVU and VSS**. Private header validation
passes on x86 and x64. The header does not enable these weapons. A main weapon
grip, native firing/reload family, exact current inventory/mesh and restoration
proof remain independent requirements. M95/SV98 bolt-action dispatch is not
admitted by the automatic-rifle family registry.

## One shared role improvement

The geometric filter can find both a bolt and a magazine because both are rigid,
move during reload and have similar dimensions. In explicit paired-grasp mode,
the extractor now tests every already-qualified geometric candidate against the
same complete wrist/finger contact criteria. It selects a part only if exactly
one succeeds. Multiple successful contacts or no contact remain an error; no name
or bone number selects the magazine. Existing unique-role results stay unchanged.
This resolves SV98/SVU/VSS through data, without per-weapon controllers or relaxed
shape/contact tolerances.

The 30 Hz mechanism sampler and strict 60 Hz magazine sampler remain distinct.
For example, M9 has a diagnostic contact but no strict 200 ms interval. That is
not silently promoted to an accepted grasp. All poses retain complete same-frame
wrist, item and 15-finger relations. Static closed transforms come from the exact
authored binding. Linear entry rails are geometry estimates and shared VR assist
settings, not native constraints. CPU previews can contain skin intersections.

## Chambering and physical feed families

Pistol eject/insert/chamber and slide-stop interactions are requested capabilities,
not proven native state. Current native loaded ammunition is pooled; this work
does not create a separate chamber round or add a bonus round during tactical
reload. Tactical chamber retention needs explicit authoritative state and count
proof. A static lever or slide animation is not that proof.

`rtMagazine` describes native bulk transfer, not a removable box. The MP412 remains
a separate cylinder job. Pump shotguns use single-round transfer in these authored
definitions; magazine-fed shotguns remain separate. Underbarrel buckshot stays a
capacity-one launcher-family job, not a tube shotgun. Rifle charging handles and
manual underbarrel reload remain deferred.

## Reproduce and validate

Use `inspect_lmg_common.py --all-weapons` to obtain current common metadata, then
run the existing `bc2_authored_mechanism_geometry.py` with exact names. For example:

```powershell
python -B tools/bc2_authored_mechanism_geometry.py --game "<installation>" --metadata "<common-metadata.json>" --inventory "<weapon-inventory.json>" --hand-poses "<hand-poses.json>" --output "<private-output>" --asset M9 --asset M9-3 --asset MP443 --asset MP412 --asset M1911
python -B -m unittest discover -s tests -p test_bc2_paired_role.py
```

The private output holds exact clip/mesh/skeleton hashes, parent-local part motion,
full contact matrices and unresolved rows. The public report contains only derived
metadata; game bytes, generated headers and images are excluded from source and
runtime packages. The source change has four focused role tests, including actual
synthetic clip sampling and multiple/no-contact rejection, plus the existing
magazine/drum regressions. No native or headset test occurred in this batch.
