# Candidate284: retain the existing pump grasp during a brief native read gap

The283 input-only run completed two physical pumps. On the third stroke, a
native view still identified the same Held cycle, but exposed no currently live
held lease for one controller packet. The adapter discarded the mechanism claim
at gate4. Fresh native authority returned on the next packet; the cancelled
physical recognizer correctly required neutral/regrip, while the diagnostic was
still pulling. It timed out. The game stayed alive and detached cleanly. This
run did not reach the last round and therefore did not validate283's empty-tube
Step dispatch fix.

284 distinguishes missing current authority from loss of the existing grasp.
Only the exact same native owner, cycle, shot, hand owner, item and gun claim may
retain an already-live mechanism claim. Its original deadline and packet remain
unchanged. No held lease, guided target, native release or completion is created
while authority is unavailable. Suspension, replacement, expiry and actual hand
release abandon the physical stroke. The existing neutral/regrip path can then
restart that same owed cycle without resetting its deadline or ammunition.

The new regression fails on283 at the retained-claim assertion. It also checks
changed weapon/cycle/shot, native suspension, native cancellation, expiry and
explicit release, then exercises neutral/regrip after a longer gap. Existing
geometry-rejection behavior remains intact. No additional native cancellation
was added: the physical consumer already supports same-debt neutral/regrip.

The actual284 run completed eight physical pumps with exact shots, held
boundaries and completion transitions in all three native copies. Ammunition
fell8/24 to0/24, then manual shell insertion produced1/23. Support returned
30.6–33.4ms after pump Ready and131.4ms after the shell transfer. The shoulder
swap completed. There were no native cycle/control failures, and BC2 remained
alive with hooks drained and disabled.

The overall script still failed: its original50second action bound expired
during the returned rifle's magazine stroke. That subtest spent six seconds in
a redundant startup wait after actual inventory publication. Its removal had
completed, but seating and subsequent rifle fire had not. This is explicitly a
pump/shell pass, not a combined-sequence pass. Native trace SHA256:
`5ba46855b9e037ecbfb05f8becafc1bf5240d35f2b78ee998b7392754edeac2a`.
See `resource-empty284-01/pump-shell-receipt-review.json` for all24 branch tails;
record contention means it does not assert complete callback coverage.

285 changes only the combined diagnostic's final warmup to one second, retaining
its fresh native/renderer guards and original overall deadline. Normal gameplay
is unchanged from284. A regression withholds native publication through the
shortened wait and confirms that no command occurs until fresh evidence returns.
The focused x86/x64 checks for284 passed. Full normal builds and headset
acceptance remain separate requirements.
