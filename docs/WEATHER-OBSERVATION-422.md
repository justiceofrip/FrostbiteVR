# Opening weather observer 422 — diagnostic, not a rendering fix

Retained opening02 pairs216 and239 contain the boat intro. Matching upper-cloud
features shift approximately245–280 pixels horizontally between eyes, with
normalized correlations0.95–0.997. Nearby people shift27–80 pixels and the distant
hill0–8 pixels. This reproduces a cloud alignment problem in native eye images;
it does not identify the shader or establish a correction. Initial forest
camera samples from that run cannot explain a later opening frame.

The installed795745 executable contains an environment renderer at0xbdda10.
Its caller0xbe0680 supplies six stack arguments: drawing-context owner, pass
kind, current primary camera, root primary camera, viewport and weather object.
The function reads the root camera position and builds camera-centered world
matrices. Three sites call0x969ad0, which copies64 bytes into a native context
allocation referenced atcontext+0x18. Existing stereo context correction covers
view/projection/position at+0x1c/+0x20/+0x24. Whether weather world data and the
effective view disagree during the opening remains an observation target.

The new observer is off unless the **game process** inherits
`BC2_WEATHER_OBSERVER=1`. Optional `BC2_WEATHER_OBSERVER_DELAY_MS` accepts1000–90000;
the default is50000 milliseconds after the probe starts. The first admitted
tracked frame after that deadline is selected once. World, request, both root
views and native frame are immutable observation identities; a later scene or
frame cannot replace the selection. A cancelled selected frame is reported as
cancelled rather than silently replaced.

Two hooks install only when opted in. Exact code pins verify the entire native
environment function, world-matrix setter, caller and camera getters against
both the executable file and live bytes before hook installation. Both wrappers
use the existing render-callback counter and hook-disable lifecycle, plus a
dedicated observer admission counter. Shutdown closes observation admission,
disables both hooks and waits up to 200 ms for admitted wrappers to return. Late
entries delegate to retained native trampolines; failed drain or incomplete rows
are explicit and cannot qualify as complete evidence. Each calls the original
once, preserves its native EAX result, and copies observations into owned storage.
No native camera, weather setting, matrix, shader, scheduling or draw count is changed.

At most128 records per eye are retained. Each record is published once with a
release/acquire ready flag. Environment records include all six arguments and
both camera transforms. The three verified matrix-setter sites add the incoming
matrix and the post-call world, view, projection and position bindings. Values
are serialized as uint32 IEEE754 bit patterns, so invalid floats cannot corrupt
the JSON. Overflow, missing attribution and callbacks completing outside the
selected frame remain explicit evidence failures.

Phase 0 infers the eye from the root-camera argument; it does not establish an
enclosing draw-eye boundary. Matrix copies are sequential CPU observations,
not an atomic snapshot or proof of what the GPU consumed.

`weather-observation.json` identifies the selected native frame and whether its
ordinary stereo pair was published. `weather-eye-0.rgba` and
`weather-eye-1.rgba` copy the native color target immediately after each selected
eye draw. The existing camera/context/geometry diagnostics also select that
frame. The image admission checks immutable world/request/root-view/frame
identity before and after the original draw and after scheduling the copy;
changed owners or frames are rejected and reported. Both extra images total 16,588,800 bytes at
1920×1080. The usual per-run storage budget still applies; the caller must reserve
these images in addition to all ordinary captures. Image copies use the existing
nonblocking graphics-thread staging and saved-image retirement paths.

Useful evidence requires state4, a published pair, image mask3, no overflow,
closed admission, disabled observer hooks, a completed callback drain, zero
incomplete rows, identity mismatches and invalid-image bits,
two saved images with the same selected frame, and current valid matrix records
for both eyes. A root-camera eye differing from the enclosing render boundary is
recorded for investigation; it must not be discarded merely because it may be
the mismatch being sought. GPU consumption and the final correction boundary
still require analysis. No headset acceptance, weather fix or release promotion
is claimed by this observer.

Portable regressions were added to the existing ProjectionOverride suite for
disabled behavior, bounded delay, owner changes before/after selection, per-eye
capacity, cancellation, admission closure, callback drain and rejection of late records. Build and actual-game
results are pending the parent audit; no tests were run by the implementation
agent.
