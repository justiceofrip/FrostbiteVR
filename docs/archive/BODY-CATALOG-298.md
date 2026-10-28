# Local298: body catalog capacity and reload-to-fire check

The native and host body renderers had a 16-asset catalog limit while the cache
loader accepted64. Our46-record cache parsed correctly but its entire catalog
was rejected by both renderers. Both now share the same64-asset limit; the eight
simultaneous display slots and all per-asset identity/geometry checks remain.
Queue rejection has its own status instead of being mislabeled geometry failure.

Both architectures parsed and queued the full installed cache:46 parts and
269,940 triangles. Software D3D11 tests drew the final asset of a64-entry catalog
into both eyes, checked depth and atomic expiry, and rejected duplicate/oversized
catalogs. Five focused suites and all243 normal suites pass on both architectures.

The actual campaign test accepted all46 native catalog entries, removed the
MP443_sp magazine, loaded a chest replacement and then fired one ordinary
controller-trigger shot:17/85 ->0/85 ->17/68 ->16/68. The independent audit
matches both transfers and the subsequent shot to the server and both client
branches. All240 stereo pairs were consumed, with40 pixel checks. There was no
magazine presentation fallback, conflicting hand claim, GPU error or camera
restore failure. Hooks drained and BC2 remained alive. No OS input was used.

The earlier297 run failed before any controller input or stereo pair. The receiver
could observe a successful handshake, then re-read its nonblocking connection
status and mistake mutex contention for timeout. Setup now retains the first
successful observation; subsequent input/frame operations still validate the
live peer. A deterministic regression covers success followed by contention,
real timeout and a backwards clock. The exact mutex race in297 was not captured;
the original failure remains saved. Its no-frame shutdown retained hooks and
required closing that exact game process; that cleanup case remains unresolved.

Prior296 separately verifies17 single-fire shots through empty, a5.071-second
empty hold and manual refill. Prior294 verifies VR-menu pointer checkpoint
recovery with a removed rifle magazine. Prior290 verifies eight SPAS pumps,
shell insertion and F2000 magazine return; prior293 verifies five M95 bolts
through empty with native magazine-refill fallback. These are native emulation
results, not whole-roster, headset placement or sustained campaign acceptance.

Open work includes pistol slide/chamber and hip routing, belt-fed native loading,
broader exact enrollment, M95 manual magazine loading, sustained campaign/death
recovery, and headset visual checks. Ammo metadata delivery is separately verified
from existing296 records; that does not establish compositor visibility. No
headset session was launched and nothing was published.

Evidence: `pistol298-native01/singlefire-magazine-audit.json`.
Source: `9dfb6bbbe24086197ff2e41e9cf96317c210c86269e8edb95782e0129e42cfe5`.

Trace: `28a4cb29b3cf50b0db937df492d90df53fa126fac57bdb7a1609c694924d64c7`.
