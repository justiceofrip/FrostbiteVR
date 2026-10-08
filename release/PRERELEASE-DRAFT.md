# Frostbite VR: Bad Company 2 — unreleased development preview

BC2 has native stereo views through OpenXR, tracked weapons and support hands,
roomscale movement, recentering and an in-headset pause menu. User tests have
also accepted SPAS bottom-entry shell loading, XM8 and AEK magnetic magazine
insertion, XM8 launcher-sight interaction, visible chest ammunition and the
tested boat's head aiming and firing. These are observed WIP interactions;
they are not a complete campaign or all-weapon compatibility claim.

The latest headset run accepted the pickup recovery. Empty SPAS/AEK automatic
reload remains the main regression under test: the shared guard now includes
the missing shell path, and separate monitor checks passed for SPAS and XM8.
AEK and the final combined build still need headset verification. The floating
optic dot, campaign restart stability and animation polish remain open.

Development now uses shared weapon families, exact asset/configuration identity
and measured contact data. The current offline magazine batch covers 21 exact
configurations across seven weapon names, using the real portable consumer with
mock native receipts. The expanded body catalog has 19 whole-weapon display rows.
Those counts do not mean those weapons are playable or headset accepted. New
SMG/LMG native admission, the AEK horizontal sight and vehicle reticle still need
their own integration evidence.

The intended controls are physical slots: long guns on the back, sidearms at the
hip and gadgets on the chest. Current development switches and legacy selection
fallbacks remain while item coverage and transitions are completed. Next work
includes reliable shared reload and inventory transitions, manual pump/bolt and
belt-fed mechanisms, physical zoomed scopes, full-body IK and campaign coverage.
Multiplayer and remote IK follow the single-player foundation.

This snapshot is unpublished and has no exact-package headset acceptance. It
requires the player's own supported 32-bit BC2 installation, Direct3D 11, a
compatible Windows PC and OpenXR headset runtime. No game assets or saves are
distributed. Source tools prepare local body models from the player's files.
The included feature matrix and test card distinguish observed behavior from
offline candidates. Complete the packaged launch, reconnect, transition and
headset checks before a public release.
