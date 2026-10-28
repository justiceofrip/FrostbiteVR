# Physical inventory follow-up after the October 2 headset test

The intended default is physical weapon access: rifles/long guns on the back,
sidearms at the hip, gadgets on the chest, and usable empty hands. Thumbstick
scrolling is a temporary fallback until every carried item has a verified route.
Optional holstering is a development gate, not the intended final UX.

The failed draw was SPAS empty hands followed by ordinary XM8 selection. The
show-only recovery fix preserves SPAS-only hide admission and requires current
native suppression plus a matching paired renderer Show receipt before releasing
the firing block. It needs the actual cross-weapon native test and headset check.

Body inventory previously replaced belt supply with chest supply. The contact
candidate keeps both zones on one provider, resource pool, reservation and hand
claim. In recentered body coordinates (+X right, +Y up, +Z forward), chest remains
(-.16, -.25, .10) m with .16 m radius; belt is (-.23, -.55, .02) m with .18 m radius.
These are separate spheres, not a large region joining them. Looking around does
not move either zone; recenter sets a new forward reference. The belt dimensions
match the old local pouch, but its body-fixed frame deliberately differs from the
legacy head-yaw frame. Sessions without BodyInventory keep the original pouch.

The smallest next implementation steps are:

1. Verify SPAS-empty -> left-shoulder XM8 draw, fresh paired Show receipt, new
   GunHold and released action suppression in the live game.
2. Run a separate scoped-XM8 hide/free-hands/show and fire-suppression test in
   both eyes. Admit XM8 mask bit 2 only after that evidence; the draw repair does
   not itself grant permission to hide XM8.
3. Keep native inventory generations and actual pickup replacement authoritative;
   assign all observed long-gun families to back contacts by metadata and retain
   current-item identity through native selection acknowledgements.
4. Reflect and verify sidearm and gadget classes and their non-firing native
   selection routes before adding hip/chest bindings. The existing reader only
   verifies class values 0..5. A separate read-only static audit now identifies all
   21 enum entries, including Hgr 6, At 7, ATMine 8, AutoInjector 9, C4 10, Knife 11,
   LaserDesignator 12, MedKit 13, MortarStrike 14, MotionSensor 15, PowerTool 16,
   TracerDart 17 and AmmoBox 18 (19 Count, 20 None). There is no pistol/sidearm
   entry: hip assignment requires additional verified semantic metadata or a
   profile. Static enum names alone do not authorize item routes or visibility.
5. Make BodyInventory/physical selection the normal launch path after coverage
   and headset acceptance. Disable thumbstick scrolling only when every current
   carried item has a reachable physical slot and a verified draw route; retain
   a recovery fallback for unsupported pickups until their bindings exist.

Neither candidate changes ammunition counts or fabricates a native reload
completion. Belt/chest acquisition and cross-weapon draw still need live/headset
acceptance. Full builds must recompile every AmmoSupplyConfig consumer because
its optional contact is an appended ABI field.
