# Ammo display with empty hands

The old chest-ammo source requires a current right-hand GunHold. This is useful
for a reload pickup but prevented any ammo from appearing after automatic stow.
The neutral360 capture demonstrated the missing display before any deliberate
weapon draw. Removing the free-hand guard alone would not fix the claim dependency.

362 adds a separate, read-only display for the native-selected weapon while it
is physically holstered. It consumes the existing holstered-source evidence,
the same configured chest contact, the current selected mesh, and an independently
observed positive reserve. It does not construct a HandClaim, AmmoSupplySource,
reservation, native reload request or synthetic input. Current reload and pickup
consumers retain their own prerequisites.

The magazine profile comes from the existing exact native registration and measured
geometry join. The measured SPAS shell uses its existing geometry. Unknown geometry
remains unavailable; a category or asset name cannot grant another weapon's reload
support. This currently displays availability for the native-selected holstered
item, not separate ammunition for every stored weapon.

Original native owner, three-copy idle reserve, selected-mesh, hide/suppression,
input and tracking-space leases all remain mandatory. Positive reserve is only
an availability signal, not a visible count of spare magazines. Empty reserve,
draw, selection, scene or reference-space changes revoke the sample. Both eyes
validate the original source and share its world transform; a later observation
cannot renew the first eye's expiry. Held and stowed sources cannot substitute
for one another within a stereo pair.

The deterministic cases cover shell and magazine display with both hands empty,
agreement with the existing chest transform at two world scales, head turning,
invalid/changed/expired reserve, revoked hide proof, and stereo source transitions.
Full-build and actual-game results will be recorded after they complete. No new
headset acceptance, native reload coverage or full-material claim is implied.
