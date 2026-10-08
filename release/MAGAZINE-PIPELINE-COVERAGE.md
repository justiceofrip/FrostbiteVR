# Measured multi-weapon magazine batch: offline consumer evidence

This isolated batch tests twelve exact descriptor paths: three variants each of
M416, XM8 Compact, MG36 and XM8 LMG. All use the existing registry, measured
magazine geometry selector and Bc2MagazinePhysicalReload consumer. No per-weapon
state machine, native binding or production registration was added.

Each x86/x64 probe checks partial and empty magazine insertion, exactly conserved
ammunition with mocked native transfer receipts, cycle retirement, descriptor
timing-word corruption, changed exact path/capacity denial and missing empty-control
evidence denial. Multiple paths sharing one name must not resolve through name-only
lookup. The independently compiled disabled registry rejects every candidate.

`tools/bc2_magazine_pipeline_coverage.py` consumes the existing descriptor-jobs JSON,
measured AR/SMG and drum profile JSON, and their unchanged private generated headers.
It emits public gap metadata and two PRIVATE probe registries. The fixture registry
models native admission **only in a standalone offline test EXE**; it is not a
review document and must never become a game build input. The production registry
is emitted through the existing exporter with zero reviewed/enabled rows.

Run the tool with `--source`, `--jobs`, repeated `--geometry`/`--header`, an unused
`--private-output` directory and `--report`. Then run `Build-Batch.ps1 -Source <same
source> -Architecture x86` and x64. Run the tool again without `--private-output`,
adding all four `--probe-results` JSONL paths. Reconciliation rejects duplicate,
missing, mixed-mode or unsuccessful rows; a compile alone is never coverage.

The current catalog has 112 AR/SMG/LMG descriptors. Seven names have measured
magazine contacts in these input batches; twelve exact paths across four names
match the existing zero-bolt dispatch. Nine additional measured paths for 9A91,
AKS74u and UZI retain nonzero authored bolt fields and are deferred by the existing
native descriptor exporter. That is a native stock completion/dispatch proof gap,
not a requirement for a physical charging-handle interaction. If verified, stock
bolt completion may remain automatic behind the shared magazine interaction.

Missing geometry labels mean absent from these supplied batches, not missing
project-wide evidence. Accepted scoped-XM8 and AEK baseline geometry, plus the
separate belt-fed authored mechanism work, are outside this measured input set.
The report preserves exact paths and baseline references; unscoped or multiplayer
variants do not inherit a scoped singleplayer baseline's native admission.

No shared consumer defect was found in the twelve covered paths. Missing fixture
empty-control evidence correctly prevented the first empty test; the final fixture
explicitly mocks that capability and separately tests its absence. No production
admission checks were changed to make coverage pass.

These results do not prove current native configuration, all-three firing-branch
receipts, ordinary grip/support targets, active mesh/skin identity, native magazine
render suppression, belt-fed mechanics or headset acceptance. Geometry extraction
and mocked consumer completion are not playable weapon support. Existing frozen
115352 source/binaries, canonical BC2 and BF2142 are unchanged.
