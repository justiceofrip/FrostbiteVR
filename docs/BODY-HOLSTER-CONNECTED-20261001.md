# Connected BC2 holster candidate — 2026-10-01

This candidate includes the earlier coordinator prerequisite and connects it to
the actual body inventory, native gather commit and private rig consumers. It
remains disabled. `EnableBodyHolsters(BodyHolsterCapabilities acceptance={})` is
the only activation API; its default is two false acceptance gates and profile
mask0. No CLI/IPC flag calls it. It requires existing body inventory and physical
reload setup, and rejects diagnostic/fixture combinations.

## Actual production connections

- `Bc2BodyInventory::TickHolster` reuses the current bounded native reader, item
  lifetimes, slot assignments, shared `BodyInventory` instance and HandInteraction
  arbiter. A right-hand press at the held item's shoulder requests holstering;
  an empty right hand pressing an assigned shoulder requests that exact item.
  Selecting another shoulder while still holding a gun uses the existing native
  shoulder-switch path. Ordinary firing, reload ownership and unsupported held
  weapons remain on that existing path.
- Gameplay defers the holster policy until **after native cache commit**. The
  current native gather gets a unique monotonic tick; `HolsterInputOverride`
  stages its mask, repeats full owner/cache/tick verification and reads back the
  actual fields before committing. Failure cannot produce an EmptyHands ack.
  This also covers ordinary native input, not only VR semantic output.
- Draw uses the existing map resolver and repeats inventory/full owner/route
  validation just before dispatch. Only a verified cycle7/mode33/mode36 pulse may
  pass through the otherwise suppressed cache. That tick explicitly cannot issue
  an EmptyHands suppression receipt. Native selection is still observed later;
  no setter/native equip function is called by the coordinator.
- Gameplay's automatic right GunHold acquisition is disabled during the
  transaction; an existing exact GunHold may renew until verified hiding releases
  it. Show acknowledgement reacquires through the real shared arbiter. The
  original physical hand generation remains distinct from native equipment epoch.
- Rig input publication now follows the actual commit/coordinator result. Its
  immutable free-hand evidence retains original input/deadline, full owner,
  accepted-hide receipt, configured meshes and current native suppression tick.
  It is never stamped with a newer packet's geometry or expiry.
- RigPublication derives the right wrist from the **raw tracked right grip** and
  verified right-hand anatomy, feeds that target into the existing arm solve, then
  generates all16 free hand/finger transforms at the solved wrist. Capacitive thumb
  and index touch use the existing shared hand-pose function. Held trigger overlay
  stays unchanged when free-hand evidence is absent.
- Each Pack callback rechecks immutable free-hand authority plus the currently
  active hide request. If either is withdrawn/expired, or no matching hidden
  palette exists, it uses the exact original source palette. It does not expose
  an open right hand while silently dropping the weapon-hiding layer. Native
  source bytes and unrelated hidden leaves remain untouched.
- `ReadWeaponShotFrame`, support and sight consumers reject action-blocked frames;
  Gameplay clears firing-pose history while blocked. A separate **body-only**
  ownership observation permits the already accepted ordinary rig frame to finish
  selection/show transitions. It does not grant EmptyHands or firing authority.
  Camera/body tracking continues normally.

The existing normal shoulder/chest mode does not instantiate this coordinator and
keeps its current behavior. Physical reload activity and ordinary shooting while
Held are explicitly regression-tested; they must not inherit holster suppression.

## Important receipt correction

The actual renderer sets `WeaponVisibilityReceipt.observedNs` to Pack completion
time. `receipt.evidence->observedNs` is the original input observation. They are
different events on the same QPC-nanosecond clock, not equal timestamps.
The connected validator accepts completion after source observation and before
the original unextended deadline. It never renews source geometry. Tests now emit
the actual native serializer shape; the original standalone synthetic fixture
had incorrectly made those values equal.

## Acceptance and remaining limits

Native gates still default false; allowed profiles still default mask0. Bit0 is
SPAS; bit1 is XM8+ACOG. There is no blanket acceptance for Frostbite weapons.
Before accepting the input gate, `EnableBodyHolsters` verifies every additional
live enum row against the already discovered input table: AltFire12, launcher33,
gadget36 and melee37, as well as the existing fire/cycle/ADS/reload/grenade rows.
The installed-file enum evidence is in
`reports/holster-input-metadata-20261001.json`.

Root's separately collected `native-trace-20261001-225926-435` and
`weapon-visibility-audit-225926.json` establish sampled SPAS baseline/hide/restore
pixels with both arms preserved and exact native Pack/source checks. They do
**not** accept this new broader suppression mask, XR holster gestures, or free
right-hand lifecycle; XM8 acceptance is also separate. Those gates remain false.

The consumer is connected but is not ready for release without those checks.
An unsupported item replacing an already hidden item clears the hide and stays
recovery-blocked; an explicit ordinary-presentation retirement path is still
needed before broad campaign pickup support. Launcher/base-family holstering is
also unaccepted. Actual meshes displayed on the player's body are not implemented
by this slice; current body slots are configurable reach zones.

Input/renderer expiry causes restore rather than stale ownership resurrection.
Tracking loss may keep the genuine current native cache suppressed while
restoring ordinary presentation; expired XR input cannot grant a new hand pose.
Stop invalidates publication and lets ordinary native gather rebuild its cache.

## Validation

- **17 coordinator groups per architecture**: real renderer completion-time
  semantics, private render guard withdrawal, expiry, owner/equipment changes,
  shared claims, suppressed input readback/rollback, verified equipment pulses,
  and anatomical/capacitive free hand output.
- **34 actual body adapter groups per architecture**: the previous29 reader,
  route, gesture and lifetime cases plus5 connected cases. New cases drive the
  real repeated reader and shoulder geometry through cache commit, shared claim
  release, same-item draw, changed native equip draw, ordinary shoulder fallback,
  suppression failure, and preservation of firing/physical reload while Held.
- x86 Gameplay and RigPublication compile with the new production calls. Portable
  tests use `/W4 /WX`; native compile additionally suppresses existing canonical
  C4456 shadow warnings, without editing unrelated source.

No native process, hooks, inputs, windows or assets were changed by this candidate.
The parent owns canonical integration, full builds and native/headset acceptance.
