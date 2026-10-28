# Input-pause recovery and actual menu restart — 385

The latest human headset test remains failed. This checkpoint does not claim
whole-campaign or new weapon acceptance.

## Reproduced failure

Actual-game384 completed four natural held-support SPAS cycles and returned
the support hand each time. During the fifth rearward stroke, a controller
publication gap suspended the native hold. Fresh tracked input returned with
squeeze still held. The service kept requiring neutral input and expired its
three-second native suspension, permanently blocking the owed cycle. The
native rejection was control reason7 / `Expired`, not the earlier headset's
structural `PublishedLinks` failure. Neither failure is a substitute for the
other's evidence.

## Change

When a suspended hold receives newer valid input from the same owner, it now
attempts the existing guarded recovery conversion. Conversion still requires
fresh, exact own-Update holds from every native branch. It cannot use old
controller motion, restore an expired hand claim, complete a pump, or change
ammunition. Without the required native evidence, the existing bounded hold
remains unchanged. Immediate actual neutral input can still resume the prior
hold route. Recovery itself requires actual release/regrip and a new complete
gesture, with independent guarded native completion.

The finite simulated player can respond once to this observed pause recovery
by releasing squeeze, approaching the current fore-end and completing a new
stroke. It records `native_pause_regrips` separately. A second unexpected
pause, wrong owner/ammunition, missing native authority, missing paired
presentation or missing support return still fails the run. It never restarts
real consumers or supplies their outcomes.

## Validation

- The focused regression fails on384. The correction keeps exact SPAS and
  M95 shot debt through more than three seconds of held input and guarded
  native updates, then permits recovery only after neutral input and a valid
  completion receipt.
- The connected test uses real controller mapping, hand arbitration, native
  cycle service, physical pump consumer and support policy. A native-first
  resume after an expired packet requires an actual release/regrip/full stroke
  and returns support. Its native callbacks and palette counters are synthetic.
- Focused native-cycle, update-route, physical-recovery, pump-driver and
  persistent-inventory suites pass. Nine report-contract tests pass, including
  distinguishing recovered runs from uninterrupted runs.
- Normal384's actual-game menu test opened the VR menu, clicked checkpoint
  restart, observed a different loaded actor, and opened/captured/closed the
  replacement menu. All14 commands succeeded; native menu diagnostics report
  three matched button down/up pairs, three visibility acknowledgements and
  no graphics/release/rejection failures. All240 stereo pairs arrived; hooks
  and callbacks drained. The exception observer reports zero exceptions and
  BC2 remained running. This is one successful restart, not proof that every
  campaign transition or startup crash is resolved.

Private evidence is under `bc2vr-recovery/overnight384` and `overnight385`.
Both full385 builds passed all246 suites. Actual-game385 did not complete:
two shots/pumps returned support, then the right gun claim changed from3 to9
before the third pump acquired the left hand. The support controller remained
tracked and held; support released with invalid-contact reason2. The pump
driver timed out waiting for acquisition. There were no native control
rejections, held suspensions, or attempted native-pause regrips in this run.
Consequently it neither validates nor disproves the385 suspension correction.
The final native cancellation follows diagnostic cleanup, not an observed
native ownership rejection. All240 stereo pairs arrived and hooks drained.

The source/build receipts and complete failed trace are preserved under
`overnight385`. Candidate386 adds bounded hand-arbiter drop observations,
including original claim/input deadlines, processing time and exact rejection
reason. This records inner-consumer failures before the next outer gather can
reacquire a gun. It changes no ownership rule. Its focused test proves that a
clock reversal remains rejected and its evidence survives later acquisition;
the journal is bounded to64 entries. Full386 tests rejected the embedded
storage: the large magazine fixture overflowed its stack on both architectures
(`0xc00000fd`, independently captured on x64). No386 diagnostic was injected.
Candidate387 moves storage into one external gameplay observer. Default hand
controllers carry only observer pointers, and a size regression prevents the
same diagnostic growth. Hand-ownership and45-group magazine focused tests pass;
full387 builds are in progress. Ownership decisions remain unchanged.

The same-gun input-pause correction is shared by native pump and bolt services.
It does not add weapon-specific geometry or expand the18 magazine prerequisites
or69 measured support configurations. Remaining work includes returning to a
pump after rifle/holster use, the original structural ownership rejection,
visible stock/hand motion, and sustained campaign acceptance.
