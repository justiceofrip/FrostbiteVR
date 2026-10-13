# Local303: retire the exact union of held and queued eye children

302 built successfully and passed243 suites on each architecture (x64 34.77s,
x86 127.30s). Its first native run did not reach checkpoint replacement: a
published menu click left the restart dropdown closed. During subsequent
shutdown, the native graph had20 registered views, not14. The private eye had
six current children at references2 and six older children at references1,
with12 pending queue entries and three exclusive current child holders.
The guard correctly refused the previously supported single-generation shape.

The verified native destructor releases both the current child holders and
every pending queue entry.303 therefore checks their exact union. A current
queued child must have two references; a queued child no longer in the current
holders must have one. Every queue entry must be a unique, correctly typed,
registered child of the same eye and request. Shared holders, unexplained
references, foreign parents, duplicate entries and missing ownership remain
rejected. Only the native root destructor releases these references.

The live path preserves the exact current original-view order and request
reference baseline. The retired-scene path still requires a verified new
world, two stable graph captures, quiescent callbacks, final ownership checks,
and detached native cache retirement before releasing the old request owner.

The menu fixture now verifies whether the dropdown opened. It makes at most
one additional dropdown click after two screenshots prove the ordinary pause
menu remains visible and the previous press was released. It never repeats an
ambiguous checkpoint confirmation. Native deadlines and click limits remain.

Evidence retained: scene302-beforefix02/retired-owned-queue-read.json,
scene302-native01/paused-extra-views-read.json, and the301 native destructor
proofs. The scene302-start04 process exited during campaign startup before
the VR probe attached; no native302 scene run was launched from that process.

Focused graph/progress suites pass on both architectures (22 graph test
groups). Full builds and native303 scene acceptance are pending.301 remains
the isolated ready headset package. No headset, death recovery, full campaign,
broader weapon, or new-generation native acceptance is claimed here yet.
