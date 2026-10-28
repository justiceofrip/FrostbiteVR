# Candidate287: keep physical custody separate from native read permission

The286 native run stopped during the first pump's forward stroke. Fresh
controller packets continued with grip held, but a temporary missing native
held lease outlasted the previous physical claim. The input driver observed
Tracking cancellation and correctly stopped rather than manufacturing a pump
completion. Native control remained at the held boundary until diagnostic
shutdown. This run therefore did not exercise286's completion-read fix.

287 lets the existing physical-cycle recognizer renew its exact current grasp
through HandInteraction::RenewFrom when the adapter still observes the same
held native owner, item, cycle and shot. The original controller/contact packet
must be fresh and present in the arbiter's evidence history, both hands must be
tracked, and the same current gun claim must remain its prerequisite. This
cannot acquire or resurrect custody, move the pump, publish a guided target,
refresh a native lease or complete a native operation. Missing/repeated contact
evidence retains only the old deadline. Missing native identity still does not
permit renewal. Motion resumes only with fresh native permission and within
the recognizer's existing input-gap bound.

Focused regressions cover a longer gap with fresh controller evidence, no
movement authority during that gap, subsequent full stroke, repeated evidence,
forged observation time, expired custody, and changed owner. Full builds and
the combined actual-game run remain pending. Normal280 is the last frozen
player candidate; no headset, whole-roster or campaign completion is claimed.

Failed286 trace: `pipeline-runtime-20261005/root-monitor/resource-empty286-01`.
SHA256: `2f562ac7e32b3318fc16494c20f06bd8535fcde756ecd830cfde23c1b8203e5d`.
