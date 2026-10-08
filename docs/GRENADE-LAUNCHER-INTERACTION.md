# Grenade-launcher interaction request

User headset feedback, October 1: launcher hand sits below its grip, the launcher
handle cannot be grabbed, and selecting it as a separate weapon slot does not
provide the desired VR interaction.

Required player behavior:
- Keep the working rifle as the physical item.
- Reach to and flip up the launcher sight to engage launcher mode.
- Use the launcher's own authored handle/grip in that mode.

Folding the sight down to return to rifle mode is the proposed inverse action;
it has not yet been implemented or headset-tested. Do not add a new thumbstick
selection scheme. General body weapon grabbing remains deferred.

Implementation boundary:
The shared framework should represent a physical weapon-mode interaction with
contact, intentional manipulation and tracking-loss cancellation. BC2 must map it
to its verified native launcher state, including whichever native equipment
selection is required internally; that backend need not be exposed as a separate
player-facing VR slot. Other Frostbite adapters supply their own native bindings.

Before enabling: observe the actual launcher item/parent rifle relationship,
authored handle and sight bones/pivot, supported sight animation/state and native
mode transition. Validate separate grip/aim/muzzle data rather than reusing the
XM8 rifle grip. Native ammo, grenade trajectory and existing animations stay
authoritative. No projectile settings or guessed transforms should be written.

Current implementation: SPAS12_sp and XM8_sp_s are the verified grip/absolute-aim/
muzzle/support profiles. The launcher is outside that gate, explaining why this
test does not inherit the full repaired behavior; its exact offset still needs
measurement. No launcher fix or sight interaction is implemented by this note.

Protect the accepted baseline: source/binaries are in
reports/weapon-alignment-20261001/checkpoint.json; partial headset acceptance is in
reports/headset-grips-success-20261001-072621/acceptance.json. Do not replace the
running headset test or alter that checkpoint to investigate the launcher.
