# Pistol support versus magazine contact307

The stricter306 actual-game check exposed a real interaction conflict before a
headset test. A squeeze at the pistol's measured support point claimed its
magazine instead. The support wrist used a legacy calibration while ammunition
used the free anatomical wrist. The two authored contacts are61.03mm apart,
inside overlapping grab volumes.

307 uses the anatomical wrist for primary-hand support contact and its emulated
controller target. Long-gun two-point steering keeps its established basis.
The same immutable magazine contact packet carries the distance to a reachable,
measured support point. Both magazine backends pass this evidence through their
existing owner, source-packet and geometry validation.

Shared `DetachableMagazine` arbitration compares initial contact distances.
The closer support target wins ties; explicit eject remains independent of a
spatial squeeze. A lost competition consumes that squeeze, so moving a held hand
onto the magazine cannot cause a delayed removal. After a magazine claim exists,
nearby support cannot steal it or interrupt its removal/insertion.

Eight focused suites pass. The added adapter check verifies no native operation
or ammunition change during a support squeeze, then one native removal after
a fresh, closer magazine grab. Shared legacy/resource fixtures cover ties,
invalid distances, explicit eject, neutral rearming and preservation of existing
magazine custody. Both normal builds passed all243 suites (x86 144.45s, x64 50.91s).

Source: `c463deae2e1671a27b8a52260f43e29edcf8d3ce187c5f0bd9570c5367b83eeb`.
The exact MP443 contact remains in support306 data;307 changes shared policy and
BC2 packet production/consumption. No new weapon's native reload capability is
admitted by this contact change. The failed306 run and saved305 checkpoint remain
intact. Headset feel, other unmeasured pistols and slide/chamber steps remain open.


Native307 pistol run02 passed support grab/release with unchanged primary aim,
manual removal, chest replacement and firing:17/85 ->0/85 ->17/68 ->16/68,
all three native copies. It consumed240 stereo pairs with zero presentation
fallbacks and drained its hooks. Run01 remains a failure during an idle approach;
no removal had started. A separate launch exited during loading before the VR
probe; cause is unknown. A later launch with a longer settling interval loaded.

Native307 SCAR combined run02 passed rifle support, original-mag return,
replacement from chest, shoulder exchange, SPAS shot/pump, shell insertion and
immediate support re-grip. It failed during the final rifle approach before
removing a magazine. The complete combined sequence did not pass. See its
runtime-status.json. The subsequent308 diagnostic change waits at most300ms
for ordinary idle rearm only with unchanged owner, request and ammunition,
no hand custody, and no operation. It does not relax active reload checks.
