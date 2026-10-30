# Feed equipment and physical pistol slide source

This batch completes additional source components for CHAT15 and CHAT16. It does
not complete the full campaign task list or enable unverified BC2 mechanics.

Free placement now carries a pouch/feed object through an irregular hand path and
requires aligned held dwell followed by a fresh release. Presentation results
explicitly distinguish object poses from hand-contact poses.

FeedAmmunition retains separate belt/container identities and finite round custody.
Transfer authority is explicit; no physical stage silently chooses an ammo write.
FeedEquipmentSession combines actual gestures, retained ammo, one-shot adapter
dispatch and exact receipts/current observations. Historical accepted work survives
tracking/stow, while stale or mismatched current counts cannot reconcile a reload.

PhysicalPistolSlide adds a serialized measured pull/return/release consumer with
current hand claims and distinct after-feed/chamber authority. Tactical retained
chambers do not invent a slide debt; unknown chamber state remains unavailable.
No Pump/Bolt receipt or loaded-ammo count substitutes for chamber authority.

Root checks passed on x86 and x64: FeedAmmunition15, FeedInteraction39,
FeedReloadSession17, FeedEquipmentSession14 and PhysicalPistolSlide18 groups.
The final equipment results are check02; check01 pins the earlier13-case source.
The other suites use check01. Results and final source manifest are in
`reports/coordinator-20261030-implementation17`. Focused review found no defects.
Unchanged FeedReload and prior staged C4/grip/sight checks were reused.

Exact MP443 authored geometry now identifies the rigid slide and signed rail
travel; hand contact is transient, and animation does not prove native chamber,
lock or firing state. Images remain private; compact measurements are retained.
CHAT27's new static service/method records narrow the missing camera-owner link;
they have not yet produced a supported runtime camera correction.

BC2 native feed/chamber bindings remain unfinished. No new native capability,
full normal build, canonical integration, source push or installation occurred.
The user will test in a headset later; no further desktop-only PC tests are planned.
