# Checkpoint 412: presentation and launch follow-up

This candidate follows the normal410 headset run, ended at the user's request
on 2026-10-16 UTC (October15 local). The user reported much better pumping and
reliable manual shell reloads, but residual loading-shell animation, no spent
shell ejection, high chest magazines, awkward ammo-counter placement and slow
startup. Those observations are partial acceptance, not whole-campaign acceptance.

The host and trace ended normally. Final trace status records no new crash,
game still responding, and a drained native module that remains mapped until
BC2 exits. BC2 and SteamVR were left open. The new candidate has not been injected
into that old process or tested in a headset; the user requested an automated
checkpoint without another headset run.

## Changes

- The current manual gun now owns a separate presentation lease for its verified
  stock loading-shell leaf after the reload transaction retires. Physical shell
  previews take priority, and ordinary equipment recovery bypasses this cosmetic
  exclusion. Ammunition, pump timing and hand ownership are unchanged. See
  [shell evidence and exact limits](SHELL-PRESENTATION-412.md).
- Chest ammo and its grab target share a reference-head-relative height of
  -0.45m, previously -0.25m. This is a shared body anchor, not an XM8-only offset.
  Measured XM8 cached geometry moves from a top edge 16.1cm below the head to
  36.1cm below it; the new centroid is about 44.1cm below the head.
- The read-only native loaded/reserve counter follows the right controller's
  aim axes beside the grip, offset (-0.15,+0.035,-0.12)m. The 20cm by3.75cm quad
  faces the viewer in local XR space and is visible to both eyes. Tracking loss,
  changed reference space, stale native counts, menus and excessively close or
  behind-head placement suppress it. There is no health bar or invented ammo.
- Startup uses one fresh storage inventory, then a short-lived receipt restricted
  to that launch's verified/session/trace stages. Each stage still checks free
  space. Builds, captures and other launches cannot reuse it. The same inventory
  measured 27.08s with the old traversal and7.50s with bounded compiled readers;
  the child handoff check measured0.64s. These are storage timings, not a new
  end-to-end game/headset startup measurement.

## Validation

Independent read-only reviews covered shell ownership/recovery precedence, XR
coordinate spaces and sample lifetime, and launch receipt process/expiry checks.
Storage and launch-scope tests passed34 and19 checks respectively, including a
real hidden child process. Full native build and software-rendering results are
recorded in the [412 handoff](../handoff/checkpoint412/README.md).

The prior trace contains seven submitted and seven completed manual shell
insertions. Its 17,456 native shell-visible contacts versus699 paired stock-shell
hides support investigating the retirement gap, but contacts are not proof of
visible pixels. New visual corrections remain unaccepted in a headset.

## Remaining limits

Spent-shell ejection is not implemented in this candidate. The known loading-shell
bone is stationary during the stock pump/fire clips; the firing asset's effect
references do not establish a safe native casing call. Immediate post-shot pump
eligibility is intentionally unchanged, following the user's request to defer it.
The counter's exact comfort and chest/holster alignment remain subjective checks.
No new weapon registrations, complete campaign claim or multiplayer support is
implied by this checkpoint.

## Storage and rollback

Generated data stays on G:. Byte-verified compression of929 existing compiler and
symbol files reclaimed1,153,523,415 bytes without deleting evidence or symbols.
No new raw game captures were generated. Existing build trees are reused.

The exact retained normal410 source is preserved in branch
`checkpoint/410-headset-rollback` at57e04e90e8d9f1d8f9448556db549725c888fa9a.
Its native binaries and original source archive remain local. The public rollback
contains source, not game assets or a player installer. The normal412 operation
receipt binds source SHA256
`5d0542ffed1acdd2688df8b05534f9b465b4780fb233f52fb45aa07c31a1370d`;
the native offsets and ABI are unchanged from410. The full source commit also
contains the XR host, launcher and tests outside that native dependency closure.
