# Selected campaign knife geometry and motion policy — 420

The opening campaign equips `Knife_Equipable`, whose native attack is action37.
The optional chest knife uses a different selection/custody path and action8.
Checkpoint418/419 established trigger routing for the opening knife, not physical
stabbing or enemy damage. Neither path can borrow the other's custody merely
because both display `KNV-1`.

Retained418 selected-configuration observations identify the same
`Objects/Weapons/Handheld/Knife/Knife_Mesh`, 147-bone rig and fingerprint
`a7f219a1426216ab` as the measured ordinary knife geometry. This permits reuse of
the measured wrist/blade constants after exact current configuration checks.
It does not establish equivalent attack animations or target acquisition.

The retained419 smoke also recorded a real right-hand `GunHold` for the selected
opening knife while the chest-knife option was disabled, with no holster
suppression commits. This supports using the existing selected-item claim;
it is not a420 runtime result or proof that every startup path produces it.

420 publishes a separate selected-knife display source from the typed selected
item observation, current equipment identity and existing right-hand claim.
The renderer binds that source to its current selected mesh and rig, uses an
independent authored profile, and saves the actual controller-to-wrist rotation
with the resulting pose. It grants no left-hand support, selection, ammunition,
aim or damage capability. A chest draw or temporary body operation is not created.

The original input/configuration deadlines remain in force. A changed actor,
native or physical equipment generation, reference space, firing-object identity,
selected configuration or rig invalidates the receipt. The normal per-copy
palette checks from419 continue to apply. Before a future motion consumer uses
the blade tip/direction, it must validate the complete receipt against a fresh
source, selected mesh and current full rig identity.

The portable `SelectedMeleeAttack` policy is added but **not connected to native
gameplay input**. It shares the existing hand claim, recognizes motion with
`MeleeStroke`, requires neutral input after interruption, and emits one intent
for a trigger edge or jab. An outstanding native outcome survives tracking or
selection changes. Uncertainty blocks another attack, without globally blocking
holsters, vehicle Use or hand ownership. A timer or idle animation cannot supply
completion. The native adapter still needs exact submission/outcome integration.

Seven focused suites passed, including new selected-knife geometry/lifetime and
melee-intent tests plus existing cycle, rig and motion-boundary tests. Full-build
results are recorded in [the420 handoff](../../handoff/checkpoint420/README.md).
There is no420 actual-game/headset acceptance of the new placement or motion.

The remaining physical-stabbing work is to connect calibrated blade motion to
the selected native attack and its acquisition lifetime, preserving native reach,
collision and damage rules. The419 read-only ownership sample found the matching
server melee effects and acquisition transform, but no cached target. A successful
native attack-state transition is not a hit: target/damage evidence and a miss
control are still required. Death/restart distortion and the original416 SPAS
failure remain separate unresolved acceptance items.
