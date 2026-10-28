# Resource hand testing and mechanism integration — 223

This development composition adds a controller driver for the real ammunition
resource consumer. It runs original-magazine return followed by discard and
chest replacement on persistent hand, supply and native service instances.
The driver changes only synthetic controller poses/buttons before normal
gameplay arbitration. It cannot acknowledge a reload or change ammunition.

Each successful cycle records its exact native removal and seat receipt,
server invocation, original item/equipment context and before/after counts.
Rendered removed, hidden, replacement and attached roles must actually be
published. The submission frame must free the left hand and admit support grip
before native completion. A separate offline auditor correlates these receipts
with independent native Update records. Missing evidence remains a failed or
inconclusive check.

## Bugs reproduced and corrected

- Original-magazine seating released its hand claim but still blocked support
  grip. The same fresh attached-target rule now covers original return and
  replacement insertion.
- Cancellation during an in-flight refill could strand its supply reservation.
  The native service now retains the exact terminal request/outcome across
  selection changes. The consumer settles only its matching reservation after
  proven application or proven rejection before dispatch. An uncertain native
  operation remains unresolved; timeout never creates an acknowledgement.
- Terminal settlement does not refresh input or reserve evidence. A new supply
  acquisition still needs a current source and neutral/grab input.
- Bounded diagnostics can use the actual chest inventory and geometry cache.
  The legacy scripted consumer selects that same chest anchor before starting.

Focused tests include two profiles, zero/partial/full originals, return and
replacement, delayed native completion, delayed renderer contact, interrupted
tracking/selection and malformed native receipts. Synthetic tests mock native
Updates and GPU publication; they are not live or headset acceptance.

## Pump and bolt work

The shared physical cycle consumer and action gate are integrated in this
composition. They use actual hand arbitration, ordered movement, one-shot
release requests and exact native readiness. See [pump consumer](PUMP-CONSUMER-222.md)
and [bolt sequencing and machine emulation](BOLT-CYCLE-222.md).

The bolt experiment executes the installed engine's original x86 instructions
against 17 extracted configuration variants, with explicit synthetic objects
and empty effect/listener boundaries. Its 153 cycle and 34 negative shot cases
do not establish live shot authority or calibrated bolt-hand geometry. Native
pump/bolt gameplay registration is not enabled by these tests.

## Validation and subsequent native failure

The integrated private x86 candidate passes 19 focused suites; both normal
builds pass all 201 suites. Source and private binaries are frozen locally under
`resource-adapters223-private` and `normal223-resource-mechanisms-20261008`.

Live `resource-hands223-01` exited the boat but its first selection drew the
existing SPAS; the rifle driver never armed. Checkpoint224 corrects that setup
and adds passive pump capture. Its private x86 build passes20 focused suites
and full x64 passes202 suites. Live `resource-hands224-01` reaches the XM8,
arms the actual hand driver and removes22 loaded rounds while preserving191
reserve. The native server/client completion succeeds; the physical consumer
cancels before recognizing it. No return/replacement success is claimed.

The same cancellation was reproduced offline by publishing native completion
before refreshing the selected count snapshot. Checkpoint225 retains the prior
bounded dispatched view until actual fresh counts match the completion receipt.
Empty-magazine suppression remained applied/restored throughout the224 live
run; stock reload states first observed after detachment are separate from the
consumer failure. Both runs delivered240 pairs and left BC2 responsive. No new
headset build or additional weapon registration is claimed.

## Actual controller-driven success — 225

`resource-hands225-01` passes the independent native/controller audit for both
cycles. Native command1 removes22/191 to0/191; command2 returns the same original
to22/191. Command3 removes that original again; the hand discards it, then chest
replacement command4 seats30 rounds and ends30/161. All four commands have their
own server authority invocation and independent client/server Update receipts.
No ammo counts are supplied by the scripted hand driver.

The consumer completes original return and replacement, accepts support on the
submission frame, and settles with no pending resource/supply operation. Its
final Cancelled state is the explicit diagnostic cleanup after Done, not an
interrupted reload. The receiver delivers240 pairs with zero async timeouts.
BC2 remains responsive; all postflight/drain checks complete. Trace SHA256:
`745175fce2e600c41fbdf48ca3219e05f8324a602275c1c091939e039c7ceefc`.
The immutable audit is in local recovery
`pipeline-runtime-20261005/root-monitor/resource-hands225-01/resource-driver-audit-v3.json`.

This establishes the bounded actual XM8 resource path. It does not establish
headset appearance, arbitrary owner transitions, AEK native acceptance of this
new backend, or the additional measured batch profiles. Those checks continue.
