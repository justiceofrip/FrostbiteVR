# Joined SPAS pump capture validator

This offline tool joins the existing three-copy hold audit to the new original-rig
capture schema. It verifies capture completeness, original native receipts,
native-owner identity, original lease bounds, actual nearby per-copy states,
proper rigid transforms, held samples and captured readiness. It also rejects a
second native8→1→2 completion for the same held shot, which the existing audit's
first-completion recognizer does not reject. It never grants runtime authority.

Run with the actual completed finite diagnostic trace and matching receiver:

```powershell
python -B audit_pump_capture.py --hold-auditor <source>/tools/audit_pump_hold.py --trace <trace>/native-trace.json --receiver <receiver> --output joined-audit.json
```

The command hashes all four input files and the selected baseline auditor. Exit0
means the supplied capture observations passed the checks, not that manual
pumping or a submitted mesh has been proved. Exit1 writes rejection details.
The dependency auditor must match pinned SHA256
`8838ad170f102857d1035fa4749b8e60806c8864849a26f0f77aa98bd35c4293`
before it is imported. Outputs resolving to, or aliasing an existing source file
via a hard link, are rejected before writing. Trace/receiver/auditor evidence is
preserved even on CLI failure.

Twenty-two synthetic schema tests exercise a valid record set, malformed samples,
original restore failure, truncation, wrong native owner/config, expired selected
and native leases, invented input time, reflected/nonfinite matrices, unjoined
native states, false stability, missing ready samples, duplicate completion,
all five CLI output-source collisions, hard-link alias and unreviewed auditor
import rejection. Workspace test scratch files are retained under test-output.
Fixtures preserve `input_observed_ns=0` as missing metadata. No TTL is subtracted
to invent a receive timestamp. Rig sampling is15ms, so short native8/1 phases may
be absent from sampled rig rows: their actual ordered transition is established
only by the complete native record stream, never by interpolation.

The fixture tests use the existing source's `test_audit_pump_hold.fixture`; their
absolute baseline path is recorded as an isolated development dependency. For
canonical integration, place this tool in tools and the test in tests and resolve
that fixture relative to the test folder. No full native rebuild is needed for
these standalone Python files; no native source or prepared binary was changed.

## Remaining live evidence

The ordinary pump backend remains disabled. This task did not run the finite
PumpHoldProbe. First integrate the separately reviewed passive capture candidate,
then obtain one clean15-second two-shot diagnostic with all-copy original hold
and restore coverage and state-labelled fore-end/wrist rows. The existing finite
probe is the only supported capture source; it holds350ms and requires at least
three loaded rounds. Actual original-once behavior is established from its
complete callback receipts and counters, not from this tool's synthetic fixtures.

Even a joined passing capture still needs measured closed stop, rear direction,
actual wrist/fore-end contact, submitted geometry association and both-eye visual
acceptance before ordinary pump presentation. A distinct chamber and last-round
or zero-loaded pump path are not covered. Bolt and other weapons remain subject
to their own native configuration/mechanism evidence. No game/process access,
launch, injection, input, canonical edit, or BF2142 edit occurred.
