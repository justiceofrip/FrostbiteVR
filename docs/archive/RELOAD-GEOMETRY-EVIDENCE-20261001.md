# Reload item and insertion geometry evidence — October 1, 2026

Saved native poses provide concrete animated-component trajectories, but they do
not yet identify a shell mesh, magazine prop, authored ammo grip or insertion
socket. No BC2 insertion profile has been created or enabled. This work reads
saved reports only; it performs no game/process operation.

## Available data and reproducible extraction

`tools/audit_reload_geometry.py` validates complete named weapon subtrees with the
existing rigid-matrix/hierarchy helpers. It keeps asset, skeleton, actor, weapon,
owner generation, space and capture episode separate, rejects duplicate/out-of-order
samples and malformed matrices, and converts translations to metres. It extracts
bone-to-weapon and bone-to-native-wrist relations without using placed VR arms.
Native hidden/collapsed leaves are counted rather than inverted. A noncollapsed
bone is not proof that its mesh section was visible.

The latest SPAS trace has94 authored samples,20 named weapon-subtree bones and a
147-bone full first-person skeleton. Its matched native reload window contributes
43 complete, settled samples. The older accepted reload contributes37 samples.
Both use skeleton fingerprint `fnv1a64:a7f219a1426216ab`, but retain separate
capture-local actor/item identities. Reports:

- `reports/reload-geometry-20261001-145328.json` links the source trace hash and
  derives its interval from matched native reload updates.
- `reports/reload-geometry-20261001-112223.json` links the older trace hash. Explicit
  bounds457278109..457282062ms come from the first reload-begin and subsequent
  ready transition in `reports/reload-native-audit-20261001-112223.json`.

Run the first report with:

```powershell
python -B tools/audit_reload_geometry.py --native reports/native-trace-20261001-145328-952/native-trace.json --asset SPAS12_sp --output reports/reload-geometry-20261001-145328.json
```

For the older trace, add `--window-ms 457278109 457282062`. The extractor uses
whole reload spans, including the post-reload sequence; motion within those spans
is not automatically an insertion gesture. It does not average animated poses
into one alignment constant or emit a runtime profile.

## Measured component leads

Four generic weapon bones move in both reload captures. Values below are maximum
translation from each capture's first reload observation, not an inferred rail
travel or a complete geometric extent.

| Native bone | Earlier capture | Latest capture | What the data establishes |
| --- | ---: | ---: | --- |
| jntWpn_3 | 69.16mm | 68.49mm | Nearly translation-only animated component |
| jntWpn_4 | 94.32mm | 94.08mm | Nearly translation-only animated component |
| jntWpn_5 | 83.56mm | 83.27mm | Animated translation plus roughly6deg rotation |
| jntWpn_7 | 272.96mm | 301.41mm | Strongest relationship to native left-hand motion; roughly50deg gun-relative rotation |

`jntWpn_7` is the best **unidentified component lead**, not a verified shell.
Its left-wrist-relative transform still changes by77–87mm and36–38deg during the
full reload. Neither capture has three contiguous observations spanning at least
150ms with that relation within3mm/3deg. A constant item-to-wrist transform cannot
be extracted from these samples with those tolerances. It may be independently
animated or follow a finger-level grasp, or represent another component; the
saved evidence does not decide this. The older capture's brief stable relation
for jntWpn_5 is likewise not a semantic attachment proof.

The rig labels are generic `jntWpn_N`, with only the previously proven weapon root
and `jntWpn_Flash` roles. There are no explicitly named shell, magazine, ammunition
or insertion-socket bones. Several unused-looking generic nodes coincide with the
weapon origin, which is not a reason to repurpose them. A straight component
trajectory is not proof of an ammunition entry axis.

## Missing evidence before correct hand placement

The saved SPAS configuration provides FireLogic, AmmoConfig and visual-state flags
such as IsPumpAction and SkipReloadAnimation. It does not contain SPAS first-person
mesh names, submesh bounds, vertices/skin weights, or a live ammo-prop attachment
identity. Existing `launcher-mesh-links-20261001.json` covers XM8/40mmgl only and
cannot establish a SPAS shell binding. The full rig report is one static snapshot;
the repeated weapon capture includes wrists but does not include the animated
finger subtree for every reload sample.

The next bounded read-only geometry capture should collect, for the exact SPAS
weapon state during one ordinary reload:

1. Reflected Meshes1p/mesh asset identities and the owned mesh-section-to-skeleton
   skin mapping. Determine whether the shell is part of the weapon's skinned mesh
   or a separately attached entity; preserve that actual distinction.
2. Native evaluated weapon subtree **and left wrist/finger chains** from the same
   pose generation, plus bind transforms, section visibility and draw/skin owner.
   Relate the moving geometry to jntWpn_7 only if mesh evidence supports it.
3. The actual shell nose/base/grasp landmarks and loading-port frame, with native
   animation phase and ammo-transfer timestamps. Derive the item hand relation
   and seated pose from these landmarks, then validate repeated cycles. A native
   animation reaching the gun is not enough to establish the physical rail.

These are new capture requirements, not already verified pointer offsets. The
known WeaponStateData reflected Meshes1p field is a route for inspection, not a
license to assume renderer skin layouts. A generic descendant-only collector
cannot rule out a separate ammo entity or a prop attached outside that subtree.

## Portable insertion interface

The parallel ReloadInsertion policy keeps `itemFromHand`, `itemFromInsertion`
(the item nose/entry landmark), and `weaponFromEntry` as explicit proper rigid
metre transforms. Its entry +Z axis points inward. Their row-vector chains were
reviewed against the runtime's convention: raw item is
`inverse(itemFromHand) * weaponFromHand`; the insertion landmark in entry space
is `itemFromInsertion * rawItem * inverse(weaponFromEntry)`.

The user's requested smooth magnetic slide belongs to presentation guidance.
Raw hand/item motion remains the evidence for capture, axial progress, release
and withdrawal; guided rendered item/wrist poses preserve the authored grip.
Correct resource identity and generation, hand ownership and native eligibility
must remain independent. Reaching the seated pose yields a physical candidate,
never a native ammunition acknowledgement. Current shell/magazine geometry,
physical ammo-resource inventory and manual native gating remain unverified.

Validation: seven focused Python tests pass, covering metre conversion and authored
relation extraction, actor/item/episode separation, malformed/nonfinite/degenerate
matrices, incomplete snapshots, hidden leaves, duplicate sequences/topology,
owned native windows, sparse samples and unchanged source data. Both real capture
extractions completed with zero rejected input rows and `native_insertion_profile:null`.
## Installed first-person SPAS shell identity — later October 1 evidence

The bounded offline extractor `tools/inspect_bc2_mesh_asset.py` now reads the
explicitly named installed archive `Dist/win32/async/weapon/ul_shg_spas-12-00.fbrb`.
This252,075-byte package contains the first-person mesh resources missing from the
saved native snapshots. The report is
`reports/reload-spas-mesh-evidence-20261001.json`. No game files were changed and
no original assets/vertices were exported; only derived metadata, bounds and hashes
are retained. Earlier statements above concern the saved native reports alone.

The exact asset `Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh`
has six sections. `jntWpn_7_Ammo_Brass` contains96vertices/90triangles and
`jntWpn_7_ammo_plastic` contains32vertices/30triangles. Their materials are the
corresponding `Objects/Weapons/Common/Common_shaders/Ammo/Ammo_Brass` and
`Ammo_Plastic`. Every vertex of both sections carries one full255/255 weight to
section palette0, which maps to mesh palette ID40. The MeshSet's hash for ID40 is
`f7f9bcd4`: the independently matching lowercase33-XOR hash of the saved native
bone name `jntWpn_7`. All nine mesh palette hashes resolve uniquely against the
saved complete SPAS weapon subtree. This establishes the installed first-person
shell geometry's bone binding; it is stronger than inferring a role from motion.
Palette ID40 is an asset skin identifier, not an assumed live skeleton index.

The extractor validates theFBRB index/stream, exact asset names/types, section
vertex/index ranges, full normalized skin weights, triangle bounds, MeshSet skin
mapping and buffer sizes. Bounds stay in asset bind coordinates. No arbitrary
wrist offset, loading port, axis direction or native insertion profile is emitted.
The first three position components are half floats; the fourth observed packed
component can be+1/-1 and is not treated as a homogeneous coordinate.

Reproduce with:

```powershell
python -B tools/inspect_bc2_mesh_asset.py --archive 'D:\Games\Battlefield Bad Company 2\Dist\win32\async\weapon\ul_shg_spas-12-00.fbrb' --mesh Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh --native reports/native-trace-20261001-145328-952/native-trace.json --asset SPAS12_sp --output reports/reload-spas-mesh-evidence-20261001.json
python -B tests/test_bc2_mesh_asset.py
```

Seven focused tests pass: raw/gzip streams, truncation/trailing data, bounds and
inflation limits, rigid skin mapping, invalid weights/indices, nonfinite positions,
buffer mismatch and bone-hash collision rejection. Real SPAS extraction passes.
Only Python tooling/tests and this evidence document changed; root's current
65x86/64x64 suites validate the concurrently integrated native candidate.

Next evidence is narrower: prove the current native SPAS WeaponStateData Meshes1p
points at this exact asset, correlate the asset bind space to its live palette,
and capture same-generation fingers/shell poses at native shell transfer. Correct
hand grip and loading-port/seated transforms still need binding; installed shell
identity does not establish an authored insertion socket, visibility or ammo
authority. No live process capture or mutation occurred in this investigation.

## Bounded live asset-ownership capture

`tools/capture_reload_state.py --mesh-links` optionally adds exact inventory
weapon-to-state-to-first-person-mesh links. Root runs the tool; the offline agent
has not accessed the current game process. The normal reload-state capture is
unchanged when this option is absent.

For each already supported inventory item, the resolver validates reflected
`SoldierWeaponData.WeaponStates` and `WeaponStateData.Meshes1p` types, element types
and observed offsets. It checks the array's native count and data getter code
against both the installed executable and live image without calling them. Array
counts, storage spans, exact mesh reflected type and repeated asset-name reads are
bounded; changed owner/array/name identity rejects the capture. The inherited
`Name` field is resolved through the actual class metadata, with the unique native
ClassInfo constructor proving parent storage at+0x14 (RVA0x100660).

```powershell
python -B tools/capture_reload_state.py --pid <current-game-pid> --seconds 0 --all-weapons --mesh-links --output reports/reload-mesh-links-20261001.json
python -B tests/test_reload_mesh_links.py
```

The existing process wrapper opens only QUERY_INFORMATION|VM_READ (0x410); this
option adds no native calls, process writes, hooks, input or focus changes. It
records all configured states, including inactive inventory weapons. Array
ownership does not claim which state is currently rendered or whether its shell
section is visible. External reads are identity-checked, not atomic native ticks.
Eight synthetic tests pass, covering correct inherited names, field/type errors,
array bounds, modified getters, array/name changes, inheritance cycles, ambiguous
fields and invalid asset names. The constructor signature also resolves uniquely
in the installed executable. Native capture acceptance is still separate.

### Live SPAS mesh link accepted

Root ran the read-only command successfully at2026-10-01T16:07:34Z against
BC2PID152416. `reports/reload-mesh-links-20261001.json` records the exact chain:
SPASweapon130060160 -> SoldierWeaponData437216560 -> state125410304 ->
Meshes1p[0]131452240 ->
`Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh`.
The reflected mesh inheritance is SkinnedMeshAsset -> MeshAsset -> Asset ->
DataContainer. Getter-code and repeated identity checks pass. Native writes,
native calls and input/focus changes are false. The selected item was the launcher;
this confirms the configured inventory SPAS mesh, not its current visibility.

`reports/reload-spas-shell-binding-20261001.json` joins that evidence to the offline
section/skin report with both source hashes and current extractor/capture-tool
hashes. It records shell bounds center[0,1.09716796875,-0.096588134765625] and
extent[0.0205841064453125,0.0205078125,0.06365966796875] in unchanged asset bind
coordinates. Z is the unsigned long axis. Brass extends0.01788330078125farther
at the negative-Z end than plastic; their positive-Z extent matches. This supports
+Z as an explicitly unverified nose-direction candidate. Bounds-center endpoints
are not claimed as an authored rim, grip or insertion socket. No native bone-local
transform or loading port was invented and `runtime_profile_eligible` is false.

The current native asset-name ownership gap is closed. Exact loaded renderer
resource bytes/visibility and mesh-bind-to-live-palette correspondence remain
separate. Saved reports still lack shell inverse-bind; the existing static hand
bind report contains right-hand bones only, and the full147-bone native report
serializes world matrices only. A bounded static shell inverse-bind capture and
new same-generation finger samples can now target the proven shell identity.

## Same-snapshot shell bind and native finger evidence

`tools/audit_reload_shell.py` consumes the optional per-bone `inverse_bind` and
inclusive `native_left_hand_bones` capture extension. It checks the mesh evidence's
asset name and skeleton fingerprint, proper rigid matrices, units, complete named
hand hierarchy, constant inverse-bind values and identity/sequence boundaries.
Legacy captures report missing inverse-bind explicitly; incomplete hand captures
retain wrist-only observations without inventing finger poses. Authored collapsed
shell/finger leaves are kept separate from usable rigid poses.

The reusable coordinate chain applies the declared native asset RH-to-canonical
Z reflection, units conversion and captured shell inverse-bind. It transforms
bounds corners and endpoints individually; it does not rotate an AABB as if it
were already a local frame. Asset/native vertex-space correspondence remains an
explicit condition until a renderer vertex capture corroborates it. In the current
native bind, the shell center lands within1.01mm of `jntWpn_7` origin:
[approximately0,0.000335459,0.000945806]m. The measured long axis is local-Z and
length63.6597mm. This is strong geometric corroboration, not a new socket claim.

`reports/reload-shell-observations-20261001-161937.json` analyzes the actual root
reload trace161937-497. It has96 SPAS samples,95 settled complete rows,16 captured
hand/finger bones and zero analysis issues. Seventeen rows contain an authored
collapsed shell. Nine uncollapsed shell poses occur inside the owned native reload
window. Phase annotations use preceding complete same-owner native updates within
250ms; they do not pretend the animation pose and simulation update were atomic.

The current read-only mesh report1617 joins by the same process152416,
actor443557184 and SPAS item122021968. It preserves that SPAS was unselected in
the mesh snapshot but subsequently produced the selected weapon pose/reload trace.
This closes the historical actor/item mismatch. Resource continuity and actual
visible skin-section draws remain unverified across those separate snapshots.

No wrist or individual finger relation meets a contiguous150ms/3mm/3deg stable
grasp interval in this capture. The shell-to-wrist relation changes153mm and36.6deg
across the sampled reload. This diagnoses the changing animation relationship; it is not a requirement
that the wrist remain fixed throughout insertion and release. The contact analysis
below supplies a measured pre-insertion grasp candidate.

The two observed +0x3c shell transfers (6->7 and7->8) have last uncollapsed
shell-center observations only0.179mm apart, near weapon-local
[0,-0.04525,-0.50355]m. Those observations precede transfer by203ms and156ms.
Their complete frame orientations differ17.14deg and their long axes differ3.14deg.
This is a repeatable loading-area lead, not a verified loading-port frame or
seated pose. The tool retains before/after transfer neighbors without averaging,
interpolating or promoting them to runtime geometry. `native_insertion_profile`
remains null.

```powershell
python -B tools/audit_reload_shell.py --native reports/native-trace-20261001-161937-497/native-trace.json --binding reports/reload-spas-shell-binding-20261001.json --mesh-links reports/reload-state-20261001-1617-current.json --asset SPAS12_sp --output reports/reload-shell-observations-20261001-161937.json
python -B tests/test_reload_shell.py
```

Thirteen focused tests pass, including basis/units conversion, wrong asset/rig,
legacy/incomplete input, changing bind/identity, hidden leaves, wrong hierarchy,
phase age/owner guards, exact mesh capture joins and transfer-neighbor limits.
The earlier idle161622 capture correctly yields zero reload observations. Legacy
145328 data explicitly reports93 missing inverse-bind rows and emits no geometry
group. No game process access, writes or profile enablement occurs in this analyzer.


## Disabled SPAS insertion candidate from actual mesh contact

The follow-up `audit_reload_contacts.py` skins the real first-person shell and
receiver triangles through the captured palettes. It ranks individual pre-insertion
poses using thumb/index terminal-joint proximity to the shell surface. Those joints
are anatomical proxies, not measured fingertip skin. Whole-cycle wrist stability is
not an eligibility gate.

Row58 is the best observed pre-insertion candidate: thumb/index terminal joints
are15.28mm/7.08mm from the actual shell surface. Rows59 and53 provide alternatives
with maximum proxy distance about19mm. The exact row58 shell-center-to-wrist frame,
including orientation, is preserved; its inverse supplies the draft itemFromHand.
Native insertion naturally changes that relation afterwards.

The receiver query identifies a connected two-triangle BrushedMetal plate about
13.17mm wide and177.25mm long, with planeY=-0.048981m, rigidly skinned to the
weapon root. Two-sided underside rays hit closed native faces in this area. This
is actual render mesh geometry, not an open loading aperture or native collision
shape. The closed surface is retained as asset occlusion/polish work rather than
blocking the initial interaction candidate. Observed pre-insertion approach vectors
are[-0.22885,0.74196,-0.63017] and[0.03118,0.81618,-0.57695]; the seated shell's
nose is almost weapon-Z. The draft rail deliberately follows the terminal nose
orientation, distinguishing that design choice from the measured approach motion.

`reports/spas-reload-candidate-20261001.json` is a BC2-only disabled draft with
portable ReloadInsertion field names. It is generated by
`tools/build_spas_reload_candidate.py` and is not loaded by the runtime.

- Measured: row58 grasp frame, actual shell length63.6597mm, row62 terminal pose,
  finger surface distances, native transfer neighbor and receiver surface.
- Constructed: inferred nose landmark at the positive asset-Z end, row62 terminal
  orientation reframed so entry+Z points inward, and a chosen50mm insertion stroke.
- Initial design tolerances:30/60mm capture/release radius,35/70degree cones,
  3mm seating tolerance,120ms alignment,60ms seating dwell,40mm/60degree maximum
  pose step,100ms sample gap and5s guidance limit. Keyed roll remains the first
  draft's explicit setting; these are interaction choices, not extracted BC2 rules.

The tip rail starts at[-0.000140692,-0.045522127,-0.485363551]m and targets
[0.000168477,-0.044601695,-0.535354121]m in weapon coordinates, with inward
axis[0.006183382,0.018408631,-0.999811401]. The final shell center matches the
observed row62 position. Tests reconstruct the measured grasp and confirm that
entry+50mm reaches that exact terminal pose without sliding the item through its
hand. The JSON explicitly separates measured evidence, constructed geometry and
design values. Native hold/commit/reconciliation and visual acceptance are pending;
it enables no ammo operation or runtime feature.

Validation: six contact/mesh-skinning tests and four candidate geometry tests pass,
in addition to the13 shell analyzer tests. Reports retain source hashes and no
original vertices/triangles are exported. Reproduce the draft after contact analysis:

```powershell
python -B tools/build_spas_reload_candidate.py --contacts reports/reload-contact-candidates-20261001-161937.json --observations reports/reload-shell-observations-20261001-161937.json --output reports/spas-reload-candidate-20261001.json
python -B tests/test_reload_contacts.py
python -B tests/test_spas_reload_candidate.py
```
