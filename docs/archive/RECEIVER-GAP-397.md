# Checkpoint397: input-gap recovery passes; later shell stop fails

Both complete normal builds pass259 C++ suites. The normal build now supports
an explicit player-grenade option through the real XR host and verified native
menu ownership. The option is not enabled by the preserved390 headset launcher.
Physical knife handling is still private development and is not enabled.

## Actual-game result

One changed diagnostic stops all emulated controller publication for about
five seconds during a partial SPAS pump, then returns in a new tracking space.
It retains the owed cycle, accepts the first genuinely fresh neutral packet,
and completes three physical pumps with native acknowledgements. The run then
commits two manual shell insertions. It fails while stopping native shell loading:
an unwanted eighth shell loads, leaving8/21 instead of7/22. Both observed client
copies retain8/21 in all31 coherent post-detach samples. There are no rejected
post-detach reads. The overall test failed and is not a headset-ready candidate.

The native callback that first attempts the stop began preparing before the
stop was armed. Its exact owner, configuration and ammunition are unchanged.
The existing cleanup rejects it and discards the pending stop. Its original
prepared-input timestamp was not serialized, so the precise rejected scalar is
not independently available in397. A corrected lifecycle and additional source
timestamps are being developed for398; that change needs an actual-game check.

A separate code review finds that clearing presentation/input ownership can
immediately discard a stop that physical cancellation just armed. An accepted,
bounded stop must survive presentation loss on the same native owner. This is
cleanup authority only: it must not renew input, enable firing, transfer ammo,
or survive actual equipment replacement. The original deadline remains binding.

The recovery diagnostic also misses its final stroke flag when the parent moves
to the shell phase before its next observation. Three native pump completions
are independently present. Correcting that diagnostic flag does not change the
failed shell-stop result or make the overall run pass.

## Other checks and remaining limits

One steeper ordinary grenade gesture completed with paired stereo captures and
400 coherent health observations. It was nonlethal, so it does not validate the
actual death-screen fix. The earlier native checkpoint-fade result remains a
separate, narrower pass. The owned game process was closed and its exact original
display settings restored after the check.

The396 native pause/Resume check and separately audited grenade gesture remain
valid within their recorded scope; they were not repeated unchanged. Investigation
of campaign vehicles found that the active camera selector differs from the entry
variant selector. A verified reader correction and attached-soldier observation
work are private398 candidates. Forced passenger weapons, UAV controls, physical
knife handling, actual death and sustained campaign play are not accepted.

The most recent human headset run is the failed390 test. No whole-campaign or
new headset acceptance is claimed.

Local evidence: `bc2vr-recovery/overnight397/normal-build/build-receipt.json`,
`native01-full-receiver-gap`, `death01`, and `source-before-native/checkpoint.json`.
Raw traces, recordings, installed game assets and machine-local paths are not
included in the source distribution.
