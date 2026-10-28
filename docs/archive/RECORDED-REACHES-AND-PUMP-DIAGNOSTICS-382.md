# Recorded reaches and repeated pump diagnostics — 382

The latest accepted evidence remains the failed October 12 headset test of 375.
This checkpoint follows the 381 handoff correction. It is not a playable-campaign
or whole-roster acceptance claim.

## Shoulder acquisition

Shoulder contact now uses a capsule extending 4 cm forward from the existing
17 cm sphere. The rendered weapon anchor and its orientation stay unchanged.
Both ordinary selected-weapon switching and empty-hand holster interaction use
the same shared contact function. Hip acquisition and chest ammunition retain
their existing geometry.

The regression uses the fourteen recorded right-controller presses near the
left shoulder, then mirrors them to exercise the right shoulder. The original
geometry accepts five; the new contact accepts thirteen. The remaining point,
over 10 cm outside the old sphere, is still rejected. Normal forward positions,
the head center, chest, hip and opposite shoulder remain rejected. Rearward
contact and the rendered placement are preserved. This checks geometry, not a
human comfort verdict or every possible holster state.

## Repeated use in the controller driver

The empty-tube diagnostic can now start with the support hand gripping the gun,
fire and pump eight times with the existing overshoot/off-axis motion, load one
shell, return through shoulder/rifle switching, manually reload the rifle and
fire it. The change affects the explicitly enabled diagnostic driver only.
Normal player input is unchanged.

The persistent-consumer fixture completes both the prior eight-cycle route and
this held-support variant. It verifies eight native submissions/acknowledgements,
eight support returns, one shell transfer, conserved ammunition and subsequent
rifle use. The native service in that fixture is mocked. A successful fixture
cannot establish that the real engine's third-cycle failure is fixed.

## Exact ownership-rejection evidence

The server-boundary diagnostic now describes changed fields instead of reporting
only PublishedLinks or ChangedLinks. It retains the total count and the first
four named differences, including array indices and expected/observed values.
Existing phase-flag handling and all ownership decisions are unchanged. The
native control-rejection report serializes those details after callbacks drain.

Tests exercise flags, global capacity, effect identity, both inventory arrays,
changes between bracketed reads, bounded reporting and clearing old diagnostics
on a fresh valid read. These observations grant no native access or recovery.

## Remaining blockers

The recorded native cancellation, its resulting equipment lockup and residual
SPAS mesh distortion are still unresolved. Diagnostic counters showing private
palette writes do not prove the stock ring or other geometry looked correct.
No new headset test should be offered solely because these checks compile/pass.

Private work and actual-game results are recorded in
`bc2vr-recovery/overnight382`. Build and runtime results are added after completion.
