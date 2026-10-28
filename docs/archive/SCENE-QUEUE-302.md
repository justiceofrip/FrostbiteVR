# Local302: queued child ownership across checkpoint replacement

The finite native test `scene302-beforefix02` used the VR menu channel to
restart the checkpoint after creating an extra eye but before requesting any
world frames. It reached a replacement player and world, but the old world
could not retire: recovery rejected1543 times and hooks remained installed.
There was no native exception or direct game-state write.

Two identical read-only snapshots found seven old registered views, world
references1, request references7, root references1, and six child references2.
Each child had exactly one exclusive holder and one entry in the old eye's
pending queue. This is the same native destructor ownership proved in301.
The six additional child references do not add six request references.

The shared ownership predicate and native capture now serve both same-world
shutdown and old-world retirement. Each path rejects duplicate, shared,
foreign or unaccounted references. Scene retirement captures the graph twice,
checks it again before releasing the root, and uses the verified native root
destructor to release its holders and pending queue. The existing detached
cache retirement runs before the final request-owner release. No child
reference counts, native ammunition, gameplay timers or deadlines are edited.

The first diagnostic attempt, `scene302-beforefix01`, never reached restart:
its helper used cached expired menu status and a pointer command was rejected.
The corrected helper waits for valid unexpired status. It may retry only a
pointer move explicitly rejected before publication; clicks are not replayed.
That fixture failure is preserved separately from the native cleanup failure.

Focused graph/progress suites pass on both architectures. Full build and
corrected native scene acceptance are pending. This work does not claim death
recovery, headset image delivery, full campaign completion or new weapon
acceptance. The saved301 headset package remains separate during development.
