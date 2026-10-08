# Repeated simulated-player checks, October 7–8

Work in progress: no new headset acceptance. The latest actual headset feedback
remains the failed193 rifle reload and unrequested draw. Native tests below use
scripted tracked input through real consumers; they do not emulate subjective
comfort, headset optics, or every human gesture.

## What the extended tests found

Candidate198 extends the magazine diagnostic to return an original partial
magazine, then remove it again and insert a replacement without resetting the
consumer, native cycle, inventory, claims or ammo. Only the synthetic driver
advances to the next stage. The original30-second deadline remains in force.
Its success check reads the final outer result, so a successful first stage cannot
hide a failed second stage.

- `magazine-player198-01`: original22-round magazine returned without changing
 22/191; replacement completed30/183. Real free carry measured12.65cm and0.65rad.
 Both actual native cycles completed. One asynchronous image timeout makes the
 overall transport result inconclusive; preserve that failure.
- `crossdraw-player198-01`: actual inventory slots produced XM8→SPAS→XM8, with
 two paired stows and a paired draw recovery after an input interruption during
 ShowPending.240 stereo pairs, zero timeouts or camera restoration failures.
 The runner incorrectly expected SPAS afterward; raw postflight failure remains
 saved, alongside independent coherent XM8 postflight evidence. This does not
 establish XR-host belt/back texture correctness or interruption while Empty.
- `magazine-player198-06`: first original return completed. During second carry
 the diagnostic failed17 and explicitly cancelled the real consumer. Native ammo
 eventually reached30/179 through ordinary fallback, with zero physical seat
 requests or completions. Final ammo alone is therefore not a passing result.
- `shells-player198-01`: one shell acquired; zero submissions/completions. A
 fresh typed CohortGap during native startup caused the real tube consumer to
 cancel before its first held lease. Stereo delivery and normal cleanup passed.

Raw evidence is under `pipeline-runtime-20261005/root-monitor` in the recovery
root. Shell trace and receiver are also preserved under source reports:
`native-trace-20261008-004522-485` and `native-ipc-20261008-004521`.

## Diagnostic199 correction

Prepare runs before the next real consumer Tick. The preceding cached native
hold observation can expire between them. The scripted Carry driver now waits
at most50ms from its first missing observation while preserving its existing
command. It neither moves nor measures the hand during that wait. Loss of active
state, hand ownership, removed phase or observed paired role fails immediately;
actual consumer cancellation still wins. Native authority and gameplay are
unchanged. Exact failure flags and timestamps distinguish later failures.

A real shared-consumer mock with one10ms lease reproduces the old driver failure
on x86/x64; the staged correction passes18 focused groups. The old native trace
does not log the individual failed predicate, so lease expiry is a supported
ordering explanation rather than a directly recorded historical field value.

## Tube startup200 investigation

At input424 in the shell run, coherent owner/configuration evidence overlapped a
native callback revision27373→27389. The reserve deadline was still fresh and
the exact player, item and configuration were current. The consumer's CohortGap
handling required an already held native lease even during startup, where no
such lease existed yet. It cancelled MissingSource before native timers reached
the hold window. The fixture reported its own failure about2.85s afterward.

The shared tube consumer now preserves only its existing shell during this
startup gap. Its wait ends at the earliest of 50 ms after the first gap, the
previous accepted controller deadline, or the original 3-second startup limit.
It rechecks time after the native keep-alive call. Fresh ownership, selected mesh,
original reserve and the exact existing shell claim remain required. No geometry,
new acquisition, native hold or ammunition acknowledgement is inferred.

Both architectures pass 47 focused consumer cases; the old consumer fails the new
startup continuation case. Tests include release, lost tracking/owner, rejected
reads, duplicate and fresh packets across timeout, expired startup pulse, original
startup timeout, and a keep-alive call crossing the deadline. A fresh reserve read
alone still cannot authorize insertion without the genuine native held lease.

## Combined200 results

Both complete builds pass all 186 suites (x86: 69.74s; x64: 23.47s).
Build receipt: `pipeline-runtime-20261005/normal-player200-build-receipt.json`.
Probe SHA256: `82084e3bd522b9be58f9c1dc37a6deb1e902cc41959dd557eeefcbf976b331a2`.

`shells-player200-01` completed two actual insertions in one native cycle:
6/24 → 7/23 → 8/22. Claims2/3, request/seat IDs1/2 and native invocation IDs2703/3692
are distinct; each shell has its own measured approach, entry and insertion.
The only cancellation followed the second acknowledgement because the tube was
full. All native hold writes restored, 240 stereo pairs arrived without timeout,
and BC2 remained responsive through 15 seconds of post-run observation.

The current and archived fixture both command a −9cm approach. The old audit
incorrectly required −6cm. Its three endpoint checks now match the actual authored
path while retaining the 0.1mm endpoint tolerance and all original receipt,
ownership and raw-motion requirements. All 17 checker regressions pass, including
obsolete/fabricated/missing trajectories. Seven audit sections pass on the actual
run; the overall audit **still fails** its separate ordinary-palette fallback
coverage requirement, which this run did not exercise. Do not relabel the complete
audit as passing.

The actual run's startup-gap counters are zero. Its native two-shell completion
is established; live execution of the corrected intermittent gap remains
unverified. The added baseline-failing consumer tests establish that path only
in controlled software execution.

`magazine-player200-01` failed the repeated magazine sequence. The original
22-round magazine returned correctly with 22/191 conserved. Cycle 2 started at
input 611; the native gate then cancelled with Timing while the original 100ms
Reload pulse was still active. Input 613 received KeepAlive rejection before
removal or free carry. The diagnostic reported failure 5 afterward. The 199
Carry correction was not exercised. Final 30/183 is ordinary native fallback,
not physical reload success. The strict sequence audit remains inconclusive.

This run delivered 240 stereo pairs with zero timeouts, 31 clean client and 31
clean server samples afterward, and verified loaded code matching the200 build.
Hooks retired and native render state restored; BC2 was minimized afterward.
Next correction: an immutable startup-pulse boundary before native Arming may
enter Holding, with fresh safe observations from all three branches afterward.
Do not loosen the Holding input checks or accept fallback ammo as success.
