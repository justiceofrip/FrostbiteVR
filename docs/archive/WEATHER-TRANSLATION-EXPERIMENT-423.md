# Opening cloud translation experiment — checkpoint423

This is an opt-in, one-frame diagnostic, not an enabled gameplay correction.
The source is prepared for independent review and builds; this document does
not claim a successful build, actual-game correction, or headset acceptance.
Frozen checkpoint422 and the installed desktop runtime remain unchanged.

## Measured basis

The exact opening02 frame83258 in checkpoint422 contains two environment state
packets, kind1 and kind8, whose identity world matrices are translated to the
right-eye camera. Their world allocations are separate from the retained
camera contexts corrected by `ScopedContextEye`. Matching cloud features in
the paired images have roughly271–283px disparity while a distant hillside is
about3px. This supports an A/B experiment; it does not prove left-eye GPU use.

The retained static/native analysis is
`bc2vr-recovery/overnight422/opening02-weather-matrix-review.json`, SHA256
`6f9f0b3f21b0b032404fe01dcce2f4980e1c1a65769fc8ebe287212d58a7687c`.
The installed executable is SHA256
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
The packet allocator creates128-byte packets, the world setter creates a64-byte
matrix through the packet's owner arena, and enqueue retains the packet pointer.
Arena `used` is a byte reservation cursor. The existing observer pins the
six-argument environment ABI and world setter;423 additionally pins the exact
allocator, enqueue, and sky-submit bytes in both disk and runtime images.

## Admission and changes

Both `BC2_WEATHER_OBSERVER=1` and
`BC2_WEATHER_TRANSLATION_EXPERIMENT=1` must be inherited by the game process.
The observer delay remains bounded1000–90000ms, default50000ms. Absence of the
experiment flag leaves normal rendering and the read-only observer unchanged.

Only the observer's selected world/request/main/eye/native-frame identity can
admit a scope, and only the exact left Prepare followed by left Draw may apply.
Each requires completed environment-builder records for the two verified
kind/site combinations. It rechecks packet owner, arena identity, bounds,
allocation cursor, world pointer and flag0x2000. All packet and full matrix
regions must be disjoint. The entire64-byte matrix must still match the
recorded identity basis with translation exactly equal to the current right
camera. Camera bases must agree; the finite measured eye delta must be nonzero
and no larger than0.15m.

For each packet, only matrix words12–14 (12bytes) are replaced with the left-eye
translation. Protected copies compare all64bytes before writing; no protection
changes, allocation changes, or camera/weapon/gameplay mutations occur. The
inside snapshots must retain exact storage and exactly match the planned full
matrix. Each scope restores immediately after its original Prepare or Draw.
The right-eye path remains unchanged. A successful experiment makes eight
12-byte writes: two apply plus two restore for each of the two left scopes.

## Restoration and limits

The fixed two-entry transaction has an inert destructor. Every apply and
restore uses the same guarded current-owner/frame/thread/arena callback.
The original native calls are wrapped in leaf `__try/__finally` functions so
restoration runs on structured-exception unwind as well as normal return.
Original exception propagation is preserved. Callback admission is sealed and
drained using the observer's existing dedicated lifetime.

Owner change, modified expected bytes, unreadability, or a partially faulted
write rejects restoration rather than writing through a stale destination.
Unresolved writes, or failure to verify exact post-restore bytes, set the
existing terminal render-restoration failure and invalidate capture acceptance.
Pre-write rejections invalidate the experiment without changing native data.
No failed attempt is retried in another frame.

These checks establish exact observed storage at the boundaries; they do not
make sequential reads an atomic native allocation snapshot. The experiment
uses the existing native per-view Prepare/Draw consumer boundary; native
producer exclusivity relies on the existing gather/render ordering. It does not
introduce a native allocator lock or prove all asynchronous GPU command
lifetimes. A completed builder and exact matrices are required, and any observed
concurrent change invalidates the receipt. The paired images, rather than
phase0 root-camera attribution, must establish whether these packets affect
the cloud mismatch. Production enablement requires that result and a separate
lifetime/performance review.

## Evidence contract

`weather-translation-experiment.json` uses schema
`fvr.bc2.weather-translation423.v1`. It records the immutable selected key,
attempt count, overflow/failure, callback drain, incomplete attempts, applied
and restored scope counts, native write count, and two bounded scope records.
Each record contains phase, eye, thread, reason, applied/restored state,
written/unresolved masks, before/inside/after validity masks and owner keys,
and two packet snapshots at each point. Each snapshot contains packet/owner/
packet_owner/world/arena/base/capacity/used/flags plus16 uint32 matrix words.
Keys are `world`, `request`, `main`, `eye_view`, `native_frame`.

The observer report remains separately available with `observer_only=false`,
`translation_experiment=true`, and the actual native write count. Its two
images retain the same selected frame identity. Native graph cleanup, exact
experiment restoration, paired-image availability and visual correction are
separate acceptance decisions. No image is accepted simply because all
eight writes restored.

The portable ProjectionOverride suite covers measured translation admission,
changed storage/camera rejection, allocation and full-region overlap checks,
complete and partial transactions, newer-byte conflict, owner retirement with
no stale reads on destruction, and indeterminate partial-write failure. The
parent owns focused/full builds and the bounded actual-game A/B run.
