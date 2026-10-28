# Stationary controller testing — 383

The October 12 headset375 failure remains the latest human acceptance result.
This work improves the test driver; it does not establish a playable campaign.

## Failure found by the longer actual-game check

Candidate382's normal builds pass all246 suites on each architecture. Its
eight-cycle held-support composition passes with mocked native responses. The
same input driver in BC2 completed the first pump but timed out acquiring the
second. The second shot did fire; loaded ammunition fell from7 to6.

The recorded virtual left hand then crossed behind the right hand. It moved
from approximately(-0.118,-0.353,-0.377) at support return to
(-0.262,-0.429,+0.296) before support released with reasonCrossed. Squeeze,
tracking and focus remained active. The driver's movement was bounded per
packet but followed the rendered weapon throughout native waiting/settling.
That creates feedback with the two-hand aim solver, which moves the rendered
weapon in response to the same controller. Constant controllers in the simpler
renderer fixture had concealed this problem.

The run had no native control rejection before its diagnostic timeout. Its
eventual cancellation followed the failed driver's neutral input and shutdown.
It does not reproduce or explain headset375's third-cycle PublishedLinks
rejection. Exact named-link diagnostics from382 remain available for that case.

The first actual pump did demonstrate381's earlier support return: the ordinary
support claim appeared before native completion, without granting early firing
or erasing native cycle debt.

## Correction and regression

The finite controller driver retains its last tracking-space hand pose during
native waits, settling and recovery waits. Held-support scenarios also retain
it while firing and awaiting the native held boundary. Actual approach and
pumping phases continue their bounded, measured motion. Normal player input,
native completion checks and support ownership policy are unchanged.

The regression perturbs rendered weapon placement during waiting/settling
while the virtual player holds still. It fails on382 because controller position
changes. On383 it requires unchanged position/orientation and completion of two
shots, releases, acknowledgements and support returns. Native responses and
renderer boundaries in that regression remain synthetic.

Source identity:
`c0b4fb6693ef4c4d56ddccc581517dc164a6598587d6fb0962e96fc75d05f311`.
Native operation admissions are unchanged. Private work/evidence lives under
`bc2vr-recovery/overnight383`; build and actual-game results will be recorded
after completion. No headset-ready package is declared by this document.

## Completed results and reproduced production gap

Both383 normal builds pass all246 suites (x86 142.47s, x64 85.00s). The actual
game no longer exhibits the earlier simulated hand drift. It completes the
first physical/native pump, then waits five seconds for support return and
fails. The hand stays squeezed; native cycle completion succeeds and records
no control rejection. The game remains alive and hooks are disabled.

MechanismAimContinuation drops its captured correction when the physical
mechanism claim ends. Fresh support contact arrives only on a subsequent
renderer packet. During that gap the weapon returns to raw controller aiming,
moving the closed fore-end away from the stationary left hand. This prevents
the intended support return. The connected consumer fixture calculated support
aim but previously did not pass it to its renderer. Once that connection is
included in the held-support case, it reproduces the same first-cycle Settle
timeout. Candidate384 addresses this measured transition;383 is not accepted.

## Remaining work

- Reproduce and resolve the recorded native ownership cancellation, including
  return to the shotgun after holster/rifle use.
- Verify repeated pump and reload completion in the actual game with the
  corrected controller trajectory, then campaign menu/checkpoint transitions.
- Inspect residual SPAS animation distortion in paired rendered captures;
  palette counters alone cannot establish visual correctness.
- Obtain human acceptance of controller alignment, pump feel and shoulder
  acquisition after the automation is trustworthy.
