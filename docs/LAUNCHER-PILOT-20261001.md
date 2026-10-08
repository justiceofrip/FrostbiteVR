# Scoped XM8 launcher pilot — October 1, 2026

The accepted SPAS/XM8 headset checkpoint remains unchanged. This pilot is being
validated natively; headset feel has not been accepted.

The exact scoped XM8 and 40mmgl share the reflected PersistentWeapon.Id sp_xm8_s
and the same XM8 mesh and ACOG mesh. The native switching map resolves dedicated
entry action33 to the launcher and action36 back to this same rifle.
ResolveWeaponMode validates identities and both map directions; native equipment
eligibility and animation remain authoritative. No equipped-pointer write exists.
See LAUNCHER-BINDINGS-20261001.md.

Capture native-trace-20261001-081204-659 records XM8 to 40mmgl to XM8, two requests
and two native acknowledgements,119 source samples, and no source/packing/fallback
errors. Receiver delivered240 pairs with one timeout. One tracked pose was rejected.
Both the grip and mechanism pipelines pass strict audits.

The launcher registry entry enables the shared visible -Z/+Y model orientation
and its separately sampled authored wrist/support relation. Live use of generic
name40mmgl additionally requires this exact scoped-XM8 family, preventing other
rifles' launchers from inheriting the evidence. Native grip offsets are sampled
after settling; candidate JSON offsets are not hard-coded into runtime.

jntWpn_Flash remains at the rifle muzzle in both native modes. Launcher translated
muzzle support remains UNVERIFIED and disabled. Native grenade trajectory/ammo
are not modified; firing-origin/impact accuracy remains separate work.

The two repeatable90-degree child changes are jntWpn_9 and jntWpn_11.
Projection into captured frames45 and90 locates the former at the visible rear
ladder sight hinge, the latter at the front post. See the saved projection evidence.
An approximately11cm rear ladder extent is observed in these images; interaction
tolerances are pilot parameters requiring headset adjustment, not extracted mesh
bounds. No native sight-palette manipulation is needed: the game's original mode
animation raises and folds both sights.

The default-off -SightFlip pilot uses neutral grip then intentional squeeze near
the rear ladder, followed by signed rotation about the measured hinge. Nearest
contact chooses sight versus support grip. While manipulating the sight, the
off-hand remains independent and firing is suppressed. The mode is committed
only after native acknowledgement. Releasing, losing tracking/focus, reloading
or changing physical weapons cancels interaction. Folding down requests the rifle.

## Final native acceptance

Final capture: reports/native-trace-20261001-083335-015; receiver083334.
Two physical sight requests, two native acknowledgements and two policy commits;
zero gesture cancellations. One launcher support grab/release and310 held samples
in the preceding repair run; use the final summary for this capture's exact count.
The final support checker proves10.148mm of active seating from a deliberate1cm
offset. Maximum right grip error0.415mm; attached left0.101mm, free left0.134mm.
Barrel versus item/space-scoped native aim differs by at most0.000292degrees.
240 stereo pairs, zero timeouts, no observed exception; hooks disabled and game
alive after observation. Source animation, palette packing and fallback checks pass.

The first physical pilot exposed a duplicate-pose acknowledgement/contact gap and
unreachable synthetic hand positions. The policy now waits for fresh tracking
without discarding a matching native acknowledgement; the reachable fixture tests
active seating. Earlier failed evidence remains in reports for traceability.
Mode request state is independent of the32-entry chronological log; logging cannot
disable continuous interaction. Captures retain96 observations per item/rig group.

Builds pass39 x86 and38 x64 suites; discovery rejects91 mutations. Both Python
pipelines pass24 regressions each and strict audits of the final capture.
Native evidence: reports/launcher-sight-20261001.json and
reports/launcher-alignment-acceptance-20261001.json.
Source/binary checkpoint: reports/launcher-sight-20261001/checkpoint.json.

Headset launch after SteamVR is ready:
Start-BC2VRSession.ps1 -SightFlip
This keeps the session running until the game/runtime exits, as before.

Headset check: squeeze the left grip near the rear ladder sight, then lift it
about its hinge; release and grip the launcher's handle. Reach to the raised sight,
squeeze and fold it down to return to the rifle. Gripping alone does not switch
mode. The native animation finishes the fold after intentional movement.
Assess reach/contact, unexpected support coupling, orientation and headset
removal/reconnect. No automatic claim of headset acceptance follows this test.

Before batch-promoting other rifles' launchers, extend capture identity with the
full asset path/persistent family. The short name40mmgl alone is not globally
unique. Current runtime requires this exact scoped-XM8 family; offline candidates
remain unverified and cannot silently grant another model these capabilities.
