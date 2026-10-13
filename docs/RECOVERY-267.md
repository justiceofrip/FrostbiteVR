# Rifle-first actor replacement recovery267

Source digest: `d18322b81b9b72a8c631f869ffa94b2f890118545802f7f318d00f6f0b24769b`.
Focused native-cycle service tests and all240 suites in both full normal
player builds pass on x86 and x64. Actual-game and headset acceptance of267
remain pending. Binaries, matching build symbols and receipts are frozen in
`bc2vr-recovery/normal267-player-checkpoint`; the source archive is in
`bc2vr-recovery/checkpoint267-local`. Automated input drivers are disabled.

The266 retirement path required selecting a new pump or bolt operating mode.
After a checkpoint replaced the player, drawing a rifle first could instead
reach the request-family switch with an unfinished cycle from the old player.
That blocked switching the reload configuration. The resource reload consumer
also requires that configuration, so this affected the shared magazine path,
not just a legacy input path. An already-selected matching rifle configuration
could bypass the family switch, explaining why the failure was conditional.

The service now provides a family-neutral `RetireForReplacementActor` operation.
Before selecting a reload profile, the runtime checks whether the client actor
really changed. For a replacement it obtains fresh, coherent idle observations
from all three firing copies, verifies the enrolled configuration independently
of the old request family, excludes callbacks, and rechecks the published owner
before committing. Both client and server actors must have changed, actor
generation must increase, and the drain must name the exact old cycle and shot.
This operation performs no native ammunition write.

Retirement archives the old outcome, release/Ready evidence and unresolved
cycle recovery evidence. It clears only the old lifetime's live control state
and leaves the service unarmed. Sequence high-water marks survive. Pump and
bolt selection use the same primitive but separately validate their operating
configuration; a rifle can yield to its resource reload path without selecting
a pump mode. Ordinary same-actor swaps or tracking interruptions still cannot
discard an owed mechanism action.

The focused regression failed against266 and passes with267. Coverage includes
rifle-first retirement and resource yield, 27 invalid reserve/drain proofs,
monotonic sequence/time/revision, old callback and Ready rejection, and retained
pending, completed and uncertain outcomes. Tests use synthetic native evidence;
they do not establish successful in-game checkpoint recovery.

Private test receipts are in `bc2vr-recovery/actor-retirement267-tests`. Frozen264
and266 remain preserved. The qualified scene265 restart used diagnostic264 and
returned to the boat; it did not exercise267 or on-foot rifle reacquisition.
The next distinct native check must cross a real actor replacement and then
draw the rifle before returning to the SPAS. The current emulated interaction
driver cancels on actor replacement, so replaying its existing sequence alone
cannot establish that result.

Separate open issues include the graphics-worker crash, preservation of magazine
custody after hand-claim expiry during a stall, and retirement of an uncertain
ammunition-service operation. This change does not claim to fix those issues.
Broader weapon, vehicle and mission coverage and sustained headset acceptance
remain release gates. Current maturity is pre-alpha; an experimental
single-player alpha playtest is a proposed next milestone, not an accepted build.
