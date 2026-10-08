# Bounded body holster diagnostic

This candidate exercises the connected production body consumer. It does not
enable production holstering or grant acceptance from a test flag. The separate
coordinator admission is SPAS only and expires after an original fixed15s QPC
deadline. Parent integration of `body-holster-diagnostic-state-candidate` is a
prerequisite; normal launches retain false acceptance capabilities.

`Test-NativeStream.ps1 -BodyHolsterProbe -Seconds 15 -Pairs 240 -StaticPose -Async`
uses bit `0x80000000u`. It implies ordinary BodyInventory/PhysicalReload plus the
existing tracked hands, fire and support prerequisites. Physical reload remains
inert with neutral left grip. Other action/reload/visibility/optic diagnostics,
host lifetime and nonstatic input are rejected before startup.

The receiver sends neutral original input. Gameplay modifies only the fixture's
private right-controller pose/squeeze copy before `ObserveHandOwnership`. The
driver reads the actual selected item's assigned shoulder, walks to that anchor,
and issues a fresh grip edge. `Bc2BodyInventory` discovers contact and requests
holstering; the fixture never creates a contact proof, claim or acknowledgement.
It waits for the real shared GunHold release, paired hidden receipt, committed
native suppression and actual private free-right Pack copies before moving the
right hand between two separated positions. Drawing uses a second neutral/press
edge at the same actual assigned shoulder. A distinct shared GunHold must return
only after a genuine show receipt. No native weapon switch is forced.

During the second confirmed free-hand interval, one synchronous cache challenge
sets only the already verified weapon actions: Fire8/cycle7 float1, AltFire12,
ADS14, Reload29, launcher33, gadget36, melee37 and grenade38. This happens inside
the existing owned Gather callback, after current ordinary input staging. The
existing `HolsterInputOverride` immediately suppresses and reads these fields
back before the callback can return. RAII removes fabricated values on failure;
unrelated fields retain native ownership. No original engine call/replay occurs
between staging and retirement. The actual before/challenged/written/readback
words, source input, native tick, request and cache identity are recorded.

Read-only process/path-guarded preflight requires a stable idle SPAS before any
receiver or injection. Native loaded/reserve counts are observed again after the
run. Unchanged counts complement the cache result; they do not prove every
possible firing route. The audit never changes `production_input_accepted`.

Native Draw samples bounded phase/claim/receipt data for both real eye frames.
The existing receiver saves pixels every24pairs, plus the final pair. The auditor
joins real native frame IDs, validates captured dimensions/raw byte counts and
hashes source reports/pixels. Baseline, two free-hand positions and restored eye
pairs are listed for visual review. Pixel presence/difference never establishes
GPU visibility or headset acceptance automatically. Even one recovered frame
timeout keeps the strict transport and overall mechanical result false.

Focused validation:8 C++ diagnostic groups on x86/x64,44 launch/parser checks,
8 Python audit regressions; native Gameplay/NativeProbe x86 and IPC receiver x64
translation units compile. These are synthetic/offline checks. Full builds and
the actual native diagnostic belong to the integrating parent. No process was
opened, modified or given input by this candidate's author.
