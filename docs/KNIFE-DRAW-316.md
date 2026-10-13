# Native knife selection and deployment316

Private native diagnostics establish a route to select the existing KNV-1 in
slot7, keep it selected for1.5seconds, and return to the previous rifle with
unchanged ammunition across the server and both client firing copies. This is
not yet connected to a chest grab, tracked knife pose or motion attack. Player
checkpoint312 remains unchanged.

## Why simple selection was insufficient

313 observed the original selection chooser0x6ef590. The local active client
selector and server selector call it from0x6f19d2 inside their ordinary update.
The inactive client selector did not appear in the owned observations. The
observer preserved every returned value and detached with original code restored.

314 requested slot7 through that chooser, leaving the owning update's original
eligibility check, setter, deploy callbacks and replication intact. A half-second
draw/return passed, but that was too short to prove persistent holding.

315 extended the hold to1.5seconds and observed all three owned knife firing
updates. It failed: the knife entered states5/6 without fireRequested and
returned to the rifle. All798 observed contexts decoded, no firing records were
dropped, and688 updates included a shot step. Rifle ammunition and hook cleanup
were intact. This failure is preserved; it is not headset evidence.

Live reflected WeaponFiringData identifies Boolean `FireOnDeploy` at+0x48, true
for the installed knife. In native deployment0x6e6d20, the query at0x6d6eb0,
returning to0x6e6da5, gates the automatic fire call. This explains why clearing
ordinary controller fire input alone cannot implement drawing the knife.

## Scoped prototype and acceptance

316 returns false only for that FireOnDeploy query when the exact owned knife
firing object, callback return address, native actor/inventory membership and
bounded diagnostic lifetime match. Ordinary fire/availability queries and other
items retain their original result. Shared weapon data is never changed.

The same longer sequence now passes. Both native selectors retain slot7 for
about1.500seconds before the explicit return request. It records1021 firing
updates, all with decoded context and fireRequested=false, with states1/2 only.
One ordinary firing record missed the nonblocking logging lock; there is no
claim of complete callback coverage. The deployment query was suppressed once
for client+3c and once for the server; client+40 remained idle without a direct
suppression. Four selection intents were emitted. The original rifle returned
with unchanged ammunition, all hooks drained/removed, all checked code restored,
and BC2 remained alive/responding.

Evidence under `bc2vr-recovery`:

- `knife313-observe01`: observation of actual native selection boundaries.
- `knife314-draw01`: half-second check with its original limited scope.
- `knife315-hold01/hold-audit.json`: failed longer control and reflected field.
- `knife316-hold01/hold-audit.json`: corrected native acceptance and trace hash.
- `knife316-deploy-source`: frozen private source, build receipt and binaries.

The installed executable SHA256 is
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
No new player feature is enabled by this diagnostic. Production integration must
own the selection request and deployment gate for the real knife lifetime,
handle native actor replacement and tracking loss, and preserve holster ownership.
Post-stab return suppression, native500ms shot delay, hand-directed hit origin,
range/damage, chest geometry and authored grasp remain separate unproven steps.
