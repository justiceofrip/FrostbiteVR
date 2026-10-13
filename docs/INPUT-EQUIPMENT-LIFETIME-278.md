# Candidate278: separate input pauses from equipment replacement

The274 native failure changed F2000 equipment generation3 to4 after a native
input scheduling gap, despite an unchanged actor, item and tracking space.
The same counter was passed to semantic button rearming and native equipment
ownership. That made a pause look like weapon replacement to reload consumers.

The adapter now maintains independent native equipment and input generations.
The complete resolved owner tuple still advances both on genuine replacement;
an observed pause advances only input generation. All six ControllerActions
routes use input generation, so held trigger/button actions still require
neutral before firing again. Native reload, visibility and equipment bindings
retain the unchanged equipment generation. No old input deadline is extended.

An already committed empty holster may remember this same-owner pause without
inventing a draw. It still requires new native suppression and paired visibility
receipts. Incomplete holstering, changed actor/item/space and explicit cancellation
are not carried through this path. The older ObserveNativeInputGap API remains
for compatibility tests; the current runtime uses ObserveNativeInputPause.

Four focused suites pass, including three new coordinator groups: real
ControllerActions release/neutral/repress across a pause with stable native
equipment, empty-hand rehide with stale receipt rejection, and changed-owner
rejection. The existing native cycle and composed resource tests pass.

A private build adds exactly one180ms Sleep at the local input-update boundary
when the combined diagnostic reaches its final rifle reload. It re-resolves the
entire owner after waiting and records both generations and elapsed time. This
is an injected scheduling interruption, not a fabricated native reload result.
It never writes ammunition or controller input. The option requires the private
ordinary resource driver and is explicitly OFF in Build-Checkpoint.ps1.

The first native run (`resource-pause278-01`) reached the final rifle after
completing the first two magazine transactions, a recovered interrupted SPAS
pump, a shell insert and the return shoulder draw. The injected pause lasted
188 ms. Equipment generation stayed 3 while input generation advanced 3 to 4,
but the final magazine check failed with reason 6: the physical resource ID
changed. The game stayed alive and the bounded diagnostic restored its hooks.
This was a failed combined run, not an accepted partial completion.

The additional reset was in Bc2BodyInventory::Observe: any 150 ms metadata
observation gap called Cancel, destroying all item keys even when the returning
coherent native snapshot identified the same actor, container and weapon data.
A scheduler pause now suspends interactions and retires pending requests. It
does not itself destroy physical resources. The returning read must still pass
the existing native coherence and ownership checks; item identity comparison
includes actor/weak lifetime, player, inventory, switching container and weapon
metadata. Failed reads, explicit cancellation, clock regression and actual
identity changes retain their invalidation behavior. The original binding still
expires after 100 ms; only a new coherent read can issue a new binding.

A regression using the actual inventory reader failed on the old implementation
and now passes. It covers the 180 ms gap, expired old binding, retained resource,
item-data/persistence and actor replacement, explicit cancel, failed read and
pending shoulder command retirement. All five focused suites pass. The updated
native pause run and full normal builds are pending. Frozen277 is retained.
No headset, whole-roster, sustained campaign or release acceptance is claimed.


The second native attempt (`resource-pause278-02`) did not reach the injected
pause. It completed two magazine transactions and the interrupted pump, then
cancelled the submitted shell. The three-copy native request policy required
all previous Update contexts to be younger than 50 ms even when merely allowing
one currently verified branch to advance. The native trace contains a 56 ms
gap, followed by State cancellation and physical KeepAliveRejected. Native
ammunition subsequently reached8/23, but the physical request was never
acknowledged. This is an unresolved operation in the saved failed run.

A measured58ms-gap regression reproduces cancellation. The advancing baseline
path now observes its own current verified Update without requiring older
contexts from the other copies to remain fresh. It performs no zero-delta write.
The existing current-context, input, identity, insertion deadline, transfer and
prediction-restoration checks remain. allThreeHeld still requires recent
contexts and actual holds; a restore cannot invent a new own-Update observation.
The regression checks both the shell policy and its native family coordinator,
including exactly one completion only after all actual transfers are observed.
The earlier normal x64 build passed243 suites before this additional shell fix;
full normal validation must be repeated for the new source digest.


The same advancing-context condition existed in Bc2MagazineReloadCycle. XM8 and
AEK configurations reproduce it in the shared native-policy regression. The
matching fix removes only the dependency on other copies' old contexts while
observing the current branch; all current-source validity checks, observed
transfers, native tail completion and one-shot acknowledgements remain. This
is a common magazine policy change, not additional per-weapon enrollment.
The private03 binary was built but deliberately not run after this audit found
the shared magazine path; private04 incorporates both operation families.


Completed278: both normal architectures pass243 suites. Private pause04 passed
all three magazine cycles and six native magazine commands; shell counts7/24
became8/23. Pump recovery completed exactly once. The172ms native input pause
left equipment generation3 unchanged, advanced input3 to4 and retained rifle
resource generation2 across the real shoulder swap. Independent receipt reviews
record all counts, contexts and original native receipts. There were9 recorder
lock drops, so this is not a claim of complete callback coverage.

Scene278 menu/restart: one new world, balanced3 pointer-down/up pairs, no graphics,
menu rejection or exception. The replacement actor exited the boat and drew the
XM8 via controller input. A single magazine presentation fallback (old/current
target3.2465ms beyond its deadline) caused diagnostic failure11 after native
removal, before reinsertion. This is preserved as a failed full recovery check.
The consumer's first active cancellation followed diagnostic terminal input;
that later cancellation does not prove the consumer originally failed.

Normal278 and its source archive are local checkpoints. No headset stage is
promoted from this partial recovery check. Work continues on separating visual
continuity evidence from mechanical completion in the bounded diagnostic.
