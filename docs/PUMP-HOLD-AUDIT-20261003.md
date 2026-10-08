# Finite SPAS pump-hold audit

This is an offline auditor for the explicitly opted-in `PumpHoldProbe` diagnostic.
It cannot access a game process, send input, or authorize ordinary manual pumping.
The separately frozen source candidate has manifest SHA256
`96604e9c320c8a1ec321dff21149f37cbc84974d860cdbd40ffae21147af5ec1`.

After the source has been integrated and every configuration-layout consumer has
been rebuilt, the supervised monitor trial is:

```powershell
& <local-workspace>\Test-NativeStream.ps1 -PumpHoldProbe -Seconds 15 -Pairs 240 -StaticPose -Async
```

That command is **not executed by this audit**. It requires the already selected
SPAS, at least three loaded rounds, and a suitable ordinary firing direction.
The receiver supplies ordinary 80 ms trigger pulses at 3 and 6 seconds. The native
diagnostic may hold the first shot's verified three-copy state 7 for 350 ms, then
lets the native simulation advance normally. There are no reload/equip markers.

After the wrapper finishes, run the auditor against its actual output paths:

```powershell
python -B tools/audit_pump_hold.py --trace <native-trace-folder>/native-trace.json --receiver <receiver-folder> --output <new-audit.json>
```

The receiver folder must contain `result.json`, `pump-hold-preflight.json`, and
`pump-hold-postflight.json`. The audit hashes all four original input files and
writes a separate result. It never changes those inputs.

## Acceptance and limitations

- Pre/postflight must be coherent read-only observations of the same process,
  supported executable, selected SPAS, and exact native client/server ownership.
- All three exact firing objects must retain current 7, previous 6, next 8,
  loaded/reserve counts, and their own timer across the hold. Every recorded held
  invocation requires original-once counters and exact original four delta bytes
  restored; unrelated native output bytes are preserved and may differ.
- Every copy must have applied receipts near both ends of the finite hold; no
  selected Update may escape it. The arming invocation may enter just before the
  policy begin timestamp, but must overlap that timestamp and finish promptly.
- Native Commit observations must actually show 8, 1, then idle 2 after release.
  Missing intermediate evidence is a failed observation requirement, not proof
  that the game failed to advance. The tool never synthesizes those transitions.
- Each copy must observe exactly two one-round decrements, ordinary Fire input,
  and the prescribed pulse windows (80 ms plus the existing 150 ms input lease).
  Reserve stays unchanged; no reload-transfer callback may occur. Final idle
  counts must agree with the separate postflight.
- Missing/unfinished/unretained observations, read/context/nesting failures,
  recorder contention, truncation, or expired recording windows make coverage
  incomplete. Positive mechanical observations and complete coverage are
  separate verdicts; `passed` requires both. `owner_misses` is retained separately
  because the hook also sees unrelated native firing objects before selection.
  Native patch/restore errors or inconsistent original-call/apply/restore counters
  always fail the mechanical verdict, even when the visible rows look correct.
- No result claims HMD acceptance, gesture-controlled readiness, last-round or
  auto-reload behavior, visual animation suppression, continuous pumping, or
  native bolt/LMG support. Closed-stop, motion sign, hand-contact geometry, and
  native-state correlation still need the corresponding captured rig evidence.

`test_audit_pump_hold.py` contains synthetic recorder-schema cases; its passing
fixtures are **not native evidence**. It exercises byte restoration, all-three
coverage, native identity, missing/changed evidence, count/trigger chronology,
and actual file/CLI hashing. Run `python -B -m unittest discover -s tests -p test_audit_pump_hold.py`.

The historical-negative report deliberately checks a real older SPAS shell
reload trace against this different pump contract; it must fail and cannot be
used as a substitute pump run.

# Corrected canonical SPAS geometry

The frozen mechanism source candidate correctly marked its original exploded
assembled image as unsuitable for absolute grasp placement. Parent review then
identified the missing coordinate conversion in that separate offline image:
asset vertices need `[x, y, -z, 1]` before applying the inverse-bind and native
matrices already converted by `Bc2Rig::ReadFirstPersonRig`.

The corrected saved-data replay is
`<local-recovery>\check-spas-canonical-geometry.py`.
Its result is
`<local-recovery>\spas-canonical-geometry-root-review\spas-assembled-side-derived.png`,
with derived metadata in the adjacent `spas-current-motion-derived.json`.
The parent visually verified a contiguous receiver/barrel, ribbed `jntWpn_4`
fore-end under the barrel, and folded stock above it. The corrected assembled
bounds are `[-0.02673, -0.18179, -1.03558]` to
`[0.02591, 0.04875, -0.28375]` in the canonical units.

This fixes absolute assembly of the authored geometry. It does not identify
which dynamic stop is closed, establish the rearward sign, or correlate a rig
snapshot with native firing state 7/8. The measured relative fore-end travel
remains 0.09495844 m. The private-part planner still requires an independently
verified captured closed pose and signed rear direction; the frozen source did
not consume the exploded bounds and is unchanged.

`evidence.json` binds this addendum to the replay script, corrected image, and
derived metadata hashes. No original mesh or animation assets are copied here.
