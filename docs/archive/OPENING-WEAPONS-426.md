# Opening mission weapon coverage — 426 candidate

The current opening weapon is live-confirmed as `Nambu100`, exact configuration
`Objects/Weapons/Handheld/Beach/Nambu100/Nambu100`. The read-only native survey in
`overnight426/game02/all-inventory.json` observed it in slot 0, with persistence
zero, process 26896 / creation FILETIME 134368038403330872. Slot 7 held ordinary
`Objects/Weapons/Handheld/Knife/Knife`; the separate opening `Knife_Equipable`
definition is not the observed selected knife. This survey establishes identity
and reflected configuration, not manual reload execution or headset placement.

## Shared support and source discovery

The opening weapon's typed configuration and animation graph occur only in
`Dist/win32/levels/sp_01/level-00.fbrb`; its shared skeleton is in `sp_common`,
while its mesh and animation payloads are also available in the exact Nambu async
archive. The old common-level catalog missed this configuration. The new
`bc2_weapon_config_pipeline.py --campaign` catalog discovers canonical installed
`sp_NN` / `sp_NNN` level archives, retains exact resource/GUID/content variants
and visible extraction failures, and grants no runtime admission. The installed
catalog contains 146 exact configurations across 10 archives, with no failures.

The authored grip/action tools now accept explicitly pinned dependency archives.
They resolve exact resources and reject conflicting content or resource types;
archive order does not choose a winner. Requested-resource reads and archive
counts remain bounded. This permits the SP_01 graph to resolve its shared
skeleton without duplicating content or introducing a weapon-specific pipeline.

Both shared authored grip and support resolvers now accept zero persistence.
The existing equipment identity reader permits it, the authored Nambu reference
is null, and the live survey confirms zero. Data, exact asset, configured mesh,
rig, epoch and repeated-current equipment checks still apply. Tests reject a
null-to-nonnull persistence change, data replacement and asset replacement.

`profiles/opening426/CombinedSupport.h` preserves the prior contact set and adds
one exact measured Nambu left-hand contact. This is measured authored geometry;
comfortable headset placement remains unverified.

## Magazine geometry and native dispatch boundary

The shared complete-subtree extractor uniquely identifies Nambu magazine root
`jntWpn_12`, containing constant descendants `jntWpn_13`, `jntWpnwpnJnt_14` and
`jntWpn_15`. The root owns 1,020 source triangles, including the depth-only copy;
the detached render cache retains 510 color triangles. Descendants are unweighted but must
remain part of the complete transform/hide proof. The alternative moving bolt
candidate is rejected by the paired-hand contact evidence. The initial leaf-only
failure is retained in recovery rather than relabeled as success.

`profiles/opening426/NambuGeometry.generated.h` and its JSON receipt use the
existing rigid assembly renderer, geometry binding and magazine interaction.
They introduce no Nambu state machine. The C++ assembly regression binds the
complete synthetic rig, verifies the exact hide set, and rejects missing or
reparented descendants.

The exact configuration has AutomaticFire type 2, magazine reload, Fire 8,
Reload 29, capacity 30, reload time 3.700000047683716, threshold
0.800000011920929, delay/post-reload zero, and nominal bolt fields 0.5 / 1.0 /
hold-fire true. Those fields must not be rewritten to zero or classified as a
physical pump/charging-handle requirement. The existing instruction proof in
[AUTOMATIC-STOCK-BOLT-DISPATCH.md](../AUTOMATIC-STOCK-BOLT-DISPATCH.md) establishes
type 2's stock route bypassing bolt states 7/8. The registry exporter previously
rejected enabling this candidate under the zero-bolt family. Root owns the
explicit family review and actual native operation checks; geometric extraction
does not supply those receipts.

Useful content pins:

| Item | SHA-256 |
| --- | --- |
| Nambu weapon DBX | `24ca715ffcaf75cedc25712d8c43e80a5bc7a51b72eb750593d5cb7725cc0120` |
| Nambu firing DBX | `5e9779f47e5f567f20315f1b2afb448321adc16822256a2f69a0d472a9677b0a` |
| Static hand clip | `3c825f064283d3c1c1c94e22d6681d35f32e5da33529b377456b88a7241227d0` |
| Reload clip | `4a08705e7db8eee06be6867a19555ba761240c088be4a11d4b21366f80bf9e0f` |
| Shared skeleton | `d1d713cc7301bc5ae2b011a0991ca03c2fcbb26c1187900f6fd129533d173c09` |

## Installed SP_01 inventory audit

The bounded typed scan finds 11 non-AI weapon definitions. A non-AI definition
alone does not prove player availability. Japanese soldier pickup definitions
prove Nambu100 and Type5 drop routes; the live survey proves the carried Nambu.
The main level's dictionary mentions several other player configurations, but
its complete typed graph exceeds the existing node bound. No level-graph limit
was loosened, and progression-only references are not treated as live items.

Paths below are relative to `Objects/Weapons/Handheld/`; mesh names omit `.res`.

| Native name | Exact configuration | Configured first-person mesh | Availability evidence / current coverage |
| --- | --- | --- | --- |
| Nambu100 | `Beach/Nambu100/Nambu100.dbx` | `Beach/Nambu100/Nambu100_Mesh` | Live slot 0 and JP SMG drop. Exact support and magazine assembly generated; bounded native return/replacement, support reacquisition and follow-up shot accepted below. Headset/render acceptance remains open. |
| Garand | `Beach/Jap_Type5/Type5.dbx` | `Beach/Jap_Type5/Jap_Type5_Mesh` | JP rifleman drop. Exact Type5 identity, not an M1Garand alias. Varying left-hand chain; static support deferred. Manual reload not enrolled. |
| M1A1 Thompson | `Beach/Thompson/Thompson.dbx` | `Beach/Thompson/thompson_Mesh` | Main-level dictionary; US NPCs hold distinct AI_Thompson. Player progression unconfirmed. Varying left-hand chain; support/manual reload deferred. |
| Nambu | `Beach/Jap_Nambu_pistol/nambu_pistol.dbx` | `Beach/Jap_Nambu_pistol/jap_nambu_pistol_mesh_Mesh` | Scientist-held and level dictionary; pickup unconfirmed. Exact static support candidate extracted but not selected for enrollment. Manual reload deferred. |
| JAP_riflegrenade | `Beach/Type5_rgl/Type5_rgl.dbx` | `Beach/Jap_Type5/Jap_Type5_Mesh` | Level dictionary; distinct one-round configuration. Support/firearm class not admitted; reload deferred. |
| Expack_US | `Beach/Expack/Expack.dbx` | `Beach/Expack/expack_detonator_Mesh`; `Beach/Expack/expack_explosive_Mesh` | Level dictionary, two states. No magazine/support enrollment. |
| Shingunto | `Beach/Jap_ShinGunto_Knife/Jap_ShinGunto_Knife.dbx` | `Beach/Jap_ShinGunto_Knife/Jap_ShinGunto_knife_Mesh` | Japanese NPC-held; player pickup unconfirmed. No firearm support/reload enrollment. |
| HG-2 | `Beach/Jap_Type97_Gren/Jap_Type97_Gren.dbx` | `Beach/Jap_Type97_Gren/Jap_Type97_Gren_Mesh` | Non-AI definition; NPC uses distinct AI grenade. Player availability unconfirmed; no firearm support/reload enrollment. |
| HG-2 | `Beach/US_Pineapple_Gren/US_Pineapple_Gren.dbx` | `Beach/US_Pineapple_Gren/US_Pineapple_Mesh` | Non-AI definition; player availability unconfirmed. Exact path distinguishes the same display name. No firearm support/reload enrollment. |
| M18 | `Beach/M18/M18_jap.dbx` | `Beach/M18/M18_mesh_jap_Mesh` | JP RPG soldier holds it but drops Nambu100. Player route unconfirmed. Varying left-hand chain; support/reload deferred. |
| KNV-1 | `Knife_Equipable/Knife_Equipable.dbx` | Not resolved from the selected SP_01 mesh set | Installed definition only. Live slot 7 is ordinary `Knife/Knife`, handled by the separate knife work. |

The audit's unresolved varying support chains can use the existing whole-curve
bounded-reference tool; no point sampling or class-wide default was substituted.
Remaining WWII mechanisms require exact geometry/action evidence before their
shared adapters can be enrolled.

## Validation and retained evidence

The focused Python run passes 166 authored-tool tests and 15 campaign/config
tests. It covers cross-archive exact sources, conflicting resources, stale index
receipts, opening-only configuration discovery and retained failures. Root owns
x86/x64 builds and C++ grip, support and assembly test execution.

Private evidence and installed render-cache regeneration are under
`G:/Unity/VRMMO/bc2vr-recovery/overnight426/smg`. These include the SP_01 typed
catalog, exact soldier/drop references, selected mesh inventory, rejected and
accepted magazine derivations, support candidates, complete visibility source
receipts, and focused test logs. Generated `.fvrprop` geometry must remain local.
The shared 16 MiB cache limit and 64-profile limit remain unchanged.

The complete 48-record render cache regenerated successfully at 16,626,509
bytes, leaving 150,707 bytes below the cap. SHA-256 is
`b0825b563c4965dc70fd3922251150ebaaef6a709e18c906c7027465e0039dc4`.
Nambu adds a 33,789-byte detached magazine record and a 119,680-byte complete
body record. Reconstructing the prior 46-record header over the preserved
prefix reproduces its exact 16,473,040-byte SHA-256
`bb8663974198249eb38780493c1b505242203350c173375cbbb51b42880148e5`.
The visibility descriptor checks every configured mesh/LOD and arm separation
against the existing exact native skeleton topology receipt. This is render
metadata validation, independent of current Nambu native operation admission.
The public body/ammo catalog and generated body/visibility headers now include
the exact Nambu rows. `profiles/opening426/resource-enrollment.json` joins the
reviewed descriptor, complete configured visibility, assembly and body sources.
Its family is explicitly `bc2.automatic-magazine-inactive-stock-bolt.v1`; mixed
enrollment documents retain each row's reviewed family instead of relabeling
stock-bolt or SingleFire rows as zero-bolt AutomaticFire.

## Actual native Nambu check

`overnight426/nambu-native01/native-audit.json` independently accepts the bounded
ordinary-consumer run on source
`1d2e623ff56c7781259ffaa23f3dc7e2c918d5dc0e19d55c59d0f034e1314557`.
The same live Nambu actor, weapon, server item and three firing objects remained
current before and after. The synthetic receiver used no OS input, and process
26896 remained alive after the diagnostic. The active episode took 13.785 seconds.

A native shot first changed 30/114 to 29/114. Removing and returning that partial
magazine preserved 29/114; removing it again and inserting a chest replacement
produced 30/84. The four authority-owning Updates are 6662, 8162, 8236 and 11418.
Each has three distinct subsequent own-Update receipts, one per firing branch,
with converged counts and idle state before the operation completed. This checks
the original native records independently of the diagnostic driver's success.

The ordinary support consumer acquired claim 3 initially and claim 7 after the
reload, with no ownership failures or forced resets. Claim 7 remained held across
the follow-up shot. That shot's client and server callbacks share native context
token 503105 with Updates 12184, 12189 and 12193; each branch changes 30/84 to
29/84 through the original firing transition. The next input generation releases
the trigger. Two read-only postflight samples retain 29/84 on all three branches.

This accepts native reload/support/firing behavior under synthetic input, not
headset placement or visible detached-magazine rendering. The cache loaded all
48 records and magazine packs reported no interaction fallback, but body/ammo
GPU draws were zero. Of 240 received eye pairs, 40 were pixel checked and distinct;
200 were explicitly unchecked. Three reload-record lock drops preclude complete
callback coverage; all required operation and shot receipts are retained.
The fire observation is before spread, so final projectile direction/impact
remains unmeasured. Nambu muzzle-origin translation remains disabled, with zero
client/server origin writes. The initial audit's overly strict all-pairs pixel
assertion is preserved in `native-audit-initial-failed.json`; the corrected audit
uses the existing295 checked/unchecked condition without weakening native checks.
