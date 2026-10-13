# Final-shell support return follow-up —260

The expanded259 game run passed SCAR support and the interrupted continuous
SPAS pump, then timed out waiting for support after successfully inserting a
shell. Preserve that failed combined result; do not treat its successful parts
as a full pass. See [259](RECOVERY-259.md).

The follow-up retains already settled support-return intent through a missing
source observation. It grants no ammunition, current geometry or hand claim.
Fresh input, the same gun claim, and fresh contact are still required. Pending
shell work and explicit cancellation remain separate. A bounded lifecycle
journal identifies why the return was armed, settled, retained, rejected or
cancelled, including the previously unlabelled pump-debt cancellation flag.

A second failing regression reproduces an intervening pump consumer advancing
the hand arbiter's processing time. The later shell-support acquisition used an
older stored processing timestamp and was rejected. Gameplay now supplies
fresh processing time for the exact same packet; owner, sequence, observation
and deadline cannot change. Expired evidence still rejects. Both defects have
failed-before logs and passing x86/x64 physical-reload tests. The combined
consumer regressions also pass with current objects.

The input-test status reviewer now recognizes the natural pump scenario and
requires evidence of the SCAR support claim/release and nonzero aiming
correction. Seven reviewer regressions pass. Both full architectures passed
240 suites. The actual260 repeat again passed SCAR support, rifle magazine
work and the interrupted natural pump. It failed later: the shell was loaded
natively (7/23 to8/22), but an exact client prediction Restore rewound that
client before the server transfer. The tube request policy did not support
this evidence and cancelled with State; the later keepalive rejection was a
consequence. Support return was never reached. Trace SHA-256:
`51f02b8d38b7d381c395b2ca0857e47efe42adc35e45ce26692cc532a870e77f`.
Normal player builds remain pending; no headset candidate is ready yet.

The eventual headset check should cover:

- Holster the SPAS, lower both hands, then squeeze the right grip: stay empty.
  Draw only with a shoulder grab, including after a brief tracking interruption.
- Acquire/release the SCAR left support grip and perform a magazine reload.
- Aim the SPAS with both hands, fire, and pump back/forward without endpoint
  pauses. Repeat with a changed gun angle, then verify firing is enabled.
- Insert a shell and return directly to the fore-end while maintaining squeeze.
- Perform a shoulder swap and confirm the other weapon still works.

Headset placement and comfort still need user feedback. Campaign checkpoint
reload crashing is unresolved. GitHub remains245; no automatic publication.
