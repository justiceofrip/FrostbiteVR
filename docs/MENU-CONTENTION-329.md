# Preserve a held menu button through brief contention —329

The native menu consumer previously emitted cleanup button-up on every Busy IPC
read. A Busy read is mutex contention, not a controller release. This could end
a held click early and disarm the next button event even while the last controller
sample was still valid.

The shared menu sequence now retains the original control deadline and epoch.
The runtime keeps an already-owned press during Busy only if the last sample
contained a focused pointer-down in the current menu epoch and remains unexpired.
It sends no cursor, down or up event during this interval. Busy data never renews
the deadline. Expiry, invalid/closed input, lost focus, and clock reversal revoke
retention and require a fresh neutral baseline before new actions. The326 fresh
cursor-before-release correction remains in place. Native ABI and graphics paths
are unchanged.

Focused regression passes199 successive Busy reads with unusable future-deadline
bytes, exact expiry, different menu epoch, clock reversal, fresh neutral input,
and invalid/timeout/closed recovery. Native telemetry adds `control_busy_retained`
so a later headset trace can distinguish exercised retention from zero contention.

The real-game327 and328 restart sequences remain prior-source evidence; neither
observed Busy reads, so they are not native acceptance of this329 branch. The326
intermittent AMD command-worker crash remains unresolved. See
[restart evidence and the combined headset checklist](RESTART-EMULATION-327.md).
Knife, extra native pistol capacity, broader chambering and full belt-fed loading
remain separate unfinished capabilities, not implied by menu or reload test passes.


## Validation

Both full normal builds pass244 suites: x86 140.57s, x64 44.09s.
The normal329 DLL passes `menu329-contention03`:35.17ms and125.46ms IPC
contention, seven retained Busy observations, two downs/two ups, neutral
rearming, native menu open/close,240 stereo pairs, zero exceptions, disabled
hooks and BC2 still alive. Trace SHA-256:
`5bce8ce5640ea10d1f4bced585a67e5e9a1395f8919c93dee1fe8c4d2496c59f`.
No desktop input or weapon commands were sent during the menu test. The separate
native setup exited the boat and equipped the rifle beforehand.

Earlier attempts remain failures:01 was rejected before hooks for missing normal
session flags;02 exercised menu retention but produced no world-eye pairs. Its
setup omitted neutral controller input and the on-foot preflight.03 corrects the
harness, without changing329 source or weakening any acceptance condition. The
separate setup timestamp-format refusal also sent no input; its exact process
creation time was preserved for the corrected invocation.

This is native menu acceptance, not new reload, death, headset or campaign
stability acceptance. The private327/328 scenarios retain their original scope.
