# SPAS bottom-entry candidate, revision 4

The user clarified that shells enter the SPAS from underneath, not through the side ejection port. The previous candidate was derived from a saved shell-animation pose near the underside centerline, not an ejection socket, but its authored 50 mm stroke ran nearly horizontally. More permissive capture alone did not correct that trajectory.

Revision 4 starts the shell below the receiver and moves it upward and forward along the direction measured between saved native animation rows 60 and 62. It preserves the measured terminal shell pose and the existing hand-to-shell grasp. A portable unit `travelDirection` now controls displacement separately from the shell's orientation, so the shell stays lengthwise while the hand guides it upward. Existing profiles default to travel along entry +Z.

The SPAS travel direction in weapon coordinates is approximately `[0.031178, 0.816184, -0.576950]`. Over the authored 50 mm stroke this rises 40.809 mm and advances 28.848 mm. The conservative transformed shell bounds start more than 25 mm below the sampled receiver plate. Independent exact-vertex analysis finds 27.189 mm clearance. The terminal pose is unchanged; the hand grasp and axial-roll alignment remain unchanged.

This is an interaction candidate, not a verified loading-mouth binding. The sampled underside surface is closed, and the saved terminal shell bounds still straddle that surface. No named native loading socket has been identified. The 50 mm straight travel follows the final measured approach direction; it does not reconstruct a two-stage hinged/pivoting insertion or provide receiver collision simulation.

The 3 cm capture sphere, 35 degree shell-axis limit, 120 ms alignment, 5 cm insertion travel and 60 ms seating dwell are unchanged. Capture requires observed motion from outside the sphere; it cannot insert ammunition by itself. Claim ownership, source deadlines, pose-jump limits, native-cycle checks, native acknowledgements and ammunition conservation remain enforced. The runtime consumer, diagnostics and scripted probe now use the independent travel direction consistently.

The probe's retained `target_z` diagnostic field now denotes signed projected travel distance, not entry-frame Z. Geometry `rail_tip_m` remains the three-coordinate entry-frame position; `radial_m` is perpendicular distance from the travel axis.

Focused offline validation passed on x86 and x64 with `/W4 /WX`: 19 portable rail groups, 10 SPAS adapter groups, 18 physical-consumer groups, 9 probe/actual-tracked-rig groups and 7 private-presentation groups. These include upward travel without rotating the shell, withdrawal/lateral/jump guards, below-plate start, the preserved native terminal pose, repeated two-shell consumer operation and matching shell/hand transforms in the emitted private palette. All six production callers of the changed by-value profile were freshly compiled for these tests to avoid mixing old and new object layouts.

The retained 192-contact regression remains explicitly frozen to revision 3's original coordinate frame. Its representative rotations are reconstructed from recorded axis and total angles, not recovered full wrist matrices. It is historical regression evidence, not evidence that revision 4 captures that same movement. An independent position-only re-expression finds three old samples inside the new capture geometry, at source sequences 3616–3618; that calculation does not prove state-machine capture or seating.

No game process, input, graphics fixture, SteamVR or BF2142 session was accessed by this candidate. Full canonical builds and subsequent native/headset validation remain separate integration steps. There is no new headset acceptance claim.

Related evidence: `reports/spas-reload-candidate-20261001.json`, `reports/reload-shell-observations-20261001-161937.json`, `reports/reload-spas-mesh-evidence-20261001.json` and `reports/spas-bottom-entry-20261002.json`.
