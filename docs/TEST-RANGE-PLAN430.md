# Offline weapon test-range plan — October 20, 2026

The new [catalog planner](../tools/bc2_test_range_catalog.py) identifies exact
configuration candidates for an eventual test range. It does not create a
playable range, spawn pickups, change mounts, or enable weapon capabilities.

The bounded installed-data run reused the saved campaign and multiplayer-common
typed reports. It read 15 archive indexes, with all 424 configuration-origin
entries matching their recorded index hashes and active resource entries. It
retains 288 distinct resource/GUID/content identities, 102 native names, 19 native
classes and 11 serialized dispatch groups. These are overlapping configurations
and metadata groups, not 288 distinct guns or accepted VR weapons. Three
unresolved campaign reference rows remain explicit.

| Selected map | Mounted definition candidates | Ambiguous overlay candidates | Uncovered supplied definitions |
| --- | ---: | ---: | ---: |
| SP_01 | 13 | 125 | 150 |
| SP_02 | 96 | 32 | 160 |
| MP_001 | 195 | 81 | 12 |

Each row partitions the same 288 supplied identities. A candidate has matching
typed origin evidence in the map's literal manifest targets and no conflicting
indexed resource owner. Ambiguity retains unknown overlays and deletion entries;
the tool does not guess precedence. Map-specific package overlays lack supplied
typed reports; MP_001's base map archive also lacks one. Every map therefore
reports `all_definitions_cataloged: false`. Index matches do not revalidate
payload contents, dependency closure, player availability or live equipment.

The private result is `reports/test-range-catalog-20261020.json`, 576,119 bytes,
SHA-256 `11cd8563d2b4dbf1ebbb600afc2a38135632f4de098c7dfdcbbdab007d89632e`.
The existing `/reports/` ignore rule keeps exact game metadata out of source.
The report records hashes and locations for both saved reports, installed
`package.mft` and `bundleManifest`, plus observed archive-index hashes. Source
asset bytes were neither extracted nor included here.

The CLI accepts repeated `--report` and `--map` arguments, plus exact `--asset`,
`--native-class`, `--directory` and dispatch `--family` filters. Native class
labels remain unchanged; they are not reliable physical weapon categories.
It requires explicit game/manifest paths and a new `.json` output outside the
game installation. Inputs are bounded to 8 MiB each/32 MiB combined, output to
8 MiB, and archive-index reads to 128. A repeated invocation against the saved
output was rejected before inspection, with its hash unchanged.

The immediate implementation candidate is a private selector for already
available, individually reviewed native definitions, using the existing
provisioning harness. Retained actual-game evidence verifies a scoped native
assignment and subsequent client/server convergence. The adapter replaces an
item in one of the current two inventory slots; it does not produce a world
pickup or an additional carried slot. Exact selector and native-family bindings
still need current validation. Historical variant numbers are not stable API
identifiers. No player-facing launcher or isolated alternate-profile range root
has been tested.

World racks and ground pickups require a separate spawn/exchange adapter. The
[BC2 ground-pickup capability remains disabled](archive/GROUND-PICKUP-20261003.md);
portable policy tests do not establish native spawning, target ownership,
ammunition accounting or outgoing drops. Broad common-bundle mounting is also
not an accepted shortcut: both attempted SP_03 additions crashed while loading,
including the base-common-only attempt, as recorded in the
[loading audit](archive/FORK-LOADING-AUDIT-340.md). The catalog's needed-origin lists are
research leads, not safe mount instructions.

Fourteen focused planner tests pass, covering exact variants, stale indexes,
ambiguous overlays, missing references, literal mount interpretation, logical
manifest root markers, selection, and overwrite rejection. The completed run
used process-local G: scratch and disabled bytecode writes. It made no game
writes or process calls, launched no session, and ran no native build.
