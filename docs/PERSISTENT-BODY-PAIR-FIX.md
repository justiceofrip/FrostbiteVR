# Keep one carried-weapon world pose for each stereo pair

The original persistent body candidate read a fresh display batch independently
at each eye. A new gather could keep the same inventory cohort while changing
the body anchor. An independent actual-consumer test reproduced a single frame
accepting controller inputs 1 and 2 with a 0.2-metre difference between the eye
instances. The per-slot host identity check could not detect this pose mismatch.

The follow-up gives the existing native tracked transaction an immutable
`BodyCarriedPairSource`. Only eye zero can establish it from its actual fresh
pre-draw source. It is bound to channel generation, request, native owner/frame,
device, render tracking generation and reference space. The same original batch
and world transforms are used for both eyes. Current evidence still validates
exact actor/equipment/inventory/configuration and intersects original deadlines.
A changed owner, recenter, expired original source, backwards input, or different
image lease cannot reuse it. Reset runs at new-lease acquisition and frame
retirement. Atomic shared ownership keeps callback references immutable.

**Controller input and render tracking have separate counters.** The host can
increment the input counter on inactive frames without a new tracking frame.
There is intentionally no equality test between those counters. The original
controller sequence stays in the original typed inventory snapshot; the image
lease's tracking sequence remains a separate key. A later gather can validate
the first-eye source but cannot supply a replacement second-eye pose.

This does not claim that controller and camera predicted times are identical.
Both eyes use one actual first-eye source, retaining existing freshness and
same-space requirements. It introduces no history lookup, synthesized input or
new source lifetime. Cold/missing/stale configured geometry, native owner loss,
reload-busy policy and original expiry still omit display. A first-eye source
loss cannot be repaired by populating only the second eye. Current native frame
timing and visible admission rates remain a monitor/headset check.

The 128-byte instance, 2352-byte frame and eight-instance limit are unchanged.
No new native writes or authority for weapon selection, fire, hide or ammo.
Existing ammo and selected committed-hide paths are not changed by this patch.

The new deterministic regression builds actual inventory assignments and host
instances, then exercises the actual pair helper and host comparator. It covers
separate counters, a later gather changing world position, all frame-key fields,
original expiry, same-pointer inventory replacement, recenter/focus loss,
concurrent source establishment and reset. Both architectures pass five groups;
the composed x86 NativeProbe translation unit compiles. No game/GPU run occurred.
