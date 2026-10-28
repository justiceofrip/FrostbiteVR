# Local304: queued generations use the registered-view bound

303 successfully retired the old scene after a VR-pointer checkpoint restart,
then initialized a replacement eye view. Its final shutdown still rejected
the replacement graph. Two identical read-only snapshots proved26 registered
views and18 queued children: the current six children had holder+queue refs2,
and twelve previous-generation children had queue-only refs1. Every private
child reference was accounted for. The snapshot's16-entry queue limit was
incorrectly tied to the current holder-count bound.

304 gives the pending queue the same64-entry bound already enforced on the
native registered-view snapshot. It keeps the exact holder/queue ownership
union introduced in303, without changing native reference counts or calling
child Release directly. The root destructor handles all owned generations.
Duplicate, foreign, shared, missing and unaccounted references still reject.
Tests cover up to54 queued children/62 live registered views, along with
overflow rejection and preserved original-view order/reference baseline.

Evidence: scene303-native01/queued-generations-read.json and its failed
scene-queue-audit.json distinguish successful scene replacement from failed
final shutdown. The earlier301 destructor proofs apply to the same executable.
No engine ammunition, health, gameplay timers, native menu deadlines or
controller admission rules are changed by this work.

Focused progress/graph suites pass on both architectures (23 graph groups).
Both full normal builds pass243 suites: x64 39.43s and x86 127.88s.

The native304 combined audit passed. The VR-pointer menu restarted the
checkpoint; a replacement actor/world loaded; the old seven-view subtree and
its six pending references retired exactly once. A replacement eye initialized.
Final shutdown accounted for18 pending references across three generations,
released the private19-view subtree, restored the current original seven-view
order exactly, and returned request references27->8. Cache refresh completed,
reload flow drained, every hook disabled, and BC2 remained alive with no captured
native exception or camera/GPU restoration error. The old initialization view
order differed from the current survivor order; the current order was preserved.

Evidence: scene304-native01/scene-queue-audit.json. Source SHA-256:
5eeca63b4f04cb6e9a74026c23fa51ace8266e2e2d8b49457464e2d0f5527ec1.
Trace SHA-256:46115499c019fcb933730c0f7f803fa51d6ba70a20e83b62777b025206f4a42e.
The no-frame receiver intentionally requested zero world frames/controller
packets. The general stereo probe therefore exits12 (wrapper2) by design;
the independent audit verifies scene retirement and shutdown, not image
delivery. Prior rendered-frame and reload evidence retains its original
checkpoint identity. The isolated304 headset package requires a fresh game.

Outstanding acceptance includes headset feel/visibility, actual death recovery
and a campaign playthrough. Weapon work still includes belt-fed native reloads,
pistol slide/chamber and hip routing, broader enrollment, and M95 manual
magazine loading (native fallback remains). No game geometry is distributable;
nothing has been published from this work.
