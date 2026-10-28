# Recovery249: retain admitted magazine poses through drawing

Actual248 aborted before any magazine or pump operation. The magazine palette
fell back three times, twice during warmup and once during approach. The final
fallback had coherent tracked input, unchanged source/owner/claims, and fresh
pose/mesh/family deadlines; only its native reserve publication had expired,
1.491ms earlier. No user headset result is claimed.

The resource consumer now records the actual admission time of its immutable
presentation packet. Rendering verifies that the original native evidence was
valid then, and independently checks the original input, target, family, selected
mesh and ownership limits at drawing time. Both old and current packets must
still retain the same role, native context, original token and hand/gun claims.
No timestamp is rewritten and a newer packet cannot extend an older pose.

The native ammunition reader, operation admission, raw magazine contact, chest
ammo supply and ordinary MagazineTrackingFresh/MagazineTargetFresh checks retain
their original deadlines. Only render publication, palette construction and
palette selection use the distinct visual check. Legacy animation-hold poses
without a resource presentation admission retain their earlier behavior.

Six focused suites pass on x86 and x64, including 45 legacy physical groups,
12 shared-profile groups and six replacement-spawn groups. New tests reproduce
native expiry during a still-valid pose, prove that ammo reads/new operations
remain rejected, and reject changed ownership/claims, stale input/mesh/family/
target, invalid admission, mismatched resource state, absent current packet and
incoherent shot evidence. Full249 passes all240 suites on both architectures
(x86 121.13s, x64 33.21s). Frozen source digest:
`74d40976d29c182e294b3c3100206a0f066ba0448f05239ce11fe2ec137fb157`.
Actual249 completed one original-magazine return with conserved 30/210 counts,
then removed/discarded the original and acquired the replacement. It recorded
796 paired magazine presentations and zero fallbacks. It failed during Guided
replacement carry on an expired resource read (first active cancellation: View,
input1335, request855, cycle3). No refill, holster or pump step completed. Native
ammo calls3/completions3; the separate discard also completed. All hooks drained,
the game remained responsive, and scene diagnostics flushed42/42 rows without
loss. Trace SHA256:
`af9e65fca105484dc2a2ccfc7d9142e3e0ce47007f47cb66259e2b60e086bc85`.
This is partial native evidence for249, not combined-test acceptance.

This fixes the reproduced publication-to-draw boundary. Custody-only deferral
from248 still publishes no new pose during missing native data; longer visual
continuity gaps and replacement supply gaps remain separate work. Pump recovery
and checkpoint-reload crash acceptance are still outstanding.
