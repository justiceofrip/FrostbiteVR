# BC2 hand ownership integration — October 1, 2026

The portable HandInteraction arbiter now runs in BC2's support-grip and launcher
sight path. The exact candidate is monitor-validated for those interactions, with
the delivery limitation below. It has not had a headset test. Physical reloads,
body inventory/empty hands and new scope rendering remain disabled.

## Runtime behavior and engine boundary

Bc2Gameplay supplies verified actor/rig/equip/reference-space identity and a
physical item key. Only the verified scoped-XM8/40mm launcher family shares that
key across its native weapon-pointer change. Other equipment changes reset
ownership. The native equipped gun supplies the right-hand GunHold prerequisite;
this does not implement physical holstering or empty hands.

The left hand can own support or the sight, with exact dependent tokens. Trial
gesture updates become committed only after ownership succeeds. Denied grabs do
not manufacture a support-grab/release pair. Sight preference and support distance
use one immutable weapon publication. Existing grip calibration, rig math,
weapon roots, shot transforms and native animation sources are unchanged.

Current tracking/release safety is separate from original rendered contact
evidence. The renderer may supply packet N-1 while gather sees N. The From APIs
validate recorded original generation, identity and deadline, without
retimestamping geometry. Explicit release, tracking loss, reset and dependency
invalidation block stale evidence. See [portable API](HAND-INTERACTION-20261001.md).

## Bounded continuation after a gather stall

SightOwnershipRecovery permits eligibility only after expiry of the exact held
claim or its exact gun prerequisite, with the same physical item/owner and fresh
tracked, held input. SightFlip must additionally commit the exact pending native
acknowledgement within its existing timing limits. Only then can BC2 create a
new strict-current reservation. It cannot resurrect the old token, borrow stale
geometry, create another native request, or add a preview to the native animation.
Denied trial commits cancel the original pending policy and clear its ack.

SupportOwnershipRecovery instead requires the unchanged SupportGrip grasp token,
newer unexpired real contact evidence and an expiry-only lifecycle transition.
AcquireFrom remains authoritative. It creates a new ownership claim while
preserving the accepted support token and steering math. No fresh geometry means
no continuation; dependency invalidation, real release and tracking loss still
cancel. This does not establish that the user's earlier shotgun drops are fixed.

## Validation of the final payload

DLL SHA256: b0b0cbaf0765373bc670263d45e89c9699c3ba475c8205c67efd50bc16f699c4.

| Check | Result |
| --- | --- |
| Full build/regressions | 52 x86 / 51 x64 suites pass |
| Ownership-auditor regressions | 14 Python tests pass |
| Combined campaign fixture 131432-920 | Two sight requests, native acknowledgements and commits; zero cancellations; one launcher support grab/release |
| Combined ownership audit | 2,768 checks, 18 retained events, zero ownership failures/rejections/drops |
| Support/tracking fixture 131545-800 | Three grabs/releases, including tracking loss; 734 held samples; 2,947 checks; zero ownership failures/rejections/drops |
| Sight preview and hand-role audits | Pass; no headset/skin-appearance claim |
| Launcher geometry | Passes measured geometry checks; strict overall audit fails solely on receiver timeout |
| Combined delivery | 240 pairs; one recovered timeout; max completed latency 62.146 ms; three completions above 50 ms |
| Support delivery | 240 pairs; zero timeouts; max completed latency 139.285 ms; one completion above 50 ms |
| Native restoration | Both runs: source changes, packing/fallback failures, camera restoration and GPU-stage failures zero; hooks disabled; responsive game; watcher exceptions zero |

Final traces are reports/native-trace-20261001-131432-920 and
reports/native-trace-20261001-131545-800, with matching native-ipc and exception
watch folders. Named final ownership/launcher/preview/roles reports and exact
source hashes are indexed by reports/hand-ownership-bc2-20261001.json.

The final native runs exercise normal acknowledgements and actual tracking loss.
They do not reproduce expiry during the mode acknowledgement or a held-grip
lease continuation. Deterministic tests cover those cases: a 110 ms sight lease
gap, exact/wrong/duplicate ack and denial cleanup, and a 95 ms support gather gap
using original contact evidence with only 5 ms validity remaining. No runtime
test stalls native simulation deliberately.

## Preserved preliminary evidence

- 125631-631: three support grabs/releases worked, but repeated inactive polls
  filled the event buffer (805 events, 293 dropped). Reset telemetry now deduplicates.
- 125856-808: first native mode ack arrived after a roughly 107 ms gather gap and
  the 100 ms lease expired. Two native acks but only one policy commit. This
  motivated the exact-ack continuation. Startup event time was also zero and is
  now sampled from QPC. The checker was not weakened.
- 130939-032: intermediate build passed both sight commits and support ownership;
  240 pairs with one recovered timeout. Not the final payload.

All failed reports remain preserved. No blanket rendering/performance fix is
claimed, and the earlier headset grip drops remain unexplained.

## Next headset check and deferred work

Test normal XM8/SPAS support acquire/release, launcher sight grab/lift/fold,
free-hand return, recenter, and headset removal/reconnect. Inspect for unintended
hand release or changed hand/gun alignment. Reload checks need available ammo;
the last read-only inventory snapshot had empty SPAS/XM8 ammunition. Monitor
fixtures do not replace that check.

No XR session was launched for this batch. BC2 was left responsive on its
existing monitor without raising it or injecting desktop input. The next native
work remains reload/resource bindings for the reusable reload/mechanism policies.
First release remains single-player; multiplayer is a later phase.
