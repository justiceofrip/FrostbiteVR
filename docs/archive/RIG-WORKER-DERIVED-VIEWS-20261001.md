# Worker batch child-view correction

Trace `native-trace-20261001-193317-512` completed 240 stereo pairs with no
timeouts, no worker admission, and 1,385 wrong-type results. All four observed
renderer/primary-view/request/world tables matched. The rejection occurred in
the additional batch view list before actor selection.

## Proven layout and cause

The complete character-prepare function is already fingerprinted. Its loop at
0x005f28bf reads the byte count at batch+0x24, starts at batch+0x28, and advances
four bytes at 0x005f2901. The embedded list is an array of view pointers, not a
larger structure. It invokes each view's virtual method at +0x9c.

The operator's bounded read-only follow-up found six currently active child
views with exact table 0x01477588. All six have the same request at +0x70 and
parent at +0x1670 equal to the actual primary view 1204586128. Repeated object
headers and parent words matched. The old temporary stereo views now carry the
abstract/destructor table 0x01476be0; their historical pointer values do not
prove current ownership and that table remains rejected.

The auxiliary constructor 0x00bab960 calls common constructor 0x00bab650, stores
its parent at +0x1670, installs table 0x01477588, and registers with the parent's
child collection. Main constructor 0x00bab9f0 calls the same common constructor
and installs the existing verified main table 0x014776f8. Common construction
stores request at +0x70. These complete functions, their call relationships,
all three 240-byte tables, and the common request/camera/kind getters are checked
before hook installation. This is a specific class relationship, not permission
to accept arbitrary vtables or stale objects.

## Narrow behavior change

The actual primary view still requires the exact original main table. An
enumerated entry may use the original main table or the verified child table.
The child additionally requires its parent to equal this worker's actual primary
view; every entry still requires the same exact request. The entire worker read
is repeated, including all new table/request/parent identities. Common-base,
abstract, unrelated derived and wrong-parent/request objects are rejected.

Per-entry diagnostic fields and the rejected index now explain future failures.
They do not grant ownership or cause native calls/writes. Startup, original frame
deadlines, actor checks, source/packed-byte proof and GPU draw matching are unchanged.

## Shared palette boundary

Worker calls equal world calls (2,919) in this trace, while draw/prepare calls are
3,159, or world calls plus the 240 extra stereo draws. The first saved worker
primary is the temporary right view; later primaries are the main view. This
suggests native palette work is shared across draws. The correction only proves
the actual worker origin; it does not synthesize right-eye ownership or authorize
using a left palette by time proximity. Sharing must join the exact request/frame,
actor and submitted packed palette to each actual GPU draw.

## Validation and integration

22 focused cases cover the existing worker/lifecycle/lease rules plus valid
derived children, foreign parent/request, unsupported classes and changes during
repeated reads. Offline discovery also rejects mutations in all nine inspected
functions and all three view tables. Native behavior of this correction requires
the operator's next bounded capture.

Only four existing files change: Bc2RigWorkerBinding.h/.cpp,
Bc2RigWorkerRuntime.cpp and Bc2RigWorkerBindingTests.cpp. No NativeProbe wiring,
CMake, lifecycle API, production settings or game state change is needed.
