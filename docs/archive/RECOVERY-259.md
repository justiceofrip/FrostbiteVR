# Pump, support contact and empty-hand recovery —259

Headset258 failed: SCAR manual magazines worked, but its support grip was absent.
The SPAS fore-end moved without completing a pump stroke; interactions then
blocked. The user also reported that right grip away from a shoulder equipped
the SPAS. The mod ended cleanly and its trace is preserved privately under
`bc2vr-recovery/headset258-20261009/headset-feedback.json`.

The source changes address separate reproduced problems:

- A temporary input/reload suspension cleared committed empty-hand intent and
  recovered by showing the native selected gun. Recovery now retains only the
  intent, rechecking current inventory and new paired hide/suppression evidence.
  Off-body squeeze cannot draw it. Explicit cancellation/replacement remain
  distinct. Eight suspension scenarios and a subsequent real shoulder draw
  pass both architectures. Headset history confirms unrequested redraw, but its
  bounded transition journal lost the exact earlier trigger.
- The normal pump required a pause at each end and imposed a wrist-roll gate
  on a non-rotating fore-end. The shared opt-in sliding-rail policy accepts ordered
  rear/front crossings, bounds overtravel and speed, and retains lateral,
  ownership, tracking and complete-stroke requirements. Bolt detent semantics
  remain separate. A continuous original-controller stroke fails before the
  change and succeeds after it; partial strokes and tracking/identity loss
  cannot complete. A transient unavailable native observation no longer destroys
  an existing physical grasp, nor does it grant new pose/native authority.
- Support aiming was discarded when the hand became a pump mechanism. The
  resulting gun rotation changed weapon-local raw contact even with stationary
  controllers. `MechanismAimContinuation` preserves the right-local aiming
  correction across the exact support/gun -> mechanism/gun claim transfer.
  A real HandInteraction/TrackedRig regression reproduces the discontinuity and
  proves continuity with the helper, plus rotation/duplicate/expiry negatives.
  Left controller data and stroke progress remain independent. End-of-cycle
  transition smoothing is not established by that regression.
- Support-only authored enrollment covers twelve existing magazine variants
  through six measured anchors. It changes no right-hand or ammunition binding.
  Original selected-mesh/input evidence survives through publication and Pack
  validation. See [support enrollment and explicit gaps](../../profiles/support259/README.md).

The combined input driver now must acquire and release rifle support before
reloading. Its interrupted-pump scenario uses a continuous faster stroke,
overtravel, and nonzero two-hand aiming correction before firing. Missing support
evidence makes the diagnostic fail before ammunition operations. A bounded pump
gesture journal records early phase/failure, raw position and admission failures;
later native owner errors no longer stand in for the first physical cause.

Validation: all240 suites pass on x86 and x64. Actual-game SCAR259-01 acquires
and releases SCAR support, completes two magazine cycles, and completes the
interrupted continuous SPAS pump with 0.347682rad initial support correction,
one native acknowledgement and support return. Shell insertion then succeeds
(7/24 -> 8/23), but the later shell-to-support return times out. The complete
combined run therefore FAILED; its trace is preserved under
`bc2vr-recovery/pipeline-runtime-20261005/root-monitor/resource-scar259-01`.
Aim continuation recorded92 captures,27 applications and zero rejections.
These are actual native input-test results, not headset acceptance or
whole-roster readiness. Follow-up260 investigates the final-shell return.
Campaign death/checkpoint reload crashing remains unresolved.
GitHub remains checkpoint245; no automatic publication.
