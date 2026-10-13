# Sustained support diagnostic309

308's successful rifle/pistol native sequences acquired and immediately released
support. The pistol retained only two held samples, so it could not detect a
hand that attaches then drops off.309 changes only the emulated player: hold the
acquired controller pose and squeeze for at least750ms before releasing. Do not
chase the weapon's corrected contact while testing its support stability.

The diagnostic requires the same continuous claim, fresh source/current input,
and no observation gap over150ms. A repeated source packet cannot extend the
accepted duration. A lost/replaced claim fails instead of permitting silent
reacquisition. Counters record the first/last held timestamp, unique samples and
maximum gap. Production grip, reload, native input and ammunition behavior are
unchanged from308.

Nine focused suites and both full243-suite normal builds pass. The extended
pistol native check held support for751.6ms/46 unique packets, with primary-hand
aim unchanged, then completed17/85 ->0/85 ->17/68 ->16/68 ammunition flow.
An inspected stereo frame shows both hands at the pistol grip.

The combined SCAR/SPAS run FAILED after its751.1ms/46-packet rifle support hold
and two successful magazine cycles. The SPAS pump reached its rear endpoint and
returned toward closed, then cancelled at native invocation9514. Its first
failure journal records controller1756 expired41ms before the server Update;
native snapshot1764 still has the same weapon, actor and7/24 ammunition. The
preceding callback gap was123ms. Hooks drained and BC2 stayed alive. This build
is not staged or represented as a successful combined check.310 investigates
that native/controller publication ordering.

Evidence: support309-pistol-native01/singlefire-magazine-audit.json and
visual-support-review.json; support309-scar-native01/native-cycle-first-failure.json
and runtime-status.json. No309 headset acceptance. Checkpoint308's
tested player package remains ready and unchanged. No headset acceptance.

Separate read-only knife research is recorded in KNIFE-BINDING-INVESTIGATION.md;
it does not add a working chest knife or motion attack to either build.
