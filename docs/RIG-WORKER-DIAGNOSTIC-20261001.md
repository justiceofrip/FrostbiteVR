# Review candidate: character worker to native view/request

This candidate supersedes the failed assumption that palette packing happens on
the Visibility/Prepare caller thread. It adds a default-off diagnostic hook around
the actual character prepare callback; it does not change rendering behavior,
native animation sources, inventory, ammunition, simulation or input.

## Files and validation

`Bc2RigWorkerBinding` holds BC2-only discovery and callback-driven ownership
validation. `Bc2RigWorkerRuntime` owns the optional x86 native hook. The existing
portable VR policies and aperture math are unaffected.

Eighteen focused cases pass on x86 and x64, covering valid main/right identity,
no eye guessing, shared stereo batches, caller/types/bounds, missing/duplicate
actor rows, changes during repeated reads, normal arena allocation progression,
foreign view ownership/read failures, 1024-row limits and immutable view-key
deadline/generation checks, original-deadline preservation, conflicting-frame
invalidation, frame ordering/wrap and explicit global-start failure/cleanup.
Installed-EXE discovery and six mutations outside
signature headers pass/reject as expected on both architectures. Runtime objects
compile for both; the isolated NativeProbe x86 object compiles with the repository's
NOMINMAX definition. No live call/hook/fixture has run for this candidate.

## Static binding

Discovery requires unique executable signatures and complete inspected function
fingerprints for character prepare, its worker, scheduler, workspace initializer,
workspace finalizer and renderer constructor. Vtable slot+0x24 and queued callback
relationships are checked. Live installation verifies those complete code ranges
again before creating a disabled hook. Scene vtables come from the parent's
existing independently verified view/request/world discoveries.

The character method uses ECX renderer and four stack arguments, ending in ret16.
Its sole inspected invocation returns to worker+0x75 (preferred0x00bb4895).
Runtime uses the matching thiscall trampoline/fastcall detour; passes all four
arguments untouched; preserves the incidental EAX result; calls original once.

## Read proof and scope

For each current owned actor, the reader checks:

1. Exact renderer vtable, caller and renderer+0x6b0 workspace.
2. Workspace→job packet/batch metadata/arena; unique job entry matching the actual
   callback row count, row array and third argument.
3. Actual batch primary view→verified view vtable→request at+0x70→world at+8;
   exact request/world vtables and stable world frame counter at+0x88.
4. The bounded embedded batch view list, same-request view ownership, and one
   exact actor row with the native view-mask word. Missing or duplicate actor
   rows cannot acquire ownership.

The complete identity is read twice. After original returns, the runtime repeats
it again and requires the identical fresh semantic owner and view key. Native
arena allocation cursors may advance normally; they are not stable identity.
Bounds are1024 actor rows,4096 job entries and16 batch views; ranges/overflow and
duplicates are checked. Readers never call a native method. Partial failures are
logged separately and never become an admissible producer identity.

The actor packer's existing GetA/GetB ownership proof and actual packed-byte check
remain inside the new worker scope. Its existing source token, original tracking
deadline, paired destinations, named bone roles and ambiguity guards still apply.
The hook records metadata only; no palette replacement or GPU suppression is added.

## Cross-thread view identity

The old `ReloadProducerViewKey` accesses non-atomic stereo control fields, so the
worker cannot call it directly. The isolated NativeProbe patch publishes a small
immutable pair of already-validated native eye keys at the existing visibility
boundary. A worker obtains that atomic snapshot, checks its original200ms lease,
the exact native frame and request links, and looks up its ACTUAL batch primary
view. It never substitutes the current global eye or the nearest pose timestamp.

Repeated visibility callbacks for an identical world/request/frame retain the
first observation and deadline, including after expiry. A differing eye pair for
that frame invalidates it permanently until a newer frame arrives. Older frame
or older observation publications are rejected; unsigned frame progression
permits counter wrap. A bounded atomic compare/exchange loop prevents concurrent
publishers from replacing a newer lease with their earlier snapshot. Thus the
visibility publisher cannot resurrect a stale frame by refreshing its timestamp.

An unrecognized primary view remains unassociated even if a recognized view is
elsewhere in the batch. A batch containing both recognized stereo eyes is marked
multiple_stereo_views and remains unassociated. Additional same-request shadow
views may be recorded; they do not change the primary-view key. The first native
capture must confirm this observed layout and view relationship. This establishes
a diagnostic producer scope, not proof of selected skin/GPU draw ownership.

## Parent wiring and lifetime

`integration.patch` is the minimal NativeProbe change; `integration-base.json`
stores the exact staged source hash it was generated against. The full isolated
copy is only a review/compile artifact. Apply the small patch around concurrent
boat changes rather than replacing the staged file wholesale.

* Add Bc2RigWorkerBinding.cpp to BC2Camera and its test to the shared test list.
* Add Bc2RigWorkerRuntime.cpp to BC2NativeProbe (Windows/x86 implementation;
  unsupported x64 stubs stay disabled).
* Installation is reached only by the existing bounded PassEvidence/reload-hold
  diagnostic path, with Hands enabled, non-continuous mode and duration≤15s.
  Production headset sessions do not install the hook.
* Install requires explicit diagnostic=true and creates only a disabled hook.
  BeginGlobalStart marks the hook potentially enabled BEFORE MH_ALL_HOOKS.
  CompleteGlobalStart is called even if global enabling fails; failure performs
  an explicit local disable and never opens admission. Only this bracketed,
  successful global-start contract accepts MH_ERROR_ENABLED from the subsequent
  local enable verification. A partial global-enable failure cannot report
  quiescence before successful disable. Admission stops after 15s or12288 calls.
* Disable runs before gameplay cleanup. Quiescent requires both the native entry
  patch disabled and in-flight callback count zero. Parent cleanup waits at most
  200ms and includes this result in hooks_disabled. Failed disable/drain keeps the
  reported state false and the existing retained-module policy applies.
* Partial setup/error cleanup explicitly disables the worker hook too. No
  trampoline is freed, no native callback is left pointing at unloaded code, and
  the module remains resident until process exit as in the existing probe.

Report key `reload_worker_binding` includes status names/counts and128 bounded
records with worker/job/batch/arena, actual view/request/world/frame, actor row
mask, original observation times, repeated-read outcome and observed vtables.
Jobs without this actor increment owner_missing but do not consume record slots.
The native producer report then establishes whether paired callbacks were
captured inside these real scopes; geometry/draw identity remains a separate gate.
