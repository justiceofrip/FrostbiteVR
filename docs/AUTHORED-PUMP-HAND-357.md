# Authored mechanism hands and the SPAS pump

The full-view emulated pump check exposed a presentation mismatch: the pump
used the generic compact pinch intended for small mechanisms, including the
launcher sight. The physical fore-end stroke could complete while the hand
looked only partly closed around it.

`AuthoredHandPose.h` adds a shared way to place a supplied hand shape at the
resolved wrist. It uses the existing verified finger-chain binding, preserves
unbound descendants and unrelated bones, supports the adapter's world scale,
and does not modify source animation. The shared code contains no BC2 weapon
names, native addresses, ammunition logic or input ownership rules.

The BC2 pump calibration supplies its authored fore-end shape. This changes the
fingers during the existing pump claim; it does not change the contact point,
rail, fire gating, shell transaction, holsters or launcher-sight hand pose.
The renderer reports authored versus generic pump-hand poses separately.

## Data provenance

`profiles/pump357/spas-hand.json` records the installed SPAS `HandsIkPose`, its
clip and skeleton hashes, and all 15 wrist-relative finger transforms. The
measured pump wrist and the authored wrist agree within 0.03 mm. Thirteen finger
joints have constant source chains. The two index-finger joints use the authored
starting controls; every required curve is at its first endpoint, so no claim
about native spline interpolation is needed. This is a fixed presentation shape,
not native action authority or optical finger tracking.

`tools/bc2_authored_hand_shape.py` derives the same data from an explicit clip,
skeleton and hand. Its output reproduces all 15 selected transforms exactly.
Other mechanism adapters can supply their own measured shape to the shared
renderer without adding a weapon-name branch to the interaction logic.

## Validation

The shared regression checks that a changed native animation cannot replace the
authored grasp, while wrist placement, finger shape, unrelated branches and
unbound descendants remain correct at two world scales. It rejects wrong joint
counts, malformed transforms, duplicate joints and invalid chains.

Both full normal builds pass all 245 suites. The isolated 358 game check uses
the same runtime plus a deterministic controller pose that keeps the entire
shotgun visible. It starts with support held before firing, transfers that
claim into the pump without a release, then completes the manual stroke,
shell insertion, shoulder swaps and final rifle shot. Three magazine cycles
and six native magazine commands agree across the server and both clients;
the rifle finishes at 29 loaded / 180 reserve.

All 364 recorded moving pump poses use the authored hand shape, with zero
generic pinch poses, reach/pose/source rejection or presentation fallback.
The 1,135 resting and 364 moving poses reach both eye-copy paths. Support
returns 105.8424 ms after shell completion. The capture has 240 color pairs,
239 paired depth copies and 177 prop compositions, with no composition,
geometry or rendering failure. Props are outside this sideways inspection
view; the separate 353 arm/chest occlusion evidence remains the visible
body-prop check. Twenty-seven record-lock drops exclude full callback coverage.

The sampled frames show the authored hand around the moving fore-end, with
the receiver, folded stock and ring remaining attached. These are sampled
images, not a claim about every animation frame. Private evidence is at
`bc2vr-recovery/inspection358-native01`; its exact diagnostic source is saved
separately. The three diagnostic source edits were restored byte-for-byte.
Normal 357 contains no scripted player input; 354 remains preserved.

Earlier private checks 355 and 356 passed the two-handed-before-fire case:
support transfers to the pump without releasing; the manual stroke, shell
insertion, support return, shoulder swaps and final rifle shot complete.
Check 356 shows the entire gun in frame. Its sampled fore-end follows the
manual stroke while the receiver, stock and locking ring stay attached.
It captured 240 stereo pairs, 239 depth pairs and 177 prop compositions with
no composition or rendering failure. Support returned 105.8563 ms after shell
completion. Twenty-six record-lock drops mean this is not full callback coverage.

These checks are not headset acceptance of controller alignment or feel, nor
proof that every animation frame and campaign transition is correct.

## Shared material pipeline progress

The new `bc2_vertex_channels.py` reads channels from explicit shader declarations
and validates them against the independently decoded mesh position/skin layout.
It preserves vertex order, all components and repeat coordinates. Instance data
is not misread from a mesh buffer. Six deterministic checks pass.

`bc2_shader_coordinates.py` traces straight-line coordinate arithmetic through
the shader interfaces. Runtime constants, nonlinear dependencies and unknown
values remain unknown; control flow rejects. Arrays and cube maps are not
treated as ordinary 2D textures. Eight deterministic checks pass.

Using the installed MP001 database, 75 of 77 catalog material/section pairs
resolve. Across their 310 textured passes, 1,498 material texture samples have
traceable coordinates. Those expressions evaluate against 4,179,612 vertex
samples without invalid coordinates. The two unresolved M16 kit variants remain
explicit. These are material and vertex counts, not playable-weapon counts.

The tools neither execute original shader bytecode nor export game assets into
source. Runtime material conversion is still unfinished: body props retain
plain colors. Active shader-pass selection, complete material shading and human
visual acceptance remain separate from this offline coordinate evidence.
