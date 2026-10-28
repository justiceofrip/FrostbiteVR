# Native manual-cycle callback evidence (222)

`Bc2NativeCycleEvidence` supplies pure validators for the common native cycle
service owned by the pump/runtime candidate. It does not install hooks, select
families, touch memory, mutate ammunition, or authorize a sniper configuration.

* `ValidateCycleUpdate`: completed independent invocation, exact owned branch,
  original parent/update identifiers, retained owner, matching thread/snapshot,
  valid copied context and exact context preservation except the native scratch
  word at `+0x14`.
* `CycleShot`: original unheld Update, exactly one loaded round consumed, reserve
  unchanged, positive remaining ammunition and a manual post-shot state. Runtime
  admission must still exclude ammunition resource commands and require the
  exact configuration. Last-round/empty-feed charging is outside this scope.
* `CycleHold`: parameterized current/previous/next and timer bounds, exact
  requested/applied/restored delta receipt, unchanged state/timer/counts/flags.
* `CycleCommit`: a completed direct child of the caller's actual open Update,
  exact native current/previous/next transition, no context/timer/count change.
  It grants no ready acknowledgement by itself. The service accumulates the
  required ordered tail and waits for the successful parent Update on all three
  owned firing branches.

The runtime is responsible for fresh exact code/config admission, invocation
replay protection, serialized cohort state, cancellation/debt preservation,
and original-callback execution. The shared validators deliberately do not
replace those duties. Add the implementation to `BC2Camera` and its test to the
normal BC2 test group; it only depends on the existing context decoder. The
focused MSVC `/W4 /WX` candidate builds and five evidence test groups pass on
x86 and x64. The unchanged decoder dependency is compiled separately at `/W4`
because it has existing shadow warnings.

`probe_bc2_cycle_callbacks.py` executes the installed original Update and Commit
instructions with the 17 extracted authored bolt configurations. It passed 102
cases (three deltas, stock and 59-update tail hold) and captured 306 actual
Commit calls. Each is a direct original Update child, returns with the expected
stack, and preserves context, timer, and ammunition across Commit. The original
tail is `7→8→1→2`. For the sniper hold candidate, `7→8` precedes the held
`current=8, previous=7, next=1` boundary; release requires the later `8→1→2`.
SPAS instead holds `7/6/8` with positive authored delay. These boundaries must
not be substituted for one another. The callback report is
`bc2vr-recovery/bolt222-candidate/native-callbacks-01.json`.

This emulator uses synthetic state7 setup and empty effect listeners. It proves
no shot receipt, live owner, server/client cohort, native sniper admission, or
headset presentation. Earlier 34 shot negative controls confirm that this setup
cannot stand in for a successfully consumed shot. No game process was opened.

The separate `native-readiness-payload` updates `WeaponActionGate` with explicit
`WeaponActionReadiness::NativeAfterShot`. It keeps the exact owed physical action
until matching gesture and native completion, then removes its veto so ordinary
native firing rules apply. It accepts unknown chamber state without asserting a
separate chamber or an extra round. Independent acknowledged native feed adds
no charging requirement in this mode. Fifteen composed physical gesture test
groups pass on both architectures, including repeated bolt/pump cycles with
chamber knowledge remaining Unknown. Semantic chamber mode remains available.

This native readiness mode supersedes the earlier frozen bolt report's proposed
separate chamber-model prerequisite for after-shot cycling. Remaining sniper
integration gaps are exact runtime configuration/owner admission, actual shot
and all-three native completion receipts, and measured handle/closed-pose/part
calibration. M24/SV98 saved motion alone does not establish those part semantics;
no sniper palette writer or registration is included.
