# Shell presentation, checkpoint 412

The 410 headset run reported substantially better pumping and reliable shell
insertion, with a leftover stock loading-shell animation. Its final native
report records seven submitted and seven completed shell insertions, 18,327
SPAS raw contacts, 17,456 native shell-visible contacts, and 699 paired stock
shell hides. This is evidence that the manual transaction worked; it does not
prove that each native-visible bone produced visible pixels.

The previous shell exclusion followed the active reload cycle. Completion at
full capacity or no reserve retires that cycle while the original loading clip
can still leave its independently skinned shell visible. The candidate adds a
separate `ReloadShellPresentation` lease, owned by the current manual gun and
selected mesh. It suppresses only the already-bound SPAS loading-shell leaf
when there is no physical shell preview. It does not retain a native reload,
claim a hand, change ammunition, or alter pump timing.

Both native palette callbacks still verify the current actor, native weapon,
physical gun claim, mesh identity, original deadlines, source bytes and rig
fingerprint. Ownership loss or a new weapon cannot inherit this exclusion.
Physical shell previews and body-ammo presentation retain their established
precedence. The exclusion explicitly yields to an ordinary-equipment recovery
request so a cosmetic overlay cannot prevent holster acknowledgement.

Regression cases cover completed/full/empty transactions, loss of focus and
tracking, replacement actor/weapon/mesh/claim, expired original leases, and
physical-shell preview precedence. The two existing suites to run are
`Bc2ReloadPreviewTests` and `Bc2PhysicalReloadTests`; combined build/runtime
results belong in the checkpoint handoff. No new headset claim is made here.

## Spent-shell ejection remains a separate feature

The installed SPAS mesh has a rigid loading-shell leaf, `jntWpn_7` (240
triangles including duplicated sections). The retained exact asset audit finds
less than 0.00000006 m of movement from its reference in `1P_BoltAction`,
`1P_Fire` and `1P_ZoomFire`. `1P_Reload` moves it about 0.710 m. Therefore those
known pump/fire curves do not provide a spent-shell ejection path.

A bounded read of the installed assets found a dedicated rigid shotgun casing
(`Shell_Generic_Shotgun_Mesh`) and `FX_ShellEjection_ShotGun_Right`. The casing
has 60 visible triangles plus a duplicate Z-only section; validated Half4
positions fit the authored bounds, approximately 63.72 by 19.26 by 19.37mm.
The mesh and LOD SHA256 values are respectively
`2e8e7a485d9998a6d245e0c7e55079c35944dca4546d9b214d274127664ab5a9`
and `26d0db1043e10f82c5947811f72b77e07b060730a51019038f1fa95408cac09b`.
Only derived metadata is retained; no extracted game mesh is published.

The effect's internal 90-degree rotation and zero translation do not identify
its weapon-local attachment point. The inspected SPAS firing definition exposes
muzzle effects; the pump clip has no wrapper/text/vector/extended event data,
and the skeleton has no named ejection locator. No verified ejection-port
transform or native dispatch binding emerged.

A local cosmetic path is feasible: observe the accepted rear-stop transition
once per weapon lifetime and shot, retain its consumed watermark across regrips
and interruptions, and sample a bounded ballistic transient once for both eyes.
Existing stereo prop rendering can carry it, but it needs a rigid-mesh decoder,
measured port placement and a transient manager; those are not enabled in412.
No ammunition writes or extra native shot are needed for that future visual.

Immediate post-shot pump eligibility and fire-rate timing are unchanged, as
requested for this pass. Local investigation metadata is retained under
`../bc2vr-recovery/overnight412/shell-presentation` without extracted game assets.
