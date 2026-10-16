# Checkpoint 414: shared lower-chest ammunition

The user reports spare ammunition appearing too high. The common ammo contact
now sits 55 cm below the current HMD, 16 cm left and 6 cm forward in the upright,
recentered body frame. This lowers the prior contact by 10 cm and brings it
4 cm closer. Looking around does not rotate the contact with head pitch or roll.
Roomscale movement and recentering use the current tracking reference.

Magazine and shell displays now use that same primary chest contact when body
inventory is active. The older belt reach remains an alternate pickup region,
but no longer selects a different displayed shell location. Held/stowed ammo
shares the body placement contract. There are no per-gun position corrections,
changes to hand-grasp calibration, ammunition grants or reload timing changes.

## Evidence and limits

The preceding 413 real-game emulation actually delivered the XM8 spare grasp at
45 cm below the eyes and the SPAS shell at its old 55 cm belt location. No doubled
camera translation or oversized mesh was demonstrated. Historical headset logs
do not contain the matrices needed to reconstruct the reported high placement;
this change implements a lower common placement rather than claiming an unproven
renderer fault was found.

Focused tests pass: 18 registered magazine configurations across 6,048 emulated
physical poses, with both eye projections checked against independently placed
XR controllers. Cases include seated/standing height, pitch/yaw/roll, translated
native worlds, body turns, roomscale compensation, recenter invalidation and
reacquisition, weapon-identity changes and held/stowed geometry parity. Shell
pickup works at both chest and alternate belt; a visor-height reach is rejected.
These tests use synthetic input and native-call fixtures, not a headset.

The existing installed cache validates all 46 mesh parts. Six currently resolved
magazine meshes place their tops at least 43.7 cm below a neutral head after the
change; XM8 is 46.1 cm, AEK 46.8 cm, and SCAR 47.8 cm. The broader read-only geometry
audit checked 11 ammunition parts against 19 applicable calibration rows and found
rigid, correctly sized geometry. This is not a claim that every gun has a manual
reload adapter or has been accepted in the game.

Final build, actual-game placement and launcher results are recorded in the
[checkpoint handoff](../handoff/checkpoint414/README.md). Checkpoint 413 remains
the rollback. Its pump/bolt recovery and staged SPAS audio are retained.
