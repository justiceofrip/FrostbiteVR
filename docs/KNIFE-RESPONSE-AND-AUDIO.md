# Knife response and audio: staged observation and timing policy

October 25, 2026. This work is staged outside the checkout while R39 is built.
It has not been built, run, installed or enabled. Production knife windup,
recovery, damage and audio remain unchanged. The sound observer is default off;
the timing policy has no native caller.

## What the current source establishes

`Bc2SelectedKnifeNative.cpp` owns the selected action37 intent, acquisition,
search-argument mapping and original Attack/Damage observations. It receives a
read-only Update observation from the existing `Bc2ReloadFlowRuntime.cpp`
dispatcher. It does not currently change native windup or register a competing
Step/Update hook.

The historical [336 timing investigation](KNIFE-TIMING-336.md) verified that
native state5 prepares the configured 500 ms windup, and state6 prepares the
remainder of the authored `60/52` second attack cycle. A private 50 ms windup
with the other 450 ms paid into recovery retained client and server effects.
Zero windup did not retain client effects. The experiment did not establish a
production native transaction token or complete prediction-copy integration.

The current selected-melee Action is cleared when the original Attack callback
returns. That does not prove every native copy has finished its recovery.
Any timing obligation must therefore outlive gesture completion, tracking loss
and the short input-admission deadline. It cannot borrow a newer gesture.

## Authored audio identities

Read-only SP_01 DBX references supplied the following primary-instance links:

| Animation | Animation GUID | Sound reference |
| --- | --- | --- |
| Knife_Equipable/1p/1p_Fire | `1d20a23e-cc50-47c9-a980-4ad6d720f27e` | `sound/weapons/handheld/knife/knife_attack/dfdd6206-dea5-41e5-b0e1-26efb820af1d` |
| Knife_Equipable/1p/1p_FireKill | `9375ab7e-7a30-493c-9661-dc14df523de9` | `sound/weapons/handheld/knife/knife_attack_kill/06577182-b49c-4f88-9652-3e0054d5944c` |

DBX reference values are in the child `ref` attribute. These static links do not
prove which callback fires in a specific attack, that FireKill caused damage,
or that sound should be emitted on recognition rather than native acquisition.

The existing sound adapter verifies native start/stop at VA `0x881650` and
`0x881690`. `Bc2MechanismAudio` owns these hooks. The staged knife observer uses
that boundary; it adds no hook and always returns to the existing dispatch.

## Default-off observation

`FVR_BC2_PRIVATE_KNIFE_SOUND_OBSERVE=1` is read during selected-knife installation.
Absent, empty or another value leaves it disabled. Disabled dispatch performs
only the cheap flag check; no knife owner reads or journal work run.

When enabled, only the two exact animation GUIDs are considered. The receiver's
soldier, first-person byte, owner flags and retained local native knife lifetime
must match. Unknown, NPC, third-person and rejected events still run stock.
There is no suppression, replacement playback, attack request, timer write or
damage inference.

`selected_knife_native.sound_observation` retains the first32 and newest32
accepted callback rows. A nonblocking service lock produces an explicit
contention count instead of delaying sound. Each row may list the current and
last completed same-owner intent within two seconds. These are temporal
candidates, not a native shot-token join. Two candidates remain ambiguous.
The original attack records provide independent acquisition/attack timestamps,
damage-call and health observations. A sound label cannot manufacture them.

## Shared timing policy, not yet a native adapter

`MeleeResponseTiming` operates on semantic three-copy identities and a supplied
timing contract. It proposes an exact expected/replacement timer pair; only an
explicit write receipt changes its bookkeeping. It owns no gameplay state.

- Windup advances are bound to the original intent deadline, exact owner,
  callback invocation and branch revision. A delta at least as large as the
  retained windup leaves that branch on stock timing.
- Applied advances create recovery debt. Closing admission does not erase it.
  Recovery requires fresh native ownership evidence but not a live hand packet.
- Unknown/late write outcomes remain unresolved; expiry never fabricates a
  no-effect result. Only explicit exact lifetime retirement clears them.
- Prediction rewind requires a retained export receipt and an exact complete
  64-byte payload match at the corresponding original restore. A verified
  rewind creates a new branch revision; old write receipts cannot pay twice.
  The bounded eight-entry receipt journal refuses evicted provenance.

The pure regression additions cover three-copy cooldown preservation,
decelerated gesture completion followed by late recovery, 50–1000 ms hitch
fallback, stale/replaced owners, uncertain writes, exact-copy rewind and replay.
They do not prove the BC2 native export payload's field mapping or authorize
hook integration. The future adapter must supply that proof from real callbacks;
passing a semantic Boolean in a deterministic test is not native evidence.

## One bounded next observation

After root integrates and builds these staged sources, adapt the existing
`knife430/RunKnife434.ps1` route metadata to the new exact runtime and current
process. Do not rerun the old `RunGuard43404.ps1` unchanged: it carries old build,
process, approach and C: Python references. Keep all new output/temp on G:.

Use one owned SP_01 first-guard run with the existing emulated controller
fixture and original action37 path. Set the observation flag in the native game
environment before injection. Keep the existing storage, build/hash, process,
owner, cleanup and no-OS-input guards; raw images are unnecessary for audio
timing. Use the existing single attack budget. Do not substitute a sound for
the existing native acquisition/attack/health/progression checks.

The final report must show the observer enabled, unchanged native timing,
complete callback/cleanup receipts and no suppressed sounds. Retain all
unmatched/ambiguous rows and contention/rejection counts. Compare the exact
local sound timestamps with the same-owner candidate's existing native records.
If no usable callback arrives or journal contention loses it, the sound timing
question stays unresolved; do not infer success from the static GUID map.

Only after those receipts should a private timer adapter be proposed through
the existing Update/Step/Restore dispatcher. It must preserve the full authored
cycle and prove notification survival under real hitch/prediction conditions.
The old 50 ms experiment is a starting point, not production acceptance.

## Missing native copy proof and concrete staged trace

The current reload dispatcher already hooks original Update, Step and Restore.
Its function named `Transfer` is ammunition transfer, not snapshot export.
The separate 206-byte export function is already discovered and verified as
`ReloadFlowBinding.code[7]`, fingerprint `0xd2217ee6ded18b97`. The existing
276-byte restore function is `code[6]`, fingerprint `0xf1240aaf85772b50`.
Private336 used that export ABI: `thiscall(firing, snapshot)`, returns the
snapshot pointer, `ret4`.

Staged v2 adds `FVR_BC2_PRIVATE_KNIFE_TIMING_OBSERVE=1`. Only this explicit flag
registers the one missing export hook under the existing dispatcher; it shares
the dispatcher's enable/disable/drain lifecycle. No duplicate Step/Update/Restore
hook is registered. Export calls its original once and returns its actual
pointer unchanged. Default builds still register only the original five hooks.

The trace scopes wrap the existing original calls. They retain exact local
native ownership, all three firing objects, input/intent candidates, original
caller, thread, parent Update invocation, raw native timer/context words, full
64-byte export/restore payloads, and the original export return pointer. A failed
inner Update observation masks its outer TLS parent so its Step cannot borrow
the outer invocation. Rejected, contended, full or unfinished rows stay explicit.
The journal has 4096 slots and records at most two seconds after each original
intent publication. It cannot grow per frame or extend attack authority.

`AnalyzeKnifeObservation.py` in the private preparation directory reads one
pinned final native report. It reports native5/6 transition rows, exact
same-owner full-payload export/restore candidates and sound/native attack time
differences. Multiple indistinguishable exports remain ambiguous. It never
turns the application intent serial into a claimed native shot token and never
authorizes a timer change. No game or tests are launched by this analyzer.

For the next existing-fixture observation, set **both** private flags before
injection. Use one attack; do not repeat an unchanged failed attempt. The report
must show `gameplay.reload_flow.knife_export_observation_hook=true`. Preserve
native damage/health/progression and cleanup evidence alongside the observation.
The concrete remaining decision is whether each copy's state5 advance and
state6 recovery can be paired with its original Update and exact exported
prediction state without lost notifications. If the trace is incomplete, fix
that specific evidence gap before staging native writes.
