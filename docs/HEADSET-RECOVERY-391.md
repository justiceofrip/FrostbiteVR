# Checkpoint 391: fixes from the failed October 13 headset run

The human checkpoint390 test failed. Reported symptoms include unreliable left
shoulder contact, interruption during pumping leaving equipment unusable,
unwanted stock pump animation/hand release, automatic shell filling after one or
two insertions, and a painful black/white mismatch between eyes on death.
Earlier passing emulated runs do not supersede that result.

## Evidence and corrections

The first recorded pump failure occurs with both hands tracked and XR focused.
The same controller packet reaches native admission 1.2 microseconds after its
original deadline. Admission previously treated that as a mapping failure and
permanently cancelled an active mechanism before the existing suspension path
could recover. Expired packets now lose action authority without cancelling the
owed pump or bolt. They cannot move a part, release it, or renew their deadline.
Fresh native updates and fresh neutral input still govern bounded recovery.
Regression tests fail on390 and pass on391 for pump and bolt.

The second submitted shell loses its published native ownership snapshot after
85 ms: soldier flags change from11 to19. Independent executable disassembly and
older native traces establish that bits0x08 and0x10 are mutable native state;
only bit0x01 chooses the inventory layout. Publication comparisons now tolerate
only0x18, while every structural link and both reads within the current callback
remain exact. The flag's precise gameplay name is not assumed. This corrects a
specific ownership rejection; it does not make missing observations successful.
Additional failure diagnostics separate publication differences from races
inside the current read. The video shows native filling to8/19 while a manual
shell remains held; no new headset acceptance is claimed.

The holster trace records a successful left draw at inputs11844/11845, followed
by unintended stow at11856 as the drawing squeeze is released. A completed draw
now inhibits release-to-stow at that same contact until a fresh tracked pose
leaves. Missing tracking/pose is not an exit. Supported carry can observe an exit
without authorizing a stow; a later deliberate return still works. Both shoulder
regressions cover that sequence. The contact volume also extends another4 cm
forward and4 cm upward to include measured misses, without moving displayed
weapons or extending down toward chest ammunition and the pistol holster.

A GPU-only presentation safeguard samples both native eyes before body props.
Only an extreme near-uniform black/white pair causes both eyes to be presented
opaque black. Normal pixels remain unchanged, and the next valid pair restores
normal presentation immediately. This is symptom protection, not a demonstrated
death-path root-cause fix. The submitted recording contains only one eye, and
the trace does not identify a verified death transition.

## Validation and remaining work

Targeted native-cycle, recovery, reload-server, request-cycle, abort, family
handoff, holster and equipment regressions pass. The new flag tests cover32
publication cases and six within-read rejection cases. Graphics tests cover
linear/sRGB RGBA/BGRA, stale/invalid input, state restoration, both asymmetry
orders and normal recovery. Fake-XR presentation and menu transitions pass.
On the same RX9070XT used by the failed session, a bounded1920x1080-per-eye GPU
benchmark measured an added0.060 ms median and0.064 ms p95. This is not a claim
about whole-game performance or Steam Link transport stability.

Source closure:
`721700bf93f801f898ff87fa41a64c771e852a916b770e48166a0b37aae5b671`.
Both full builds pass246/246 suites (x86 137.06 s, x64 61.33 s). The single
actual-game empty-tube check passes: eight shots/pumps with support returned,
one manual shell0/24 ->1/23, rifle return, magazine handling and subsequent
rifle fire. After detach,31 read-only samples over3 seconds retain1/23 in both
inactive shotgun client copies. All240 stereo pairs arrive, with no discard,
camera restoration failure or GPU error; BC2 remains alive. Independent raw
callback review passes87 direct checks: native shot/idle evidence for all eight
cycles, one conserved shell transfer on each of three native branches, and exact
restoration of all retained temporary holds. Fourteen callback begin-lock drops
leave two cycle/branch commit tails unretained; full callback coverage is not
claimed. Six empty-control rejections remain aggregate-only.
This run did not force the expired-packet race or
prove a specific successful0x08 publication allowance, and requested one shell,
not two. Those limitations are retained beside the passing integration result.
No new weapons, native hooks or write targets are admitted. Full-campaign
playability, death recovery, sustained streaming and pump visual acceptance
remain unresolved. Private evidence is in `headset391-investigation` and
`overnight391` under the local recovery directory; game assets remain private.
