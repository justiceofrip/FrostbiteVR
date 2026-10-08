# Installed whole-weapon display batch

The existing weighted closed-geometry baker and persistent carried-slot consumer now have sixteen additional exact name rows. Nine additional base models supply measured geometry: XM8 Compact, F2000, M416, SCAR, M14 EBR, M16, PP2000, UMP and 9A-91. The remaining rows include aliases and previously measured AEK/XM8 geometry; they are not additional guns.

Every new row matches existing exact configuration, mesh, clip and skeleton content hashes. New enrollment requires constant authored transforms for the complete weapon geometry, right-hand handle and muzzle anchor. No frame time or transform is guessed. UZI, AUG and AKS variants are excluded because their right-hand anchors are nonconstant; AN94 is excluded because its closed geometry controls are nonconstant. Configuration shapes with additional mesh/state requirements are separately recorded in batch-evidence.json. Those exclusions are not playable coverage.

The six previously prepared ammunition/equipment rows remain identical. Their 6,792,891 cache record bytes remain identical. The new private cache is 10,528,200 bytes and contains twenty-two parts, including nineteen whole-weapon rows. Installed geometry remains private and must not enter the public source package.

The shared cache parser's catalog cap rises from sixteen to sixty-four entries. The 16 MiB total cache limit, eight sections per entry, exact identity/hash checks, duplicate rejection, and eight simultaneously drawn body instances remain unchanged. Catalog entries represent available geometry; they do not imply sixty-four displayed props.

The baker accepts explicit spec batches and optional strict anchor validation. Legacy preparation retains its reviewed authored frame-zero contract, preserving the existing SPAS baseline; all new rows passed strict anchor validation. This changes data extraction and catalog capacity, not per-weapon interaction behavior.

Full isolated Build.ps1 builds pass 157/157 CTest suites on both x86 and x64. Actual nineteen-row carried-source/host append tests reject stale identities and expired sources. Both final architecture binaries separately load the private twenty-two-part cache through the production parser. Four shared Python geometry groups pass. This is offline validation; no game/headset test occurred.

Native selected-weapon suppression remains unadmitted for new rows. The existing profile acceptance mask remains 3. Display enrollment does not grant firing, reloading, gripping, equipping or hiding authority. A fresh native-owned unselected item can use these rows through the existing carried consumer after later integration; this candidate leaves the prepared headset test unchanged.

Integration requires the tool, both generated headers, reviewed metadata catalog, parser cap change and new private cache together. Source/test operations and hashes are frozen separately. Never distribute private/body-ammo.fvrprop. Transferable work is shared batch validation and geometry-table enrollment; future Frostbite adapters still need their own native inventory and visibility proof.
