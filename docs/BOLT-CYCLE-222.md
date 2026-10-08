# Bolt sequencing and native boundary emulation — 222 candidate

`WeaponActionGate` composes the shared `PhysicalWeaponCycle` with persistent
shot/feed obligations. Exact adapter-issued events select the existing
`WeaponMechanism` plan; unlock/rear/forward/lock emits one physical submission.
Fire remains vetoed until the exact native readiness receipt and a coherent
semantic chamber observation arrive. Neither this gate nor a controller gesture
creates ammunition. An empty completed cycle remains unable to fire.

Tracking/focus/grip loss cannot erase the obligation. The physical consumer can
regrip the original held cycle within its original timeout; after submission,
completion persists independently of physical custody. An exact native retirement
permits a new held cycle while preserving the owed action. Sequence barriers
reject stale restart and repeated release/completion. A changed equip/space owner
does not reset the item; its old completion cannot authorize the new input owner.
Missing events, contradictory chamber information and simultaneous shot/feed
changes fail closed. Tactical feed with explicitly retained chamber does not
force an empty-feed action.

The native adapter must establish the semantic chamber model independently.
BC2's loaded count is pooled; no separate authoritative chamber count, extra
round or capacity-plus-one behavior is implied. This gate supplies an additional
veto, not native ownership or permission to dispatch input.

## Controller emulation

`WeaponActionGateTests` composes the real shared physical consumer and hand
arbiter, with synthetic geometry and native evidence. All 14 groups pass on x86
and x64 under `/W4 /WX`: ordered bolt and pump cycles, incomplete/out-of-order
motion, 12 phase/loss/regrip combinations, delayed submission completion, 14
malformed acknowledgements, empty-last-round/feed separation, retained tactical
chamber, evidence expiry, event gaps, owner changes and native retirement.
Repeated cycles retain the same consumers and hand arbiter.

## Original BC2 instructions

`tools/probe_bc2_bolt_machine.py` reuses the existing x86 emulator and verifies
the whole installed executable plus complete Update/Step function hashes. It
maps the material authored fields of all 17 extracted bolt-sniper configurations
into explicitly synthetic firing/configuration objects. Effects/listeners are
empty mocks; owner scheduling, replication, rendering and a game process are
absent. The private extraction is an input, never a production registration.

153 cases pass: 17 configurations × 16.7/59.6221/100 ms deltas × stock release,
held trigger, and scoped zero-delta tail hold. Ammunition remains unchanged,
original contexts restore exactly, and the original state machine returns to
input processing. The tail hold lasts 59 simulated frames (about 0.98/3.52/5.9 s),
which is an offline experiment and does not broaden a runtime lease or timeout.

These configurations differ from SPAS: they have zero BoltActionDelay and
BoltActionTime from 1.6 to 2.3 seconds, with hold-until-fire-release enabled.
Their positive timer boundary is **current 8, previous 7, next 1**. The existing
SPAS **current 7, previous 6, next 8** positive-delay condition does not apply.
Step 8 has already dispatched its notification before this tail; any private
part presentation must therefore account for the native animation independently.

34 shot-boundary negative controls also pass: with empty listeners, starting
Step 6 does not consume ammunition or establish a shot. State 7 entry in the
positive cases is experimental setup. It is not evidence of a live shot or a
server/client held cohort. Normal native admission remains disabled.

## Binding and runtime work still required

The saved M24/SV98 reload clips contain an unnamed `jntWpn_3` rigid channel with
about 76–77 mm translation and 1.245 rad rotation. Their role, exact closed stop,
controller grip and correlation with the native tail are not established. The
saved contact extractor samples the left hand; that cannot prove a right-hand
bolt grasp. M95/GOL reload extraction does not show a corresponding moving bolt
channel. No unsupported per-weapon palette writer was installed.

The next runtime seam must use one shared native cycle service with separate
data for the supported boundary. An exact selected sniper configuration, own
server/client Update shot receipts, safe context admission, held-cycle renewal,
completion/retirement and presentation evidence must be verified before enabling
a sniper. Do not convert candidate descriptors to ReviewedNative from this
machine run. Ordinary gameplay, actual firing, magnified optics, actual bolt
part rendering and headset acceptance remain unverified.

The private initial receipt/report is in `bolt222-candidate/initial-manifest.json`
and `native-machine-02.json`. Canonical source/docs/profiles were not changed by
this candidate. Root owns full Build.ps1 runs and runtime integration.
