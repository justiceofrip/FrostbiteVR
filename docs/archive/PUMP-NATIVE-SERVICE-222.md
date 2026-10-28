# BC2 native pump service candidate

This isolated payload connects independent native callback receipts to the
shared physical pump consumer through a sustained, invocation-local native
delta gate. Ordinary activation remains **off**. It does not modify profiles,
native ammunition, firing-state fields, phase timers, or native function bytes.

## Native operation

`Bc2NativeCycleService` consumes explicit selected-native-to-hand owner mapping.
Native equip generation and shared hand equip generation are separate domains;
the actor, actor generation, item, and space must match their explicit mapping.
Only the existing exact `IsDiagnosticSpasConfig` is considered. No sniper or
generic shotgun admission is added.

Each client/server branch must first produce its own independent unheld
OriginalUpdate receipt with exactly one loaded-round decrement, unchanged
reserve, positive remaining load, and an actual manual-cycle path. All three
receipts must agree on resulting counts/capacity. The native evidence helper
rejects diagnostic recorder IDs, sibling/nested Update substitution, wrong
owners, changed protected context bytes, and invented count-only idle shots.

Fresh neutral contexts and all three SPAS boundaries `7/previous6/next8` permit
the existing scoped context-delta override. Arming requires at least 0.1 seconds
remaining. A shared held lease is published only after all three exact own
Update receipts prove applied/restored zero delta with unchanged states, timer,
and ammunition. Issued leases preserve native/controller deadlines and are
bounded by the oldest original hold receipt. The native cycle has a 30-second
limit. A physical short stroke cannot release the gate.

Only an exact release referring to an actually issued lease removes the hold.
Direct original Commit children must then show `7→8→1→2` on each branch, followed
by each owning OriginalUpdate ending idle with unchanged ammunition. BC2's
existing native firing rules decide readiness; no separate chamber or extra
round is inferred. This SPAS tail sequence is deliberately not a generic bolt
descriptor: the measured sniper candidate holds after `7→8` instead.

The immutable ready outcome remains retained and fire remains blocked until its
exact acknowledgement. Shared `ReconcileReady` can resolve a locally timed-out
physical transaction only when its actual original completion preceded the
original physical deadline. It cannot renew a lease, publish a target, accept a
different request, or turn a completion after the deadline into success.

## Runtime wiring

The staged `Bc2ReloadFlowRuntime.cpp` routes candidate owned Update/Commit hooks
through this service, using the existing independent invocation IDs, owner and
configuration readers, stack-context proof, full native code binding, and
`RunReloadDeltaOverride` original-once/restore transaction. No service lock spans
OriginalUpdate. Critical lock failure cancels further overrides and leaves fire
blocked; a presentation read that encounters a busy lock returns unavailable.

`Bc2NativeCycleRuntime.h` exposes explicit pre-Start enable, original-timestamp
control publication, view, exact ready acknowledgement, and cancellation.
`EnableNativePumpCandidate` returns false without the private compile define
`FVR_BC2_MANUAL_CYCLE_CANDIDATE`. The initial native trial requires SPAS request
mode and excludes diagnostics, combined reload families and the resource-move
build. It rejects shell/magazine Start while the private pump service is enabled.
This is an intentional first-trial operation boundary, not support for pumping
and loading together. No CMake option or ordinary runtime caller enables it.

Cancel/focus/equip/owner/expiry failure does not claim successful manual cycling
or discard an ambiguous operation. The current candidate remains cancelled
until the trial ends; automatic recovery/retirement is a remaining integration
task. Successful cycles can repeat after exact ready acknowledgement.

## Checks and remaining evidence

`Build-Focused.ps1 -Architecture x86` and `x64` compile all new service/helper/test
translation units with `/W4 /WX`. Ten groups pass, including real shared
HandInteraction and PhysicalWeaponCycle short/full strokes, exact branch
cohorts, forged release, restore/state failure, missing tail, original clock
bounds, and retained completion after controller timeout. Native callback
receipts in these tests are synthetic. Normal and private-macro runtime TUs
compile on both architectures; the existing runtime has its existing shadow
warning. These are isolated candidate checks, not a full repository build.

Integration requires `Bc2NativeCycleService.cpp` in BC2Camera and
`Bc2NativeCycleService` in the tests list. The separate bolt-agent
`Bc2NativeCycleEvidence.cpp/.h` payload is a dependency; it is not duplicated here.
Candidate portable headers must precede the ordinary include path for isolated
tests. Exact original/new hashes and the reviewable diff accompany this payload.

Still missing: actual successful-shot/all-three callback cohorts in the live
game; the state-labelled native closed fore-end/contact and signed rear axis
from the passive SPAS capture; gameplay consumer and fire-mask wiring; current
rig/mesh-bound part and hand publication; an actual headset grasp/pump test;
and combined reload/resource/cycle arbitration plus safe native retirement.
The active portable hand qualification remains right gun / left mechanism.
No emulation, elapsed 350 ms diagnostic, or synthetic controller path alone
establishes those missing native/render/controller facts.
