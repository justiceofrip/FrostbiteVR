# Fixed-container geometry: MP412, M1 Garand, Type 5

Done: three reviewed physical models, four exact configuration selectors, six
complete supply-part cache records, and twelve passing Python regressions.
Source geometry and explicit mod policy only; no native/game/headset qualification.
Root owns C++ builds, cache preparation, runtime integration and installation.

Outputs are under `profiles/weapons46-special/`:

- `FixedContainer.generated.h`: `fvr::bc2::generated::FixedContainerEquipment` and
  `FindFixedContainerEquipment(configurationPath, mesh, rigFingerprint)`; exact
  paths omit the configuration `.dbx` suffix. Never select by native name alone.
- `geometry.json`: complete source closure, policy, per-part centers and step
  targets, same-frame hand shapes, visibility requirements and assembly offsets.
- `body-ammo-assets.json`: six existing-cache-compatible rigid part records;
  installed payload preparation can use `bc2_body_ammo_assets.py --catalog ...
  --resource-content --game ... --output ...` under root's StorageBudget.
- `exact-native.json`: all four exact firing/configuration records with full
  observed scalar values/offsets, hashes, GUIDs and origin archive/index hashes.

## Adapter semantics

Use the existing stable-weapon ammunition ledger. Every insertion requests one
bulk transfer of up to capacity, bounded by remaining capacity and reserve.
MP412 capacity/insertion is 6; M1 base/Assault and the independently resolved
Type5 are each 8. The distinct Garand meshes/configurations remain separate.
`rtMagazine` does not authorize one-round MP412 transfers, extra chambers, manual
bolt cycles, or removal of previously loaded live rounds.

Geometry array order is Open, EjectSpent, Insert, Close. MP412 is Cylinder;
the Garands are EnBlocClip and have only Insert populated. Shapes and grasps are
intended for the left hand. The Garand source animation loads with the right;
the policy explicitly mirrors its clip-local and wrist-local X coordinates to
author a proper rigid left-hand grasp. This is not a native left-hand claim.

MP412's actual hinge is barrel `jntWpn_4`. Opening follows the 42.4-degree plateau.
EjectSpent is an additional 43.6-degree hinge flick and return, reaching the
authored 86-degree peak. `returnToStart=true`: `weaponFromStepEnd[1]` is the
outward peak, and completion returns to `weaponFromStepStart[1]`. Closing reverses
the 42.4-degree opening. Use each recognizer target/progress, including return,
rather than snapping to the peak when the step completes.

`coupledOpening` explicitly couples barrel4, cylinder12 and retained live ammo13.
The source rig makes cylinder12 a sibling, with independent later rotation;
moving only the barrel or treating cylinder12 as the hinge is incorrect. The
ejection gesture is mod-owned physical state and never subtracts live rounds.

Supply must render the complete assembly: MP412 ammunition13 plus metal
hardware `jntWpnwpnJnt_14`, and Garand clip12 plus ammunition13. Compose
`supplyFromPart * weaponFromSupply` in row-vector order; `supplyCenterInRoot` is
the center of both parts, not the center of one arbitrary part. Per-step part
targets and `centerInBone` are supplied. MP412 hardware14 is visible only while
carried, not as an installed closed-weapon part. Hide the corresponding native
supply bones to avoid duplicate geometry while maintaining retained loaded
ammunition presentation. A rigid group represents up to capacity; it does not
visually enumerate individual rounds during partial top-up. No second ammo store.

Insertion source frames are initial authored examples. Rebase a newly acquired
body supply with `RebaseInsertionSource` between grips; retain source-relative
grasp/assembly and destination. Preserve the physical policy across tracking
loss/stow and honor the typed request/receipt and same-owner input contracts.

## Evidence and reproduction

`authoring-policy.json` pins the exact sampled JSON and reviewed samples.
Root ran the grouped sampler under `storage.json`/`storage02.json` and a further
right-hand sampling budget. Raw installed assets were never written here.
The first sampler stopped on a non-unit authored quaternion. The isolated
successor preserves eight rejected rows per Garand: bones12/13 at samples22–25
(0.733–0.833 seconds). Chosen insertion/grip samples33/36/69 are valid. No decoder
tolerance was loosened and no absent transform was substituted. MP412 has no gaps.

Reproduce source-only outputs with:
`py -3 tools/bc2_fixed_container_geometry.py derive --policy handoff/tasks/WEAPONS/20261030-special46/authoring-policy.json --output profiles/weapons46-special --verify`.
Run tests with `py -3 -B -m unittest discover -s tests -p test_bc2_fixed_container_geometry.py -v`.
All 12 pass; `validation.json` pins delivered artifacts. These tests establish
offline math, identities, cache metadata and reproducibility, not native success.
