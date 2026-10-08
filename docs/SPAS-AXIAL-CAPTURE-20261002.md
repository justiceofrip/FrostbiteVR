# SPAS shell entry revision 3

The real headset run `native-trace-20261002-001451-153` produced six ammunition acquisitions and three native hold cycles, but no magnetic captures, seats, submissions or completed reloads. All 980 recorded geometry evaluations stayed Free; none was rejected as a pose jump. This is an interaction failure, independent of the separately reported streaming interruption.

Two entry restrictions explain the retained near-contact evidence. The last 192 rows belong to item 6, cycle 3. Their first row (source 3476) is outside the 3 cm sphere at positive rail Z; that pose clears the previous front-only arming state. None of the remaining rows observes an outside-front pose. The first subsequent axis-fitting entry, source 3613, is 28.9593 mm from the entry center at positive Z 27.1606 mm. Its 2.57 degree longitudinal-axis error fits the existing 35 degree cone. Later, source 3629 is only 8.75938 mm from entry with a 5.65 degree axis error, but its full keyed rotation is 37.01 degrees. A round shell should not require that extra axial twist alignment.

The SPAS-specific profile now explicitly selects `EntrySphere` and `AxialSymmetry`, revision 3. EntrySphere requires a genuinely observed pose outside the same sphere before entry from either hemisphere. Initial-inside poses still cannot capture. The 30 mm sphere ends before the 50 mm seat depth and its hysteresis band; capture cannot grant a seat or ammunition. The player must still advance inward, complete alignment and dwell, then receive the separate genuine native acknowledgement. Generic magazines retain their keyed, front-only behavior.

All transforms and dimensional, angular, temporal and tracking limits remain unchanged: 30/60 mm capture/release distances, 35/70 degree axis cones, 40 mm and 60 degree maximum sample steps, 50 mm stroke, 120 ms alignment, 60 ms seat dwell and 100 ms input freshness. Axial alignment preserves roll and the authored shell-to-hand grasp. Its angle uses `atan2(axis XY length, axis Z)` to avoid generating a spurious swing from small floating-point axis-length error after the measured authored transform round trip.

The offline replay uses actual retained positions, source sequences and original timestamps. Full rotation matrices were not recorded: reconstructed rotations preserve the recorded full/axis angles, with synthetic twist sign and tilt azimuth. Therefore this is a constrained representative replay, not exact native replay or a headset acceptance result.

| Policy on the same retained 192 samples | Captures | Physical seat candidates | Pose jumps |
| --- | ---: | ---: | ---: |
| Revision 2, front hemisphere and keyed rotation | 0 | 0 | 0 |
| Front hemisphere with axial rotation only | 0 | 0 | 0 |
| Revision 3, entry sphere and axial rotation | 1 at source 3613 | 1 at source 3640 | 0 |

Both x86 and x64 focused runs passed 18 portable insertion groups, 9 SPAS adapter groups, 18 actual physical-consumer groups and 6 actual private-presentation groups. They cover unchanged generic magazine behavior, initial-inside rejection, reverse-axis rejection, observed outside entry, unchanged teleport/twist limits, no automatic seat at capture, native acknowledgement separation, measured near-contact angular constraints, and roll-preserving output through the actual shell-bone presentation planner without changing source rig bytes. Full project builds and the next actual headset test are parent-owned follow-ups; this candidate performs no live actions.
