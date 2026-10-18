# Per-frame opening cloud correction candidate —424

Checkpoint423's actual one-frame experiment aligned cloud features to0px stereo
disparity, compared with271–283px in the baseline. Nearby people retained stereo
depth. Later unmodified frames in the same423 run again showed265–288px cloud
disparity. The exact two native packets restored after both left-eye scopes;
eight12-byte writes, zero unresolved writes, matching frame64146 captures.
This establishes a measured correction for those packets, not complete campaign
or headset acceptance. The frozen423 experiment remains unchanged.

This candidate applies the same guarded translation on each eligible tracked
frame. Enable explicitly with `BC2_WEATHER_TRANSLATION=1`; default is disabled.
It requires tracked stereo and excludes `BC2_WEATHER_TRANSLATION_EXPERIMENT`.
The one-shot `BC2_WEATHER_OBSERVER` remains optional and independent, so images
can be captured at50seconds while correction continues afterward. Nothing is
installed or promoted by this source change.

## Frame ownership

A fixed two-packet cache is admitted once per tracked frame, with a monotonic
epoch plus world/request/main/eye/native-frame key. Entry to the verified kind1
and kind8 environment producers is counted before the original native call.
The entry token follows each callback through its completion. Publication
requires exact current epoch before deferred native reads, completed original
execution, unchanged root camera, verified packet/arena ownership and matching
world-setter callsite. Abandoned or duplicate producers invalidate only their
frame. Old callbacks cannot publish into reused addresses or a later frame.

Left Prepare and Draw independently claim immutable completed packet snapshots
in that order. A claim requires both packets, zero unfinished producers, no
duplicate builder and no active consumer. Entry of a producer during an active
consumer invalidates the integrity receipt. Native calls are never skipped or
replayed. The right-eye path receives no translation changes.

The existing full64-byte checks, finite measured eye delta,12-byte translation
writes and exact explicit restoration remain. Both consumer paths restore in
SEH `finally` blocks. Producer wrappers likewise abandon admission and unwind
callback lifetimes in `finally`, preserving native exception propagation and
EAX returns. Cache locks cover only small CPU copies/metadata; no lock is held
across native execution, process-memory reads, writes or graphics commands.

Frame finish retires cached addresses. A new owner/frame always starts empty.
Missing optional sky, unsupported non-right-centered authored matrices, or an
invalid inside observation skips acceptance for that frame and does not disable
later correction. Safely restored but invalid observations are never counted as
completed corrected pairs. Actual restore conflict, unresolved bytes or observed
producer/consumer overlap after writes remains a terminal render failure.

## Bounded evidence and cost

No per-frame file, image, allocation, or unbounded context scan is introduced.
Only two packet slots and first/selected/latest completed-pair snapshots are
retained. `weather-translation-runtime.json` records schema
`fvr.bc2.weather-translation424.v1`, frame/scope totals, skips by claim/rejection
reason, restore failures, publication rejections, writes and callback drain.
Drain refers to admitted observer/correction callbacks, not native pass-through
calls that access none of this state. The module and trampolines remain loaded
until process exit, so a pass-through return cannot enter unloaded code.
Each retained pair uses the423 exact owner/packet/before/inside/after schema,
adds the frame epoch and an explicit `validated` flag, and requires both left
phases. First may belong to the pre-restart owner; selected/latest identify the
later scene independently.

QPC totals/max measure bounded correction work around validation, publication,
apply and restore; they exclude original native Prepare/Draw/environment/world
calls. These measurements still include scheduling delays and are not a CPU
profiler. The global world setter delegates immediately when no relevant TLS
producer or selected diagnostic observation is active.

Native gather/render ordering remains the consumer-boundary assumption already
tested in423. Exact storage observations do not provide an atomic allocator
snapshot or a proof of every asynchronous GPU lifetime. Repeated-frame actual
captures, scene replacement, restoration/coverage counts and measured cost are
required before enablement in a normal player build.

The ProjectionOverride suite adds epoch/address reuse, delayed publication,
unfinished/duplicate/overlapping producer, ordered/repeated consumer phase,
retirement and missing-frame recovery cases. Parent owns all builds and actual
game tests. This document initially records prepared source, not a successful
424 run or headset result.
