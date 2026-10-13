# Animation wrapper recovery and mechanism pipeline377

The offline animation reader incorrectly treated a nonzero word at resource
offset4 as unsupported flags. Inspection of the installed game's loader proves
it is a record count. Correct parsing recovers10 unique authored clips across
TOZ, NeoStead, Remington870 and MP412. Existing shared tools now process their
motion and hand data; no native weapon operation is enabled by this change.
Normal player375 remains the frozen headset candidate.

## Layout evidence

The inspected executable SHA256 is
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
No process was opened or native function called for this work.

- The registration at `0x137df50` associates `GrannyAnimation` with TypeInfo
  `0x1c0b984`. The dispatch branch at `0xf4744a` selects the animation loader.
- `0xf472b0` calls `0xf47170` before `0xf46c20` reads the string table/core.
- `0xf47170` reads the record count, then an identifier and pair count per
  record, followed by two4-byte scalar reads per pair. Its in-memory record
  stride is20bytes; the serialized record length depends on the pair count.
- The stream constructor at `0x50b1d0` supplies vtable `0x1412a28`.
  Slots `0x1c`, `0x3c` and `0x40` resolve to `0x50b410`, `0x50b390` and
  `0x50b210`: pair scalar,32-bit count/id and16-bit string length reads.

The serialized layout is version, record count, repeated identifier/pair count/
pairs, then the existing string table and Granny core. The parser retains finite
IEEE754 interpretations of the pairs and opaque identifiers. Their gameplay
meaning is unresolved. Negative values and values above1 are present, so these
must not be treated as clip seconds or clamp-normalized events. They are not
chamber, ammunition, sound or visibility authority.

The reader bounds record count, total pair count and every read, rejects
nonfinite values and retains all original core validation. Model wrappers are
unchanged. `Clip.summary()` now preserves the records and explicitly reports
their semantics unverified.

## Corpus and shared-pipeline results

The installed-data audit checked39 exact weapon archives,440 resources and425
unique animation contents. All415 previously accepted contents retain identical
string tables, core bytes, sections, relocations and root references. The10 newly
accepted contents fully decode all72 named tracks. No selected container remains
rejected. This corpus covers the bound weapon archives, not every installed
animation in the entire game.

| Asset family | Newly readable clips |
| --- | --- |
| TOZ / T194 variants | Reload-in, reload, reload-out, bolt-action |
| NeoStead | Reload-in and reload |
| Remington870 | Reload-in, reload and reload-out |
| MP412 | Reload |

The existing mechanism pipeline then processed these four models as18 exact
configuration records in four distinct content jobs, with zero decoding errors
or missing references. It did not invent a detachable magazine for the tube-fed
shotguns or revolver: no detachable-magazine profile passed those criteria.
The MP412 has a sampled hinge/contact candidate, not a working manual cylinder.

The full-curve rail tool from376 additionally measures three TOZ moving pieces.
Its assembled preview identifies `jntWpn_5` in the fore-end region, with travel
from−4.816 to+90.115mm relative to the authored reference. This differs from the
870/NeoStead fore-end name `jntWpn_4`. The two smaller TOZ translating parts are
retained separately. These measurements do not establish the native closed stop.

The existing same-frame wrist/finger extractor finds seven sampled contact
windows each for the870 and NeoStead bolt-action clips. The selected windows have
less than0.9mm and1.6mm relative wrist translation respectively. TOZ produces no
window under the same criteria; it remains unresolved rather than inheriting a
different gun's hand pose. These30Hz contact checks are candidates, distinct from
the full-curve travel bounds and from native/headset acceptance.

SPAS's exact reference bundles contain neither the selected absolute bolt-action
clip nor a reload clip. Their indexes are saved separately. The existing native
SPAS calibration and357 hand shape remain unchanged; further authored dependency
resolution must follow its tree instead of guessing from bundle co-occurrence.

## Validation and handoff

All214 relevant Python tests pass, including five new wrapper/metadata cases.
Both full normal builds pass246 suites (x8622.49s, x6413.47s). The native375
source hash and frozen player files remain unchanged. No game or VR session was
launched for this offline checkpoint.

Private evidence folders beside the repository:

- `granny-wrapper377`: exact native layout receipt, disassembly and SPAS index.
- `granny-corpus377`: all source joins and old/new parser comparison.
- `recovered-mechanisms377`: four-model,18-configuration pipeline outputs.
- `mechanism-rails377-verified`: rail measurements, TOZ geometry previews and
  sampled hand-contact results, including the rejected TOZ contact.

Source checkpoint377 includes the tools and validation recipes. Game resources
and geometry previews remain private. Native boundary verification and selected
configuration enrollment are still required before adding another player pump,
revolver, shell-loading or chamber operation.
