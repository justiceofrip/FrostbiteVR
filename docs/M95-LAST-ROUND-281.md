# Candidate281: observe the native final-round boundary

The ordinary M95 cycles on280 are accepted only while a round remains. Empty
behavior is still unverified and must not be inferred from the loaded path.
This private diagnostic emits at most five100ms native trigger pulses from an
exact full five-round M95. Each subsequent pulse requires the same native owner,
unchanged reserve, precisely one consumed round, fresh controller input and
independent native-idle evidence. The run has a24second action deadline and a
25second trace. No manual bolt/reload feature is enabled in this diagnostic.

The driver stops permanently after observing the final consumed round; any
subsequent native reload is stock behavior. It never sends a reload action or
mutates ammunition, timers or native states. The receiver supplies the existing
30second neutral controller stream; it does not perform a physical bolt test.
Seventeen driver/configuration groups pass through the real ControllerActions
mapping, including the original one-shot behavior, bounded five-shot completion,
partial-magazine refusal and owner loss between pulses.

Private build: `bc2vr-recovery/m95-last-round281-private/build-receipt.json`.
Normal280 and headset280 remain the player candidate. The actual observation completed five stock shots. All three native copies used
state6/previous5/next1 with about117ms remaining for the empty shot, then entered
stock magazine reload directly (state11/next12). Ammunition went5/45 to0/45 and
then5/40. No manual hold or cycle guard was applied. The game remained alive,
hooks detached and callbacks drained. The receiver did not finish its longer
capture before cleanup; this is native-state evidence, not a complete rendering
pass. No last-round manual behavior has been enabled.

Evidence: pipeline-runtime-20261005/root-monitor/m95-last-round281-01/
last-round-observation.json. Trace SHA256:
c01bdf5d04ed2d0f05a64dc381180f93b66d1c15f2e5eac5d5fbb862c7b8f8ec.
A proper empty bolt boundary must also hand off to a magazine reload adapter;
merely reusing the loaded-round timing would not provide manual reloading.
