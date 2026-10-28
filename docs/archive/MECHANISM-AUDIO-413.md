# Mechanism audio adapter — candidate413

This candidate adds a shared physical-stage ledger, an observation-first native
audio adapter and optional locally derived SPAS cues. A single SPAS cycle now
has measured replacement-waveform and timing evidence at BC2's audio endpoint;
a separate eight-shot check has native submission evidence. These do not prove
headset or Steam Link delivery, subjective sound quality, or other weapon
families. Default gameplay preserves stock audio. The preceding412 diagnosis remains in
[PUMP-AUDIO-INVESTIGATION.md](../PUMP-AUDIO-INVESTIGATION.md).

## Native binding evidence

Executable: build795745, image base0x00400000, PE timestamp1314210909,
SizeOfImage0x1913000. Static disassembly and a bounded read-only snapshot of
PID14116 on October16 establish:

- `0x007ff060` returns the client context at0x015713d8. Its +0x48 field,
  global0x01571420, points to the animation-sound map.
- `0x0082a4ad` constructs that map with `0x006f0a30`. The constructor reads
  category pairs and inserts the animation asset's GUID and sound template.
- `0x00881650` is a receiver callback, `thiscall(self, guid*, unused)`, `ret8`.
  It reads entity/soldier from self+4; compares entity+0x475 bit0 with self+0x50;
  and forwards `(guid*, entity*, self+0x10 matrix)` to map dispatch0x006ee6a0.
  Dispatch's `ret12` and all three callsites agree on those arguments.
- `0x00881690` has the same receiver/arguments and first-person check, and
  forwards the GUID and entity to stop route0x006eafb0. Both callbacks occur in
  the receiver vtable beginning0x01435214. No guessed native sound invocation
  is added: the observer preserves their original trampoline calls.
- Live entry signatures are `568bf18b46048a887504000080e101384e50751a`
  (start) and `568bf18b46048a887504000080e101384e507515` (stop).

`tools/bc2_animation_sound_map.py` reproduces this bounded inspection and checks
the executable identity, signatures and category bounds. Private receipt:
`bc2vr-recovery/overnight413/audio-bindings413.json`. It verifies these pairs:

| Animation | Animation GUID | Sound template GUID |
| --- | --- | --- |
| SPAS12/1P/1P_BoltAction | 2d9cf41b-3aba-4a22-82a8-64051c180432 | 9515f482-793e-420d-b107-234f1c7ece0d |
| M95/1P/1p_BoltAction | 5f55f25e-d4b4-4a20-9f14-ca152a9610fa | 4ffd6e7f-10ad-4b5f-912f-26e991250a06 |

The snapshot proves the map. The later observer-only emulated run in
`bc2vr-recovery/overnight413/combined-interruption03/run/native-trace.json`
also captured one SPAS start and one stop with the exact GUID, matching local
soldier, first-person selector1 and fresh manual owner. Both physical stages
were recorded; suppression/queue/play counts remained zero. The journal
reserves the first96 and last96 known pump/bolt rows; unrelated firing/reload
callbacks take a GUID-only pass-through path. Unknown sounds, NPC owners,
third-person receivers and stale/untracked manual owners are never suppressed.

## Private SPAS cue cache

`tools/bc2_mechanism_audio_cache.py` reads the existing installed archive and
checks its index and exact resource SHA256. It accepts the observed version1,
mono, 48kHz XAS1 format; bounded 76-byte frames are decoded by the existing
FFmpeg executable through memory pipes. It writes only two small PCM WAVs and
a metadata manifest outside source on G:. No game audio is committed or shipped.

Resource `Sound/Weapons/Handheld/UL_shg_SPAS12/Spas-12_Pump_1p_Wave.res` SHA256:
`7e3cc4f14e0839f6dce1b3b6d78bf3ef38e82ca056b282557c64a0e3689f3dd5`.
The decoder returned28,928 padded samples; the authored count is28,864.
The reviewed split removes approximately145ms of initial silence and follows
two energy groups with a valley around355ms. The split remains waveform-derived;
endpoint timing is measured below, but listening quality is not headset-accepted.

| Cue | Authored sample range, end exclusive | Duration | Cache SHA256 |
| --- | --- | --- | --- |
| RearTravel | 6960–17040 | 210ms | ca559f5bc46f67ba845ba40ae8008c2edcc0e572545ab4e4f3878b205b80b0da |
| Closure | 17520–28864 | 236.333ms | e06db8a1d0ed46d2d49a73a4c59d5f606d30c75a3a7f4e939cf4aabbf47a14ea |

Both cues use0.30 digital gain and2ms edge fades; no system or game volume
setting is changed. Cache manifest SHA256:
`9505a703347eb5b7a2e64f54f1972a7a5d727b45dbe1a6756e7b43e4de7ecded`.
The runtime verifies all three files against these pins before enabling playback.
Current private cache is `bc2vr-recovery/overnight413/audio-cache`, about44KB.

The worker enumerates active Windows render endpoints and finds the existing
audio session for the current BC2 PID. It matches that exact endpoint ID to a
WinMM output device. The final hardening revision rechecks the exact endpoint
and device every100ms; suppression authorization expires after250ms without
fresh verification. A changed, ambiguous, missing or stale route permanently
disables replacement for that mod session. It never reopens on a different
device mid-cycle or selects the default mapper. A new session is required.
Detected open/prepare/write/cleanup failures similarly disable replacement.
Future native starts pass through; a start already suppressed is not replayed.
Tracking/owner expiry stops queued/current mod cues. Closure cuts any remaining
rear cue and starts its own segment. This is stage-triggered audio, not continuous
audio scrubbing to controller velocity.

One stable heap allocation owns the WinMM header and copied PCM. Reset,
unprepare and close results are checked before reuse or release. Uncertain
driver ownership retains that single bounded allocation, disables audio and
refuses another installation/unload in the same process. It does not clear or
free a buffer that the driver may still reference. Reports include route-check,
route-failure, cleanup-failure and retained-storage fields. This final hardening
passed both full builds and the final-source actual-game check documented below.
It was not present in the earlier waveform and eight-shot measurements.

## Shared policy and integration

`MechanismAudioLedger` deduplicates accepted physical rear movement and closure
by actor, item generation, cycle, shot and stage. Freshness, tracking and ordered
stages are required. Regrip/recovery retains the original debt identity; fresh
packets cannot replay an accepted stage. New item generations reuse their row
and retire older generations; explicit actor retirement releases its rows.
No audio result grants a grip, ammunition, fire readiness or native completion.

Replacement also requires an exact suppression receipt for the same nonzero
actor/item generations, cycle and shot. A stock start passed through while
output was unavailable cannot later acquire a replacement merely because the
output becomes ready. Unknown cycle/shot or receipt contention passes through
to stock; later stages never guess an association. A local replacement-intended
start that passes stock sets a terminal session veto, blocks later replacement
submission, wakes the worker and retires its queued/active cues. This prevents
retroactive replacement; it cannot erase audio already output before an
unexpected later stock start. The observed journals contain one start per cycle.

`Bc2PhysicalPumpResult.audio` and `Bc2PhysicalBoltResult.audio` publish accepted
stage events. Gameplay publishes `mechanismAudio::Source` with current native
owner, selected item and fresh input, then passes `.audio` to `Emit`. NativeProbe
installs the observer, reports `mechanism_audio`, and drains/removes its hooks
and audio worker during session shutdown.

For an explicit private replacement test, launch the game with
`BC2_MECHANISM_AUDIO_REPLACE_SPAS=1` and `BC2_MECHANISM_AUDIO_CACHE` pointing at
the checked G: cache. Replacement remains opt-in. M95 uses the same
event semantics but replacement is not implemented: its template combines mechanical
audio with randomized casing impacts, which must be preserved separately.

## Validation and measured results

Five Python regressions passed for codec/channel/rate refusal, packet/count
bounds, malformed streams and gain/fade construction. The real installed SPAS
sample decoded without errors and generated both pinned cues. The shared C++
regression covers dedup, tracking interruption, stage order, stale generations,
more than32 equip-generation transitions and independent pump/bolt items.
Both final normal x86/x64 builds passed284 tests with route/ownership hardening.
Deterministic cases cover permanent route failure, expired verification, exact
suppressed-start ownership and failed buffer cleanup. Final source closure is
`e0b5940640f31346f9ebb26b62ef0a197691210e16d94c0b70843a5e4a65364d`.
Its actual-game single-cycle check suppressed one exact stock start and completed
both physical cues. All512 route checks passed; playback, cleanup, stock-pass
veto and retained-buffer counters were zero. The same run completed shell
insertion, holstering and rifle reload/firing. This validates the stable route;
hardware unplug/reconnect and driver-failure branches have deterministic coverage
only. The personal launcher now selects this normal413 build with SPAS audio
replacement enabled for a fresh game process.

After the observer witness, the replacement run in
`bc2vr-recovery/overnight413/combined-repeated01/run/native-trace.json` admitted
two stages, queued/submitted two cues, and suppressed one matching local SPAS
stock start. Both cues completed with zero failed/stale/lost receipts. Despite
the directory name, this fixture ran **one cycle**, not three.

The observer, waveform and eight-shot runs below used the earlier413 source
`137749ee42125e16380654e3db175e42255b5fa7ffecf95461a3bea955de5d52`.
Their evidence is retained separately from the final hardening check above;
the final check did not repeat the waveform recording.

Its15-second `audio-repeated01.wav` and packet metadata contain one full rear
reference and one full closure reference, each with normalized correlation1.0.
No competing or unassigned reference peaks at correlation0.65 or above, and no
stock-like220ms rear/closure pair, were detected in this recording. The two
physical events were1.889s apart; the captured references were1.900s apart.

| Stage | Physical event to submission | Submission to captured reference onset | Physical event to captured onset | Packet-clock uncertainty |
| --- | --- | --- | --- | --- |
| Rear | 7.562ms | 30.182ms | 37.744ms | about0.809ms |
| Closure | 10.786ms | 38.404ms | 49.189ms | about0.960ms |

All1500 packets had continuous device/WAV frame positions, with only the initial
discontinuity flag and no timestamp errors. QPC estimates varied by about1ms.
The first analyzer incorrectly classified every>100us timestamp step variation
as a gap. Corrected analysis separates that measured jitter from actual frame
or flag discontinuities, reports uncertainty, and refuses latency attribution
when local clock spread exceeds5ms. Use
`bc2vr-recovery/overnight413/audio-analysis-repeated01-v2.json` and
`AUDIO-VERDICT-413.txt`; the earlier analysis is superseded.

The separate eight-shot native check reported8 suppressed starts,16 queued and
16 submitted cues; all16 completed normally, with zero playback failures,
stale events, interruptions or lost rows.
That run had no audio capture: it verifies repeated submission bookkeeping,
not sixteen captured sounds or headset delivery. The single-cycle waveform
witness verifies more than API submission but remains endpoint-level evidence.
The recording can include unrelated applications; absence of duplicate matches
is limited to these references and this interval. NPC/fire/reload pass-through
is narrowly scoped in code, not a fleet-wide listening acceptance claim.

### Bounded timing witness tooling

`tools/Measure-BC2MechanismAudio.ps1` performs storage admission and runs
`tools/bc2_audio_loopback.py`. Supply `-GameProcessId`, the existing `-Python`
runtime, a fresh G: `-OutputPrefix` outside source, and `-Seconds` (maximum15).
`-InspectOnly` queries the game endpoint and mix format without recording.
The tool selects the unique active render endpoint with a nonexpired audio
session owned by the configured BC2 executable. It refuses an ambiguous or
missing endpoint instead of falling back to the Windows default. Supported
mixes are PCM16/float32, mono/stereo, at most48kHz; float samples are converted
to PCM16 without normalization. Output WAV plus timing JSON is bounded to3MiB.
It captures the **endpoint mix**, which can include other applications on that
device; it never opens a microphone, modifies volume, or injects game input.

The JSON includes endpoint ID, mix format, QPC frequency, capture start/end ns,
and up to2000 packet rows. Each row contains WAV frame, frame count, device QPC
position in100ns, device frame, WASAPI flags, and read-time QPC ns. Multiply the
device QPC position by100 when comparing native receipt times. Discontinuities
and invalid timestamps are reported and must not be treated as synchronized
evidence. Packet timelines remain separate from the concatenated WAV timeline
so a capture gap cannot silently become a valid latency measurement.

`mechanism_audio.cues` is a fixed64-entry receipt ring. Accepted physical stages
are recorded in observation-only mode as well as replacement mode, using the
same dedup ledger. Receipts retain actor/item generations, cycle/shot, stage,
controller evidence time, admission/queue time, waveOut write start/end,
finish/stop time and status. `physical_stages`, `cue_rows_total` and
`cue_rows_lost` expose coverage. Disposition numbers are0 observed-only,
1 player-not-ready,2 queue-full,3 queued,4 playing,5 completed,6 interrupted,
7 stale,8 failed and9 stock-start-not-suppressed. Stop reasons are0 none,1 completed,2 source missing,
3 expired/untracked,4 item lifetime changed,5 cycle superseded,6 next cue,
7 shutdown,8 failure,9 route-changed and10 stock-start-passed. The
`stock_pass_veto` field reports terminal fallback. Write times measure API
submission, not acoustic onset.

Fresh publications with an unavailable native-cycle read (cycle/shot zero)
do not stop an already admitted short cue or discard its queued event. Exact
actor/item lifetimes, tracking and original deadlines remain mandatory; a
known different cycle or shot does retire it. Shared-policy regressions cover
both unavailable reads and explicit supersession.

Four loopback-format regressions passed. Read-only inspection of PID18860
found endpoint `{0.0.0.00000000}.{a6b53090-5d8a-4263-a388-17b400bb4fa7}` and
a supported mix. The later single-cycle capture used this same endpoint. Receipt:
`bc2vr-recovery/overnight413/loopback-endpoint-inspect413.json`.

Format/decoder references: [vgmstream EA Audio Core parser](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ea_eaac.c),
[FFmpeg XAS decoder](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/adpcm.c),
[Microsoft WASAPI packet timestamps](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer).
