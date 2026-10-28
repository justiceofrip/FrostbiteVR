# Bounded shell and reticle draw observation — October 1, 2026

`Bc2ReloadDrawCapture` is a diagnostic reader, not a visibility override or a
manual-reload feature. It observes the existing `DrawIndexed` hook and copies
bound resources into private staging buffers; it does not change graphics state,
invoke native drawing, flush the context, wait for a GPU query, or edit assets.

The installed SPAS geometry yields separate fingerprints for the 270-index brass
and 90-index plastic sections. Ordered indexed position/skin bytes identify the
section even when a vertex buffer is relocated through an IA offset or base
vertex. Matching only an index count is insufficient. Unknown triangle order,
layout changes, failed reads and truncated evidence remain unverified.

The same helper includes the ACOG's 12-index, stride68 reticle section. This layout
uses float3 position12 plus indices4 and weights4; the SPAS stride48 layout uses
packed position8 plus indices4 and weights4. The hashes are derived privately from
installed assets, with no vertex/index arrays exported into source or reports.

| Geometry section | Draw count / stride | Indexed vertex+skin FNV1a64 | Indexed position FNV1a64 |
| --- | --- | --- | --- |
| 1: SPAS brass | 270 /48 | e05c8e35ec306bd2 | 5fd96edbcded25ba |
| 2: SPAS plastic | 90 /48 | ce6e99d2c3cd267e | a9888be789a5c902 |
| 3: ACOG reticle | 12 /68 | 1b481b02516969b5 | 9ad7a905a35a527d |

SPAS provenance is reports/spas-shell-draw-fingerprints-20261001.json, generated
by tools/derive_spas_draw_fingerprints.py. ACOG resource SHA256 is
801361ce3160ef3392a7bb3317d77ed70b8631fb9abc661b67517314059a36a9; its indexed20-byte
stream SHA256 is930b87a3d77619f5c1f7a0528f466ccaaac0c0358ff9b809d0ca2f2eeffe47f2.
The ACOG evidence agent derived that stream from vertexStart239,
vertexBytes197976, reticle vertexOffset165240 and firstIndex5331.

## Integration and limits

Enable only an explicitly requested diagnostic before hooks start. The window
is15seconds; sample one native frame every75ms, retaining both eyes and repeated
material draws. At most1024records,32candidates per sampled frame and8pending
readbacks are retained. Each candidate can copy at most2MiB of vertex data and
128KiB total VS constant data. The full vertex data is used only to hash the
selected indices; it is discarded after readback and never written to the report.
The report stores constant-buffer hashes and packed-pose match offsets, not raw
asset geometry or full constant buffers.

Bracket each tracked native eye with BeginEye/EndEye, observe indexed draws
before the original call, and Poll on the same immediate-context render thread.
The observer owns no native hook itself. Root owns NativeProbe/CMake integration.
Use a scope guard for EndEye during normal C++ unwinding. The root's hook lifetime
must still retain the module while callbacks might execute.

Admission, callback counters and teardown use a sequentially consistent order.
Wrong threads are rejected before touching context/state. Stop closes admission
and returns false if an admitted callback remains; the caller retains the object
and module and retries later. A successful Stop abandons pending GPU copies
without waiting. Full records may be reported only after successful Stop;
undrained report calls return an explicit incomplete marker and no records.

Every draw retains native frame/request/view, eye, QPC nanoseconds and tick time,
IA buffer identities/ranges, topology, input layout, shaders, target/depth,
VS constant-buffer identities/hashes, and all128 VS SRV binding identities.
SRV contents are not captured, so a skin palette stored in a texture remains
unresolved. Unsupported/oversize buffers, queue overflow and abandoned copies
are counted. A missing matched draw is not evidence that a section was omitted
when any relevant capture is incomplete.

A geometry match proves asset-section identity only. It does not prove which
selected first-person mesh instance or native skin request produced that draw.
The optional producer record carries original ownership/deadline/rig identities
and two exact48-byte native column-packed patterns, one for jntWpn_7 and one for
jntWpn_10. Matching those patterns inside VS constants reports candidate slots
and byte offsets; multiple matches remain ambiguous. Native palette identifier40
or35 is never assumed to be a GPU slot or bone remap.

`exactRequestAssociation` and `selectedMeshIdentityVerified` default false. The
native integration must prove the request/mesh connection before setting them.
A nearby timestamp or matching transform alone is insufficient. Nothing in this
helper authorizes uncollapsing a shell, suppressing a reticle, or acknowledging
an ammunition transaction.

## Validation

Four deterministic cases cover hashes, index/vertex relocation, signed base
vertex handling, data perturbation, index/size overflow and the different optic
layout. The explicit WARP GPU fixture retains10repeated draws across both
layouts, verifies exact constant-pattern offsets, checks unchanged IA/CB state,
exercises queue/window bounds and races wrong-thread admission against Stop.
Both x86 and x64 focused builds pass. Root owns full staged builds and native
capture. No native visibility or headset acceptance is claimed for this reader.

The final helper/reticle changes are staged under the writable recovery project;
<local-workspace> remains at the last approved helper revision until promotion is allowed.
