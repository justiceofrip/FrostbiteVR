# Magazine reuse for ARs and SMGs

The XM8 interaction work is reusable. `DetachableMagazine` already owns removal,
temporary holding, release, replacement pickup, guided insertion and retained
original return. It shares `HandInteraction`, `AmmoSupply`, `ReloadInsertion` and
`ManualReload`; another AR or SMG does not need a copy of these state machines.

`Bc2MagazineGeometryProfile` now groups the measured data formerly scattered
between interaction and presentation: exact asset/mesh/rig identity, magazine
and hand bone roles, removal/rail settings, authored grasp and finger transforms,
and the attached-magazine pose. The XM8 factory and renderer consume this data.
Existing public XM8 entry points remain compatibility wrappers. No sampled
transform, timing, native command, interaction lifetime or admission changes.

Only `XM8_sp_s` is registered. Exact lookup has no AR/SMG category fallback.
A structurally valid profile is not native reload authority. The synthetic second
profile used by tests is neither registered nor enabled in the game.

| Already shared | Required evidence for another weapon |
| --- | --- |
| Raw grip/pull, hand arbitration, guided rail and original-mag identity | Its native magazine joint/mesh, exact rig and authored grasp/loading-axis transforms |
| Reserve accounting and correlated seat receipts | Its server/client owner mapping, reserve pool, capacity and native transfer behavior |
| Native-cycle and empty/full route interfaces | Its observed reload configuration, callback ABI, hold/continue/cancel and idle restoration behavior |
| Paired private palette selection and stale-pose rejection | Its weighted magazine mesh and both-eye removed/hidden/restored output |

The current native boundaries intentionally remain XM8-specific:

- `ResolveMagazineFamily` proves the scoped rifle/launcher family through
  `ResolveWeaponMode`. A rifle without that launcher needs a separately verified
  native-item or family mapping; a shared mesh/name is insufficient.
- `IsXm8MagazineConfig` and `ReadXm8MagazineTiming` require the exact scoped asset,
  fire/reload types and native fields, capacity 30, native `numberOfMagazines`
  value 4 in the observed configuration, 2.8-second reload and 0.75 threshold. These are not
  defaults for other weapons. The three native state branches and cancellation
  behavior must be observed before admitting a new native profile.
- Gameplay family routing, physical consumer guards and the idle-detach gate
  still select XM8 explicitly. Registering geometry alone cannot activate them.
- Empty reload, chamber/bolt behavior and magazine persistence across world drops
  are separate capabilities. The existing pooled reserve semantics are preserved.

For the next gun, capture its untouched native rig and ordinary reload, derive
one geometry profile, then prove its native family/configuration and exact
transfer/cancel receipts. Wire that admitted profile into the existing consumer,
run the actual-consumer monitor fixture, and finally check hand feel in VR.
Shared mechanics fixes should stay in the shared policies; per-gun differences
belong in data or a narrowly proved native capability.

This extraction passes the existing XM8 physical/return/authority regressions on
both architectures. New tests also drive the same removal coordinator with a
different synthetic travel axis/length and bone-role layout, reject unknown
assets and malformed roles, and preserve the captured XM8 calibration. These are
offline checks, not acceptance of another weapon or a new headset result.
