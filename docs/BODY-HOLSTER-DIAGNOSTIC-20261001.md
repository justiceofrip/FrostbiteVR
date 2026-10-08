# Bounded body-holster diagnostic admission

The ordinary body inventory option keeps its existing shoulder selection and chest ammunition behavior. Production holster acceptance remains false with profile mask zero. A separate explicit diagnostic API admits one SPAS trial for at most 15 seconds; it does not mark native input suppression, free-hand rendering, or XM8 visibility accepted.

After `EnableBodyInventory` and before `Start`, call `EnableBodyHolsterDiagnostic(lifetimeMs)` with `1..15000`. Setup checks the existing native action enumeration, records a genuine QPC-derived start/deadline, and refuses a second admission. This API adds no launch flag. The separate probe owns fixture input, orchestration, and results.

The fixed trial deadline is independent of each original input deadline. It bounds native suppression owner readback, coordinator decisions, private visibility plans, and free-right evidence. Rendering rejects expired trial plans and uses ordinary source fallback; no controller source timestamp or lease is extended. Failure, disconnect and owner reset clear the observation. Stop clears render requests and the immutable observation. An expired trial cannot be restarted by submitting a new input generation.

`ReadBodyHolsterProbe(nowNs)` returns a `shared_ptr<const BodyHolsterProbeSample>` from `Bc2BodyHolsterObservation.h`. The snapshot is captured after the actual cache commit and coordinator call. It contains the consumed full input and original hand sample, full native owner, actual phase/request/native tick, current shared hand claims, exact assigned shoulder even while empty, anchor configuration, genuine suppression/visibility receipts, result, queued target, and cumulative free-right packing counters. Only processing/sample times are newly sampled. The getter rejects stale source data and does not create an acknowledgement.

`rigPublication::ReadBodyHolsterPackCounters()` reports actual pose/copy/fallback counts. A paired count now requires both private copies in the same native rig scope to preserve source bytes, match destination bytes, and pass the free-right guard; a successful second copy alone is insufficient. These cumulative counters do not replace exact request/owner/receipt checks in the fixture.

Drawing the currently selected hidden item skips redundant native equipment dispatch. It still requires a fresh show request/paired receipt and current suppression before reacquiring the shared GunHold claim. Switching to a different item continues through the existing verified equipment route.

Validation: 20 coordinator groups, 34 connected body adapter groups, and 6 private visibility groups on x86 and x64; x86 Gameplay and RigPublication translation units compile. New cases cover invalid/renewed admission, fixed deadline across actual hide/release/draw, original input preservation, SPAS restriction, and expiration without old-palette renewal. No native actions or headset claims were made for this delta.
