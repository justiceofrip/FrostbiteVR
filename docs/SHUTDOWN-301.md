# Local301: no-frame startup now cleans up

A finite native emulation reproduced the300 failure: a receiver connected but
requested no frames or controller input. Every extra-eye child retained one
reference in its ordinary holder and one in that eye's pending queue. Cleanup
previously rejected the second reference indefinitely.

The guard now admits those additional references only when the exact queue,
exclusive child holders, parent/request relationships and reference totals all
agree twice at the quiescent native boundary. Both destructor loops are derived
from the verified native main destructor and fingerprinted before hooks. The
original destructor releases its own holders/queue; the mod does not decrement
child counts or call child Release individually. Foreign/shared/duplicate or
unaccounted references remain rejected.

The repeated native test passed its independent audit: six pending references
were accounted for, the original seven-view order returned exactly, request
references returned15->8, native cache refresh completed, and every hook drained.
BC2 stayed alive on the same player with XM8_sp_s22/191 unchanged. No frame or
input was requested. The general stereo trace intentionally exits12 because its
ordinary success criterion requires delivered frames; that expected diagnostic
exit is not treated as rendered-frame success.

Both normal builds pass243 suites (x64 45.78s; x86140.40s). Two focused suites
pass on both architectures. Prior native evidence for SPAS pumping, rifle/pistol
reloads, M95 bolts and VR-pointer checkpoint recovery remains attached with its
original checkpoint identity. This cleanup run does not revalidate those paths,
headset feel, death recovery or a full campaign playthrough.

Evidence: shutdown301-native01/shutdown-audit.json in local recovery.
Source:7443b43062cbe1bed70d2511d84ef2b7196bcb44d08b8b44435c0c5bed42ee37.
Trace:69ff21e6bb5fe836eeeac2d4771f21ad49e32948b616f71753bc382dcd3f95f9.

Still open: death/scene recovery with a never-rendered extra eye, sustained
campaign/headset acceptance, pistol slide/chamber and hip routing, belt-fed
native loading, broader enrollment, and M95 manual magazine loading (native
fallback remains). No game geometry is distributable and nothing was published.

