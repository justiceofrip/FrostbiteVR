# Pump presentation after headset337

The user reports substantially more reliable interaction, a working SCAR support
grip, and a functioning but janky manual pump. Three visual issues remain:

- A magazine appeared in front of the player. Its role and triggering action
  are not yet identified; no magazine-transform correction is claimed here.
- SPAS played its stock pump animation before the user's manual stroke.
- SCAR's right-hand angle was wrong. The support-hand reference works; the
  primary-hand/controller alignment needs separate evidence.

The session ended at 11:40:08 UTC on October 11 after BC2 crashed. The native final
trace is empty, so detailed grip/reload journals are unavailable. 460 periodic
samples survived, ending at 13,873 consumed stereo pairs. The crash XML is new
for this process. Its stack contains amdxx32.dll+0x1066ab in the same-process
pre-injection module map, matching the prior fault site. The initiating producer
is still unknown; this does not establish a driver defect. Evidence is pinned in
`bc2vr-recovery/headset337-review/review.json`.

## Change

The rendering adapter previously replaced the measured fore-end leaf only when
a current manual stroke target existed. Between firing and acquiring that target,
the ordinary subtree retarget retained BC2's animated fore-end.338 publishes the
measured closed fore-end whenever the selected manual-pump adapter is current
and no stroke target exists. An actual stroke still uses the existing rail,
hand claim, native cycle lease and completion checks.

The closed pose writes only that measured leaf in the private palette. It does
not move a hand, manufacture a cycle lease, complete the pump, replenish ammo,
or unblock firing. Original actor/weapon, tracking lifetime, rig fingerprint and
source palette checks remain required. A closed palette cannot remain latched
after a guided stroke begins, or a moving palette after grip release. Separate
closed-pose counters keep the existing physical-stroke diagnostic meaningful.

Periodic telemetry now flushes pump/manual-rest and magazine role counters during
play, so a future crash will not erase every presentation observation. Counters
alone cannot diagnose a wrong spatial transform; full visual/pose evidence is
still needed for the magazine and SCAR angle reports.

## Validation

Three new regression groups cover changing native stroke positions before manual
grip, measured closed placement with world scaling, no gameplay/hand authority,
wrong or expired owners/configuration, and rest/stroke handoffs. The existing
physical-pump suite now has 27 groups. Both full builds pass 244 suites each:
x86 156.02 seconds and x64 48.97 seconds.

Normal338 binaries and symbols were frozen before building the separate diagnostic
composition. One actual-game emulated run passes the SCAR support grip, three
magazine cycles across shoulder swaps, SPAS manual pump, shell insertion and
subsequent rifle shot. Independent server/client receipts agree on ammunition;
support returns 133.22 ms after shell completion. 240 stereo pairs are consumed,
40 image pairs checked, and native hooks drain with BC2 still running.

The new closed-pose path reaches 420 paired palette copies; the existing moving
path reaches 122. There are no source-palette rejections or magazine presentation
fallbacks in that bounded sequence. Two pump presentation fallbacks and 14 native
record-lock drops remain recorded: this is not a lossless callback trace. There
are no raw native part samples in this composition and no per-frame GPU bone
readback. These counters prove that both paths execute, not that the headset
animation looks correct. Human visual acceptance is still pending.

The independent results are in
`bc2vr-recovery/pump338-native01/pump-presentation-audit.json` and its underlying
combined/receipt audits. Trace SHA256:
`f8ac351a7e68f9fd8ad0768d97b05484d3977153ab15508e3cd234780e1eef94`.
