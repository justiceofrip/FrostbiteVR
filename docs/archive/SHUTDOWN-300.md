# Local300: no-frame cleanup reproduced

Both normal builds pass243 suites (x64 43.37s, x86 134.63s), with two
focused shutdown suites on both architectures. This checkpoint adds persistent,
thread-safe rejection evidence without changing cleanup admission.

The finite no-frame native test reproduced retained hooks with zero controller
input, zero requested/published frames, and no game crash. The exact rejection
was shutdown_graph. Two stable read-only captures found every extra-eye child
at reference count2; every original-view child remained at1. The six extra
references correspond exactly to the extra eye's pending queue at+0x1670.
The verified main destructor releases child holders and then that queue. The
current guard does not account for these owned queue references; correction
will require exact queue/holder ownership and native destructor fingerprints.
No refcounts or game health/ammo were edited.

Evidence: shutdown300-native02/native-trace.json, shutdown-graph-read.json,
shutdown-owned-queues.json and owned-queue-static-proof.json in local recovery.
The first attempt omitted --magazine-reload and rejected configuration before
hooks; it is retained separately as shutdown300-native01. Neither run is a
headset or campaign acceptance result. Ready headset298 remains preserved.

