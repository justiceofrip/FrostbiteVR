# Fixed-container context and original-source completion

Done: source implementation and focused regression cases. No compilation,
test execution, Git operation, game session, installation, or headset check
was performed by this worker. Root owns those checks.

`FixedContainerReload` now projects geometry with the original controller
observation time. Native count freshness remains independently validated;
queued admission is clamped to the original controller deadline. No raw
source timestamp, sequence, pose, or deadline is refreshed.

`CompleteAfterContextChange` consumes the retained exact request only against
the service's already completed receipt in the same stable ledger. It requires
the newer coherent current selected snapshot, exact conserved result counts,
Ready/no pending ledger state, and the ledger's idempotent receipt check.
Foreign resources, forged receipts, count-only changes, and stale observations
cannot consume the pending physical request. No second ledger, native call,
count mutation, or context binding occurs in this completion seam. Repeating
the exact acknowledged receipt has no additional effect. Cylinder mechanical
debt remains open; only clips finish their physical cycle at receipt completion.

The adapter invokes retained completion before RebindContext. Rebinding uses
FeedInteraction's existing MigrateContext to keep recovery geometry and revoke
the old recovery proof and hand claim. Preparation consumes its own original
frame; subsequent neutral and grip use newer frames and current contact.

Tests awaiting root execution:
- `Bc2FixedContainerResourceTests`: actual service all-three completion after
  stow/re-equip/space change; malformed/foreign/count-only rejection; independent
  N-1 renderer/native clocks and original deadline expiry.
- `FixedContainerContextTests` (new; same header-only dependencies as
  FixedContainerReloadTests): retained partial-cylinder recovery, revoked old
  proof and claims, required neutral/current contact, and open-cylinder receipt
  consumption with count conservation.
- `Bc2PhysicalFixedContainerTests`: full clip insertion with prior renderer
  frames, original queued admission deadline, one native service completion
  through context change, and partial-cylinder migration/regrip.

Changed production files: include/fvr/interaction/FixedContainerReload.h,
include/fvr/interaction/FeedInteraction.h (friend access only), and
src/games/bc2/Bc2PhysicalFixedContainer.cpp. No other production file changed.
