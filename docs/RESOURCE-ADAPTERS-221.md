# Checkpoint 221: BC2 resource adapters

This change connects the shared ammunition backend to BC2's existing physical
magazine consumer and render publications in a private candidate. It does not
enable another weapon, certify the gun roster, or promote a headset build.

## Implemented

- `AmmoResourceService` owns the per-item inventory and native completion
  observer. A bounded, nonblocking channel carries one-shot gather requests and
  current results. Empty removal/return remains an observed native no-op.
- BC2's compound physical actor, attachment-family weapon alias and independent
  hand equip generation map explicitly to the native inventory context. Native
  receipts retain their original identity.
- `Bc2MagazinePhysicalReload` can select the new resource consumer using its API.
  That consumer reuses measured/profile geometry, the hand arbiter, chest supply,
  extraction and insertion rails. It sends no stock Reload animation pulse.
- Removed originals retain their rounds. Return and replacement are separate
  operations. A confirmed physical seat releases the exact loading-hand claim
  before native completion.
- Magazine and body-ammo publications carry resource custody separately from
  animation holds. A detached well cannot authorize an attached magazine unless
  its seat request is present. Chest supply no longer requires `allThreeHeld`.
- A settled, discarded empty well can restore gesture state after an equip
  boundary with a seat-only plan. It does not synthesize an unseat acknowledgement.
- The private native candidate accepts hand requests on the owning server Update
  and requires exact code/configuration/owner/cohort/context checks, whole-object
  postconditions and all three actual completed Updates. The finite two-call
  probe is disabled in that candidate.

## Validation boundaries

The deterministic tests execute the real BC2 physical-consumer wrapper, resource
channel/service and presentation checks, with mocked native calls and Updates.
They are not headset, renderer draw-call or native all-weapon acceptance.

The previous checkpoint's actual-game remove/refill evidence remains useful
for the underlying native primitives. It does not prove this newly connected
hand-to-native path. No new game injection or headset session is claimed here.

Normal builds keep `BC2_AMMO_RESOURCE_HANDS=OFF`. The private candidate requires
`BC2_AMMO_MOVE_PROBE=ON`, `BC2_AMMO_RESOURCE_HANDS=ON` and the finite refill
fixture OFF. Existing normal/headset behavior and weapon enrollment stay intact.

## Remaining before headset promotion

1. Run and audit the connected path in BC2 with an actual simulated-controller
   driver, including repeated operations after the diagnostic record window.
2. Retain completed supply outcomes across an equip/reference-space change
   during dispatch. The current consumer quarantines an unresolved reservation;
   it does not clear it by elapsed time or credit the newly selected gun.
3. Exercise gaps and cancellation during each request phase. Settled empty-well
   restoration is covered; an in-flight native effect is a separate boundary.
4. Complete render custody at the gather-to-server handoff and inspect actual
   stereo palette/prop draws for chest flicker and transient native fallback.
5. Verify support regrip and holstering of an empty well through the full gameplay
   arbitration, then run representative distinct profiles through the same path.

## Weapon coverage

XM8 and AEK remain the two enrolled detachable-magazine profiles. Wider prepared
asset/descriptor batches are not enabled by this change. Magazine-fed AR/SMG
mechanics share the new adapter; belt feed, pump, bolt, slide/chamber and
underbarrel mechanisms retain their separate outstanding work. See
[the coverage audit](WEAPON-COVERAGE-CURRENT.md) for the distinction between
prepared data, configuration variants and supported weapons.

## Final build evidence

The private x86 candidate passes all 12 focused suites. Full normal x86 and x64
builds each pass all 198 suites, with every private ammunition switch OFF.
Normal binaries, source pins and test reports are frozen in
`<local-recovery>/normal221-resource-adapters-20261008`; the separate private
candidate is frozen in `<local-recovery>/resource-adapters221-private`.
`<local-recovery>/recovery221-evidence.json` records both receipts and the
unverified live integration boundaries above. No additional gun is enabled.
The headset212 stage and published GitHub fork snapshot remain unchanged.
