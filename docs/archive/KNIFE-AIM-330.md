# Private knife attack-transform proof330

The normal player candidate remains329.330 changes only a private diagnostic;
it does not enable the chest knife or change firearm grips/reloads. The private
build reuses322's verified owned melee shoot/composition callbacks and318's
shared hand custody/motion recognizer.

Two synthetic stroke intents each provide a separately identified diagnostic
pose, valid for one second. The client and server copy that pose identity inside
their exact owned Shoot callback, then apply opposite15-degree directions and
0.03-unit lateral shifts to their own composition outputs. A null server context
still means no native shot token; zero is never used to pair attacks. This small
fixture does not establish a production cross-thread transaction protocol.

Writes require current actor/inventory identity, knife selection on both sides,
the exact composition caller, a result wholly inside the current thread's stack,
no aliasing with either source matrix, finite values, an unexpired intent, and
at most one write per branch/intent. Only the right/forward/translation XYZ words
of the caller-owned output change. Padding, up vector, both source matrices,
weapon data, ammunition and native hit range remain untouched. The output is the
result of this attack, so it is not restored before its caller consumes it.

## Actual-game result

`knife330-aim01/aim-audit.json` passes an independent numerical/lifetime audit:

- 196 simulated controller samples produce two motion intents and two Shoot calls
  on each side. All three firing copies show the observed sequence
  `2,5,6,2,5,6,2`; all journals report zero dropped rows in this run.
- Four owned stack outputs receive the intended transforms. Independent math
  checks verify rotation, translation and orthonormality. The native callback
  reports exact byte verification and unchanged source matrices.
- Client/server orientations match for each intent. Their small original
  position differences remain; the audit checks each change against its own
  original rather than requiring prediction/server positions to match.
- The original rifle returns with unchanged ammunition; hooks are removed,
  checked native code is restored, and BC2 remains alive/responding.

Native trace SHA256:
`c72dc9666294999316d7fd6ceb85eb1710d806628eb92f9818d056da0816d369`.
The private source/build receipt is in `knife330-aim-source`; preparation and
audit helpers are copied into the evidence directory. No player rebuild is
needed for this private experiment;329 retains its existing full-build evidence.

## Limits and next integration

This verifies composition output, **not downstream hit/damage**. The native
attack still starts roughly464–507ms after the corresponding motion intent.
Chest contact/rendering, controller-to-knife placement, production attack
custody, server hit receipts and that delay remain open.322's extra prediction
recovery transition is not explained or fixed by this clean330 sequence.

The separate326 restart crash also remains unresolved. Review of the saved dump
and bridge/menu copy/release paths did not establish a producer for its null
graphics-worker command.327/328 successful restarts do not justify a speculative
graphics workaround or sustained-campaign acceptance.
