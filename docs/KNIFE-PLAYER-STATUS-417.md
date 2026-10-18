# Player knife status — checkpoint 417 audit, 2026-10-17

The user reported that the knife fitted the hand but physical jabs did no damage
during the 416 new-campaign headset run. That session had `player_knife=false`
and `contextual_tools=false`. This is not a successful player-knife test, and
the visible pose alone does not identify which native/scripted knife path was
active.

## Implemented path and normal configuration

`NativeProbe.cpp` calls `EnableChestKnifeDrawReturn` only for an explicit
`chestKnifeProbe` or `normalPlayerKnife` configuration. With both disabled, the
normal chest-knife coordinator and native attack observer are not installed.
The contextual-tool option is separate; enabling it would not enable knife
damage. `ControllerInput.cpp` also has no separate Melee action binding.

The implemented knife coordinator supports chest draw, held custody, motion
recognition, native attack requests and return. Attack admission requires fresh
controller/hand evidence, the current rendered grip, owned knife selection,
settled native mechanics and the same actor/inventory lifetime. The native
adapter sends copied stock Fire input through the two actual input consumers;
the other predicted state is observed through the engine's copy relationship.
It does not manufacture a third input consumer.

Hand tip/direction currently feed gesture recognition. The production
`Bc2ChestKnifeNative.cpp` path does not integrate the private hand-directed
shot-composition or timing experiments. Native state transitions establish an
attack receipt, not a target hit. The separate `attackQuiescentOnly` outcome
explicitly means that mechanics settled without a complete attack observation.
Neither outcome measures enemy damage.

## Retained evidence and limits

| Evidence | Established result | Remaining limit |
| --- | --- | --- |
| [Knife aim 330](KNIFE-AIM-330.md) | Two private attacks used measured hand-directed shot compositions | No downstream hit/damage proof or complete normal player integration |
| [Knife timing 336](KNIFE-TIMING-336.md) | Private client/server shot timing was shortened while retaining recovery | Not an enabled normal player path or damage acceptance |
| [Campaign diagnostics 403](CAMPAIGN-DIAGNOSTICS-403.md) | One motion attack reached both actual input consumers with predicted-copy correlation | The complete recipe failed during return |
| [Campaign diagnostics 404](CAMPAIGN-DIAGNOSTICS-404.md) | Draw, stroke, wait, return and fresh neutral completed | No attack command reached the native adapter; strict acceptance failed |
| [Campaign diagnostics 409](CAMPAIGN-DIAGNOSTICS-409.md) | Held-knife session close returned and drained with ammunition unchanged | Does not establish reliable player attacks or target damage |

These partial results cannot be combined into a claim that one complete player
sequence passed. The default remains disabled because end-to-end acceptance is
missing, not because hand placement alone needs adjustment.

## Work needed for campaign use

Use the existing coordinator/native boundary to establish one complete draw →
physical jab → native attack → authoritative enemy damage → return sequence.
Prove the relationship between the tracked knife origin/direction and the
native melee range/hit query; retain a miss/neutral case that cannot damage a
target. Then check interruption by menu, death/actor replacement and session
close without leaving weapon or holster custody blocked. Do not enable the
normal flag solely to make the feature appear available.

This audit changed documentation only. No feature was enabled, no game input
was sent, and no new runtime test or full build was performed. The separate
restart distortion from the same session remains unexplained; see
[graphics follow-up](GRAPHICS-FOLLOWUP-416.md).

## Opening campaign knife: a separate, measured input mismatch

The fresh Continue-only 417 observation is **SP_01's currently selected knife**,
not the optional chest-draw path. Root-owned PID100500, creation filetime
134367204333514644, was read using the installed executable hash
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
The existing read-only `Inspector` checked its native signatures and reflected
types. Actor, inventory and selection were identical before and after the read.
This is a bounded external observation, not an atomic native-tick snapshot.

| Current slot | Authored asset path | Display name | FireLogic.FireInputAction |
| --- | --- | --- | --- |
| 0, selected | `Objects/Weapons/Handheld/Knife_Equipable/Knife_Equipable` | KNV-1 | 37, EiaMeleeAttack |
| 7, unselected | `Objects/Weapons/Handheld/Knife/Knife` | KNV-1 | 8, EiaFire |

Evidence: `bc2vr-recovery/overnight417/continue-only01/knife-fire-routing-readonly.json`,
SHA256 `6260244450dbf78bcd91fafd654387d9a9cd19845c8c113cf70bdb6a8a3351ae`.
It agrees with the older SP_01 archive observation in
[knife binding investigation](KNIFE-BINDING-INVESTIGATION.md). No input,
function call, process write, hook, suspension or focus change occurred.

`ControllerInput.cpp` maps the right trigger to the portable `Fire` action.
`Bc2InputBinding.cpp:19` then always writes native float action8; it does not
write melee action37. `EntryAction` currently does not name melee37. Therefore
right-trigger Fire is not routed to this selected asset's authored attack
action. This proves an input mismatch; it does not establish which native
selection/attack/script response would follow a correct melee command, nor
does it prove a hit or damage. Physical jabs also have no normal selected-knife
route while the separate chest-knife coordinator is disabled.

The chest coordinator cannot simply be enabled as a substitute. It establishes
an original ordinary-item anchor, discovers a separate carried knife, performs
temporary draw/return custody, and accepts the normal knife's Fire8 definition.
`KnifeNativeReader.h:160` validates the original item's firearm effects separately
from melee effects, and `:171` explicitly requires knife Fire8. The opening
inventory contains two KNV-1 items and no firearm. Its selected slot0 knife
does not provide the original-firearm/temporary-chest-tool relationship.

## Narrow follow-up after the 417 source freeze

The proposal below was implemented and passed offline validation in
[checkpoint 418](SELECTED-KNIFE-INPUT-418.md). Its native input smoke and damage
acceptance remain separate. This 417 audit records the evidence available before
that implementation.

Keep ordinary firearm Fire8 behavior unchanged. Add a selected-weapon firing
route to the BC2 input adapter, admitting **only** the verified current
Knife_Equipable asset with authored action37 as the initial exception. A
controller-trigger fallback can then request stock melee while campaign
scripts, selection, animation, hit testing and return behavior remain native.
Do not grant every action number in arbitrary metadata, remap Fire globally,
identify this case only by the shared KNV-1 name, or misuse the existing
underbarrel `verifiedWeaponMode` argument as a melee command.

The existing typed `ReadReloadState` reader already exposes `assetPath`,
`weaponData`, `firingData`, `primaryFire` and `fireInputAction`; it does not need
manual-reload eligibility for that observation. Its verified chain is selected
ClientSoldierEntity/inventory -> weapon+4 SoldierWeaponData -> data+0x98
WeaponFiringData -> firing+0x40 FiringFunctionData -> primary+0x0c FireLogic,
whose FireInputAction is at +0x40. It rereads configuration, both firing
branches, inventory membership and selected owner before accepting a snapshot.
Reuse this typed source and its existing signature/type checks rather than
adding a name-only or unchecked offset read.

At the native gather boundary, key the route to the same player, actor/weak,
actor generation, selected weapon, equipment generation, reference-space epoch,
inventory/slot and exact configuration identity as the current action. Recheck
selection immediately before committing the input-cache edit. Menu, tracking,
actor or selection discontinuity invalidates admission; an old snapshot cannot
renew a controller packet. Neutral/rearm behavior must still prevent a held
trigger from attacking on load or reconnect. For the admitted melee route,
write boolean action37 (word0x9c, bit5) and no firearm Fire8 pulse, preserving
unrelated gathered bits and existing rollback/commit rules.

Focused portable tests should cover Fire8 unchanged, exact equipable-knife
Fire37, release, unrelated native bits, rejection of stale ownership, mismatched
asset/configuration, arbitrary action IDs, menu/tracking loss and held-trigger
reconnect. The two KNV-1 definitions must not collapse into one identity.

One bounded native check should then observe neutral -> deliberate trigger ->
release through the existing gathered-input boundary, recording selected-item
transitions, action37 and actual melee firing callbacks. No automatic repeated
attack or weapon provisioning is needed. Attack-state transitions are still
insufficient for damage acceptance: use one identified campaign enemy with a
verified authoritative health/damage reader or native damage callback, and
correlate the owned attack to a real health decrease/death. Preserve a neutral
or miss control with no damage. Such a target-health binding has not been
established by this audit; do not invent health offsets or infer damage from a
timer, sound, animation, or the configured damage value.

Physical jabs are the subsequent motion source for the same selected-knife
route. The existing `MeleeStroke` recognizer and `KnifeMotionFromGrip` can supply
one fresh intent from a verified held knife; they must first receive current
selected-knife hand custody and a measured grip calibration, independently of
chest draw. Hand-directed hit origin/timing, production integration of the
330/336 private experiments and actual enemy damage remain unfinished. A
successful trigger fallback would make no claim that physical stabbing works.

This follow-up is a proposal only. No native source, build configuration or
live input was changed during the 417 freeze.
