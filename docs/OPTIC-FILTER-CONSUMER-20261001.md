# Native lens-filter observation consumer

This candidate supplies the missing runtime consumer for the existing optic
discovery. It observes the actual native lens-filter callback, its selected weapon
configuration, and D3D11 draws executed inside that callback. It does not implement
physical scope rendering, magnification, native ADS acknowledgement, eye-proximity
activation, reticle suppression, or a replacement sight texture. Normal XM8/ACOG
rendering, gun poses, and the removed left-trigger ADS mapping are unchanged.

## Native boundary and ownership

`DiscoverOpticFilter` reuses existing full function/caller/reflection discovery.
For the inspected executable, preferred image base is `0x400000`, native filter
renderer is `0xb8d6b0`, sole direct caller is `0xbb2b60`, and return is `0xbb2b65`.
The decoder-backed MinHook trampoline preserves the `__thiscall(renderer, filter)`
ABI and `ret 4`. The wrapper invokes the original exactly once. Install compares
loaded renderer/caller bodies, type registrations, metadata and fields with the
inspected executable. Runtime retains its own image bytes through failed drains.

The reader obtains genuine immutable `SelectedMeshesSnapshot` data through
`SelectedMeshesObservation::ReadCurrent`, using the existing publication and clear
barrier. No second owner cache or inferred equipment generation is introduced.
Full native owner, original sequence/time/deadline, current IPC space and exact
native player/weak/soldier/inventory/selected-item/configuration links are checked.
The native owner/configuration is re-read around the filter read and after the
original call. A newer metadata publication cannot renew the original callback's
lease. A changed owner, expired source or disconnect prevents retained ownership.

The actual argument must expose the inspected `SniperLensScopeFilterData` type.
Reflected `WeaponStateData` fields are validated against immutable descriptors:
`ZoomedScopeFilter +0xac`, `NonZoomedScopeFilter +0xbc`, and `MeshZoom1p +0x94`.
Exact argument-pointer matches are recorded as separate masks over at most eight
configured states. Shared filters remain ambiguous; neither mask proves which
state is active or that the native game has acknowledged ADS. Asset names never
enable a rendering change.

Renderer fields `+0xc8`, `+0xd8`, and `+0xdc` are recorded as borrowed native wrapper
identities before and after the call. They are never cast to D3D interfaces.
Actual D3D resource identities come from the device-context Get methods during
the original filter's nested Draw/DrawIndexed calls, using a per-thread scope ID.
Rejected nested native callbacks mask their parent scope. Draw evidence records
its actual scope, context, shaders, RTV slots 0–3, DSV, PS SRV slots 0–7, resource
dimensions, four viewports/scissors, blend/depth state identities and topology.
This is a bounded inventory, not a complete capture of every graphics stage.
The `complete` field means the viewport/scissor arrays fit their explicit bounds.
There is no 1920×1080-only filter and repeated draws are retained.

Scope records say whether the full original owner/filter configuration survived
the callback. Unmatched configuration can still retain native callback/draw
evidence; it does not acquire a selected-owner association. Captured resources
are not assumed to be a magnified scene or reticle. No textures, game assets,
private COM references, or live GPU readbacks are stored.

## Integration and lifecycle

Apply `integration-operations.json` to compose the narrow changes with concurrent
work; `integration.patch` and preserved `base/` files supply reviewable context.
The changed existing files are CMake, NativeProbe, two Gameplay files, the two
SelectedMeshesObservation files and its regression test. No canonical or staged
file was edited by this candidate.

Reserved flag `0x8000000` is explicit and default off. The validator requires
stream mode 9, the existing controller/aim/body/pose-observer/hands prerequisites,
100–15000 ms duration, and excludes continuous sessions, physical reload/fixture,
hold/round/request fixtures, death/equip probes and the rig pulse. It does not
require the broad GPU PassEvidence flag. Callable launch options are documented
in [the launch guide](OPTIC-OBSERVER-LAUNCH-20261001.md). There is no automatic
native attachment.

NativeProbe installs the observer before global hook enable and brackets startup
with BeginGlobalStart/CompleteGlobalStart. Already-enabled MinHook status is
accepted only after the explicit successful global start. Disable closes admission
and separately drains callbacks through the existing resident-module cleanup.
Exception/partial startup paths also disable the new hook. No lock spans the
original native call.

The existing frame bridge updates connection/focus state. IPC Busy is not treated
as disconnect; a Busy source read supplies no new owner association. Actual
timeout, closed/invalid input or tracking/focus loss cancels the session generation.
Reconnect requires a metadata snapshot observed after the new connection floor.
Repeated connected notifications do not reset a valid session. The helper expires
after 20 s, limits callbacks to 4096, stores at most 128 call records and 512 draws,
and captures at most eight draws per call with explicit drop counters.

An exact optional view record is installed only around the native draw for the
known current request/world/stereo view. If the filter executes elsewhere, it gets
no guessed frame/eye association. This is important if BC2 schedules the effect
outside the ordinary eye draw. No new render or simulation pass is invoked.

## Validation and next native use

Both x86 and x64 pass nine filter/configuration/session test groups against the
installed executable, four D3D11 WARP groups (small-target capture, exact resource
identity, state preservation and explicit viewport overflow), and eight existing
configured-mesh observer groups including new ReadCurrent expiry/clear assertions.
The x86 runtime, composed NativeProbe and composed Gameplay compile. Full repository
build and a native observer run remain the integrating parent's responsibility.

Run the installed-image tests explicitly with the EXE path; ordinary CTest without
that argument runs the two portable session/configuration groups and reports the
skipped installed-image fixtures. The WARP target is explicit and not automatically
registered as a GPU test.

The first useful live run needs an exact affected sniper/Carl Gustav variant
equipped and its ordinary zoom presentation reached. This candidate does not
restore controller ADS to manufacture that phase. Record hip/entering/settled/
leaving phases separately if an authorized native input path is available. Join
`optic_filter_observation.records` to `draws` by scope ID and retain
`owner_retained`, masks, original deadlines and actual view verification. The next
implementation decision is based on those real resources/draws, rather than
guessing that a filter wrapper is a reusable scope scene. Reload native fixtures
remain the immediate project priority; this observer can stay disabled meanwhile.
