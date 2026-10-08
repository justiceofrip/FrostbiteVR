# Empty-Step capture report

This standalone, read-only tool summarizes the schema-1 journal included in the
prepared `headset-115352-fix-composition` test build. It does not modify that build,
launch BC2, inject hooks, or authorize a native reload change.

Run after the mod has stopped and flushed `native-trace.json`:

```powershell
python -B tools/report_empty_step.py C:/path/to/native-trace.json --output C:/path/to/empty-step-summary.json
python -B -m unittest discover -s tests -v
```

The report finds nested `empty_step_diagnostic` sections and preserves each
session separately. It reports paired near-empty entry into native reload
state10, the final diagnostic stage, decoded input/context values, original
owner and firing branch, fresh support-hand state, restoration anomalies, and
the amount of lost or overwritten diagnostic data. A paired entry means the
before and after reads belong to one invocation with retained ownership and
the same firing object. It does not establish why that invocation entered reload.

Interpret `visible_predicate_mismatches` as recorded values, not a complete
reimplementation of the eligibility gate. Native configuration, full server
identity and several call-boundary checks are not serialized. In particular,
the recorded clock is sampled after the original call; a deadline expired by
that observation may have been valid when the gate ran.

`explicit_reload_input` distinguishes a recorded reload-button bit from an
entry without that bit. Neither case alone proves automatic reload caused by
support grip. Unknown or expired interaction evidence stays unknown.

The previous failed test does not contain this new journal. Running the tool on
that saved trace produced `missing_diagnostic`, rather than treating its aggregate
applied counters as success. `previous-run-evidence.json` records the input hash.

Validation: 20 synthetic schema/correlation/CLI tests passed. The CLI was also run
on the complete prior native trace. Synthetic fixtures do not prove native pump,
reload, or headset behavior. Python standard library only; no native build change.
