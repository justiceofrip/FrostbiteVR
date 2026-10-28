# Local299: faster builds with the same provenance checks

This checkpoint changes build wiring only. A stable generated header replaces
changing global compiler definitions, so a reviewed operation-profile update
rebuilds only actual consumers. The current dependency records contain116/426
consumers on x64 and123/425 on x86;310 and302 other objects avoid that rebuild.
An isolated compiled fixture proved the unrelated object remains byte-identical
and is not rebuilt. It also rejected stale source, altered generated headers,
partial configuration and mismatched hashes; disabled configuration stayed off.

All243 normal suites pass on both architectures (x64 65.11s, x86 136.23s).
Four focused suites per architecture,10 provenance tests and34 source-package
tests also pass. Configure-time and per-build source verification remain active.
The new build-provenance reader supports both historical and generated wiring.

Gameplay behavior is unchanged from298. The verified native pistol/body-cache
run and ready headset package remain298; no redundant native reload test was run
for this build-only change. There is no new headset or full-campaign acceptance.
Campaign death recovery, failed-connection cleanup and remaining weapon-family
mechanics are still open. Nothing was published.

